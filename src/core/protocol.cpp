#include "protocol.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

namespace ky {
namespace {

int effectivePort(const QUrl &u) { return u.port(u.scheme() == QLatin1String("https") ? 443 : 80); }

bool sameOrigin(const QUrl &a, const QUrl &b)
{
    return a.scheme().compare(b.scheme(), Qt::CaseInsensitive) == 0
        && a.host().compare(b.host(), Qt::CaseInsensitive) == 0 && effectivePort(a) == effectivePort(b);
}

// https, a host, no userinfo/query/fragment.
bool acceptableHttps(const QUrl &u)
{
    return u.isValid() && u.scheme() == QLatin1String("https") && !u.host().isEmpty()
        && u.userInfo().isEmpty() && !u.hasQuery() && !u.hasFragment() && effectivePort(u) > 0;
}

QString field(const QJsonObject &o, const char *key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString().trimmed() : QString();
}

std::optional<QByteArray> joinFields(const QStringList &fields)
{
    for (const QString &f : fields)
        if (f.contains(QLatin1Char('|')))
            return std::nullopt;
    return fields.join(QLatin1Char('|')).toUtf8();
}

} // namespace

QString normalizeServerUrl(const QString &url)
{
    const QUrl u(url.trimmed());
    if (!acceptableHttps(u))
        return {};
    QString s = u.toString(QUrl::FullyEncoded);
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    return s;
}

QString originOf(const QString &url)
{
    const QUrl u(url.trimmed(), QUrl::StrictMode);
    const QString scheme = u.scheme().toLower();
    if (!u.isValid() || u.host().isEmpty() || (scheme != QLatin1String("https") && scheme != QLatin1String("http")))
        return {};
    QString host = u.host().toLower();
    if (host.contains(QLatin1Char(':')))
        host = QLatin1Char('[') + host + QLatin1Char(']');
    const int defaultPort = scheme == QLatin1String("https") ? 443 : 80;
    const int port = u.port(defaultPort);
    return scheme + QStringLiteral("://") + host
         + (port == defaultPort ? QString() : QLatin1Char(':') + QString::number(port));
}

std::optional<PairingRequest> parsePairing(const QString &text, qint64 nowSeconds, QString *error)
{
    const QString t = text.trimmed();
    PairingRequest r;
    qint64 expiresAt = 0;
    QString server, registration;
    if (t.startsWith(QLatin1Char('{'))) {
        const QJsonObject o = QJsonDocument::fromJson(t.toUtf8()).object();
        static const QStringList types{QStringLiteral("kyidentity_device_pairing"),
                                       QStringLiteral("kysignon_device_pairing"),
                                       QStringLiteral("kypost_device_pairing")};
        if (!types.contains(field(o, "type"))) {
            *error = QStringLiteral("Not a KyIdentity pairing code.");
            return std::nullopt;
        }
        server = field(o, "serverUrl");
        registration = field(o, "registrationUrl");
        r.pairingToken = field(o, "pairingToken");
        r.pinCode = field(o, "pinCode");
        r.userId = field(o, "userId");
        r.username = field(o, "username");
        expiresAt = qint64(o.value(QStringLiteral("expiresAt")).toDouble());
    } else {
        const QUrl u(t);
        static const QStringList schemes{QStringLiteral("kyidentity"), QStringLiteral("kysignon"),
                                         QStringLiteral("kypost")};
        if (!schemes.contains(u.scheme().toLower()) || u.host() != QLatin1String("native-pair")) {
            *error = QStringLiteral("Not a KyIdentity pairing code.");
            return std::nullopt;
        }
        const QUrlQuery q(u);
        auto get = [&q](const char *k) {
            return q.queryItemValue(QLatin1String(k), QUrl::FullyDecoded).trimmed();
        };
        server = get("srv");
        registration = get("reg");
        r.pairingToken = get("pt");
        r.pinCode = get("pin");
        r.userId = get("sub").isEmpty() ? get("uid") : get("sub");
        r.username = get("user").isEmpty() ? get("username") : get("user");
        expiresAt = get("exp").toLongLong();
    }

    r.serverUrl = normalizeServerUrl(server);
    if (r.serverUrl.isEmpty()) {
        *error = QStringLiteral("The server address must be https.");
        return std::nullopt;
    }
    if (registration.isEmpty()) {
        r.registrationUrl = r.serverUrl + QStringLiteral("/api/notifications/native/register");
    } else {
        const QUrl reg(registration);
        if (!acceptableHttps(reg) || !sameOrigin(reg, QUrl(r.serverUrl))) {
            *error = QStringLiteral("The registration address is not on the server.");
            return std::nullopt;
        }
        r.registrationUrl = reg.toString(QUrl::FullyEncoded);
    }
    if (r.pairingToken.isEmpty() && (r.pinCode.isEmpty() || r.userId.isEmpty())) {
        *error = QStringLiteral("The pairing code is incomplete.");
        return std::nullopt;
    }
    if (expiresAt != 0 && expiresAt <= nowSeconds) {
        *error = QStringLiteral("The pairing code has expired. Make a new one.");
        return std::nullopt;
    }
    return r;
}

QJsonObject registrationBody(const PairingRequest &req, const QString &deviceName,
                             const QString &deviceIdentifier, const QByteArray &spki,
                             const PushCredential &push)
{
    QJsonObject o{{QStringLiteral("deviceName"), deviceName},
                  {QStringLiteral("deviceIdentifier"), deviceIdentifier},
                  {QStringLiteral("platform"), QStringLiteral("linux")},
                  {QStringLiteral("publicKey"), QString::fromLatin1(spki.toBase64())},
                  {QStringLiteral("pushToken"), push.token},
                  {QStringLiteral("pushTransport"), push.transport},
                  {QStringLiteral("pushP256dh"), push.p256dh},
                  {QStringLiteral("pushAuth"), push.auth}};
    if (!req.pairingToken.isEmpty())
        o.insert(QStringLiteral("pairingToken"), req.pairingToken);
    if (!req.pinCode.isEmpty())
        o.insert(QStringLiteral("pinCode"), req.pinCode);
    if (!req.userId.isEmpty())
        o.insert(QStringLiteral("userId"), req.userId);
    return o;
}

std::optional<std::pair<QString, QString>> parseRegistration(int status, const QByteArray &body,
                                                             const QString &fallbackUserId, QString *error)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const QString deviceId = field(o, "deviceId");
    if (status < 200 || status > 299 || !o.value(QStringLiteral("success")).toBool() || deviceId.isEmpty()) {
        *error = serverError(status, body);
        return std::nullopt;
    }
    QString userId = field(o.value(QStringLiteral("device")).toObject(), "userId");
    if (userId.isEmpty())
        userId = fallbackUserId;
    if (userId.isEmpty()) {
        *error = QStringLiteral("The server did not say which account this device belongs to.");
        return std::nullopt;
    }
    return std::make_pair(deviceId, userId);
}

std::optional<Challenge> parseChallenge(const QByteArray &json, const QString &deviceId,
                                        const QString &userId, qint64 nowMs)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject() || deviceId.isEmpty() || userId.isEmpty())
        return std::nullopt;
    const QJsonObject o = doc.object();
    for (auto it = o.begin(); it != o.end(); ++it)
        if (!it.value().isString())
            return std::nullopt;

    Challenge c;
    c.challengeId = field(o, "challengeId");
    c.purpose = field(o, "purpose");
    c.title = field(o, "title");
    c.body = field(o, "body");
    bool ok = false;
    c.expiresAtMs = field(o, "expiresAtEpochMs").toLongLong(&ok);
    if (field(o, "type") != QLatin1String("mfa_challenge") || field(o, "deviceId") != deviceId
        || field(o, "deviceUserId") != userId || c.challengeId.isEmpty()
        || (c.purpose != QLatin1String("login") && c.purpose != QLatin1String("step_up"))
        || !ok || c.expiresAtMs <= nowMs || c.expiresAtMs > nowMs + 600000)
        return std::nullopt;
    return c;
}

std::optional<QByteArray> pushResponseMessage(const QString &origin, const QString &userId,
                                              const QString &deviceId, const Challenge &c,
                                              bool approve, const QString &digits)
{
    if (origin.isEmpty() || userId.isEmpty() || deviceId.isEmpty() || c.challengeId.isEmpty()
        || (c.purpose != QLatin1String("login") && c.purpose != QLatin1String("step_up")))
        return std::nullopt;
    return joinFields({QStringLiteral("kyidentity-push-v2"), origin, userId, deviceId, c.challengeId,
                       c.purpose, QString::number(c.expiresAtMs),
                       approve ? QStringLiteral("approve") : QStringLiteral("deny"),
                       approve ? digits : QString()});
}

std::optional<QByteArray> pushTokenMessage(const QString &origin, const QString &deviceId,
                                           qint64 issuedAtMs, const PushCredential &push)
{
    if (origin.isEmpty() || deviceId.isEmpty() || push.token.isEmpty())
        return std::nullopt;
    const QString tokenHash = QString::fromLatin1(
        QCryptographicHash::hash(push.token.toUtf8(), QCryptographicHash::Sha256).toHex());
    return joinFields({QStringLiteral("kyidentity-push-token-v2"), origin, deviceId,
                       QString::number(issuedAtMs), push.transport, tokenHash, push.p256dh, push.auth});
}

Decision parseDecision(int status, const QByteArray &body, const QString &challengeId, QString *error)
{
    if (status < 200 || status > 299) {
        *error = serverError(status, body);
        return Decision::Error;
    }
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const QString echoed = field(o, "challengeId");
    if (!echoed.isEmpty() && echoed != challengeId) {
        *error = QStringLiteral("Server answered a different challenge");
        return Decision::Error;
    }
    const QJsonValue decision = o.contains(QStringLiteral("approved")) ? o.value(QStringLiteral("approved"))
                                                                        : o.value(QStringLiteral("success"));
    if (!decision.isBool()) {
        *error = QStringLiteral("Server did not report a decision");
        return Decision::Error;
    }
    return decision.toBool() ? Decision::Approved : Decision::Denied;
}

QString serverError(int status, const QByteArray &body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    QString msg = field(o, "error_description");
    if (msg.isEmpty())
        msg = field(o, "error");
    if (msg.isEmpty())
        msg = status > 0 ? QStringLiteral("Request failed (%1)").arg(status)
                         : QStringLiteral("Could not reach the server.");
    msg.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")));
    return msg.left(200);
}

} // namespace ky
