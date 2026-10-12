#include "protocol.h"

#include <QJsonDocument>
#include <QtTest>

using namespace ky;

// Vectors shared with KyAuth-Android (golden values from its unit tests).
class TestProtocol : public QObject {
    Q_OBJECT

    static Challenge challenge(const char *purpose)
    {
        Challenge c;
        c.challengeId = QStringLiteral("c-789");
        c.purpose = QLatin1String(purpose);
        c.expiresAtMs = 1791331200000;
        return c;
    }

    static QByteArray push(qint64 expires, const char *device = "d-456", const char *user = "u-123")
    {
        return QStringLiteral("{\"type\":\"mfa_challenge\",\"title\":\"t\",\"body\":\"b\",\"challengeId\":\"c1\","
                              "\"deviceId\":\"%1\",\"deviceUserId\":\"%2\",\"purpose\":\"login\","
                              "\"expiresAtEpochMs\":\"%3\",\"devicePlatform\":\"linux\"}")
            .arg(QLatin1String(device), QLatin1String(user), QString::number(expires))
            .toUtf8();
    }

    static QString qr(const char *server, const char *token, qint64 exp = 0, const char *user = "",
                      const char *type = "kyidentity_device_pairing")
    {
        QJsonObject o{{QStringLiteral("type"), QLatin1String(type)},
                      {QStringLiteral("serverUrl"), QString::fromLatin1(server)},
                      {QStringLiteral("userId"), QLatin1String(user)},
                      {QStringLiteral("username"), QStringLiteral("alice")},
                      {QStringLiteral("expiresAt"), double(exp)}};
        if (*token)
            o.insert(QStringLiteral("pairingToken"), QLatin1String(token));
        else
            o.insert(QStringLiteral("pinCode"), QStringLiteral("123456"));
        return QString::fromUtf8(QJsonDocument(o).toJson());
    }

private slots:
    void origin_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        const char *rows[][2] = {
            {"https://ID.Example.com/", "https://id.example.com"},
            {"https://id.example.com:443", "https://id.example.com"},
            {"https://id.example.com:8443/kyidentity", "https://id.example.com:8443"},
            {"http://127.0.0.1:8080", "http://127.0.0.1:8080"},
            {"https://[::1]", "https://[::1]"},
            {"https://[::1]:8443/x", "https://[::1]:8443"},
            {"not a url", ""},
        };
        for (auto &row : rows)
            QTest::newRow(row[0]) << QString::fromLatin1(row[0]) << QString::fromLatin1(row[1]);
    }
    void origin()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(originOf(in), out);
    }

    void pushResponseMessages()
    {
        const QString o = QStringLiteral("https://id.example.com");
        const QString u = QStringLiteral("u-123"), d = QStringLiteral("d-456");
        QCOMPARE(*pushResponseMessage(o, u, d, challenge("login"), true, QStringLiteral("42")),
                 QByteArray("kyidentity-push-v2|https://id.example.com|u-123|d-456|c-789|login|1791331200000|approve|42"));
        QCOMPARE(*pushResponseMessage(o, u, d, challenge("step_up"), false, QStringLiteral("42")),
                 QByteArray("kyidentity-push-v2|https://id.example.com|u-123|d-456|c-789|step_up|1791331200000|deny|"));
        QVERIFY(!pushResponseMessage(o, u, d, challenge("session"), true, QStringLiteral("42")));
        QVERIFY(!pushResponseMessage(o, QStringLiteral("u|1"), d, challenge("login"), true, QStringLiteral("42")));
        QVERIFY(!pushResponseMessage(o, QString(), d, challenge("login"), true, QStringLiteral("42")));
    }

    void pushTokenMessages()
    {
        const QString o = QStringLiteral("https://id.example.com");
        PushCredential up{QStringLiteral("unifiedpush"), QStringLiteral("https://ntfy.example.net/upAbC123?up=1"),
                          QStringLiteral("BLO9a5hWe8nCmwQX1aEnHLLnt8zkEd8orUdvm7-T9G4iMCY1nFt6ZzoFQA9EFaeHLGtbLcxSsDi0zCgJqaMqF6k"),
                          QStringLiteral("MDEyMzQ1Njc4OWFiY2RlZg")};
        QCOMPARE(*pushTokenMessage(o, QStringLiteral("d-456"), 1791331200000, up),
                 QStringLiteral("kyidentity-push-token-v2|https://id.example.com|d-456|1791331200000|unifiedpush|"
                                "8148d101f0613cac03f05c9530160b99c97e54841c0858e6a07b6deb9ab7ee2f|%1|%2")
                     .arg(up.p256dh, up.auth).toUtf8());
        QVERIFY(!pushTokenMessage(o, QStringLiteral("d|456"), 1, up));
        up.auth = QStringLiteral("a|b");
        QVERIFY(!pushTokenMessage(o, QStringLiteral("d-456"), 1, up));
    }

    void challengeBinding()
    {
        const QString d = QStringLiteral("d-456"), u = QStringLiteral("u-123");
        const qint64 now = 1000000;
        auto c = parseChallenge(push(now + 600000), d, u, now);
        QVERIFY(c);
        QCOMPARE(c->challengeId, QStringLiteral("c1"));
        QCOMPARE(c->purpose, QStringLiteral("login"));
        QVERIFY(!parseChallenge(push(now + 660000), d, u, now));
        QVERIFY(!parseChallenge(push(500), d, u, now));
        QVERIFY(!parseChallenge(push(now + 1000, "d-999"), d, u, now));
        QVERIFY(!parseChallenge(push(now + 1000, "d-456", "u-999"), d, u, now));
        QVERIFY(!parseChallenge("{\"challengeId\":1}", d, u, now));
        QVERIFY(!parseChallenge("{\"a\":{\"b\":\"c\"}}", d, u, now));
        QVERIFY(!parseChallenge("not json", d, u, now));
        QVERIFY(!parseChallenge(push(now + 1000).replace("mfa_challenge", "other"), d, u, now));
    }

    void pairingQr()
    {
        QString err;
        auto r = parsePairing(qr("https://id.example.com/", "abc", 2000, "u1"), 1000, &err);
        QVERIFY2(r, qPrintable(err));
        QCOMPARE(r->serverUrl, QStringLiteral("https://id.example.com"));
        QCOMPARE(r->registrationUrl, QStringLiteral("https://id.example.com/api/notifications/native/register"));

        auto link = parsePairing(QStringLiteral("kyidentity://native-pair?srv=https%3A%2F%2Fid.example.com%2Fbase&pin=123456&uid=u1"),
                                 1000, &err);
        QVERIFY2(link, qPrintable(err));
        QCOMPARE(link->registrationUrl, QStringLiteral("https://id.example.com/base/api/notifications/native/register"));
        QCOMPARE(link->userId, QStringLiteral("u1"));

        QVERIFY(!parsePairing(QStringLiteral("kyidentity://native-pair?srv=https%3A%2F%2Fsignin.example.com"
                                             "&reg=https%3A%2F%2Fevil.example%2Fregister&pt=x"), 1000, &err));
        QVERIFY(!parsePairing(qr("https://a.example", "x", 1), 1000, &err));
        QVERIFY(!parsePairing(qr("http://signin.example.com", "x"), 1000, &err));
        QVERIFY(!parsePairing(qr("https://u:p@a.example", "x"), 1000, &err));
        QVERIFY(!parsePairing(qr("https://a.example", ""), 1000, &err)); // PIN without userId
        QVERIFY(!parsePairing(qr("https://a.example", "x", 0, "", "other"), 1000, &err));
    }

    void registration()
    {
        QString err;
        auto ok = parseRegistration(200, "{\"success\":true,\"deviceId\":\"d1\",\"device\":{\"userId\":\"u9\"}}", QStringLiteral("u1"), &err);
        QCOMPARE(ok->first, QStringLiteral("d1"));
        QCOMPARE(ok->second, QStringLiteral("u9"));
        QCOMPARE(parseRegistration(200, "{\"success\":true,\"deviceId\":\"d1\"}", QStringLiteral("u1"), &err)->second,
                 QStringLiteral("u1"));
        QVERIFY(!parseRegistration(200, "{\"success\":false,\"deviceId\":\"d1\"}", QStringLiteral("u1"), &err));
        QVERIFY(!parseRegistration(400, "{\"error\":\"registration_failed\",\"error_description\":\"unsupported device platform\"}", {}, &err));
        QCOMPARE(err, QStringLiteral("unsupported device platform"));
    }

    void decisions()
    {
        QString err;
        QCOMPARE(parseDecision(200, "{\"success\":true}", QStringLiteral("c"), &err), Decision::Approved);
        QCOMPARE(parseDecision(200, "{\"success\":false}", QStringLiteral("c"), &err), Decision::Denied);
        QCOMPARE(parseDecision(200, "{}", QStringLiteral("c"), &err), Decision::Error);
        QCOMPARE(err, QStringLiteral("Server did not report a decision"));
        QCOMPARE(parseDecision(200, "{\"challengeId\":\"ch-other\",\"approved\":true}", QStringLiteral("c"), &err), Decision::Error);
        QCOMPARE(err, QStringLiteral("Server answered a different challenge"));
        QCOMPARE(parseDecision(401, "{\"error\":\"signature_required\"}", QStringLiteral("c"), &err), Decision::Error);
        QCOMPARE(err, QStringLiteral("signature_required"));
    }

    void serverErrorIsSanitized()
    {
        QCOMPARE(serverError(500, "{\"error\":\"bad\\nthing\\u001b[31m\"}"), QStringLiteral("badthing[31m"));
        QCOMPARE(serverError(502, "<html>"), QStringLiteral("Request failed (502)"));
        QCOMPARE(serverError(0, {}), QStringLiteral("Could not reach the server."));
    }
};

QTEST_GUILESS_MAIN(TestProtocol)
#include "test_protocol.moc"
