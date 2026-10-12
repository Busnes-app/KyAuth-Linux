#include "unifiedpush.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QRegularExpression>
#include <QUrl>

namespace {

const QString kPrefix = QStringLiteral("org.unifiedpush.Distributor.");
const QString kPath = QStringLiteral("/org/unifiedpush/Distributor");
const QString kIface = QStringLiteral("org.unifiedpush.Distributor2");

QStringList distributors()
{
    QDBusInterface bus(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                       QStringLiteral("org.freedesktop.DBus"));
    QStringList names = QDBusReply<QStringList>(bus.call(QStringLiteral("ListNames"))).value()
                      + QDBusReply<QStringList>(bus.call(QStringLiteral("ListActivatableNames"))).value();
    names = names.filter(QRegularExpression(QStringLiteral("^org\\.unifiedpush\\.Distributor\\.[A-Za-z0-9_.-]+$")));
    names.removeDuplicates();
    names.sort();
    return names;
}

} // namespace

UnifiedPush::UnifiedPush(QObject *parent) : QObject(parent) {}

void UnifiedPush::start(const QString &token)
{
    m_token = token;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.registerService(QLatin1String(kService))
        || !bus.registerObject(QStringLiteral("/org/unifiedpush/Connector"), this, QDBusConnection::ExportAllSlots)) {
        emit failed(tr("Could not listen on D-Bus. Is KyAuth already running?"));
        return;
    }
    const QStringList found = distributors();
    const QString wanted = kPrefix + qEnvironmentVariable("UNIFIEDPUSH_DISTRIBUTOR");
    m_distributor = found.contains(wanted) ? wanted : found.value(0);
    if (m_distributor.isEmpty()) {
        emit failed(tr("No UnifiedPush distributor found. Install one, such as KUnifiedPush."));
        return;
    }
    QDBusInterface dist(m_distributor, kPath, kIface, bus);
    const QVariantMap args{{QStringLiteral("service"), QLatin1String(kService)},
                           {QStringLiteral("token"), m_token},
                           {QStringLiteral("description"), tr("KyAuth sign-in requests")}};
    auto *watcher = new QDBusPendingCallWatcher(dist.asyncCall(QStringLiteral("Register"), args), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *w;
        const QVariantMap r = reply.isError() ? QVariantMap() : reply.value();
        if (r.value(QStringLiteral("success")).toString() != QLatin1String("REGISTRATION_SUCCEEDED"))
            emit failed(tr("The push distributor refused KyAuth (%1).")
                            .arg(reply.isError() ? reply.error().name() : r.value(QStringLiteral("reason")).toString()));
    });
}

void UnifiedPush::unregister()
{
    if (m_distributor.isEmpty() || m_token.isEmpty())
        return;
    QDBusInterface(m_distributor, kPath, kIface, QDBusConnection::sessionBus())
        .asyncCall(QStringLiteral("Unregister"), QVariantMap{{QStringLiteral("token"), m_token}});
}

bool UnifiedPush::fromDistributor(const QVariantMap &args) const
{
    if (m_token.isEmpty() || args.value(QStringLiteral("token")).toString() != m_token || !calledFromDBus())
        return false;
    const QString owner = connection().interface()->serviceOwner(m_distributor).value();
    return !owner.isEmpty() && owner == message().service();
}

QVariantMap UnifiedPush::NewEndpoint(const QVariantMap &args)
{
    const QString endpoint = args.value(QStringLiteral("endpoint")).toString();
    const QUrl url(endpoint, QUrl::StrictMode);
    if (fromDistributor(args) && endpoint.size() <= 1000 && url.scheme() == QLatin1String("https") && !url.host().isEmpty())
        emit newEndpoint(endpoint);
    return {};
}

QVariantMap UnifiedPush::Message(const QVariantMap &args)
{
    QVariantMap out;
    if (args.contains(QStringLiteral("id")))
        out.insert(QStringLiteral("id"), args.value(QStringLiteral("id")).toString());
    const QByteArray body = args.value(QStringLiteral("message")).toByteArray();
    if (fromDistributor(args) && !body.isEmpty() && body.size() <= 4096)
        emit received(body);
    return out;
}

QVariantMap UnifiedPush::Unregistered(const QVariantMap &args)
{
    if (fromDistributor(args))
        emit unregistered();
    return {};
}
