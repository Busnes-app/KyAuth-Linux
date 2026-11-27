#pragma once

#include <QByteArray>
#include <optional>

namespace ky {

// A P-256 key pair as raw bytes: 32-byte private scalar, 65-byte uncompressed public point.
struct P256Key {
    QByteArray priv;
    QByteArray pub;
};

QByteArray randomBytes(int n);
QByteArray b64url(const QByteArray &bytes); // no padding
std::optional<QByteArray> fromB64url(const QByteArray &text);

P256Key generateP256();
QByteArray spkiDer(const P256Key &key);
// ECDSA P-256 over SHA-256, DER signature. Empty on failure.
QByteArray signDer(const P256Key &key, const QByteArray &message);
bool verifyDer(const QByteArray &spki, const QByteArray &message, const QByteArray &signature);

// Hybrid ML-DSA-65 + Ed25519 signing key matching ky-primitives/hybridsig ("mldsa65+ed25519").
struct HybridKey {
    QByteArray mldsaSeed; // 32 bytes
    QByteArray edSeed;    // 32 bytes
    QByteArray pub;       // 2000 bytes: "mldsa65+ed25519\0" || ML-DSA-65(1952) || Ed25519(32)
};

inline constexpr char kHybridPushResponseLabel[] = "kyidentity/1/push-response";
inline constexpr char kHybridPushTokenLabel[] = "kyidentity/1/push-token-refresh";

bool hybridSigAvailable();
std::optional<HybridKey> generateHybridSig();
std::optional<HybridKey> hybridSigFromSeeds(const QByteArray &mldsaSeed, const QByteArray &edSeed);
QByteArray signHybridSig(const HybridKey &key, const QByteArray &label, const QByteArray &message);
bool verifyHybridSig(const QByteArray &pubKey, const QByteArray &label, const QByteArray &message,
                     const QByteArray &signature);

// Literal TLS certificate public-key floors only; TLS still verifies trust and signatures.
bool tlsCertificateKeyAllowed(const QByteArray &certificateDer);

// RFC 8291 aes128gcm body (single record) addressed to `receiver` with auth secret `auth`.
std::optional<QByteArray> webPushDecrypt(const P256Key &receiver, const QByteArray &auth,
                                         const QByteArray &body);
// The sender side, padded to one record of `recordSize` bytes. The app never sends pushes;
// tests use it to stand in for the server. Empty on failure.
QByteArray webPushEncrypt(const QByteArray &receiverPub, const QByteArray &auth,
                          const QByteArray &plaintext, int recordSize = 2048);

struct ScryptParams {
    quint64 n = 1u << 17; // 128 MiB with r = 8
    quint32 r = 8;
    quint32 p = 1;
};
QByteArray scrypt(const QByteArray &passcode, const QByteArray &salt, const ScryptParams &params);

// AES-256-GCM. seal returns iv(12) || ciphertext || tag(16).
QByteArray aesGcmSeal(const QByteArray &key, const QByteArray &aad, const QByteArray &plaintext);
std::optional<QByteArray> aesGcmOpen(const QByteArray &key, const QByteArray &aad,
                                     const QByteArray &sealed);

} // namespace ky
