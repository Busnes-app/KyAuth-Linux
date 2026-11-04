#pragma once

#include "crypto.h"

#include <QJsonObject>
#include <QString>
#include <optional>

namespace ky {

// Linux has no hardware keystore to pepper a short PIN, so the minimum is 6 and any
// characters are allowed. Returns "" when acceptable.
QString passcodeProblem(const QString &passcode);

// Seconds to wait before the next attempt after `failures` wrong passcodes.
int retryDelaySeconds(int failures);
constexpr int kWipeAfterFailures = 5;

// All secrets in one scrypt + AES-256-GCM sealed JSON file.
class Vault {
public:
    // The first save already holds `data`, so a crash cannot leave a half-made vault.
    static std::optional<Vault> create(const QString &path, const QString &passcode,
                                       const ScryptParams &params = {}, const QJsonObject &data = {});
    static std::optional<Vault> open(const QString &path, const QString &passcode);
    // The file, its format and its KDF header are usable: checked before an attempt is counted.
    static bool readable(const QString &path);

    QJsonObject data;
    bool save() const;
    void clearKey();

private:
    QString m_path;
    QByteArray m_salt;
    QByteArray m_key;
    ScryptParams m_params;
};

// Writes atomically with owner-only permissions.
bool writeFileAtomic(const QString &path, const QByteArray &bytes);

} // namespace ky
