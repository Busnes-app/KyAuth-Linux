#include "controller.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <sys/stat.h>

namespace {

int g_warnings = 0;

void countWarnings(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (type != QtDebugMsg && type != QtInfoMsg)
        ++g_warnings;
    std::fprintf(stderr, "%s\n", qPrintable(msg));
}

// Drives the real QML through setup, adding an account and locking. Fails on any warning.
void runSmokeTest(Controller &c, QGuiApplication &app)
{
    QObject::connect(&c, &Controller::stateChanged, &app, [&c, &app] {
        if (c.state() != QLatin1String("unlocked"))
            return;
        if (!c.addAccount(QStringLiteral("otpauth://totp/Smoke:test?secret=JBSWY3DPEHPK3PXP")))
            ++g_warnings;
        QTimer::singleShot(1500, &app, [&c, &app] {
            if (c.accounts().size() != 1)
                ++g_warnings;
            c.lock();
            QTimer::singleShot(300, &app, [&c, &app] {
                if (c.state() != QLatin1String("locked"))
                    ++g_warnings;
                app.exit(g_warnings == 0 ? 0 : 1);
            });
        });
    });
    QTimer::singleShot(300, &c, [&c] { c.createPasscode(QStringLiteral("402917"), QStringLiteral("402917")); });
    QTimer::singleShot(30000, &app, [&app] { app.exit(2); });
}

} // namespace

int main(int argc, char *argv[])
{
    umask(077);
#ifdef KYAUTH_UBUNTU_TOUCH
    const QString appDir = QStringLiteral("kyauth.busnes"); // the click confinement path
#else
    const QString appDir = QStringLiteral("kyauth");
#endif
    QGuiApplication app(argc, argv);
    // On UT, Qt's own cache paths must fall inside the click's confined directories.
    QGuiApplication::setApplicationName(appDir);
    QGuiApplication::setApplicationDisplayName(QStringLiteral("KyAuth"));
    QGuiApplication::setApplicationVersion(QStringLiteral(KYAUTH_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("app.busnes.kyauth"));
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QLatin1String(QT_VERSION >= QT_VERSION_CHECK(6, 0, 0) ? "Basic" : "Default"));

    const bool smoke = app.arguments().contains(QStringLiteral("--smoke-test"));
    QTemporaryDir smokeDir;
    if (smoke)
        qInstallMessageHandler(countWarnings);
    const QString dataDir = smoke ? smokeDir.path()
                                  : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                                        + QLatin1Char('/') + appDir;
    // Smoke runs use a cheap KDF; the real one costs 128 MiB.
    Controller controller(dataDir, smoke ? ky::ScryptParams{1024, 8, 1} : ky::ScryptParams{});

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (smoke)
        runSmokeTest(controller, app);
    else
        controller.startPush();
    return app.exec();
}
