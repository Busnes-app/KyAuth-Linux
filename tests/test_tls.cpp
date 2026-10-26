#include "controller.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QProcess>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslDiffieHellmanParameters>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>
#include <openssl/pem.h>
#include <memory>

// Expose the actual production request implementation, without overriding it.
class NetworkController : public Controller {
public:
    using Controller::Controller;
    using Controller::request;
};

class TlsServer : public QTcpServer {
public:
    QSslConfiguration tls;
    QByteArray received;
    QString handshakeError;
    QByteArray negotiatedProtocol;
    int connections = 0;
    bool keepAlive = false;
    QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");

protected:
    void incomingConnection(qintptr descriptor) override
    {
        ++connections;
        auto *socket = new QSslSocket(this);
        socket->setSslConfiguration(tls);
        if (!socket->setSocketDescriptor(descriptor)) {
            delete socket;
            return;
        }
        connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QSslSocket::encrypted, this, [this, socket] {
            negotiatedProtocol = socket->sslConfiguration().nextNegotiatedProtocol();
        });
        connect(socket, &QSslSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
            handshakeError = socket->errorString();
        });
        connect(socket, &QSslSocket::readyRead, this, [this, socket] {
            received += socket->readAll();
            const int end = received.indexOf("\r\n\r\n");
            if (end < 0)
                return;
            int length = 0;
            for (const QByteArray &line : received.left(end).split('\n'))
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
            if (received.size() < end + 4 + length)
                return;
            socket->write(response);
            if (!keepAlive)
                socket->disconnectFromHost();
        });
        socket->startServerEncryption();
    }
};

class TestTls : public QObject {
    Q_OBJECT

    QTemporaryDir files;
    QSslConfiguration original;
    int serial = 0;

    QString path(const QString &name) const { return files.filePath(name); }

    static QByteArray read(const QString &name)
    {
        QFile file(name);
        if (!file.open(QIODevice::ReadOnly))
            qFatal("Cannot read TLS fixture");
        return file.readAll();
    }

    void write(const QString &name, const QByteArray &bytes)
    {
        QFile file(path(name));
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            qFatal("Cannot write TLS fixture");
    }

    void openssl(QStringList args)
    {
        QProcess command;
        command.setWorkingDirectory(files.path());
        command.start(QStringLiteral("openssl"), args);
        if (!command.waitForFinished(30000) || command.exitCode() != 0
            || command.exitStatus() != QProcess::NormalExit)
            qFatal("TLS fixture openssl command failed: %s", command.readAllStandardError().constData());
    }

    void key(const QString &name, const QString &algorithm, const QString &parameter)
    {
        openssl({QStringLiteral("genpkey"), QStringLiteral("-algorithm"), algorithm,
                 QStringLiteral("-pkeyopt"), parameter, QStringLiteral("-out"), name + QStringLiteral(".key")});
    }

    void root(const QString &name, int bits)
    {
        key(name, QStringLiteral("RSA"), QStringLiteral("rsa_keygen_bits:%1").arg(bits));
        openssl({QStringLiteral("req"), QStringLiteral("-new"), QStringLiteral("-x509"),
                 QStringLiteral("-key"), name + QStringLiteral(".key"), QStringLiteral("-sha256"),
                 QStringLiteral("-days"), QStringLiteral("2"), QStringLiteral("-subj"),
                 QStringLiteral("/CN=") + name, QStringLiteral("-addext"),
                 QStringLiteral("basicConstraints=critical,CA:TRUE"), QStringLiteral("-addext"),
                 QStringLiteral("keyUsage=critical,keyCertSign,cRLSign"), QStringLiteral("-out"),
                 name + QStringLiteral(".crt")});
    }

    void certificate(const QString &name, const QString &issuer, bool ca = false,
                     const QString &digest = QStringLiteral("-sha256"), int days = 2)
    {
        openssl({QStringLiteral("req"), QStringLiteral("-new"), QStringLiteral("-key"),
                 name + QStringLiteral(".key"), QStringLiteral("-subj"), QStringLiteral("/CN=localhost"),
                 QStringLiteral("-out"), name + QStringLiteral(".csr")});
        openssl({QStringLiteral("x509"), QStringLiteral("-req"), QStringLiteral("-in"),
                 name + QStringLiteral(".csr"), QStringLiteral("-CA"), issuer + QStringLiteral(".crt"),
                 QStringLiteral("-CAkey"), issuer + QStringLiteral(".key"), QStringLiteral("-set_serial"),
                 QString::number(++serial), QStringLiteral("-days"), QString::number(days < 0 ? 2 : days), digest,
                 QStringLiteral("-extfile"), ca ? QStringLiteral("ca.ext") : QStringLiteral("leaf.ext"),
                 QStringLiteral("-out"), name + QStringLiteral(".crt")});
        if (days < 0) {
            // OpenSSL 3.6 refuses a negative CLI -days value. Sign a past validity
            // interval explicitly, using the same strong issuer and public key.
            const QByteArray pem = read(path(name + QStringLiteral(".crt")));
            std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_mem_buf(pem.constData(), pem.size()), BIO_free);
            std::unique_ptr<X509, decltype(&X509_free)> cert(PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr), X509_free);
            const QByteArray keyPem = read(path(issuer + QStringLiteral(".key")));
            std::unique_ptr<BIO, decltype(&BIO_free)> keyInput(BIO_new_mem_buf(keyPem.constData(), keyPem.size()), BIO_free);
            std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> signer(PEM_read_bio_PrivateKey(keyInput.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
            std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new(BIO_s_mem()), BIO_free);
            if (!cert || !signer || !output
                || !X509_gmtime_adj(X509_getm_notBefore(cert.get()), -172800)
                || !X509_gmtime_adj(X509_getm_notAfter(cert.get()), -86400)
                || X509_sign(cert.get(), signer.get(), EVP_sha256()) <= 0
                || PEM_write_bio_X509(output.get(), cert.get()) != 1)
                qFatal("Cannot create expired TLS fixture");
            char *bytes = nullptr;
            const long size = BIO_get_mem_data(output.get(), &bytes);
            write(name + QStringLiteral(".crt"), QByteArray(bytes, int(size)));
        }
    }

    QSslConfiguration serverConfig(const QString &name, QSsl::SslProtocol protocol,
                                   const QString &dh = {}) const
    {
        QSslConfiguration config = original;
        config.setProtocol(protocol);
        config.setPeerVerifyMode(QSslSocket::VerifyNone); // server does not request a client certificate
        config.setLocalCertificateChain(QSslCertificate::fromPath(path(name + QStringLiteral(".crt"))));
        const auto algorithm = name.startsWith(QLatin1String("ec")) ? QSsl::Ec : QSsl::Rsa;
        config.setPrivateKey(QSslKey(read(path(name + QStringLiteral(".key"))), algorithm));
        config.setBackendConfigurationOption(QByteArrayLiteral("CipherString"),
            dh.isEmpty() ? QByteArrayLiteral("ALL:@SECLEVEL=0")
                         : QByteArrayLiteral("DHE-RSA-AES128-GCM-SHA256:@SECLEVEL=0"));
        if (!dh.isEmpty())
            config.setDiffieHellmanParameters(QSslDiffieHellmanParameters::fromEncoded(read(path(dh))));
        return config;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(files.isValid());
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
        QVERIFY(QSslSocket::activeBackend() == QLatin1String("openssl")
                || QSslSocket::setActiveBackend(QStringLiteral("openssl")));
#endif
        QVERIFY(QSslSocket::supportsSsl());
        qInfo("Qt build %s, runtime %s; TLS build %s, runtime %s", QT_VERSION_STR, qVersion(),
              qPrintable(QSslSocket::sslLibraryBuildVersionString()),
              qPrintable(QSslSocket::sslLibraryVersionString()));
        original = QSslConfiguration::defaultConfiguration();
        write(QStringLiteral("ca.ext"), QByteArrayLiteral("basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n"));
        write(QStringLiteral("leaf.ext"), QByteArrayLiteral("basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\nsubjectAltName=DNS:localhost\n"));
        root(QStringLiteral("root"), 2048);
        root(QStringLiteral("root1024"), 1024);
        root(QStringLiteral("root2047"), 2047);
        root(QStringLiteral("other"), 2048);
        for (int bits : {1024, 2047, 2048}) {
            const QString name = QStringLiteral("rsa%1").arg(bits);
            key(name, QStringLiteral("RSA"), QStringLiteral("rsa_keygen_bits:%1").arg(bits));
            certificate(name, QStringLiteral("root"));
            QCOMPARE(QSslKey(read(path(name + QStringLiteral(".key"))), QSsl::Rsa).length(), bits);
        }
        for (const QString &curve : {QStringLiteral("prime256v1"), QStringLiteral("prime192v1")}) {
            const QString name = curve == QLatin1String("prime256v1") ? QStringLiteral("ec256") : QStringLiteral("ec192");
            key(name, QStringLiteral("EC"), QStringLiteral("ec_paramgen_curve:") + curve);
            certificate(name, QStringLiteral("root"));
        }
        for (int bits : {1024, 2047}) {
            const QString direct = QStringLiteral("direct%1").arg(bits);
            write(direct + QStringLiteral(".key"), read(path(QStringLiteral("rsa%1.key").arg(bits))));
            openssl({QStringLiteral("req"), QStringLiteral("-new"), QStringLiteral("-x509"),
                     QStringLiteral("-key"), direct + QStringLiteral(".key"), QStringLiteral("-sha256"),
                     QStringLiteral("-days"), QStringLiteral("2"), QStringLiteral("-subj"),
                     QStringLiteral("/CN=localhost"), QStringLiteral("-addext"),
                     QStringLiteral("basicConstraints=critical,CA:FALSE"), QStringLiteral("-addext"),
                     QStringLiteral("extendedKeyUsage=serverAuth"), QStringLiteral("-addext"),
                     QStringLiteral("subjectAltName=DNS:localhost"), QStringLiteral("-out"),
                     direct + QStringLiteral(".crt")});
            const QString intermediate = QStringLiteral("inter%1").arg(bits);
            key(intermediate, QStringLiteral("RSA"), QStringLiteral("rsa_keygen_bits:%1").arg(bits));
            certificate(intermediate, QStringLiteral("root"), true);
            const QString leaf = QStringLiteral("chain%1").arg(bits);
            write(leaf + QStringLiteral(".key"), read(path(QStringLiteral("rsa2048.key"))));
            certificate(leaf, intermediate);
            write(leaf + QStringLiteral(".crt"), read(path(leaf + QStringLiteral(".crt")))
                                                 + read(path(intermediate + QStringLiteral(".crt"))));
            const QString rooted = QStringLiteral("weakroot%1").arg(bits);
            write(rooted + QStringLiteral(".key"), read(path(QStringLiteral("rsa2048.key"))));
            certificate(rooted, QStringLiteral("root%1").arg(bits));
        }
        for (const QString &name : {QStringLiteral("sha1"), QStringLiteral("expired")}) {
            write(name + QStringLiteral(".key"), read(path(QStringLiteral("rsa2048.key"))));
            certificate(name, QStringLiteral("root"), false,
                        name == QLatin1String("sha1") ? QStringLiteral("-sha1") : QStringLiteral("-sha256"),
                        name == QLatin1String("expired") ? -1 : 2);
        }
        // Published RFC 5114 / RFC 7919 groups: no slow random parameter generation.
        for (const QString &group : {QStringLiteral("dh_1024_160"), QStringLiteral("ffdhe2048")})
            openssl({QStringLiteral("genpkey"), QStringLiteral("-genparam"), QStringLiteral("-algorithm"),
                     QStringLiteral("DH"), QStringLiteral("-pkeyopt"), QStringLiteral("group:") + group,
                     QStringLiteral("-out"), group + QStringLiteral(".pem")});
    }

    void cleanup()
    {
        QSslConfiguration::setDefaultConfiguration(original);
    }

    void requests_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("trust");
        QTest::addColumn<int>("protocol");
        QTest::addColumn<bool>("accepted");
        QTest::addColumn<bool>("control");
        QTest::addColumn<QString>("dh");
        for (auto protocol : {QSsl::TlsV1_2, QSsl::TlsV1_3}) {
            const QString suffix = protocol == QSsl::TlsV1_2 ? QStringLiteral("-tls12") : QStringLiteral("-tls13");
            for (const QString &name : {QStringLiteral("rsa2048"), QStringLiteral("ec256"),
                                       QStringLiteral("rsa1024"), QStringLiteral("rsa2047"),
                                       QStringLiteral("chain1024"), QStringLiteral("chain2047"),
                                       QStringLiteral("weakroot1024"), QStringLiteral("weakroot2047"),
                                       QStringLiteral("sha1"), QStringLiteral("expired")}) {
                const QString trust = name.startsWith(QLatin1String("weakroot"))
                    ? name.mid(4) : QStringLiteral("root");
                QTest::newRow(qPrintable(name + suffix)) << name << trust << int(protocol)
                    << (name == QLatin1String("rsa2048") || name == QLatin1String("ec256"))
                    << (name != QLatin1String("expired")) << QString();
            }
        }
        QTest::newRow("ec192-tls12") << QStringLiteral("ec192") << QStringLiteral("root")
            << int(QSsl::TlsV1_2) << false << false << QString();
        // Legacy protocol enums are deliberately used only to prove their refusal.
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        for (auto protocol : {QSsl::TlsV1_0, QSsl::TlsV1_1})
            QTest::newRow(protocol == QSsl::TlsV1_0 ? "tls10" : "tls11")
                << QStringLiteral("rsa2048") << QStringLiteral("root") << int(protocol)
                << false << true << QString();
        QT_WARNING_POP
        for (int bits : {1024, 2047})
            QTest::newRow(qPrintable(QStringLiteral("direct-trust-%1").arg(bits)))
                << QStringLiteral("direct%1").arg(bits) << QStringLiteral("direct%1").arg(bits)
                << int(QSsl::TlsV1_2) << false << true << QString();
        QTest::newRow("untrusted") << QStringLiteral("rsa2048") << QStringLiteral("other")
            << int(QSsl::TlsV1_2) << false << false << QString();
        QTest::newRow("dh1024") << QStringLiteral("rsa2048") << QStringLiteral("root")
            << int(QSsl::TlsV1_2) << false << true << QStringLiteral("dh_1024_160.pem");
        QTest::newRow("dh2048") << QStringLiteral("rsa2048") << QStringLiteral("root")
            << int(QSsl::TlsV1_2) << true << true << QStringLiteral("ffdhe2048.pem");
    }

    void requests()
    {
        QFETCH(QString, name);
        QFETCH(QString, trust);
        QFETCH(int, protocol);
        QFETCH(bool, accepted);
        QFETCH(bool, control);
        QFETCH(QString, dh);
        // Simulate an inherited weak default: production must override this before sending.
        QSslConfiguration defaults = original;
        defaults.setCaCertificates(QSslCertificate::fromPath(path(trust + QStringLiteral(".crt"))));
        defaults.setProtocol(QSsl::AnyProtocol);
        defaults.setPeerVerifyMode(QSslSocket::VerifyPeer);
        defaults.setBackendConfigurationOption(QByteArrayLiteral("CipherString"), QByteArrayLiteral("ALL:@SECLEVEL=0"));
        QSslConfiguration::setDefaultConfiguration(defaults);

        TlsServer server;
        server.tls = serverConfig(name, QSsl::SslProtocol(protocol), dh);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QString url = QStringLiteral("https://localhost:%1/credential").arg(server.serverPort());
        // A real successful low-security request proves a weak fixture was actually usable.
        if (control) {
            QNetworkAccessManager manager;
            QNetworkRequest request{QUrl(url)};
            request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/octet-stream"));
            request.setTransferTimeout(5000);
            QNetworkReply *reply = manager.post(request, QByteArrayLiteral("fixture-control"));
            QSignalSpy finished(reply, &QNetworkReply::finished);
            QVERIFY(finished.wait(7000));
            QVERIFY2(reply->error() == QNetworkReply::NoError,
                     qPrintable(reply->errorString() + QStringLiteral("; server: ") + server.handshakeError));
            QVERIFY(server.received.contains("fixture-control"));
            server.received.clear();
            delete reply;
        }

        NetworkController controller(files.path(), ky::ScryptParams{1024, 8, 1});
        if (accepted) {
            server.keepAlive = true;
            server.response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}");
        }
        const int connectionsBefore = server.connections;
        const QJsonObject body{{QStringLiteral("credential"), QStringLiteral("synthetic-do-not-send-to-weak-peer")}};
        for (int attempt = 0; attempt < 2; ++attempt) {
            server.received.clear();
            int status = -1;
            controller.request(QByteArrayLiteral("POST"), url, body,
                               [&status](int value, const QByteArray &) { status = value; });
            QTRY_VERIFY_WITH_TIMEOUT(status != -1, 12000);
            if (accepted) {
                QCOMPARE(status, 200);
                QVERIFY(server.received.startsWith("POST /credential HTTP/1.1\r\n"));
                QVERIFY(server.received.contains(QJsonDocument(body).toJson(QJsonDocument::Compact)));
            } else {
                QCOMPARE(status, 0);
                QTest::qWait(25); // drain the local peer's pending read events
                QVERIFY2(server.received.isEmpty(), "Refused peer received HTTP application bytes");
            }
        }
        if (accepted)
            QCOMPARE(server.connections - connectionsBefore, 1); // also exercises a reused TLS connection
    }

    void redirectsAndHostnames()
    {
        QSslConfiguration defaults = original;
        defaults.setCaCertificates(QSslCertificate::fromPath(path(QStringLiteral("root.crt"))));
        QSslConfiguration::setDefaultConfiguration(defaults);
        TlsServer destination;
        destination.tls = serverConfig(QStringLiteral("rsa2048"), QSsl::TlsV1_2);
        QVERIFY(destination.listen(QHostAddress::LocalHost));
        TlsServer redirect;
        redirect.tls = destination.tls;
        QVERIFY(redirect.listen(QHostAddress::LocalHost));
        NetworkController controller(files.path(), ky::ScryptParams{1024, 8, 1});
        for (const QString &scheme : {QStringLiteral("https"), QStringLiteral("http")}) {
            redirect.response = QByteArrayLiteral("HTTP/1.1 307 Temporary Redirect\r\nContent-Length: 0\r\nConnection: close\r\nLocation: ")
                + scheme.toLatin1() + QByteArrayLiteral("://localhost:") + QByteArray::number(destination.serverPort())
                + QByteArrayLiteral("/stolen\r\n\r\n");
            int status = -1;
            controller.request(QByteArrayLiteral("POST"),
                QStringLiteral("https://localhost:%1/start").arg(redirect.serverPort()), {},
                [&status](int value, const QByteArray &) { status = value; });
            QTRY_COMPARE_WITH_TIMEOUT(status, 307, 12000);
            QVERIFY(destination.received.isEmpty());
            QCOMPARE(destination.connections, 0);
            redirect.received.clear();
        }
        int status = -1;
        controller.request(QByteArrayLiteral("POST"),
            QStringLiteral("https://127.0.0.1:%1/wrong-name").arg(destination.serverPort()), {},
            [&status](int value, const QByteArray &) { status = value; });
        QTRY_VERIFY_WITH_TIMEOUT(status != -1, 12000);
        QCOMPARE(status, 0);
        QVERIFY(destination.received.isEmpty());
        const int connectionsBeforePlaintext = destination.connections;
        status = -1;
        controller.request(QByteArrayLiteral("POST"),
            QStringLiteral("http://localhost:%1/plaintext").arg(destination.serverPort()), {},
            [&status](int value, const QByteArray &) { status = value; });
        QCOMPARE(status, 0);
        QVERIFY(destination.received.isEmpty());
        QCOMPARE(destination.connections, connectionsBeforePlaintext);
    }

    void queuedAndReentrantRefusals()
    {
        QSslConfiguration defaults = original;
        defaults.setCaCertificates(QSslCertificate::fromPath(path(QStringLiteral("root.crt"))));
        QSslConfiguration::setDefaultConfiguration(defaults);
        TlsServer server;
        server.tls = serverConfig(QStringLiteral("rsa2047"), QSsl::TlsV1_2);
        server.keepAlive = true;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        NetworkController controller(files.path(), ky::ScryptParams{1024, 8, 1});
        const QString url = QStringLiteral("https://localhost:%1/credential").arg(server.serverPort());
        int completed = 0;
        bool allRefused = true;
        std::function<void(int, const QByteArray &)> done;
        done = [&](int status, const QByteArray &) {
            allRefused = allRefused && status == 0;
            if (++completed == 1)
                controller.request(QByteArrayLiteral("POST"), url,
                                   {{QStringLiteral("credential"), QStringLiteral("reentrant-only")}}, done);
        };
        for (int i = 0; i < 2; ++i)
            controller.request(QByteArrayLiteral("POST"), url,
                               {{QStringLiteral("credential"), QStringLiteral("queued-only")}}, done);
        QTRY_COMPARE_WITH_TIMEOUT(completed, 3, 15000);
        QVERIFY(allRefused);
        QTest::qWait(25);
        QVERIFY(server.received.isEmpty());
    }

    void http1PolicyAgainstHttp2Server()
    {
        QSslConfiguration defaults = original;
        defaults.setCaCertificates(QSslCertificate::fromPath(path(QStringLiteral("root.crt"))));
        QSslConfiguration::setDefaultConfiguration(defaults);
        TlsServer server;
        server.tls = serverConfig(QStringLiteral("rsa2047"), QSsl::TlsV1_2);
        const QList<QByteArray> protocols{QByteArrayLiteral("h2"), QByteArrayLiteral("http/1.1")};
        server.tls.setAllowedNextProtocols(protocols);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        // The fixture really can select h2 with a client that offers it.
        QSslSocket control;
        auto config = defaults;
        config.setBackendConfigurationOption(QByteArrayLiteral("CipherString"), QByteArrayLiteral("ALL:@SECLEVEL=0"));
        config.setAllowedNextProtocols(protocols);
        control.setSslConfiguration(config);
        QSignalSpy encrypted(&control, &QSslSocket::encrypted);
        control.connectToHostEncrypted(QStringLiteral("localhost"), server.serverPort());
        QVERIFY(encrypted.wait(5000));
        QCOMPARE(control.sslConfiguration().nextNegotiatedProtocol(), QByteArrayLiteral("h2"));
        control.disconnectFromHost();
        for (int bits : {2047, 2048}) {
            server.tls = serverConfig(QStringLiteral("rsa%1").arg(bits), QSsl::TlsV1_2);
            server.tls.setAllowedNextProtocols(protocols);
            server.received.clear();
            server.negotiatedProtocol.clear();
            NetworkController controller(files.path(), ky::ScryptParams{1024, 8, 1});
            int status = -1;
            controller.request(QByteArrayLiteral("POST"),
                QStringLiteral("https://localhost:%1/credential").arg(server.serverPort()),
                {{QStringLiteral("credential"), QStringLiteral("synthetic-alpn-only")}},
                [&status](int value, const QByteArray &) { status = value; });
            QTRY_VERIFY_WITH_TIMEOUT(status != -1, 12000);
            QVERIFY(server.negotiatedProtocol != QByteArrayLiteral("h2"));
            QCOMPARE(status, bits == 2048 ? 200 : 0);
            if (bits == 2048)
                QVERIFY(server.received.startsWith("POST /credential HTTP/1.1\r\n"));
            else
                QVERIFY(server.received.isEmpty());
        }
    }

    void certificateKeyTypes()
    {
        QVERIFY(!ky::tlsCertificateKeyAllowed({}));
        QVERIFY(!ky::tlsCertificateKeyAllowed(QByteArrayLiteral("not a certificate")));
        const auto valid = QSslCertificate::fromPath(path(QStringLiteral("rsa2048.crt"))).constFirst().toDer();
        QVERIFY(ky::tlsCertificateKeyAllowed(valid));
        QVERIFY(!ky::tlsCertificateKeyAllowed(valid + QByteArrayLiteral("trailing data")));
        // EC-192 may be non-negotiable even in the weak TLS fixture. Prove the
        // literal certificate policy independently of transport negotiation.
        for (const QString &name : {QStringLiteral("rsa1024"), QStringLiteral("rsa2047"), QStringLiteral("ec192")}) {
            const auto der = QSslCertificate::fromPath(path(name + QStringLiteral(".crt"))).constFirst().toDer();
            QVERIFY2(!ky::tlsCertificateKeyAllowed(der), qPrintable(name));
        }
        // Qt's key wrappers do not consistently expose these public-key types.
        // Decode DER through OpenSSL rather than treating an opaque/null wrapper as safe.
        for (const QString &algorithm : {QStringLiteral("RSA-PSS"), QStringLiteral("ED25519"), QStringLiteral("ED448")}) {
            QStringList args{QStringLiteral("genpkey"), QStringLiteral("-algorithm"), algorithm,
                             QStringLiteral("-out"), QStringLiteral("typed.key")};
            if (algorithm == QLatin1String("RSA-PSS"))
                args << QStringLiteral("-pkeyopt") << QStringLiteral("rsa_keygen_bits:2048");
            openssl(args);
            openssl({QStringLiteral("req"), QStringLiteral("-new"), QStringLiteral("-x509"),
                     QStringLiteral("-key"), QStringLiteral("typed.key"), QStringLiteral("-subj"),
                     QStringLiteral("/CN=typed-key"), QStringLiteral("-days"), QStringLiteral("2"),
                     QStringLiteral("-out"), QStringLiteral("typed.crt")});
            const auto der = QSslCertificate::fromPath(path(QStringLiteral("typed.crt"))).constFirst().toDer();
            QVERIFY2(ky::tlsCertificateKeyAllowed(der), qPrintable(algorithm));
        }
    }

    void invalidBackendOption()
    {
        QSslConfiguration defaults = original;
        defaults.setCaCertificates(QSslCertificate::fromPath(path(QStringLiteral("root.crt"))));
        defaults.setBackendConfigurationOption(QByteArrayLiteral("NoSuchTlsConfigurationOption"), QByteArrayLiteral("1"));
        QSslConfiguration::setDefaultConfiguration(defaults);
        TlsServer server;
        server.tls = serverConfig(QStringLiteral("rsa2048"), QSsl::TlsV1_2);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        NetworkController controller(files.path(), ky::ScryptParams{1024, 8, 1});
        int status = -1;
        controller.request(QByteArrayLiteral("POST"),
            QStringLiteral("https://localhost:%1/credential").arg(server.serverPort()),
            {{QStringLiteral("credential"), QStringLiteral("synthetic-only")}},
            [&status](int value, const QByteArray &) { status = value; });
        QTRY_VERIFY_WITH_TIMEOUT(status != -1, 12000);
        QCOMPARE(status, 0);
        QTest::qWait(25);
        QVERIFY(server.received.isEmpty());
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
    void missingBackend()
    {
        // A new process has no cached TLS plugin. Remove plugin search paths only there.
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--without-tls")});
        QVERIFY(child.waitForFinished(15000));
        QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                 child.readAllStandardError().constData());
    }
#endif
};

int main(int argc, char **argv)
{
    // The Qt server loads its private key before applying backend options. Start
    // its OpenSSL context weak enough to serve the negative fixtures; the product
    // request must override this inherited default before sending credentials.
    QTemporaryDir configuration;
    QFile file(configuration.filePath(QStringLiteral("openssl.cnf")));
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QByteArrayLiteral("openssl_conf=init\n[init]\nssl_conf=tls\n[tls]\nsystem_default=weak\n[weak]\nCipherString=ALL:@SECLEVEL=0\nMinProtocol=TLSv1\n")) < 0)
        return 2;
    file.close();
    qputenv("OPENSSL_CONF", QFile::encodeName(file.fileName()));
    QGuiApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--without-tls"))) {
        QTemporaryDir directory;
        QCoreApplication::setLibraryPaths({directory.path()});
        NetworkController controller(directory.path(), ky::ScryptParams{1024, 8, 1});
        int status = -1;
        QString callerError;
        controller.request(QByteArrayLiteral("POST"), QStringLiteral("https://localhost:9/credential"), {},
                           [&status, &callerError](int value, const QByteArray &body) {
            status = value;
            callerError = ky::serverError(value, body);
        });
        return status == 0 && controller.notice().contains(QStringLiteral("OpenSSL 3"))
            && callerError.contains(QStringLiteral("OpenSSL 3")) ? 0 : 1;
    }
    TestTls tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_tls.moc"
