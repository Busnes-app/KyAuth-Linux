#pragma once

#include <QDBusContext>
#include <QObject>
#include <QVariantMap>

// UnifiedPush D-Bus connector, spec DBUS_0.3.0 (org.unifiedpush.Connector2).
class UnifiedPush : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.unifiedpush.Connector2")

public:
    static constexpr const char *kService = "app.busnes.kyauth";

    explicit UnifiedPush(QObject *parent = nullptr);
    // Claims the bus name and registers `token` with a distributor. Emits failed() when impossible.
    void start(const QString &token);
    void unregister();

signals:
    void newEndpoint(const QString &endpoint);
    void received(const QByteArray &body);
    void unregistered();
    void failed(const QString &reason);

public slots: // D-Bus methods, called by the distributor only
    QVariantMap NewEndpoint(const QVariantMap &args);
    QVariantMap Message(const QVariantMap &args);
    QVariantMap Unregistered(const QVariantMap &args);

private:
    bool fromDistributor(const QVariantMap &args) const;

    QString m_token;
    QString m_distributor;
};
