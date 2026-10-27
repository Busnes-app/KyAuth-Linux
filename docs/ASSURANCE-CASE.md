# Assurance case

[Product overview](../README.md) · [Push contract](push-contract.md) · [Verifying releases](VERIFYING-RELEASES.md)

This is the argument that KyAuth for Linux phones meets its security requirements: what it
protects, from whom, where the trust boundaries are, and why the design and the code hold at
each one. The contracts live in [AGENTS.md](../AGENTS.md) and the
[README's security model](../README.md#security-model). Paths are relative to the repository
root.

## What KyAuth must protect

KyAuth holds TOTP secrets and a device signing key that KyIdentity trusts to approve
sign-ins. Its security requirements:

1. **Only the passcode holder can read the secrets or approve a sign-in.** Everything secret
   sits in one encrypted file, opened only by the passcode.
2. **A sign-in is approved only on purpose.** The user types the two digits the sign-in
   screen shows, for the request on screen, before it expires.
3. **Answers go only to the paired server, over HTTPS.**
4. **The push path learns nothing and cannot inject requests.** Pushes are end-to-end
   encrypted to keys only this phone holds; no plaintext push path exists.
5. **Guessing the passcode on the phone is bounded.** Delays, then erasure after five
   failures.

## Threat model

| Attacker | Can | Wants |
|---|---|---|
| Network attacker | Read and alter traffic | Steal tokens, forge answers, pair with a fake server |
| Push service, distributor, Ubuntu Push | See and replay push traffic | Learn who signs in where, inject a request |
| Malicious QR code or pairing link | Put any server and registration URL in front of the user | Send the pairing or answers elsewhere |
| Another process on the D-Bus session bus | Call KyAuth's UnifiedPush connector | Inject or unregister pushes |
| Someone holding the phone | Open the app, guess passcodes | Read codes, approve a sign-in |
| Someone with a copy of `vault.json` | Guess passcodes offline | Recover the secrets |

Out of scope: an attacker who runs code as the user on the phone, and a compromised OS.
Linux phones have no hardware keystore, so offline guessing against a copied file is bounded
only by scrypt and the passcode's strength. The README says so and asks for a passphrase.

## Trust boundaries

1. **Phone → KyIdentity.** Every request is refused unless the URL is HTTPS. Redirects are
   not followed, requests time out after 10 s, and at most 64 KiB of a reply is read
   (`Controller::request`, `src/app/controller.cpp`). Before dispatch, that shared path
   requires QtNetwork's actual OpenSSL >=3 backend and sets TLS >=1.2, `VerifyPeer`
   and `CipherString=DEFAULT@SECLEVEL=2`. OpenSSL enforces signature/exchange strength;
   `tlsCertificateKeyAllowed` (`src/core/crypto.cpp`) enforces literal certificate
   floors, including RSA-2047 that newer OpenSSL strength estimates round up.
   Startup materializes the platform trust source (`src/app/main.cpp`); requests
   filter weak anchors and disable on-demand roots, with no empty-list fallback.
   HTTP/1.1 and full handshakes let the documented pre-data `encrypted` signal reject
   weak peer-chain keys before HTTP bytes. The dedicated manager only pools checked
   connections. Qt retains CA/name/time checks.
   This fixed profile replaces inherited cipher/security-level settings, including
   stricter levels. An unavailable backend refuses requests with a readable error
   while local codes remain usable. A pairing's registration URL must have
   the same origin as its server (`parsePairing`, `src/core/protocol.cpp`).
2. **Push transport → phone.** The body is RFC 8291 `aes128gcm`, decrypted with this
   pairing's keys (`webPushDecrypt`, `src/core/crypto.cpp`). A body that does not decrypt is
   dropped (`Controller::handlePushBody`).
3. **D-Bus → UnifiedPush connector.** Calls must carry KyAuth's token and come from the
   current owner of the chosen distributor's bus name (`src/app/unifiedpush.cpp`).
4. **App → its files.** `vault.json` is scrypt plus AES-256-GCM, written atomically with mode
   0600 (`src/core/vault.cpp`). One process owns the data directory (`QLockFile`,
   `src/app/main.cpp`).
5. **Flatpak sandbox.** The app gets Wayland or X11, the network, and two D-Bus names: the
   UnifiedPush distributors and notifications. No filesystem access is granted
   (`packaging/flatpak/app.busnes.kyauth.yaml`).

## Secure design principles applied

- **Least privilege.** The Flatpak asks for no filesystem access. `src/core` depends only on
  QtCore and OpenSSL and holds every protocol and crypto rule. `src/app` is glue.
- **Fail-safe defaults.** A failure count that cannot be saved means the passcode is not
  checked. A vault file with out-of-range scrypt parameters is refused rather than run. A
  push that does not decrypt, names another device or account, or expires more than ten
  minutes ahead is dropped.
- **Complete mediation.** Approve and deny carry the challenge ID the screen showed, and the
  controller refuses any other. A key derivation that finishes after the app locked never
  opens the vault (the lock epoch).
- **Economy of mechanism.** Two libraries: Qt and OpenSSL 3. QZXing is added for scanning on
  Ubuntu Touch.
- **Open design.** scrypt (RFC 7914), AES-256-GCM, P-256 ECDSA and ECDH, HMAC-SHA-256 and
  HKDF, RFC 6238 TOTP, RFC 8291 Web Push. All published.
- **Psychological acceptability.** The README states the offline-guessing cost in hours
  for a 6-digit PIN, and that there is no backup.

## Common weaknesses and how they are countered

| Weakness | Counter | Where |
|---|---|---|
| Secrets at rest | AES-256-GCM under a scrypt key (N=2^17, r=8, p=1), random 16-byte salt, fixed associated data, file mode 0600, atomic write | `src/core/vault.cpp`, `src/core/crypto.cpp` (`scrypt`, `aesGcmSeal`) |
| Tampered vault demanding huge work | scrypt parameters bounded on open (N ≤ 2^18, r ≤ 8, p = 1, 16-byte salt) | `Vault::open` |
| Passcode guessing on the phone | Delays of 5, 30 and 300 s before the 3rd to 5th tries; the 5th failure erases data; the attempt is saved before it is checked | `retryDelaySeconds`, `kWipeAfterFailures` (`src/core/vault.h`), `Controller::unlock` |
| Weak passcodes | At least 6 characters; common and sequential PINs refused | `passcodeProblem` |
| Undersized TOTP keys | Import and generation require 16 decoded bytes for SHA1/SHA256/SHA512. Storage validation retains legacy records, while the account screen shows provider re-enrolment guidance and copying is refused without changing the clipboard | `src/core/totp.cpp` (`parseOtpauth`, `totpCode`, `TotpEntry::fromJson`), `Controller::accounts` / `copyCode`, `qml/Main.qml`; `tests/test_totp.cpp`, `tests/test_controller.cpp` |
| Plaintext or forged push | RFC 8291 decryption only; one record; the padding delimiter is checked | `webPushDecrypt` |
| Push for another device, stale or replayed | Device ID, account, type and purpose must match; the expiry must be in the future and at most 10 minutes ahead | `parseChallenge`, `src/core/protocol.cpp` |
| Answer redirection | HTTPS required on every request; registration URL same-origin; redirects not followed | `Controller::request`, `parsePairing` |
| Weak TLS keys, certificates or exchange parameters | OpenSSL >=3 runtime, TLS >=1.2 and level 2 before dispatch; literal certificate floors (RSA/DH >=2048, EC >=224), filtered explicit anchors, pre-data peer-chain check, CA/name/time checks | `tlsCertificateKeyAllowed`, `Controller::request`, `src/app/main.cpp`; real TLS connections in `tests/test_tls.cpp` |
| Oversized replies | Replies read up to 64 KiB | `Controller::request` |
| Invalid curve points | Every peer public key is checked to be on P-256 before use | `loadP256` (`EVP_PKEY_public_check`) |
| Injection into the UI | Server-supplied text is shown as `Text.PlainText` | `qml/Main.qml` |
| Spoofed D-Bus callbacks | Token match, `calledFromDBus()`, sender must own the distributor's name | `UnifiedPush`, `src/app/unifiedpush.cpp` |
| Clipboard leak | A copied code is cleared after 30 s if it is still there | `src/app/controller.cpp` |
| Weak randomness | Keys, salts and nonces from OpenSSL `RAND_bytes` and `EVP_EC_gen`; a failure aborts | `randomBytes`, `generateP256` |
| Secrets in logs | The source has no logging calls (`qDebug`, `qInfo`, `qWarning`, `qCritical`) | `src/` |
| Memory-safety slips | `-Wall -Wextra -Werror=format-security -fstack-protector-strong`, default `_FORTIFY_SOURCE=2` with toolchain levels of at least 2 retained; OpenSSL objects in `unique_ptr` | `CMakeLists.txt`, `src/core/crypto.cpp` |

TOTP accepts HMAC-SHA-1 because RFC 6238 and most issuers require it. HMAC-SHA-1 is not
affected by SHA-1's collision weakness.

## Residual risk

- **No hardware keystore.** A copied `vault.json` can be attacked offline at scrypt speed.
  A 6-digit PIN falls in about 6 hours on a 20-core desktop (README). Use a passphrase.
- **The device is graded `none`.** There is no hardware attestation, so it cannot be used for
  MFA-grade device sign-on.
- **The click package is unsigned,** and no release is published yet
  ([VERIFYING-RELEASES.md](VERIFYING-RELEASES.md)).
- No dynamic memory-error checking (sanitizers or fuzzing) runs, though the core is C++.
  CodeQL static analysis runs on every push and pull request (`.github/workflows/codeql.yml`). No independent security audit has taken place.

## Evidence

- **Tests:** `ctest` runs `test_crypto` (RFC 8291 and RFC 7914 vectors), `test_totp`
  (RFC 6238), `test_protocol` (pairing, challenge parsing, KyAuth-Android golden strings),
  `test_vault`, `test_controller` (lock, save failure, push requeue, request binding) and a QML
  smoke test that fails on any QML warning.
- **TLS regressions:** `test_tls` drives the real production request through local
  TLS servers. Weak leaf/intermediate/root and directly trusted keys, SHA1-signed
  certificates, DH-1024 and TLS 1.0/1.1 must fail without HTTP application bytes;
  RSA-2048/P-256 TLS 1.2/1.3 and DH-2048 controls must succeed. Low-security fixture
  controls establish that weak test servers are usable. Ordinary trust, expiry,
  hostname, redirect, queued/reentrant refusal and repeated/reused-connection checks
  remain separate assertions. DER boundary checks include RSA-PSS, Ed25519/Ed448,
  malformed input and trailing bytes; they do not claim every such certificate type
  was negotiated end-to-end by every Qt package.
  This does not certify phone packaging, OS push transports or live KyIdentity.
- **D-Bus regressions:** `test_unifiedpush` runs the actual connector on a private
  bus, with an owned distributor process and no installed-service activation.
  It checks token/current-owner admission, owner loss/replacement, binary delivery,
  endpoint/message bounds, registration refusal and unregistration. This does not
  qualify a real phone distributor or Ubuntu Push delivery.
  The Flatpak builds pinned test-only D-Bus tools because its SDK lacks a daemon;
  its assertions remain enabled and cleanup removes that module before producing
  the delivered app. The SDK's stronger fortification level is retained.
- **CI:** every pull request builds and tests on Qt 5 and Qt 6 and builds the arm64 click
  (`.github/workflows/ci.yml`). The release workflow builds and smoke-tests both Flatpak
  architectures on every pull request (`.github/workflows/release.yml`).
- **Review:** changes land through pull requests.

This case is updated when a trust boundary or a security requirement changes. A pull request
that moves one must update this page in the same change.
