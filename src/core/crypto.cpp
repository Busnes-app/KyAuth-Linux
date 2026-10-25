#include "crypto.h"

#include <QtGlobal>
#include <memory>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/param_build.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

namespace ky {
namespace {

template<typename T, void (*F)(T *)>
struct Free {
    void operator()(T *p) const { F(p); }
};
using Pkey = std::unique_ptr<EVP_PKEY, Free<EVP_PKEY, EVP_PKEY_free>>;
using PkeyCtx = std::unique_ptr<EVP_PKEY_CTX, Free<EVP_PKEY_CTX, EVP_PKEY_CTX_free>>;
using MdCtx = std::unique_ptr<EVP_MD_CTX, Free<EVP_MD_CTX, EVP_MD_CTX_free>>;
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, Free<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free>>;
using ParamBld = std::unique_ptr<OSSL_PARAM_BLD, Free<OSSL_PARAM_BLD, OSSL_PARAM_BLD_free>>;
using Params = std::unique_ptr<OSSL_PARAM, Free<OSSL_PARAM, OSSL_PARAM_free>>;
using Bn = std::unique_ptr<BIGNUM, Free<BIGNUM, BN_clear_free>>;
using Kdf = std::unique_ptr<EVP_KDF, Free<EVP_KDF, EVP_KDF_free>>;
using KdfCtx = std::unique_ptr<EVP_KDF_CTX, Free<EVP_KDF_CTX, EVP_KDF_CTX_free>>;

const uchar *u(const QByteArray &b) { return reinterpret_cast<const uchar *>(b.constData()); }
uchar *u(QByteArray &b) { return reinterpret_cast<uchar *>(b.data()); }

// Builds an EC key from raw parts. Without `priv` it is a public key, checked to be on the curve.
Pkey loadP256(const QByteArray &priv, const QByteArray &pub)
{
    if (pub.size() != 65 || (!priv.isEmpty() && priv.size() != 32))
        return {};
    ParamBld bld(OSSL_PARAM_BLD_new());
    Bn bn(priv.isEmpty() ? nullptr : BN_bin2bn(u(priv), 32, nullptr));
    if (!bld || !OSSL_PARAM_BLD_push_utf8_string(bld.get(), OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0)
        || !OSSL_PARAM_BLD_push_octet_string(bld.get(), OSSL_PKEY_PARAM_PUB_KEY, pub.constData(), 65)
        || (bn && !OSSL_PARAM_BLD_push_BN(bld.get(), OSSL_PKEY_PARAM_PRIV_KEY, bn.get())))
        return {};
    Params params(OSSL_PARAM_BLD_to_param(bld.get()));
    PkeyCtx ctx(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr));
    EVP_PKEY *raw = nullptr;
    const int selection = bn ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY;
    if (!params || !ctx || EVP_PKEY_fromdata_init(ctx.get()) != 1
        || EVP_PKEY_fromdata(ctx.get(), &raw, selection, params.get()) != 1)
        return {};
    Pkey key(raw);
    PkeyCtx check(EVP_PKEY_CTX_new(key.get(), nullptr));
    if (!check || EVP_PKEY_public_check(check.get()) != 1)
        return {};
    return key;
}

// RFC 5869 HKDF-SHA256, extract and expand.
QByteArray hkdf(const QByteArray &salt, const QByteArray &ikm, const QByteArray &info, int length)
{
    Kdf kdf(EVP_KDF_fetch(nullptr, "HKDF", nullptr));
    KdfCtx ctx(kdf ? EVP_KDF_CTX_new(kdf.get()) : nullptr);
    const auto octets = [](const char *key, const QByteArray &b) {
        return OSSL_PARAM_construct_octet_string(key, const_cast<char *>(b.constData()), size_t(b.size()));
    };
    const OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, const_cast<char *>("SHA256"), 0),
        octets(OSSL_KDF_PARAM_SALT, salt), octets(OSSL_KDF_PARAM_KEY, ikm), octets(OSSL_KDF_PARAM_INFO, info),
        OSSL_PARAM_construct_end()};
    QByteArray out(length, 0);
    if (!ctx || EVP_KDF_derive(ctx.get(), u(out), size_t(length), params) != 1)
        return {};
    return out;
}

std::optional<QByteArray> aesGcm(bool encrypt, const QByteArray &key, const QByteArray &iv,
                                 const QByteArray &aad, const QByteArray &input)
{
    const EVP_CIPHER *cipher = key.size() == 16 ? EVP_aes_128_gcm()
                             : key.size() == 32 ? EVP_aes_256_gcm() : nullptr;
    if (!cipher || iv.size() != 12 || (!encrypt && input.size() < 16))
        return std::nullopt;
    const QByteArray body = encrypt ? input : input.left(input.size() - 16);
    QByteArray tag = encrypt ? QByteArray(16, 0) : input.right(16);
    QByteArray out(body.size(), 0);
    CipherCtx ctx(EVP_CIPHER_CTX_new());
    int len = 0;
    if (!ctx || EVP_CipherInit_ex(ctx.get(), cipher, nullptr, u(key), u(iv), encrypt ? 1 : 0) != 1
        || (!aad.isEmpty() && EVP_CipherUpdate(ctx.get(), nullptr, &len, u(aad), aad.size()) != 1)
        || EVP_CipherUpdate(ctx.get(), u(out), &len, u(body), body.size()) != 1)
        return std::nullopt;
    if (!encrypt && EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, tag.data()) != 1)
        return std::nullopt;
    int tail = 0;
    if (EVP_CipherFinal_ex(ctx.get(), u(out) + len, &tail) != 1)
        return std::nullopt;
    if (encrypt) {
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1)
            return std::nullopt;
        out += tag;
    }
    return out;
}

// RFC 8291 section 3.4: ECDH between `self` and `peerPub`, then the content key and nonce.
std::optional<std::pair<QByteArray, QByteArray>> contentKeys(const P256Key &self, const QByteArray &peerPub,
                                                             const QByteArray &uaPub, const QByteArray &asPub,
                                                             const QByteArray &auth, const QByteArray &salt)
{
    Pkey own = loadP256(self.priv, self.pub);
    Pkey peer = loadP256({}, peerPub);
    if (!own || !peer)
        return std::nullopt;
    PkeyCtx ctx(EVP_PKEY_CTX_new(own.get(), nullptr));
    size_t len = 32;
    QByteArray ecdh(32, 0);
    if (!ctx || EVP_PKEY_derive_init(ctx.get()) != 1 || EVP_PKEY_derive_set_peer(ctx.get(), peer.get()) != 1
        || EVP_PKEY_derive(ctx.get(), u(ecdh), &len) != 1 || len != 32)
        return std::nullopt;
    const QByteArray ikm = hkdf(auth, ecdh, QByteArray("WebPush: info", 14) + uaPub + asPub, 32);
    return std::make_pair(hkdf(salt, ikm, QByteArray("Content-Encoding: aes128gcm", 28), 16),
                          hkdf(salt, ikm, QByteArray("Content-Encoding: nonce", 24), 12));
}

} // namespace

QByteArray randomBytes(int n)
{
    QByteArray b(n, 0);
    if (RAND_bytes(u(b), n) != 1)
        qFatal("RAND_bytes failed");
    return b;
}

QByteArray b64url(const QByteArray &bytes)
{
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

std::optional<QByteArray> fromB64url(const QByteArray &text)
{
    auto r = QByteArray::fromBase64Encoding(
        text, QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!r)
        return std::nullopt;
    return *r;
}

P256Key generateP256()
{
    Pkey key(EVP_EC_gen("P-256"));
    BIGNUM *raw = nullptr;
    if (!key || EVP_PKEY_get_bn_param(key.get(), OSSL_PKEY_PARAM_PRIV_KEY, &raw) != 1)
        qFatal("P-256 key generation failed");
    Bn bn(raw);
    P256Key out{QByteArray(32, 0), QByteArray(65, 0)};
    size_t len = 0;
    if (BN_bn2binpad(bn.get(), u(out.priv), 32) != 32
        || EVP_PKEY_get_octet_string_param(key.get(), OSSL_PKEY_PARAM_PUB_KEY, u(out.pub), 65, &len) != 1
        || len != 65)
        qFatal("P-256 key export failed");
    return out;
}

QByteArray spkiDer(const P256Key &key)
{
    Pkey pkey = loadP256({}, key.pub);
    if (!pkey)
        return {};
    uchar *der = nullptr;
    const int len = i2d_PUBKEY(pkey.get(), &der);
    if (len <= 0)
        return {};
    QByteArray out(reinterpret_cast<char *>(der), len);
    OPENSSL_free(der);
    return out;
}

QByteArray signDer(const P256Key &key, const QByteArray &message)
{
    Pkey pkey = loadP256(key.priv, key.pub);
    MdCtx md(EVP_MD_CTX_new());
    size_t len = 0;
    if (!pkey || !md || EVP_DigestSignInit(md.get(), nullptr, EVP_sha256(), nullptr, pkey.get()) != 1
        || EVP_DigestSign(md.get(), nullptr, &len, u(message), message.size()) != 1)
        return {};
    QByteArray sig(int(len), 0);
    if (EVP_DigestSign(md.get(), u(sig), &len, u(message), message.size()) != 1)
        return {};
    sig.resize(int(len));
    return sig;
}

bool verifyDer(const QByteArray &spki, const QByteArray &message, const QByteArray &signature)
{
    const uchar *p = u(spki);
    Pkey pkey(d2i_PUBKEY(nullptr, &p, spki.size()));
    MdCtx md(EVP_MD_CTX_new());
    return pkey && md && EVP_DigestVerifyInit(md.get(), nullptr, EVP_sha256(), nullptr, pkey.get()) == 1
        && EVP_DigestVerify(md.get(), u(signature), signature.size(), u(message), message.size()) == 1;
}

std::optional<QByteArray> webPushDecrypt(const P256Key &receiver, const QByteArray &auth,
                                         const QByteArray &body)
{
    // RFC 8188 header: salt(16) | rs(4) | idlen(1) | keyid(idlen = sender public key).
    if (auth.size() != 16 || body.size() < 86 + 17 || quint8(body[20]) != 65)
        return std::nullopt;
    const QByteArray salt = body.left(16);
    const quint32 rs = (quint32(quint8(body[16])) << 24) | (quint32(quint8(body[17])) << 16)
                     | (quint32(quint8(body[18])) << 8) | quint32(quint8(body[19]));
    const QByteArray senderPub = body.mid(21, 65);
    const QByteArray record = body.mid(86);
    if (rs < 18 || quint32(record.size()) > rs) // one record only
        return std::nullopt;

    auto keys = contentKeys(receiver, senderPub, receiver.pub, senderPub, auth, salt);
    if (!keys)
        return std::nullopt;
    auto plain = aesGcm(false, keys->first, keys->second, {}, record);
    if (!plain)
        return std::nullopt;
    // Strip zero padding; the last record ends with delimiter 0x02.
    int end = plain->size() - 1;
    while (end >= 0 && plain->at(end) == 0)
        --end;
    if (end < 0 || plain->at(end) != 2)
        return std::nullopt;
    return plain->left(end);
}

QByteArray webPushEncrypt(const QByteArray &receiverPub, const QByteArray &auth,
                          const QByteArray &plaintext, int recordSize)
{
    const int pad = recordSize - 16 - 1 - plaintext.size(); // tag, delimiter
    if (auth.size() != 16 || pad < 0)
        return {};
    const P256Key sender = generateP256();
    const QByteArray salt = randomBytes(16);
    auto keys = contentKeys(sender, receiverPub, receiverPub, sender.pub, auth, salt);
    auto record = keys ? aesGcm(true, keys->first, keys->second, {}, plaintext + '\x02' + QByteArray(pad, 0))
                       : std::nullopt;
    if (!record)
        return {};
    const quint32 rs = quint32(recordSize);
    QByteArray header = salt;
    header += char(rs >> 24);
    header += char(rs >> 16);
    header += char(rs >> 8);
    header += char(rs);
    header += char(65);
    return header + sender.pub + *record;
}

QByteArray scrypt(const QByteArray &passcode, const QByteArray &salt, const ScryptParams &params)
{
    QByteArray key(32, 0);
    const quint64 maxmem = 128 * params.n * params.r * params.p + (64u << 20);
    if (EVP_PBE_scrypt(passcode.constData(), passcode.size(), u(salt), salt.size(), params.n, params.r,
                       params.p, maxmem, u(key), 32) != 1)
        return {};
    return key;
}

QByteArray aesGcmSeal(const QByteArray &key, const QByteArray &aad, const QByteArray &plaintext)
{
    const QByteArray iv = randomBytes(12);
    auto ct = aesGcm(true, key, iv, aad, plaintext);
    if (!ct)
        qFatal("AES-GCM seal failed");
    return iv + *ct;
}

std::optional<QByteArray> aesGcmOpen(const QByteArray &key, const QByteArray &aad, const QByteArray &sealed)
{
    if (sealed.size() < 12 + 16)
        return std::nullopt;
    return aesGcm(false, key, sealed.left(12), aad, sealed.mid(12));
}

} // namespace ky
