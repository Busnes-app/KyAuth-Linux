#include "controller.h"
#include "theme.h"
#include "totp.h"

#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
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
    void legacyShortAccountStaysVisibleAndUncopyableAcrossVaultSaveAndReload()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        setUp(c);
        TotpEntry legacy;
        legacy.id = QStringLiteral("legacy");
        legacy.title = QStringLiteral("Retained account");
        legacy.issuer = QStringLiteral("Original provider");
        legacy.secret = QByteArray("012345678"); // 72 bits: under the 80-bit floor
        legacy.algorithm = QStringLiteral("SHA1");
        const auto strong = parseOtpauth(QStringLiteral("otpauth://totp/Strong?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ"));
        QVERIFY(strong);
        const QJsonArray entries{legacy.toJson(), strong->toJson()};
        c.m_vault->data.insert(QStringLiteral("totp"), entries);
        QVERIFY(c.m_vault->save());
        c.lock();
        c.unlock(pass);
        QTRY_COMPARE(c.state(), QStringLiteral("unlocked"));
        QCOMPARE(c.accounts().size(), 2);
        const QVariantMap old = c.accounts().first().toMap();
        QCOMPARE(old.value(QStringLiteral("title")).toString(), legacy.title);
        QVERIFY(old.value(QStringLiteral("requiresReenrolment")).toBool());
        QVERIFY(old.value(QStringLiteral("code")).toString().isEmpty());
        const QVariantMap working = c.accounts().last().toMap();
        QVERIFY(!working.value(QStringLiteral("requiresReenrolment")).toBool());
        QVERIFY(!working.value(QStringLiteral("code")).toString().isEmpty());

        QGuiApplication::clipboard()->setText(QStringLiteral("unchanged"));
        c.copyCode(legacy.id);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("unchanged"));
        QVERIFY(c.notice().contains(QStringLiteral("account provider")));
        c.copyCode(strong->id);
        QCOMPARE(QGuiApplication::clipboard()->text().size(), 6);
        QVERIFY(!c.addAccount(QStringLiteral("otpauth://totp/Rejected?secret=JBSWY3DPEHPK3PX")));
        QCOMPARE(c.m_vault->data.value(QStringLiteral("totp")).toArray(), entries);
        const auto persisted = Vault::open(dir.filePath(QStringLiteral("vault.json")), pass);
        QVERIFY(persisted);
        QCOMPARE(persisted->data.value(QStringLiteral("totp")).toArray(), entries);

        Theme theme(dir.filePath(QStringLiteral("theme.ini")));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("controller"), &c);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        // Qt 6 Repeater delegates belong to the visual tree rather than QObject children.
        std::function<QQuickItem *(QQuickItem *, const QString &)> findItem;
        findItem = [&findItem](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
                return item;
            for (QQuickItem *child : item->childItems())
                if (auto *found = findItem(child, name))
                    return found;
            return nullptr;
        };
        QQuickItem *code = nullptr;
        QTRY_VERIFY((code = findItem(window->contentItem(), QStringLiteral("accountCode"))));
        QCOMPARE(code->property("text").toString(), QStringLiteral("Unavailable"));
        const auto *hint = findItem(window->contentItem(), QStringLiteral("accountCodeHint"));
        QVERIFY(hint);
        QVERIFY(hint->property("text").toString().contains(QStringLiteral("account provider")));
        const auto *countdown = findItem(window->contentItem(), QStringLiteral("accountCountdown"));
        QVERIFY(countdown);
        QVERIFY(!countdown->isVisible());
        QTest::qWait(100); // Allow the real QML layout to settle for optional local inspection.
        if (qEnvironmentVariableIsSet("KYAUTH_TEST_CAPTURE"))
            window->grabWindow().save(qEnvironmentVariable("KYAUTH_TEST_CAPTURE"));
        c.removeAccount(legacy.id); // Removal remains an explicit user choice.
        QCOMPARE(c.accounts().size(), 1);
        QCOMPARE(c.m_vault->data.value(QStringLiteral("totp")).toArray(), QJsonArray{strong->toJson()});
    }

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
        Theme theme(dir.filePath(QStringLiteral("theme.ini")));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("controller"), &c);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
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

    // The real QML: touch keyboards cannot paste, so the Paste buttons are the only way in.
    void pasteButtonsFillTheirFields()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        Theme theme(dir.filePath(QStringLiteral("theme.ini")));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("controller"), &c);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        setUp(c);
        QObject *root = engine.rootObjects().constFirst();

        // The link field is in the Add sheet on the vault; the pairing field is on the connect page.
        const struct { const char *page, *field, *paste; } cases[] = {
            {"vault", "otpUri", "otpPaste"}, {"connect", "pairCode", "pairPaste"}};
        for (const auto &t : cases) {
            root->setProperty("page", QLatin1String(t.page));
            QObject *field = nullptr;
            QTRY_VERIFY((field = root->findChild<QObject *>(QLatin1String(t.field))));
            QObject *paste = root->findChild<QObject *>(QLatin1String(t.paste));
            QVERIFY(paste);
            QGuiApplication::clipboard()->setText(QLatin1String(t.field));
            QVERIFY(QMetaObject::invokeMethod(paste, "clicked"));
            QCOMPARE(field->property("text").toString(), QLatin1String(t.field));
        }
    }

    // The real QML: pairing from the connect page moves on to settings, which offers Unpair.
    void pairingLeavesConnectPage()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        Theme theme(dir.filePath(QStringLiteral("theme.ini")));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("controller"), &c);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        setUp(c);
        QObject *root = engine.rootObjects().constFirst();
        root->setProperty("page", QStringLiteral("connect"));
        pair(c);
        emit c.pairingChanged(); // pair() bypasses the controller
        QCOMPARE(root->property("page").toString(), QStringLiteral("settings"));
        c.lock();
        QTRY_COMPARE(root->property("page").toString(), QStringLiteral("vault"));
    }

    // The real QML: the appearance page sets the pair and the half, and the window repaints.
    void appearancePageSetsTheTheme()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        Theme theme(dir.filePath(QStringLiteral("theme.ini")));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("controller"), &c);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        setUp(c);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QCOMPARE(window->property("bg").value<QColor>(), QColor(QStringLiteral("#f8f6f0")));
        std::function<QQuickItem *(QQuickItem *, const QString &)> findItem;
        findItem = [&findItem](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
                return item;
            for (QQuickItem *child : item->childItems())
                if (auto *found = findItem(child, name))
                    return found;
            return nullptr;
        };
        window->setProperty("page", QStringLiteral("settings"));
        QQuickItem *connect = nullptr;
        QTRY_VERIFY((connect = findItem(window->contentItem(), QStringLiteral("connectButton"))));
        // A filled button's label is the half's on-accent colour (white on Busnes Light).
        QCOMPARE(connect->property("contentItem").value<QQuickItem *>()->property("color").value<QColor>(),
                 QColor(QStringLiteral("#ffffff")));
        window->setProperty("page", QStringLiteral("appearance"));
        QQuickItem *nord = nullptr;
        QTRY_VERIFY((nord = findItem(window->contentItem(), QStringLiteral("pair_nord-pair"))));
        QVERIFY(QMetaObject::invokeMethod(nord, "picked"));
        QQuickItem *dark = findItem(window->contentItem(), QStringLiteral("mode_dark"));
        QVERIFY(dark);
        QVERIFY(QMetaObject::invokeMethod(dark, "picked"));
        QCOMPARE(theme.pair(), QStringLiteral("nord-pair"));
        QCOMPARE(theme.mode(), QStringLiteral("dark"));
        QVERIFY(nord->property("checked").toBool());
        QCOMPARE(window->property("bg").value<QColor>(), QColor(QStringLiteral("#2e3440")));
        QCOMPARE(window->property("danger").value<QColor>(), QColor(QStringLiteral("#ecc4c8")));
        // Visual only: nothing about the vault or state changed.
        QCOMPARE(c.state(), QStringLiteral("unlocked"));
    }

    void pushStateAndNoticeKind()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        Controller &b = c;
        QCOMPARE(c.pushState(), QStringLiteral("waiting"));
        b.setPushStatus(QStringLiteral("Ready"));
        QCOMPARE(c.pushState(), QStringLiteral("ready"));
        b.setPushStatus(QStringLiteral("Ubuntu Push error: x"));
        QCOMPARE(c.pushState(), QStringLiteral("error"));
        setUp(c);
        QVERIFY(c.addAccount(QStringLiteral("otpauth://totp/A:b?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ")));
        QCOMPARE(c.noticeKind(), QStringLiteral("success"));
        QVERIFY(!c.addAccount(QStringLiteral("otpauth://totp/A:b?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ")));
        QCOMPARE(c.noticeKind(), QStringLiteral("warning"));
        QVERIFY(!c.addAccount(QStringLiteral("not a link")));
        QCOMPARE(c.noticeKind(), QStringLiteral("danger"));
    }

    void duplicateAccountIsRefused()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        setUp(c);
        QVERIFY(c.addAccount(QStringLiteral("otpauth://totp/A:b?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ")));
        // Same secret under another label still yields the same codes.
        QVERIFY(!c.addAccount(QStringLiteral("otpauth://totp/Other:name?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ")));
        QVERIFY(c.notice().contains(QStringLiteral("already")));
        QCOMPARE(c.accounts().size(), 1);
        QVERIFY(c.addAccount(QStringLiteral("otpauth://totp/A:c?secret=KRSXG5CTMVRXEZLUKRSXG5CTMVRXEZLU")));
        QCOMPARE(c.accounts().size(), 2);
    }

    void unlockFinishingAfterLockStaysLocked()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        setUp(c);
        c.addAccount(QStringLiteral("otpauth://totp/A:b?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ"));
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
        QCOMPARE(c.attemptsLeft(), kWipeAfterFailures); // ...so it was not counted either
        QCOMPARE(c.retryIn(), 0);
    }

    // A vault that cannot be read is never erased, however often the right passcode is typed.
    void unreadableVaultIsNeverErased()
    {
        if (geteuid() == 0)
            QSKIP("root ignores file permissions");
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        setUp(c);
        c.lock();
        const QString vault = dir.filePath(QStringLiteral("vault.json"));
        QVERIFY(QFile::setPermissions(vault, QFileDevice::Permissions()));
        for (int i = 0; i < kWipeAfterFailures + 1; ++i) {
            QCOMPARE(c.retryIn(), 0);
            c.unlock(pass);
            QTRY_VERIFY(!c.busy());
            QVERIFY(c.notice().contains(QStringLiteral("not checked")));
        }
        QVERIFY(QFile::setPermissions(vault, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QCOMPARE(c.state(), QStringLiteral("locked"));
        QCOMPARE(c.attemptsLeft(), kWipeAfterFailures);
        c.unlock(pass);
        QTRY_COMPARE(c.state(), QStringLiteral("unlocked"));
    }

    void wrongPasscodeCountsDown()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        setUp(c);
        c.lock();
        c.unlock(QStringLiteral("999999x"));
        QTRY_VERIFY(!c.busy());
        QCOMPARE(c.attemptsLeft(), kWipeAfterFailures - 1);
        c.unlock(pass);
        QTRY_COMPARE(c.state(), QStringLiteral("unlocked"));
        QCOMPARE(c.attemptsLeft(), kWipeAfterFailures);
    }

    // After the clock moves back, the delay counts down from now instead of freezing.
    void retryDelayCountsDownAfterClockGoesBack()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        Controller &b = c;
        setUp(c);
        c.lock();
        b.m_failures = 2;
        b.m_lastFailureMs = QDateTime::currentMSecsSinceEpoch() + 86400000;
        QCOMPARE(c.retryIn(), retryDelaySeconds(2));
        QTRY_VERIFY_WITH_TIMEOUT(c.retryIn() < retryDelaySeconds(2), 4000);
        QTRY_COMPARE_WITH_TIMEOUT(c.retryIn(), 0, 8000);
    }

    // Issuers choose the key length: 80 bits works with a warning, a typed setup key works too.
    void shortSetupKeyAddsWithWarning()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        setUp(c);
        QVERIFY(!c.addKey(QStringLiteral(" "), QStringLiteral("JBSWY3DPEHPK3PXP")));
        QVERIFY(!c.addKey(QStringLiteral("Too short"), QStringLiteral("JBSWY3DPEHPK3PX")));
        QVERIFY(c.addKey(QStringLiteral("GitHub: alice"), QStringLiteral("jbsw y3dp ehpk 3pxp")));
        QCOMPARE(c.accounts().size(), 1);
        const QVariantMap a = c.accounts().first().toMap();
        QCOMPARE(a.value(QStringLiteral("title")).toString(), QStringLiteral("GitHub (alice)"));
        QVERIFY(a.value(QStringLiteral("shortKey")).toBool());
        QVERIFY(!a.value(QStringLiteral("requiresReenrolment")).toBool());
        QCOMPARE(a.value(QStringLiteral("code")).toString().size(), 7); // "123 456"
        QVERIFY(c.addAccount(QStringLiteral("otpauth://totp/Strong?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ")));
        QVERIFY(!c.accounts().last().toMap().value(QStringLiteral("shortKey")).toBool());
    }

    // Answered just before a lock: the requeued push must not come back after unlocking.
    void answeredBeforeLockDoesNotReturn()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        Controller &b = c;
        setUp(c);
        pair(c);
        b.handlePushBody(challengePush(c, "c1"));
        c.deny(QStringLiteral("c1"));
        QCOMPARE(c.requests.size(), 1);
        c.lock(); // the user switched away before the reply
        c.requests.at(0).done(200, "{\"success\":false}");
        c.unlock(pass);
        QTRY_COMPARE(c.state(), QStringLiteral("unlocked"));
        QVERIFY(c.challenge().isEmpty());
    }

    // The server did not decide: the request stays so the user can answer again.
    void unavailableServerKeepsRequest()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        Controller &b = c;
        setUp(c);
        pair(c);
        b.handlePushBody(challengePush(c, "c1"));
        for (int status : {503, 429}) {
            c.deny(QStringLiteral("c1"));
            c.requests.last().done(status, {});
            QCOMPARE(c.challenge().value(QStringLiteral("id")).toString(), QStringLiteral("c1"));
        }
        c.deny(QStringLiteral("c1"));
        c.requests.last().done(409, "{\"error\":\"already answered\"}");
        QVERIFY(c.challenge().isEmpty());
    }

    // A full queue keeps the newest push, which is the one the user is waiting for.
    void fullQueueKeepsNewestPush()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        Controller &b = c;
        setUp(c);
        pair(c);
        QList<QByteArray> pushes;
        for (int i = 1; i <= 9; ++i)
            pushes.append(challengePush(c, QByteArray("c" + QByteArray::number(i)).constData()));
        c.lock();
        for (const QByteArray &p : pushes)
            b.handlePushBody(p);
        c.unlock(pass);
        QTRY_COMPARE(c.state(), QStringLiteral("unlocked"));
        QCOMPARE(c.challenge().value(QStringLiteral("id")).toString(), QStringLiteral("c9"));
    }

    // The real QML: a typed setup key asks for a name and adds the account.
    void addSheetTakesASetupKey()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        Theme theme(dir.filePath(QStringLiteral("theme.ini")));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("controller"), &c);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        setUp(c);
        QObject *root = engine.rootObjects().constFirst();
        QObject *field = nullptr;
        QTRY_VERIFY((field = root->findChild<QObject *>(QStringLiteral("otpUri"))));
        QObject *name = root->findChild<QObject *>(QStringLiteral("keyName"));
        QObject *add = root->findChild<QObject *>(QStringLiteral("addAccountButton"));
        QVERIFY(name && add);
        field->setProperty("text", QStringLiteral("otpauth://totp/A?secret=JBSWY3DPEHPK3PXP"));
        QVERIFY(!name->property("needed").toBool()); // a link names itself
        field->setProperty("text", QStringLiteral("JBSW Y3DP EHPK 3PXP"));
        QVERIFY(name->property("needed").toBool());
        QVERIFY(!add->property("enabled").toBool());
        name->setProperty("text", QStringLiteral("Forge"));
        QVERIFY(add->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(add, "clicked"));
        QCOMPARE(c.accounts().size(), 1);
        QCOMPARE(c.accounts().first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Forge"));
    }

    // The real QML: a confirmation never stays over the lock screen.
    void confirmationClosesOnLock()
    {
        QTemporaryDir dir;
        FakeController c(dir.path(), fast);
        Theme theme(dir.filePath(QStringLiteral("theme.ini")));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("controller"), &c);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        setUp(c);
        QObject *confirm = engine.rootObjects().constFirst()->findChild<QObject *>(QStringLiteral("confirm"));
        QVERIFY(confirm);
        QVERIFY(QMetaObject::invokeMethod(confirm, "open"));
        QTRY_VERIFY(confirm->property("visible").toBool());
        c.lock();
        QTRY_VERIFY(!confirm->property("visible").toBool());
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
