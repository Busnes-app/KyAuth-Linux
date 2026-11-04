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
    // derive() then unseal(), for callers that do not count attempts.
    static std::optional<Vault> open(const QString &path, const QString &passcode);
    // Slow: reads the file and derives the key. nullopt when the file or the key derivation
    // fails (scrypt out of memory), which says nothing about the passcode.
    static std::optional<Vault> derive(const QString &path, const QString &passcode);
    // Fast: checks the passcode against the GCM tag and decrypts. False: wrong passcode.
    bool unseal();
    // The file, its format and its KDF header are usable: checked before an attempt is counted.
    static bool readable(const QString &path);

    QJsonObject data;
    bool save() const;
    void clearKey();

private:
    QString m_path;
    QByteArray m_salt;
    QByteArray m_key;
    QByteArray m_sealed; // set by derive() until unseal()
    ScryptParams m_params;
};

// Writes atomically with owner-only permissions.
bool writeFileAtomic(const QString &path, const QByteArray &bytes);

} // namespace ky
