#include "unifiedpush.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QProcess>
#include <QtTest>
#include <cstdio>

namespace {
const QString distributor = QStringLiteral("org.unifiedpush.Distributor.KyAuthTest");
const QString path = QStringLiteral("/org/unifiedpush/Distributor");
const QString interface = QStringLiteral("org.unifiedpush.Distributor2");
const QString token = QStringLiteral("disposable-public-test-token");

QVariantMap call(const QString &service, const QString &method, const QVariantList &args = {})
{
    auto message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(args);
    QDBusPendingCallWatcher pending(QDBusConnection::sessionBus().asyncCall(message, 2000));
    QSignalSpy finished(&pending, &QDBusPendingCallWatcher::finished);
    if (!pending.isFinished() && !finished.wait(3000)) {
        QTest::qFail("private test bus call timed out", __FILE__, __LINE__);
        return {};
    }
    const QDBusPendingReply<QVariantMap> reply = pending;
    if (reply.isError()) {
        QTest::qFail(qPrintable(reply.error().message()), __FILE__, __LINE__);
        return {};
    }
    return reply.value();
}

class DistributorProcess : public QProcess {
public:
    ~DistributorProcess() override
    {
        if (state() != NotRunning) { kill(); waitForFinished(3000); }
    }
    bool launch()
    {
        start(QCoreApplication::applicationFilePath(), {QStringLiteral("--distributor")});
        return waitForStarted(3000) && waitForReadyRead(3000)
            && readAllStandardOutput().trimmed() == QByteArray("ready");
    }
};
} // namespace

// A separate process gives delivery calls the distributor's real unique bus owner.
class TestDistributor : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.unifiedpush.Distributor2")
    QVariantMap registration, removal;
    bool refuse = false;
public slots:
    QVariantMap Register(const QVariantMap &args)
    {
        registration = args;
        return {{QStringLiteral("success"), refuse ? QStringLiteral("REFUSED") : QStringLiteral("REGISTRATION_SUCCEEDED")},
                {QStringLiteral("reason"), QStringLiteral("test refusal")}};
    }
    QVariantMap Unregister(const QVariantMap &args) { removal = args; return {}; }
    QVariantMap Stats() { return {{QStringLiteral("registration"), registration}, {QStringLiteral("removal"), removal}}; }
    QVariantMap Refuse() { refuse = true; return {}; }
    QVariantMap Release() { return {{QStringLiteral("released"), QDBusConnection::sessionBus().unregisterService(distributor)}}; }
    QVariantMap Deliver(const QString &method, const QVariantMap &args)
    {
        auto message = QDBusMessage::createMethodCall(QLatin1String(UnifiedPush::kService),
            QStringLiteral("/org/unifiedpush/Connector"), QStringLiteral("org.unifiedpush.Connector2"), method);
        message.setArguments({args});
        const QDBusReply<QVariantMap> reply = QDBusConnection::sessionBus().call(message, QDBus::BlockWithGui, 2000);
        return reply.isValid() ? reply.value() : QVariantMap{{QStringLiteral("fixture_error"), reply.error().name()}};
    }
};

class TestUnifiedPush : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        qputenv("UNIFIEDPUSH_DISTRIBUTOR", "KyAuthTest");
        QTRY_VERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered(distributor).value());
    }
    void cleanup()
    {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(QStringLiteral("/org/unifiedpush/Connector"));
        bus.unregisterService(QLatin1String(UnifiedPush::kService));
        qunsetenv("UNIFIEDPUSH_DISTRIBUTOR");
    }
    void missingAndRefusedDistributorsReportFailure()
    {
        UnifiedPush push;
        QSignalSpy failed(&push, &UnifiedPush::failed);
        push.unregister();
        push.start(token);
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.takeFirst().at(0).toString().contains(QStringLiteral("No UnifiedPush")));
        cleanup();
        DistributorProcess process;
        QVERIFY(process.launch());
        call(distributor, QStringLiteral("Refuse"));
        push.start(token);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed.first().at(0).toString().contains(QStringLiteral("test refusal")));
    }
    void deliveryRequiresTheTokenAndCurrentBusOwner()
    {
        DistributorProcess original;
        QVERIFY(original.launch());
        const QString oldOwner = QDBusConnection::sessionBus().interface()->serviceOwner(distributor).value();
        QVERIFY(!oldOwner.isEmpty());
        UnifiedPush push;
        QSignalSpy endpoints(&push, &UnifiedPush::newEndpoint), messages(&push, &UnifiedPush::received);
        QSignalSpy unregistered(&push, &UnifiedPush::unregistered), failed(&push, &UnifiedPush::failed);
        push.start(token);
        QTRY_COMPARE(qdbus_cast<QVariantMap>(call(distributor, QStringLiteral("Stats")).value(QStringLiteral("registration"))).value(QStringLiteral("token")).toString(), token);
        const auto registered = qdbus_cast<QVariantMap>(call(distributor, QStringLiteral("Stats")).value(QStringLiteral("registration")));
        QCOMPARE(registered.value(QStringLiteral("service")).toString(), QLatin1String(UnifiedPush::kService));
        QVERIFY(!registered.value(QStringLiteral("description")).toString().isEmpty());
        QCOMPARE(failed.count(), 0);
        UnifiedPush duplicate;
        QSignalSpy duplicateFailed(&duplicate, &UnifiedPush::failed);
        duplicate.start(token);
        QCOMPARE(duplicateFailed.count(), 1);
        const auto deliver = [&](const QString &owner, const QString &method, const QVariantMap &args) {
            const auto reply = call(owner, QStringLiteral("Deliver"), {method, args});
            if (reply.contains(QStringLiteral("fixture_error"))) QTest::qFail("fixture delivery failed", __FILE__, __LINE__);
            return reply;
        };
        const QVariantMap endpoint{{QStringLiteral("token"), token}, {QStringLiteral("endpoint"), QStringLiteral("https://push.example.invalid/endpoint")}};
        // Direct C++ calls cannot impersonate a D-Bus sender, even with the token.
        push.NewEndpoint(endpoint);
        QCOMPARE(endpoints.count(), 0);
        deliver(oldOwner, QStringLiteral("NewEndpoint"), endpoint);
        QCOMPARE(endpoints.count(), 1);
        for (const QString &bad : {QStringLiteral("http://push.example.invalid/e"), QStringLiteral("https:///missing-host"), QString(1001, QLatin1Char('x'))}) {
            auto args = endpoint; args[QStringLiteral("endpoint")] = bad;
            deliver(oldOwner, QStringLiteral("NewEndpoint"), args);
        }
        auto wrong = endpoint; wrong[QStringLiteral("token")] = QStringLiteral("wrong-token");
        deliver(oldOwner, QStringLiteral("NewEndpoint"), wrong);
        QCOMPARE(endpoints.count(), 1);
        const QByteArray ciphertext = QByteArray::fromHex("00017f80ff"); // Public binary transport canary.
        const QVariantMap body{{QStringLiteral("token"), token}, {QStringLiteral("id"), QStringLiteral("receipt-1")}, {QStringLiteral("message"), ciphertext}};
        QCOMPARE(deliver(oldOwner, QStringLiteral("Message"), body).value(QStringLiteral("id")).toString(), QStringLiteral("receipt-1"));
        QCOMPARE(messages.count(), 1);
        QCOMPARE(messages.first().at(0).toByteArray(), ciphertext);
        for (int size : {0, 4096, 4097}) {
            auto args = body; args[QStringLiteral("message")] = QByteArray(size, 'x');
            deliver(oldOwner, QStringLiteral("Message"), args);
        }
        QCOMPARE(messages.count(), 2); // Only the 4096-byte boundary is admitted.
        auto wrongBody = body; wrongBody[QStringLiteral("token")] = QStringLiteral("wrong-token");
        deliver(oldOwner, QStringLiteral("Message"), wrongBody);
        QCOMPARE(messages.count(), 2);
        // The app's own bus identity has no distributor authority.
        auto attack = QDBusMessage::createMethodCall(QLatin1String(UnifiedPush::kService),
            QStringLiteral("/org/unifiedpush/Connector"), QStringLiteral("org.unifiedpush.Connector2"), QStringLiteral("Message"));
        attack.setArguments({body});
        QDBusPendingCallWatcher rejected(QDBusConnection::sessionBus().asyncCall(attack, 2000));
        QSignalSpy done(&rejected, &QDBusPendingCallWatcher::finished);
        QVERIFY(rejected.isFinished() || done.wait(3000));
        QVERIFY(!QDBusPendingReply<QVariantMap>(rejected).isError());
        QCOMPARE(messages.count(), 2);
        QVERIFY(call(oldOwner, QStringLiteral("Release")).value(QStringLiteral("released")).toBool());
        deliver(oldOwner, QStringLiteral("Message"), body);
        QCOMPARE(messages.count(), 2);
        DistributorProcess replacement;
        QVERIFY(replacement.launch());
        deliver(oldOwner, QStringLiteral("Message"), body);
        QCOMPARE(messages.count(), 2);
        deliver(distributor, QStringLiteral("Message"), body);
        QCOMPARE(messages.count(), 3);
        deliver(oldOwner, QStringLiteral("Unregistered"), {{QStringLiteral("token"), token}});
        QCOMPARE(unregistered.count(), 0);
        deliver(distributor, QStringLiteral("Unregistered"), {{QStringLiteral("token"), QStringLiteral("wrong-token")}});
        QCOMPARE(unregistered.count(), 0);
        deliver(distributor, QStringLiteral("Unregistered"), {{QStringLiteral("token"), token}});
        QCOMPARE(unregistered.count(), 1);
        push.unregister();
        QTRY_COMPARE(qdbus_cast<QVariantMap>(call(distributor, QStringLiteral("Stats")).value(QStringLiteral("removal"))).value(QStringLiteral("token")).toString(), token);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (!qEnvironmentVariableIsSet("KYAUTH_TEST_PRIVATE_BUS")) {
        std::fputs("Run this test through CTest's private dbus-run-session fixture.\n", stderr);
        return 2;
    }
    if (app.arguments().contains(QStringLiteral("--distributor"))) {
        TestDistributor mock;
        auto bus = QDBusConnection::sessionBus();
        if (!bus.registerService(distributor) || !bus.registerObject(path, &mock, QDBusConnection::ExportAllSlots)) return 2;
        std::puts("ready"); std::fflush(stdout);
        return app.exec();
    }
    TestUnifiedPush tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_unifiedpush.moc"
