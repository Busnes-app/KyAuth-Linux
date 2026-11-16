#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <optional>

// KyIdentity wire contract, shared with KyAuth-Android. See docs/push-contract.md.
namespace ky {

struct PairingRequest {
    QString serverUrl;       // normalized: https, no trailing slash
    QString registrationUrl; // same origin as serverUrl
    QString pairingToken;
    QString pinCode;
    QString userId;
    QString username;
};

struct PushCredential {
    QString transport; // "unifiedpush" | "ubuntupush"
    QString token;     // endpoint URL or Ubuntu Push token
    QString p256dh;    // base64url, 65-byte point
    QString auth;      // base64url, 16 bytes
    bool operator==(const PushCredential &o) const
    {
        return transport == o.transport && token == o.token && p256dh == o.p256dh && auth == o.auth;
    }
    bool operator!=(const PushCredential &o) const { return !(*this == o); }
};

struct Challenge {
    QString challengeId;
    QString purpose; // login | step_up
    qint64 expiresAtMs = 0;
    QString title;
    QString body;
};

enum class Decision { Approved, Denied, Error };

// "" when the URL is not an acceptable KyIdentity server.
QString normalizeServerUrl(const QString &url);
// scheme://host[:port], lowercase, default port dropped. "" when invalid.
QString originOf(const QString &url);

// QR text (JSON or kyidentity://native-pair link) to a request. Sets `error` on failure.
std::optional<PairingRequest> parsePairing(const QString &text, qint64 nowSeconds, QString *error);

// QR pairing sends `userId` and proves the token in a header; PIN pairing sends `pinCode`.
// `pairingToken` is never sent.
QJsonObject registrationBody(const PairingRequest &req, const QString &deviceName,
                             const QString &deviceIdentifier, const QByteArray &spki,
                             const PushCredential &push);
// Returns deviceId and userId on success, else sets `error`.
std::optional<std::pair<QString, QString>> parseRegistration(int status, const QByteArray &body,
                                                             const QString &fallbackUserId, QString *error);

// KyAuth pairing proof v1, QR pairing only. The key is SHA-256 of the QR pairing token.
inline constexpr char kPairingProofHeader[] = "KyIdentity-Pairing-Proof";
QByteArray pairingKey(const QString &pairingToken);
// base64url-no-pad HMAC-SHA256 over the exact request body bytes.
QByteArray pairingProof(const QByteArray &key, const QString &origin, const QByteArray &body);
QByteArray pairingReplyProof(const QByteArray &key, const QString &origin, const QByteArray &requestBody,
                             const QByteArray &responseBody);
// Constant-time check of the reply header over the exact response bytes.
bool pairingReplyVerified(const QByteArray &key, const QString &origin, const QByteArray &requestBody,
                          const QByteArray &responseBody, const QByteArray &header);

// Flat all-string JSON from a decrypted push, bound to this pairing.
std::optional<Challenge> parseChallenge(const QByteArray &json, const QString &deviceId,
                                        const QString &userId, qint64 nowMs);

std::optional<QByteArray> pushResponseMessage(const QString &origin, const QString &userId,
                                              const QString &deviceId, const Challenge &c,
                                              bool approve, const QString &digits);
std::optional<QByteArray> pushTokenMessage(const QString &origin, const QString &deviceId,
                                           qint64 issuedAtMs, const PushCredential &push);

Decision parseDecision(int status, const QByteArray &body, const QString &challengeId, QString *error);

// error_description, then error, then a generic message; capped for display.
QString serverError(int status, const QByteArray &body);

} // namespace ky
