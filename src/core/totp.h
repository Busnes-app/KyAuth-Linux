#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <optional>

namespace ky {

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
// otpauth://totp/... only. HOTP and malformed URIs return nullopt.
std::optional<TotpEntry> parseOtpauth(const QString &uri);
QString totpCode(const TotpEntry &entry, qint64 unixSeconds);
QString displayCode(const QString &code); // "123 456"

} // namespace ky
