#pragma once

#include "protocol.h"
#include "totp.h"
#include "vault.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <optional>

class UnifiedPush;

// Everything the QML sees. Secrets live only in m_vault while unlocked.
class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)          // setup | locked | unlocked
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(int retryIn READ retryIn NOTIFY tick)
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY tick)
    Q_PROPERTY(QVariantMap pairing READ pairing NOTIFY pairingChanged)
    Q_PROPERTY(QString pushStatus READ pushStatus NOTIFY pushStatusChanged)
    Q_PROPERTY(QVariantMap challenge READ challenge NOTIFY tick)
    Q_PROPERTY(bool ubuntuTouch READ ubuntuTouch CONSTANT)
    Q_PROPERTY(bool qrScanner READ qrScanner CONSTANT)
    Q_PROPERTY(QString license READ license CONSTANT)

public:
    Controller(const QString &dataDir, const ky::ScryptParams &kdf, QObject *parent = nullptr);

    void startPush(); // Nura: UnifiedPush over D-Bus. Ubuntu Touch push starts from QML.

    QString state() const { return m_state; }
    bool busy() const { return m_busy; }
    QString notice() const { return m_notice; }
    int retryIn() const;
    QVariantList accounts() const;
    QVariantMap pairing() const;
    QString pushStatus() const { return m_pushStatus; }
    QVariantMap challenge() const;
    bool ubuntuTouch() const;
    bool qrScanner() const;
    QString license() const;

    Q_INVOKABLE void createPasscode(const QString &passcode, const QString &confirm);
    Q_INVOKABLE void unlock(const QString &passcode);
    Q_INVOKABLE void lock();
    Q_INVOKABLE bool addAccount(const QString &uri);
    Q_INVOKABLE void removeAccount(const QString &id);
    Q_INVOKABLE void copyCode(const QString &id);
    Q_INVOKABLE void pair(const QString &code);
    Q_INVOKABLE void unpair();
    // `challengeId` is the request the screen showed; a replaced request is refused.
    Q_INVOKABLE void approve(const QString &challengeId, const QString &digits);
    Q_INVOKABLE void deny(const QString &challengeId);
    Q_INVOKABLE void clearNotice();

    // Ubuntu Touch PushClient glue.
    Q_INVOKABLE void ubuntuPushToken(const QString &token);
    Q_INVOKABLE void ubuntuPushMessage(const QString &json);
    Q_INVOKABLE void ubuntuPushError(const QString &status);

signals:
    void stateChanged();
    void busyChanged();
    void noticeChanged();
    void tick();
    void pairingChanged();
    void pushStatusChanged();

private:
    void setState(const QString &s);
    void setBusy(bool b);
    void say(const QString &text);
    void setPushStatus(const QString &s);
    void loadState();
    bool saveState() const;
    // Saves the vault; on failure restores `before` and reports it.
    bool saveVault(const QJsonObject &before);
    void clearChallenge();
    void wipe();
    void onUnlocked(ky::Vault vault);

    std::optional<ky::PushCredential> currentPush() const;
    void handlePushBody(const QByteArray &body);
    void refreshPushIfNeeded();
    void respond(const QString &challengeId, bool approve, const QString &digits);
    void notifyDesktop();

protected:
    // HTTPS JSON request; status 0 means no response. Virtual so tests can answer it.
    virtual void request(const QByteArray &verb, const QString &url, const QJsonObject &body,
                         std::function<void(int, const QByteArray &)> done);

private:
    friend class TestController;

    QString m_dir;
    ky::ScryptParams m_kdf;
    QString m_state;
    bool m_busy = false;
    QString m_notice;
    QString m_pushStatus;
    std::optional<ky::Vault> m_vault;
    std::optional<ky::Challenge> m_challenge;
    QByteArray m_challengeBody; // its encrypted push, requeued if locked before an answer
    quint64 m_epoch = 0;        // bumped by lock(): a KDF result from an older epoch is dropped
    bool m_refreshing = false;
    QList<QByteArray> m_pending; // encrypted pushes that arrived while locked
    QTimer m_ticker;
    QNetworkAccessManager m_net;
    UnifiedPush *m_up = nullptr;

    // state.json: not secret, readable while locked.
    int m_failures = 0;
    qint64 m_lastFailureMs = 0;
    QString m_upToken;
    QString m_latestToken; // UnifiedPush endpoint or Ubuntu Push token
};
