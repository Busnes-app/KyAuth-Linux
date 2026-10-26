#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <optional>

namespace ky {

constexpr int kMinTotpSecretBytes = 16; // RFC 4226 section 4: at least 128 bits.

struct TotpEntry {
    QString id;
    QString title;
    QString issuer;
    QByteArray secret;   // decoded key bytes
    QString algorithm;   // SHA1 | SHA256 | SHA512
    int digits = 6;      // 6..8
    int period = 30;     // seconds, > 0

    QJsonObject toJson() const;
    static std::optional<TotpEntry> fromJson(const QJsonObject &o);
};

std::optional<QByteArray> base32Decode(const QString &text);
// otpauth://totp/... only. HOTP, malformed URIs and undersized secrets return nullopt.
std::optional<TotpEntry> parseOtpauth(const QString &uri);
// Empty for an invalid/undersized entry; legacy records remain loadable through fromJson.
QString totpCode(const TotpEntry &entry, qint64 unixSeconds);
QString displayCode(const QString &code); // "123 456"

} // namespace ky
