#include "controller.h"
#include "unifiedpush.h"

#include <QClipboard>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QSysInfo>
#include <QThread>
#include <QUuid>

using namespace ky;

namespace {

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

QJsonObject keyJson(const P256Key &k)
{
    return {{QStringLiteral("priv"), QString::fromLatin1(k.priv.toBase64())},
            {QStringLiteral("pub"), QString::fromLatin1(k.pub.toBase64())}};
}

P256Key keyFrom(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    return {QByteArray::fromBase64(o.value(QStringLiteral("priv")).toString().toLatin1()),
            QByteArray::fromBase64(o.value(QStringLiteral("pub")).toString().toLatin1())};
}

QJsonObject pushJson(const PushCredential &p)
{
    return {{QStringLiteral("transport"), p.transport}, {QStringLiteral("token"), p.token},
            {QStringLiteral("p256dh"), p.p256dh}, {QStringLiteral("auth"), p.auth}};
}

PushCredential pushFrom(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    return {o.value(QStringLiteral("transport")).toString(), o.value(QStringLiteral("token")).toString(),
            o.value(QStringLiteral("p256dh")).toString(), o.value(QStringLiteral("auth")).toString()};
}

} // namespace

Controller::Controller(const QString &dataDir, const ScryptParams &kdf, QObject *parent)
    : QObject(parent), m_dir(dataDir), m_kdf(kdf)
{
    // Install before this dedicated manager can create any pooled connection.
    connect(&m_net, &QNetworkAccessManager::encrypted, this, [](QNetworkReply *reply) {
        // Qt guarantees no user data at this HTTP/1 signal. New connections get a
        // full handshake; this manager only pools connections checked here.
        const auto chain = reply->sslConfiguration().peerCertificateChain();
        if (chain.isEmpty())
            return reply->abort();
        for (const QSslCertificate &cert : chain)
            if (!tlsCertificateKeyAllowed(cert.toDer()))
                return reply->abort();
    });
    QDir().mkpath(m_dir);
    QFile::setPermissions(m_dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    loadState();
    m_state = QFile::exists(m_dir + QStringLiteral("/vault.json")) ? QStringLiteral("locked") : QStringLiteral("setup");
    m_pushStatus = tr("Starting push…");

    m_ticker.setInterval(1000);
    connect(&m_ticker, &QTimer::timeout, this, [this] {
        if (m_challenge && m_challenge->expiresAtMs <= nowMs())
            clearChallenge();
        emit tick();
    });
    m_ticker.start();

    // Lock whenever the app leaves the foreground.
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
        if (s != Qt::ApplicationActive)
            lock();
    });
}

bool Controller::ubuntuTouch() const
{
#ifdef KYAUTH_UBUNTU_TOUCH
    return true;
#else
    return false;
#endif
}

QString Controller::license() const
{
    QFile f(QStringLiteral(":/LICENSE.txt")); // QML's XMLHttpRequest may not read resources
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

bool Controller::qrScanner() const
{
#ifdef KYAUTH_QR
    return true;
#else
    return false;
#endif
}

// ---- state.json ---------------------------------------------------------------------------

void Controller::loadState()
{
    QFile f(m_dir + QStringLiteral("/state.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m_failures = o.value(QStringLiteral("failures")).toInt();
    m_lastFailureMs = qint64(o.value(QStringLiteral("lastFailureMs")).toDouble());
    m_upToken = o.value(QStringLiteral("unifiedPushToken")).toString();
    m_latestToken = o.value(QStringLiteral("pushToken")).toString();
}

bool Controller::saveState() const
{
    const QJsonObject o{{QStringLiteral("failures"), m_failures},
                        {QStringLiteral("lastFailureMs"), double(m_lastFailureMs)},
                        {QStringLiteral("unifiedPushToken"), m_upToken},
                        {QStringLiteral("pushToken"), m_latestToken}};
    return writeFileAtomic(m_dir + QStringLiteral("/state.json"), QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// ---- lock ---------------------------------------------------------------------------------

int Controller::retryIn() const
{
    const qint64 until = m_lastFailureMs + qint64(retryDelaySeconds(m_failures)) * 1000;
    const qint64 now = nowMs();
    if (now < m_lastFailureMs) // clock went backwards: wait the full delay from now
        return retryDelaySeconds(m_failures);
    return until > now ? int((until - now + 999) / 1000) : 0;
}

void Controller::createPasscode(const QString &passcode, const QString &confirm)
{
    if (m_state != QLatin1String("setup") || m_busy)
        return;
    const QString problem = passcodeProblem(passcode);
    if (!problem.isEmpty())
        return say(problem);
    if (passcode != confirm)
        return say(tr("The passcodes do not match."));
    setBusy(true);
    QPointer<Controller> self(this);
    const QString path = m_dir + QStringLiteral("/vault.json");
    const ScryptParams kdf = m_kdf;
    const quint64 epoch = m_epoch;
    QThread *t = QThread::create([self, path, passcode, kdf, epoch] {
        auto v = Vault::create(path, passcode, kdf);
        QMetaObject::invokeMethod(qGuiApp, [self, v, epoch]() mutable {
            if (!self)
                return;
            self->setBusy(false);
            if (!v)
                return self->say(tr("Could not create the vault."));
            const P256Key pushKey = generateP256();
            v->data = {{QStringLiteral("deviceIdentifier"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                       {QStringLiteral("pushKey"), keyJson(pushKey)},
                       {QStringLiteral("pushAuth"), QString::fromLatin1(randomBytes(16).toBase64())},
                       {QStringLiteral("totp"), QJsonArray()}};
            if (!v->save())
                return self->say(tr("Could not save the vault."));
            if (self->m_epoch != epoch) { // left the foreground meanwhile
                v->clearKey();
                return self->setState(QStringLiteral("locked"));
            }
            self->onUnlocked(*v);
        });
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void Controller::unlock(const QString &passcode)
{
    if (m_state != QLatin1String("locked") || m_busy || retryIn() > 0)
        return;
    // Count the attempt before checking it, so killing the app cannot skip the count.
    ++m_failures;
    m_lastFailureMs = nowMs();
    if (!saveState())
        return say(tr("Could not record the attempt, so the passcode was not checked. Free some storage and try again."));
    setBusy(true);
    QPointer<Controller> self(this);
    const QString path = m_dir + QStringLiteral("/vault.json");
    const quint64 epoch = m_epoch;
    QThread *t = QThread::create([self, path, passcode, epoch] {
        auto v = Vault::open(path, passcode);
        QMetaObject::invokeMethod(qGuiApp, [self, v, epoch]() mutable {
            if (!self)
                return;
            self->setBusy(false);
            if (v) {
                self->m_failures = 0;
                self->m_lastFailureMs = 0;
                self->saveState();
                if (self->m_epoch != epoch) { // left the foreground meanwhile: unlock again
                    v->clearKey();
                    return;
                }
                return self->onUnlocked(*v);
            }
            if (self->m_failures >= kWipeAfterFailures) {
                self->wipe();
                return self->say(tr("Too many wrong passcodes. KyAuth erased its data."));
            }
            self->say(tr("Wrong passcode. %n attempt(s) left before KyAuth erases its data.", nullptr,
                         kWipeAfterFailures - self->m_failures));
        });
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void Controller::onUnlocked(Vault vault)
{
    m_vault = std::move(vault);
    setState(QStringLiteral("unlocked"));
    emit pairingChanged();
    const QList<QByteArray> pending = std::exchange(m_pending, {});
    for (const QByteArray &body : pending)
        handlePushBody(body);
    refreshPushIfNeeded();
    emit tick();
}

void Controller::lock()
{
    ++m_epoch;
    if (m_vault)
        m_vault->clearKey();
    m_vault.reset();
    if (m_challenge && m_pending.size() < 8)
        m_pending.prepend(m_challengeBody); // re-checked, expiry included, on unlock
    clearChallenge();
    if (m_state == QLatin1String("unlocked"))
        setState(QStringLiteral("locked"));
    emit pairingChanged();
    emit tick();
}

void Controller::wipe()
{
    if (m_up)
        m_up->unregister();
    lock();
    QFile::remove(m_dir + QStringLiteral("/vault.json"));
    QFile::remove(m_dir + QStringLiteral("/state.json"));
    m_failures = 0;
    m_lastFailureMs = 0;
    m_upToken.clear();
    m_latestToken.clear();
    m_pending.clear();
    setState(QStringLiteral("setup"));
    setPushStatus(tr("Push stopped. Restart KyAuth after setting it up again."));
}

bool Controller::saveVault(const QJsonObject &before)
{
    if (m_vault->save())
        return true;
    m_vault->data = before;
    say(tr("Could not save, so nothing was changed. Is the storage full?"));
    emit pairingChanged();
    emit tick();
    return false;
}

void Controller::clearChallenge()
{
    m_challenge.reset();
    m_challengeBody.clear();
}

// ---- TOTP ---------------------------------------------------------------------------------

QVariantList Controller::accounts() const
{
    QVariantList out;
    if (!m_vault)
        return out;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const QJsonValue &v : m_vault->data.value(QStringLiteral("totp")).toArray()) {
        auto e = TotpEntry::fromJson(v.toObject());
        if (!e)
            continue;
        out.append(QVariantMap{{QStringLiteral("id"), e->id},
                               {QStringLiteral("title"), e->title},
                               {QStringLiteral("code"), displayCode(totpCode(*e, now))},
                               {QStringLiteral("requiresReenrolment"), e->secret.size() < kMinTotpSecretBytes},
                               {QStringLiteral("remaining"), int(e->period - now % e->period)},
                               {QStringLiteral("period"), e->period}});
    }
    return out;
}

bool Controller::addAccount(const QString &uri)
{
    auto e = parseOtpauth(uri);
    if (!m_vault || !e) {
        say(tr("Use a valid otpauth://totp link with a secret of at least 128 bits (16 decoded bytes). "
               "If the secret is too short, set up two-step verification again with the account provider."));
        return false;
    }
    const QJsonObject before = m_vault->data;
    QJsonArray list = m_vault->data.value(QStringLiteral("totp")).toArray();
    // Same codes means the same account, whatever its label.
    for (const QJsonValue &v : list) {
        const auto old = TotpEntry::fromJson(v.toObject());
        if (old && old->secret == e->secret && old->algorithm == e->algorithm && old->digits == e->digits
            && old->period == e->period) {
            say(tr("%1 is already in your vault.").arg(old->title));
            return false;
        }
    }
    list.append(e->toJson());
    m_vault->data.insert(QStringLiteral("totp"), list);
    if (!saveVault(before))
        return false;
    say(tr("Added %1.").arg(e->title));
    emit tick();
    return true;
}

void Controller::removeAccount(const QString &id)
{
    if (!m_vault)
        return;
    const QJsonObject before = m_vault->data;
    QJsonArray list = m_vault->data.value(QStringLiteral("totp")).toArray();
    for (int i = 0; i < list.size(); ++i)
        if (list.at(i).toObject().value(QStringLiteral("id")).toString() == id)
            list.removeAt(i--);
    m_vault->data.insert(QStringLiteral("totp"), list);
    saveVault(before);
    emit tick();
}

void Controller::copyCode(const QString &id)
{
    for (const QVariant &v : accounts()) {
        const QVariantMap a = v.toMap();
        if (a.value(QStringLiteral("id")).toString() != id)
            continue;
        const QString copied = a.value(QStringLiteral("code")).toString().remove(QLatin1Char(' '));
        if (copied.isEmpty()) {
            say(tr("This secret is too short. Set up two-step verification again with the account provider."));
            return;
        }
        QGuiApplication::clipboard()->setText(copied);
        say(tr("Code copied. It is cleared from the clipboard in 30 seconds."));
        QTimer::singleShot(30000, this, [this, copied] {
            if (QGuiApplication::clipboard()->text() == copied)
                QGuiApplication::clipboard()->clear();
        });
    }
}

// ---- Push ---------------------------------------------------------------------------------

void Controller::startPush()
{
    if (ubuntuTouch())
        return;
    m_up = new UnifiedPush(this);
    connect(m_up, &UnifiedPush::failed, this, &Controller::setPushStatus);
    connect(m_up, &UnifiedPush::newEndpoint, this, [this](const QString &endpoint) {
        m_latestToken = endpoint;
        saveState();
        setPushStatus(tr("Ready"));
        refreshPushIfNeeded();
    });
    connect(m_up, &UnifiedPush::unregistered, this, [this] {
        m_latestToken.clear();
        saveState();
        setPushStatus(tr("The push distributor removed KyAuth. Restart KyAuth to register again."));
    });
    connect(m_up, &UnifiedPush::received, this, [this](const QByteArray &body) {
        handlePushBody(body);
        notifyDesktop();
    });
    if (m_upToken.isEmpty()) {
        m_upToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
        saveState();
    }
    if (!m_latestToken.isEmpty())
        setPushStatus(tr("Ready"));
    m_up->start(m_upToken);
}

void Controller::ubuntuPushToken(const QString &token)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9+/=_.:-]{1,512}$"));
    if (!valid.match(token).hasMatch())
        return setPushStatus(tr("Ubuntu Push returned an unusable token."));
    m_latestToken = token;
    saveState();
    setPushStatus(tr("Ready"));
    refreshPushIfNeeded();
}

void Controller::ubuntuPushMessage(const QString &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    auto body = fromB64url(o.value(QStringLiteral("kyauth")).toString().toLatin1());
    if (body && !body->isEmpty() && body->size() <= 4096)
        handlePushBody(*body);
}

void Controller::ubuntuPushError(const QString &status)
{
    setPushStatus(tr("Ubuntu Push error: %1").arg(status.left(100)));
}

std::optional<PushCredential> Controller::currentPush() const
{
    if (!m_vault || m_latestToken.isEmpty())
        return std::nullopt;
    const P256Key key = keyFrom(m_vault->data.value(QStringLiteral("pushKey")));
    const QByteArray auth = QByteArray::fromBase64(m_vault->data.value(QStringLiteral("pushAuth")).toString().toLatin1());
    return PushCredential{ubuntuTouch() ? QStringLiteral("ubuntupush") : QStringLiteral("unifiedpush"),
                          m_latestToken, QString::fromLatin1(b64url(key.pub)), QString::fromLatin1(b64url(auth))};
}

void Controller::handlePushBody(const QByteArray &body)
{
    if (!m_vault) {
        if (m_pending.size() < 8)
            m_pending.append(body);
        return;
    }
    const QJsonObject p = m_vault->data.value(QStringLiteral("pairing")).toObject();
    if (p.isEmpty())
        return;
    auto plain = webPushDecrypt(keyFrom(m_vault->data.value(QStringLiteral("pushKey"))),
                                QByteArray::fromBase64(m_vault->data.value(QStringLiteral("pushAuth")).toString().toLatin1()),
                                body);
    if (!plain)
        return;
    auto c = parseChallenge(*plain, p.value(QStringLiteral("deviceId")).toString(),
                            p.value(QStringLiteral("userId")).toString(), nowMs());
    if (c) {
        m_challenge = c;
        m_challengeBody = body;
        emit tick();
    }
}

void Controller::refreshPushIfNeeded()
{
    // Pairing and answering call this again when they finish, so a skipped refresh resumes.
    if (!m_vault || m_busy || m_refreshing)
        return;
    const QJsonObject p = m_vault->data.value(QStringLiteral("pairing")).toObject();
    auto push = currentPush();
    if (p.isEmpty() || !push || *push == pushFrom(p.value(QStringLiteral("push"))))
        return;
    const QString server = p.value(QStringLiteral("serverUrl")).toString();
    const QString deviceId = p.value(QStringLiteral("deviceId")).toString();
    const qint64 issuedAt = nowMs();
    auto msg = pushTokenMessage(originOf(server), deviceId, issuedAt, *push);
    if (!msg)
        return;
    QJsonObject body{{QStringLiteral("pushToken"), push->token},
                     {QStringLiteral("pushTransport"), push->transport},
                     {QStringLiteral("pushP256dh"), push->p256dh},
                     {QStringLiteral("pushAuth"), push->auth},
                     {QStringLiteral("issuedAt"), double(issuedAt)},
                     {QStringLiteral("signature"),
                      QString::fromLatin1(signDer(keyFrom(p.value(QStringLiteral("key"))), *msg).toBase64())}};
    const QString url = server + QStringLiteral("/api/notifications/native/devices/")
                      + QString::fromLatin1(QUrl::toPercentEncoding(deviceId)) + QStringLiteral("/push-token");
    const PushCredential sent = *push;
    m_refreshing = true;
    request("PUT", url, body, [this, sent, deviceId](int status, const QByteArray &reply) {
        m_refreshing = false;
        if (status < 200 || status > 299)
            return setPushStatus(tr("Could not update push on the server: %1").arg(serverError(status, reply)));
        if (!m_vault)
            return;
        QJsonObject p = m_vault->data.value(QStringLiteral("pairing")).toObject();
        if (p.value(QStringLiteral("deviceId")).toString() != deviceId)
            return; // re-paired meanwhile
        const QJsonObject before = m_vault->data;
        p.insert(QStringLiteral("push"), pushJson(sent));
        m_vault->data.insert(QStringLiteral("pairing"), p);
        if (!saveVault(before))
            return;
        setPushStatus(tr("Ready"));
        refreshPushIfNeeded(); // the token may have changed again while this was in flight
    });
}

void Controller::notifyDesktop()
{
    // Fixed text: nothing about the request leaves the app.
    QDBusInterface n(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                     QStringLiteral("org.freedesktop.Notifications"));
    n.asyncCall(QStringLiteral("Notify"), QStringLiteral("KyAuth"), 0u, QStringLiteral("app.busnes.kyauth"),
                tr("Sign-in request"), tr("Open KyAuth to approve or deny."), QStringList(),
                QVariantMap{{QStringLiteral("desktop-entry"), QStringLiteral("app.busnes.kyauth")},
                            {QStringLiteral("urgency"), QVariant::fromValue(uchar(2))}},
                -1);
}

// ---- Pairing ------------------------------------------------------------------------------

QVariantMap Controller::pairing() const
{
    if (!m_vault)
        return {};
    const QJsonObject p = m_vault->data.value(QStringLiteral("pairing")).toObject();
    if (p.isEmpty())
        return {};
    return {{QStringLiteral("server"), originOf(p.value(QStringLiteral("serverUrl")).toString())},
            {QStringLiteral("username"), p.value(QStringLiteral("username")).toString()}};
}

void Controller::pair(const QString &code)
{
    if (!m_vault || m_busy)
        return;
    QString error;
    auto req = parsePairing(code, QDateTime::currentSecsSinceEpoch(), &error);
    if (!req)
        return say(error);
    auto push = currentPush();
    if (!push)
        return say(tr("Push is not ready yet: %1").arg(m_pushStatus));

    const P256Key key = generateP256();
    const QString deviceName = tr("KyAuth on %1").arg(QSysInfo::machineHostName().left(60));
    const QJsonObject body = registrationBody(*req, deviceName,
                                              m_vault->data.value(QStringLiteral("deviceIdentifier")).toString(),
                                              spkiDer(key), *push);
    setBusy(true);
    const PairingRequest r = *req;
    const PushCredential sent = *push;
    request("POST", r.registrationUrl, body, [this, r, key, sent](int status, const QByteArray &reply) {
        setBusy(false);
        QString error;
        auto ids = parseRegistration(status, reply, r.userId, &error);
        if (!ids)
            return say(tr("Pairing failed: %1").arg(error));
        if (!m_vault)
            return say(tr("Locked before pairing finished. Pair again."));
        const QJsonObject before = m_vault->data;
        m_vault->data.insert(QStringLiteral("pairing"),
                             QJsonObject{{QStringLiteral("serverUrl"), r.serverUrl},
                                         {QStringLiteral("username"), r.username},
                                         {QStringLiteral("deviceId"), ids->first},
                                         {QStringLiteral("userId"), ids->second},
                                         {QStringLiteral("key"), keyJson(key)},
                                         {QStringLiteral("push"), pushJson(sent)}});
        if (!saveVault(before))
            return;
        clearChallenge();
        say(tr("Paired. Sign-in requests will appear here."));
        emit pairingChanged();
        refreshPushIfNeeded(); // the endpoint may have changed while pairing
    });
}

void Controller::unpair()
{
    if (!m_vault)
        return;
    const QJsonObject before = m_vault->data;
    m_vault->data.remove(QStringLiteral("pairing"));
    if (!saveVault(before))
        return;
    clearChallenge();
    say(tr("Unpaired. Also remove this device in your KyIdentity account."));
    emit pairingChanged();
    emit tick();
}

// ---- Push MFA -----------------------------------------------------------------------------

QVariantMap Controller::challenge() const
{
    if (!m_challenge)
        return {};
    return {{QStringLiteral("id"), m_challenge->challengeId},
            {QStringLiteral("purpose"), m_challenge->purpose},
            {QStringLiteral("title"), m_challenge->title},
            {QStringLiteral("body"), m_challenge->body},
            {QStringLiteral("expiresIn"), int(qMax<qint64>(0, (m_challenge->expiresAtMs - nowMs()) / 1000))}};
}

void Controller::approve(const QString &challengeId, const QString &digits)
{
    static const QRegularExpression twoDigits(QStringLiteral("^\\d{2}$"));
    if (!twoDigits.match(digits).hasMatch())
        return say(tr("Type the 2-digit number shown on the sign-in screen."));
    respond(challengeId, true, digits);
}

void Controller::deny(const QString &challengeId) { respond(challengeId, false, {}); }

void Controller::respond(const QString &challengeId, bool approve, const QString &digits)
{
    if (!m_vault || !m_challenge || m_busy)
        return;
    if (m_challenge->challengeId != challengeId)
        return say(tr("A newer request replaced that one. Check the number again."));
    const QJsonObject p = m_vault->data.value(QStringLiteral("pairing")).toObject();
    const QString server = p.value(QStringLiteral("serverUrl")).toString();
    const QString deviceId = p.value(QStringLiteral("deviceId")).toString();
    const Challenge c = *m_challenge;
    auto msg = pushResponseMessage(originOf(server), p.value(QStringLiteral("userId")).toString(), deviceId, c,
                                   approve, digits);
    const QByteArray sig = msg ? signDer(keyFrom(p.value(QStringLiteral("key"))), *msg) : QByteArray();
    if (sig.isEmpty())
        return say(tr("Could not sign the answer."));
    const QJsonObject body{{QStringLiteral("challengeId"), c.challengeId},
                           {QStringLiteral("deviceId"), deviceId},
                           {QStringLiteral("selectedDigits"), approve ? digits : QString()},
                           {QStringLiteral("approve"), approve},
                           {QStringLiteral("signature"), QString::fromLatin1(sig.toBase64())}};
    setBusy(true);
    request("POST", server + QStringLiteral("/api/mfa/push/respond"), body,
            [this, c, approve](int status, const QByteArray &reply) {
                setBusy(false);
                QString error;
                const Decision d = parseDecision(status, reply, c.challengeId, &error);
                // Answered (or refused) by the server: never offer it again. Keep it on network errors.
                if (status != 0 && m_challenge && m_challenge->challengeId == c.challengeId)
                    clearChallenge();
                emit tick();
                refreshPushIfNeeded();
                if (d == Decision::Error)
                    return say(tr("Not sent: %1").arg(error));
                if (!approve)
                    return say(tr("Denied."));
                say(d == Decision::Approved ? tr("Approved.")
                                            : tr("Not approved. The number did not match; sign in again."));
            });
}

// ---- Plumbing -----------------------------------------------------------------------------

void Controller::request(const QByteArray &verb, const QString &url, const QJsonObject &body,
                         std::function<void(int, const QByteArray &)> done)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    // Qt 5.15's pre-data encrypted signal is only reliable for HTTP/1.
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setTransferTimeout(10000);
    if (req.url().scheme() != QLatin1String("https"))
        return done(0, {});
    const auto unavailableTls = [this, &done] {
        const QString error = tr("Secure networking is unavailable. Install Qt's OpenSSL 3 TLS backend.");
        say(error);
        done(0, QJsonDocument(QJsonObject{{QStringLiteral("error"), error}}).toJson(QJsonDocument::Compact));
    };
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
    if (QSslSocket::activeBackend() != QLatin1String("openssl")
        && !QSslSocket::setActiveBackend(QStringLiteral("openssl")))
        return unavailableTls();
#endif
    // Qt 5.15/6.0 select their backend at build time. Check QtNetwork's actual
    // runtime, not the independent OpenSSL::Crypto library used by the vault.
    if (!QSslSocket::supportsSsl()
        || !QSslSocket::sslLibraryVersionString().startsWith(QLatin1String("OpenSSL "))
        || QSslSocket::sslLibraryVersionNumber() < 0x30000000L)
        return unavailableTls();
    QSslConfiguration tls = req.sslConfiguration();
    tls.setProtocol(QSsl::TlsV1_2OrLater);
    tls.setPeerVerifyMode(QSslSocket::VerifyPeer);
    // Level 2 enforces exchange/signature strength, but newer OpenSSL estimates
    // RSA-2047 as 112 bits. Enforce literal certificate sizes as well.
    tls.setBackendConfigurationOption(QByteArrayLiteral("CipherString"),
                                      QByteArrayLiteral("DEFAULT@SECLEVEL=2"));
    QList<QSslCertificate> anchors;
    for (const QSslCertificate &cert : tls.caCertificates())
        if (tlsCertificateKeyAllowed(cert.toDer()))
            anchors.append(cert);
    // Also disables on-demand roots: no omitted weak anchor can enter the chain.
    // An empty explicit list remains empty, rather than falling back to OS trust.
    tls.setCaCertificates(anchors);
    tls.setSessionTicket(QByteArray());
    tls.setSslOption(QSsl::SslOptionDisableSessionSharing, true);
    tls.setSslOption(QSsl::SslOptionDisableSessionPersistence, true);
    tls.setSslOption(QSsl::SslOptionDisableSessionTickets, true);
    req.setSslConfiguration(tls);
    QNetworkReply *reply = m_net.sendCustomRequest(req, verb, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        done(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
             reply->isReadable() ? reply->read(64 * 1024) : QByteArray());
    });
}

void Controller::setState(const QString &s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

void Controller::setBusy(bool b)
{
    if (m_busy == b)
        return;
    m_busy = b;
    emit busyChanged();
}

void Controller::say(const QString &text)
{
    m_notice = text;
    emit noticeChanged();
}

void Controller::clearNotice() { say({}); }

void Controller::setPushStatus(const QString &s)
{
    if (m_pushStatus == s)
        return;
    m_pushStatus = s;
    emit pushStatusChanged();
}
