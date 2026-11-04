#include "vault.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace ky {
namespace {

const QByteArray kAad = QByteArrayLiteral("kyauth-linux-vault-v1");

bool isRun(const QString &pin)
{
    int up = 0, down = 0, same = 0;
    for (int i = 1; i < pin.size(); ++i) {
        const int d = pin.at(i).unicode() - pin.at(i - 1).unicode();
        up += d == 1;
        down += d == -1;
        same += d == 0;
    }
    const int n = pin.size() - 1;
    return up == n || down == n || same == n;
}

struct Header {
    ScryptParams params;
    QByteArray salt;
    QByteArray sealed;
};

// Everything that can fail before a passcode is involved.
std::optional<Header> readHeader(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value(QStringLiteral("v")).toInt() != 1)
        return std::nullopt;
    const QJsonObject kdf = o.value(QStringLiteral("kdf")).toObject();
    Header h;
    h.params.n = quint64(kdf.value(QStringLiteral("n")).toDouble());
    h.params.r = quint32(kdf.value(QStringLiteral("r")).toInt());
    h.params.p = quint32(kdf.value(QStringLiteral("p")).toInt());
    h.salt = QByteArray::fromBase64(kdf.value(QStringLiteral("salt")).toString().toLatin1());
    // Bound the work a tampered file can demand.
    if (h.params.n < 2 || h.params.n > (1u << 18) || h.params.r < 1 || h.params.r > 8
        || h.params.p != 1 || h.salt.size() != 16)
        return std::nullopt;
    h.sealed = QByteArray::fromBase64(o.value(QStringLiteral("data")).toString().toLatin1());
    if (h.sealed.size() < 12 + 16)
        return std::nullopt;
    return h;
}

} // namespace

QString passcodeProblem(const QString &passcode)
{
    static const QSet<QString> common{
        QStringLiteral("123123"), QStringLiteral("112233"), QStringLiteral("121212"),
        QStringLiteral("696969"), QStringLiteral("11223344"), QStringLiteral("12341234"),
        QStringLiteral("123321"), QStringLiteral("159753"), QStringLiteral("147258"),
        QStringLiteral("102030"), QStringLiteral("101010"), QStringLiteral("131313")};
    if (passcode.size() < 6)
        return QStringLiteral("Use at least 6 characters.");
    if (passcode.size() > 128)
        return QStringLiteral("Use at most 128 characters.");
    bool digitsOnly = true;
    for (QChar c : passcode)
        digitsOnly = digitsOnly && c.isDigit();
    if (digitsOnly && (common.contains(passcode) || isRun(passcode)))
        return QStringLiteral("This PIN is too easy to guess.");
    return {};
}

int retryDelaySeconds(int failures)
{
    static const int delays[] = {0, 0, 5, 30, 300};
    return failures < 0 ? 0 : delays[qMin(failures, 4)];
}

std::optional<Vault> Vault::create(const QString &path, const QString &passcode, const ScryptParams &params,
                                   const QJsonObject &data)
{
    Vault v;
    v.data = data;
    v.m_path = path;
    v.m_params = params;
    v.m_salt = randomBytes(16);
    v.m_key = scrypt(passcode.toUtf8(), v.m_salt, params);
    if (v.m_key.isEmpty() || !v.save())
        return std::nullopt;
    return v;
}

std::optional<Vault> Vault::open(const QString &path, const QString &passcode, bool *unreadable)
{
    bool ignored;
    bool &failed = unreadable ? *unreadable : ignored;
    failed = true; // until the passcode is actually checked
    const auto h = readHeader(path);
    if (!h)
        return std::nullopt;
    Vault v;
    v.m_path = path;
    v.m_params = h->params;
    v.m_salt = h->salt;
    const QByteArray &sealed = h->sealed;
    v.m_key = scrypt(passcode.toUtf8(), v.m_salt, v.m_params);
    if (v.m_key.isEmpty()) // out of memory, or parameters OpenSSL refuses
        return std::nullopt;
    bool wrongKey = false;
    auto plain = aesGcmOpen(v.m_key, kAad, sealed, &wrongKey);
    failed = !plain && !wrongKey;
    if (!plain)
        return std::nullopt;
    v.data = QJsonDocument::fromJson(*plain).object();
    plain->fill(0);
    return v;
}

bool Vault::readable(const QString &path) { return readHeader(path).has_value(); }

bool Vault::save() const
{
    QByteArray plain = QJsonDocument(data).toJson(QJsonDocument::Compact);
    const QByteArray sealed = aesGcmSeal(m_key, kAad, plain);
    plain.fill(0);
    const QJsonObject o{
        {QStringLiteral("v"), 1},
        {QStringLiteral("kdf"), QJsonObject{{QStringLiteral("alg"), QStringLiteral("scrypt")},
                                            {QStringLiteral("n"), double(m_params.n)},
                                            {QStringLiteral("r"), int(m_params.r)},
                                            {QStringLiteral("p"), int(m_params.p)},
                                            {QStringLiteral("salt"), QString::fromLatin1(m_salt.toBase64())}}},
        {QStringLiteral("data"), QString::fromLatin1(sealed.toBase64())}};
    return writeFileAtomic(m_path, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void Vault::clearKey()
{
    m_key.fill(0);
    m_key.clear();
    data = {};
}

bool writeFileAtomic(const QString &path, const QByteArray &bytes)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return f.write(bytes) == bytes.size() && f.commit();
}

} // namespace ky
