#include "totp.h"

#include <QMessageAuthenticationCode>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace ky {
namespace {

QCryptographicHash::Algorithm hashFor(const QString &algorithm)
{
    if (algorithm == QLatin1String("SHA256"))
        return QCryptographicHash::Sha256;
    if (algorithm == QLatin1String("SHA512"))
        return QCryptographicHash::Sha512;
    return QCryptographicHash::Sha1;
}

bool valid(const TotpEntry &e)
{
    return !e.secret.isEmpty() && e.digits >= 6 && e.digits <= 8 && e.period > 0 && e.period <= 3600
        && (e.algorithm == QLatin1String("SHA1") || e.algorithm == QLatin1String("SHA256")
            || e.algorithm == QLatin1String("SHA512"));
}

} // namespace

std::optional<QByteArray> base32Decode(const QString &text)
{
    QByteArray out;
    quint32 buffer = 0;
    int bits = 0;
    for (QChar c : text.toUpper()) {
        if (c.isSpace() || c == QLatin1Char('='))
            continue;
        int v;
        if (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
            v = c.unicode() - 'A';
        else if (c >= QLatin1Char('2') && c <= QLatin1Char('7'))
            v = c.unicode() - '2' + 26;
        else
            return std::nullopt;
        buffer = (buffer << 5) | quint32(v);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.append(char((buffer >> bits) & 0xff));
        }
    }
    return out;
}

std::optional<TotpEntry> parseOtpauth(const QString &uri)
{
    const QUrl url(uri.trimmed());
    if (!url.isValid() || url.scheme().compare(QLatin1String("otpauth"), Qt::CaseInsensitive) != 0
        || url.host().compare(QLatin1String("totp"), Qt::CaseInsensitive) != 0)
        return std::nullopt;
    const QUrlQuery q(url);
    auto secret = base32Decode(q.queryItemValue(QStringLiteral("secret"), QUrl::FullyDecoded));
    if (!secret)
        return std::nullopt;

    TotpEntry e;
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.secret = *secret;
    e.issuer = q.queryItemValue(QStringLiteral("issuer"), QUrl::FullyDecoded).trimmed();
    QString label = url.path(QUrl::FullyDecoded).mid(1).trimmed();
    if (label.isEmpty())
        label = QStringLiteral("Account");
    const int colon = label.indexOf(QLatin1Char(':'));
    if (colon > 0)
        e.title = QStringLiteral("%1 (%2)").arg(label.left(colon).trimmed(), label.mid(colon + 1).trimmed());
    else if (!e.issuer.isEmpty() && e.issuer != label)
        e.title = QStringLiteral("%1 (%2)").arg(e.issuer, label);
    else
        e.title = label;

    const QString alg = q.queryItemValue(QStringLiteral("algorithm")).toUpper();
    e.algorithm = (alg == QLatin1String("SHA256") || alg == QLatin1String("SHA512")) ? alg : QStringLiteral("SHA1");
    bool ok = true;
    if (q.hasQueryItem(QStringLiteral("digits")))
        e.digits = q.queryItemValue(QStringLiteral("digits")).toInt(&ok);
    if (ok && q.hasQueryItem(QStringLiteral("period")))
        e.period = q.queryItemValue(QStringLiteral("period")).toInt(&ok);
    if (!ok || !valid(e))
        return std::nullopt;
    return e;
}

QString totpCode(const TotpEntry &entry, qint64 unixSeconds)
{
    const quint64 counter = quint64(unixSeconds) / quint64(entry.period);
    QByteArray msg(8, 0);
    for (int i = 0; i < 8; ++i)
        msg[7 - i] = char((counter >> (8 * i)) & 0xff);
    const QByteArray mac = QMessageAuthenticationCode::hash(msg, entry.secret, hashFor(entry.algorithm));
    const int off = mac.at(mac.size() - 1) & 0x0f;
    const quint32 bin = (quint32(quint8(mac[off]) & 0x7f) << 24) | (quint32(quint8(mac[off + 1])) << 16)
                      | (quint32(quint8(mac[off + 2])) << 8) | quint32(quint8(mac[off + 3]));
    quint32 mod = 1;
    for (int i = 0; i < entry.digits; ++i)
        mod *= 10;
    return QString::number(bin % mod).rightJustified(entry.digits, QLatin1Char('0'));
}

QString displayCode(const QString &code)
{
    if (code.size() == 6 || code.size() == 8)
        return code.left(code.size() / 2) + QLatin1Char(' ') + code.mid(code.size() / 2);
    return code;
}

QJsonObject TotpEntry::toJson() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("title"), title},
            {QStringLiteral("issuer"), issuer},
            {QStringLiteral("secret"), QString::fromLatin1(secret.toBase64())},
            {QStringLiteral("algorithm"), algorithm},
            {QStringLiteral("digits"), digits},
            {QStringLiteral("period"), period}};
}

std::optional<TotpEntry> TotpEntry::fromJson(const QJsonObject &o)
{
    TotpEntry e;
    e.id = o.value(QStringLiteral("id")).toString();
    e.title = o.value(QStringLiteral("title")).toString();
    e.issuer = o.value(QStringLiteral("issuer")).toString();
    e.secret = QByteArray::fromBase64(o.value(QStringLiteral("secret")).toString().toLatin1());
    e.algorithm = o.value(QStringLiteral("algorithm")).toString();
    e.digits = o.value(QStringLiteral("digits")).toInt();
    e.period = o.value(QStringLiteral("period")).toInt();
    if (e.id.isEmpty() || !valid(e))
        return std::nullopt;
    return e;
}

} // namespace ky
