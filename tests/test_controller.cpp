#include "controller.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QtTest>
#include <unistd.h>

using namespace ky;

// Answers requests from the test instead of the network.
class FakeController : public Controller {
public:
    using Controller::Controller;
    struct Request {
        QByteArray verb;
        QString url;
        QJsonObject body;
        std::function<void(int, const QByteArray &)> done;
    };
    QList<Request> requests;

protected:
    void request(const QByteArray &verb, const QString &url, const QJsonObject &body,
                 std::function<void(int, const QByteArray &)> done) override
    {
        requests.append({verb, url, body, std::move(done)});
    }
};

class TestController : public QObject {
    Q_OBJECT

    const ScryptParams fast{1024, 8, 1};
    const QString pass = QStringLiteral("402917");
    const QString server = QStringLiteral("https://id.example.com");

    static QByteArray b64(const QJsonValue &v) { return QByteArray::fromBase64(v.toString().toLatin1()); }

    void setUp(FakeController &c)
    {
        c.createPasscode(pass, pass);
        QTRY_COMPARE(c.state(), QStringLiteral("unlocked"));
    }

    // Pairs without a server: the registered push credential equals the current one.
    P256Key pair(Controller &c)
    {
        const P256Key key = generateP256();
        c.m_latestToken = QStringLiteral("https://push.example.net/up1");
        const PushCredential push = *c.currentPush();
        c.m_vault->data.insert(QStringLiteral("pairing"), QJsonObject{
            {QStringLiteral("serverUrl"), server}, {QStringLiteral("username"), QStringLiteral("alice")},
            {QStringLiteral("deviceId"), QStringLiteral("d1")}, {QStringLiteral("userId"), QStringLiteral("u1")},
            {QStringLiteral("key"), QJsonObject{{QStringLiteral("priv"), QString::fromLatin1(key.priv.toBase64())},
                                                {QStringLiteral("pub"), QString::fromLatin1(key.pub.toBase64())}}},
            {QStringLiteral("push"), QJsonObject{{QStringLiteral("transport"), push.transport},
                                                 {QStringLiteral("token"), push.token},
                                                 {QStringLiteral("p256dh"), push.p256dh},
                                                 {QStringLiteral("auth"), push.auth}}}});
        if (!c.m_vault->save())
            qFatal("save failed");
        return key;
    }

    // What the server would push: the challenge JSON, RFC 8291 encrypted to the vault's keys.
    QByteArray challengePush(Controller &c, const char *id)
    {
        const QJsonObject k = c.m_vault->data.value(QStringLiteral("pushKey")).toObject();
        const QJsonObject msg{{QStringLiteral("type"), QStringLiteral("mfa_challenge")},
                              {QStringLiteral("challengeId"), QLatin1String(id)},
                              {QStringLiteral("deviceId"), QStringLiteral("d1")},
                              {QStringLiteral("deviceUserId"), QStringLiteral("u1")},
                              {QStringLiteral("purpose"), QStringLiteral("login")},
                              {QStringLiteral("expiresAtEpochMs"),
                               QString::number(QDateTime::currentMSecsSinceEpoch() + 300000)}};
        return webPushEncrypt(b64(k.value(QStringLiteral("pub"))), b64(c.m_vault->data.value(QStringLiteral("pushAuth"))),
                              QJsonDocument(msg).toJson(QJsonDocument::Compact));
    }

    static bool readOnly(const QString &dir, bool on)
    {
        return QFile::setPermissions(dir, on ? QFileDevice::ReadOwner | QFileDevice::ExeOwner
                                             : QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

private slots:
    void initTestCase()
    {
        QQuickStyle::setStyle(QLatin1String(QT_VERSION >= QT_VERSION_CHECK(6, 0, 0) ? "Basic" : "Default"));
    }

    // The real QML: a number typed for one request must not approve its replacement.
    void typedNumberClearsWhenRequestIsReplaced()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        Controller &b = c; // private members are reachable as a Controller
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("controller"), &c);
        engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        setUp(c);
        pair(c);

        b.handlePushBody(challengePush(c, "c1"));
        QObject *root = engine.rootObjects().constFirst();
        QObject *digits = nullptr;
        QTRY_VERIFY((digits = root->findChild<QObject *>(QStringLiteral("requestDigits"))));
        QObject *approve = root->findChild<QObject *>(QStringLiteral("approveButton"));
        QVERIFY(approve);
        digits->setProperty("text", QStringLiteral("42"));
        QTRY_VERIFY(approve->property("enabled").toBool());

        b.handlePushBody(challengePush(c, "c2"));
        QTRY_COMPARE(digits->property("text").toString(), QString());
        QVERIFY(!approve->property("enabled").toBool());
        QVERIFY(c.requests.isEmpty());
    }

    void unlockFinishingAfterLockStaysLocked()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        setUp(c);
        c.addAccount(QStringLiteral("otpauth://totp/A:b?secret=JBSWY3DPEHPK3PXP"));
        c.lock();
        c.unlock(pass);
        QVERIFY(c.busy());
        c.lock(); // the app left the foreground while the key was being derived
        QTRY_VERIFY(!c.busy());
        QCOMPARE(c.state(), QStringLiteral("locked"));
        QVERIFY(c.accounts().isEmpty());
        c.unlock(pass);
        QTRY_COMPARE(c.state(), QStringLiteral("unlocked"));
        QCOMPARE(c.accounts().size(), 1);
    }

    void unlockRefusedWhenAttemptCannotBeRecorded()
    {
        if (geteuid() == 0)
            QSKIP("root ignores directory permissions");
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        setUp(c);
        c.lock();
        QVERIFY(readOnly(dir.path(), true));
        c.unlock(pass);
        const bool busy = c.busy();
        QVERIFY(readOnly(dir.path(), false));
        QVERIFY(!busy); // the passcode was never checked
        QCOMPARE(c.state(), QStringLiteral("locked"));
        QVERIFY(c.notice().contains(QStringLiteral("not checked")));
    }

    void unpairKeepsPairingWhenSaveFails()
    {
        if (geteuid() == 0)
            QSKIP("root ignores directory permissions");
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        setUp(c);
        pair(c);
        QVERIFY(readOnly(dir.path(), true));
        c.unpair();
        QVERIFY(readOnly(dir.path(), false));
        QCOMPARE(c.pairing().value(QStringLiteral("username")).toString(), QStringLiteral("alice"));
        QVERIFY(c.notice().contains(QStringLiteral("Could not save")));
    }

    void unansweredRequestSurvivesLockAndApproveSigns()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        Controller &b = c; // private members are reachable as a Controller
        setUp(c);
        const P256Key device = pair(c);
        b.handlePushBody(challengePush(c, "c1"));
        QCOMPARE(c.challenge().value(QStringLiteral("purpose")).toString(), QStringLiteral("login"));

        c.lock();
        QVERIFY(c.challenge().isEmpty());
        c.unlock(pass);
        QTRY_COMPARE(c.state(), QStringLiteral("unlocked"));
        QVERIFY2(!c.challenge().isEmpty(), "the unanswered request must come back after unlocking");

        c.approve(QStringLiteral("stale"), QStringLiteral("42"));
        QVERIFY(c.requests.isEmpty()); // not the request on screen: refused
        c.approve(QStringLiteral("c1"), QStringLiteral("42"));
        QCOMPARE(c.requests.size(), 1);
        const FakeController::Request r = c.requests.at(0);
        QCOMPARE(r.url, server + QStringLiteral("/api/mfa/push/respond"));
        QCOMPARE(r.body.value(QStringLiteral("challengeId")).toString(), QStringLiteral("c1"));
        const QByteArray signedText = QByteArray("kyidentity-push-v2|https://id.example.com|u1|d1|c1|login|")
                                    + QByteArray::number(b.m_challenge->expiresAtMs) + "|approve|42";
        QVERIFY(verifyDer(spkiDer(device), signedText, b64(r.body.value(QStringLiteral("signature")))));

        r.done(200, "{\"success\":true}");
        QVERIFY(c.challenge().isEmpty());
        c.lock();
        c.unlock(pass);
        QTRY_COMPARE(c.state(), QStringLiteral("unlocked"));
        QVERIFY2(c.challenge().isEmpty(), "an answered request must not come back");
    }

    void refreshSkippedWhileAnsweringRunsAfterwards()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        Controller &b = c; // private members are reachable as a Controller
        setUp(c);
        const P256Key device = pair(c);
        b.handlePushBody(challengePush(c, "c2"));
        c.deny(QStringLiteral("c2"));
        QCOMPARE(c.requests.size(), 1);

        b.m_latestToken = QStringLiteral("https://push.example.net/up2"); // the distributor moved us
        b.refreshPushIfNeeded();
        QCOMPARE(c.requests.size(), 1); // busy answering: deferred

        c.requests.at(0).done(200, "{\"success\":false}");
        QCOMPARE(c.requests.size(), 2);
        const FakeController::Request r = c.requests.at(1);
        QCOMPARE(r.verb, QByteArray("PUT"));
        QCOMPARE(r.url, server + QStringLiteral("/api/notifications/native/devices/d1/push-token"));
        QCOMPARE(r.body.value(QStringLiteral("pushToken")).toString(), QStringLiteral("https://push.example.net/up2"));
        const PushCredential now = *b.currentPush();
        auto text = pushTokenMessage(server, QStringLiteral("d1"), qint64(r.body.value(QStringLiteral("issuedAt")).toDouble()), now);
        QVERIFY(verifyDer(spkiDer(device), *text, b64(r.body.value(QStringLiteral("signature")))));

        r.done(204, {});
        QCOMPARE(c.requests.size(), 2); // acknowledged: nothing left to send
    }
};

QTEST_MAIN(TestController)
#include "test_controller.moc"
