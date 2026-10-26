#include "controller.h"

#include <QDir>
#include <QGuiApplication>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <sys/stat.h>
#ifdef KYAUTH_QR
#include <QZXing.h>
#endif

namespace {

int g_warnings = 0;

void countWarnings(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (type != QtDebugMsg && type != QtInfoMsg)
        ++g_warnings;
    std::fprintf(stderr, "%s\n", qPrintable(msg));
}

// Walks the visual tree: Qt 6 Repeater items are not QObject children, so findChild misses them.
const QQuickItem *findItem(const QQuickItem *item, const char *name)
{
    if (item->objectName() == QLatin1String(name))
        return item;
    for (const QQuickItem *child : item->childItems())
        if (const QQuickItem *found = findItem(child, name))
            return found;
    return nullptr;
}

void check(bool ok, const char *failure)
{
    if (!ok) {
        std::fprintf(stderr, "%s\n", failure);
        ++g_warnings;
    }
}

// Drives the real QML through setup, adding an account, each page and locking. Fails on any warning.
void runSmokeTest(Controller &c, QGuiApplication &app, QObject *root)
{
    QObject::connect(&c, &Controller::stateChanged, &app, [&c, &app, root] {
        if (c.state() != QLatin1String("unlocked"))
            return;
        check(c.addAccount(QStringLiteral("otpauth://totp/Smoke:test?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ")), "Adding an account failed.");
        const auto *window = qobject_cast<QQuickWindow *>(root);
        auto item = [window](const char *name) { return window ? findItem(window->contentItem(), name) : nullptr; };
        int delay = 0;
        for (const char *page : {"settings", "about", "connect", "vault"})
            QTimer::singleShot(delay += 400, root, [root, page] { root->setProperty("page", QLatin1String(page)); });
        QTimer::singleShot(1100, &app, [item] {
            const QQuickItem *license = item("licenseText");
            check(license && license->property("text").toString().startsWith(QLatin1String("MIT License")),
                  "The license text did not load.");
        });
        QTimer::singleShot(2000, &app, [&c, &app, item] {
            check(c.accounts().size() == 1, "The account is missing.");
            // The code sits under the title; at 0 the card's layout was lost.
            const QQuickItem *code = item("accountCode");
            check(code && code->y() > 0, "Account card is not laid out.");
            const QQuickItem *left = item("nav_vault"), *mid = item("nav_lock"), *right = item("nav_settings");
            check(left && mid && right && left->width() > 0 && qAbs(left->width() - mid->width()) <= 1.5
                      && qAbs(right->width() - mid->width()) <= 1.5,
                  "The navigation tabs are not equal widths.");
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
    // Lomiri sizes UIs in grid units of 8 px at 1x; a plain Qt Quick app must scale itself.
    const int gridUnit = qEnvironmentVariableIntValue("GRID_UNIT_PX");
    if (gridUnit > 0 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
        qputenv("QT_SCALE_FACTOR", QByteArray::number(gridUnit / 8.0));
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
    // One process owns the data directory: two would each write their own copy of the vault.
    QDir().mkpath(dataDir);
    QLockFile owner(dataDir + QStringLiteral("/kyauth.lock"));
    if (!owner.tryLock(0)) {
        std::fprintf(stderr, "KyAuth is already running.\n");
        return 1;
    }
    // Smoke runs use a cheap KDF; the real one costs 128 MiB.
    Controller controller(dataDir, smoke ? ky::ScryptParams{1024, 8, 1} : ky::ScryptParams{});

#ifdef KYAUTH_QR
    QZXing::registerQMLTypes();
#endif
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (smoke)
        runSmokeTest(controller, app, engine.rootObjects().constFirst());
    else
        controller.startPush();
    return app.exec();
}
