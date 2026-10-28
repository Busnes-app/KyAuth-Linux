# KyAuth for Linux phones

[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/15920/badge)](https://www.bestpractices.dev/projects/15920)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/Busnes-app/KyAuth-Linux/badge)](https://scorecard.dev/viewer/?uri=github.com/Busnes-app/KyAuth-Linux)
[![OctoCounts](https://api.octocounts.com/badge/Busnes-app/KyAuth-Linux/branch/master)](https://octocounts.com/github/Busnes-app/KyAuth-Linux/tree/master)

Authenticator codes and KyIdentity push sign-in approvals for **Nura** (postmarketOS) and
**Ubuntu Touch**. A port of KyAuth-Android.

- **Codes:** TOTP (RFC 6238; SHA-1, SHA-256 and SHA-512; 6 to 8 digits). Scan the
  account's QR code (Ubuntu Touch) or paste its `otpauth://totp/` link. An account already
  in the vault is refused.
- **Push sign-in:** pair with KyIdentity once. Each sign-in then arrives on the phone, and you
  approve it by typing the two-digit number shown on the sign-in screen.
- **Passcode lock:** a passcode encrypts everything. The app locks as soon as it leaves the
  screen.

## Independent use and prerequisites

Local TOTP works without a Ky server. Set a strong passphrase, add an `otpauth://totp/`
setup link (or scan on Ubuntu Touch), and verify a generated code at the issuing site.
Keep its recovery codes separately; this app has no vault backup/export/sync workflow.
New setup links require at least 128 bits of secret material (16 decoded bytes),
as required by RFC 4226. Short secrets are refused for every supported HMAC algorithm.
An older account with a short secret stays in your encrypted vault with its original
name, issuer, ID and secret, but shows **Unavailable** instead of a code. Use the
provider's recovery/sign-in process to set up two-step verification again, confirm
the new code there, then explicitly remove the old entry when it is no longer needed.
Do not pad or otherwise change the old secret yourself: it must match the provider.
This update does not rewrite or remove old accounts or change the vault format.

For push MFA you additionally need a compatible HTTPS KyIdentity server and the transport
for your platform. Linux device sign-on for other apps is not implemented.

The code supports Nura/postmarketOS and Ubuntu Touch, plus Linux desktop evaluation.
Source/CI tests exercise protocol vectors and UI components; they do not establish real
phone background delivery, camera behavior or a store release. Verify those on your target.

## How push works

| | Nura | Ubuntu Touch |
|---|---|---|
| Transport | UnifiedPush, through a D-Bus distributor such as KUnifiedPush | Ubuntu Push (push.ubports.com) |
| Payload | RFC 8291 encrypted to keys held only by this phone | The same encrypted payload |

On both, the push service sees only ciphertext and timing. On Ubuntu Touch the notification
text is fixed ("Sign-in request") and says nothing about who is signing in or where. The
server needs KyIdentity's `linux` platform and `ubuntupush` transport; see
[docs/push-contract.md](docs/push-contract.md).

On Nura, KyAuth needs a UnifiedPush distributor. If you have several, set
`UNIFIEDPUSH_DISTRIBUTOR` to choose one; otherwise the first, alphabetically, is used. When a
push arrives, the distributor starts KyAuth normally, at its lock screen, rather than headless
as the UnifiedPush spec suggests. Approving needs you to unlock it anyway.

KyIdentity only delivers to push endpoints on public addresses. A self-hosted ntfy on your LAN
or tailnet needs `KYIDENTITY_ALLOW_PRIVATE_CALLBACKS=true` on the server.

## Pairing

1. In KyIdentity, open Devices and create a pairing code. It expires after 90 seconds.
2. In KyAuth, open Settings, then Connect your account.
3. On Ubuntu Touch, tap Scan QR code. On Nura, scan the code with a QR app such as
   Megapixels, copy the text, tap Enter code manually, then Paste and Pair.

KyAuth scans with QZXing, which Ubuntu Touch ships; the click build requires it. Qt 6 builds
have no scanner, because QZXing's camera filter is Qt 5 only.

## Security model

- One file, `vault.json`, holds the TOTP secrets, the device signing key (ECDSA P-256) and
  the push decryption keys. It is encrypted with AES-256-GCM, under a key derived from your
  passcode with scrypt (N=2^17, r=8, p=1).
- **Linux phones have no hardware keystore.** Android peppers its PIN with a key in secure
  hardware. Here, anyone who copies the file can try passcodes offline at scrypt speed, so a
  6-digit PIN falls in about 6 hours on a 20-core desktop (measured: 0.46 s per guess per
  core). **Use a passphrase.**
- Five wrong passcodes in a row erase KyAuth's data. Waits of 5 s, 30 s and 300 s come before
  the 3rd, 4th and 5th tries.
- KyIdentity grades the device `none`, because there is no hardware attestation. Push MFA is
  unaffected, but this device cannot be used for MFA-grade device sign-on.
- A copied code is cleared from the clipboard after 30 seconds.
- Pairing, credential refresh and MFA responses require Qt's OpenSSL 3 TLS backend,
  TLS 1.2 or later, and normal certificate/hostname verification. The application sets
  OpenSSL security level 2 before sending, plus literal certificate-key checks:
  RSA/DH keys below 2048 bits, EC keys below 224 bits and weak certificate signatures
  are refused, including weak keys in the trusted chain. Redirects are never followed.
  There is no weaker compatibility mode.
  Replace an undersized server/CA key and reissue its certificates; install your private
  CA through the platform's normal trust mechanism rather than bypassing verification.
  A missing OpenSSL 3 backend disables network requests with a readable error; local
  TOTP remains available.

The supported TLS profile is fixed at `DEFAULT@SECLEVEL=2`. This explicitly replaces
inherited TLS <=1.2 cipher selections/security levels, even if a system selected level
3 or higher; it is not a promise to preserve a stricter administrator profile. The
TLS policy covers KyAuth's own requests, not the separate OS push service/distributor.
At startup the app loads the platform's CA certificates explicitly, then each request
filters out weak anchors and disables lazy root loading. It uses HTTP/1.1 and full TLS
handshakes so Qt 5.15 and Qt 6 can check the peer's chain before sending any HTTP data.
Checked HTTP connections can be reused. Certificate keys must be recognized RSA/RSA-PSS,
DSA/DH, EC, Ed25519 or Ed448 at the stated floors; other key types fail closed.

## Install

**Distribution status:** releases start at v0.5.0. If the
[releases page](https://github.com/Busnes-app/KyAuth-Linux/releases) has none yet, the click and
Flatpak commands below have nothing to install; use [Build](#build) instead.

**Ubuntu Touch:** download `kyauth.busnes_<version>_arm64.click` from the
[latest release](https://github.com/Busnes-app/KyAuth-Linux/releases/latest) and open it on the
phone.

**Nura and Linux desktops:** add KyAuth's signed Flatpak repository once; `flatpak update` then
keeps it current.

```sh
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak remote-add --if-not-exists kyauth https://busnes-app.github.io/KyAuth-Linux/kyauth.flatpakrepo
flatpak install kyauth app.busnes.kyauth
```

Or install `kyauth-<arch>.flatpak` from the release; it updates from the same repository. See
[docs/DISTRIBUTION.md](docs/DISTRIBUTION.md) for how releases are built.

## Build

You need Qt 5.15 or Qt 6 (Core, Network, Gui, Qml, Quick, QuickControls2, DBus, Test),
OpenSSL 3, CMake 3.16 or later, and `dbus-run-session` (the `dbus-daemon` package)
for tests. On Qt 5, QZXing and Qt Multimedia add QR scanning when
found.

```sh
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

**Nura:** run `sudo cmake --install build`. This installs the binary, the desktop entry, the
icon, and the D-Bus activation file the distributor uses to start KyAuth.

**Ubuntu Touch:** run `clickable build --arch arm64 --accept-review-errors` (Clickable 8.9 or
later). Install the click from `build/aarch64-linux-gnu/app/` with `clickable install`.

## Security and contributing

- Report vulnerabilities privately through the repository's **Security** tab
  ([policy](https://github.com/Busnes-app/.github/blob/main/SECURITY.md)).
- [Contributing](CONTRIBUTING.md) — process, tests and coding standard.
- [Verifying releases](docs/VERIFYING-RELEASES.md) — check the Flatpak signing key and what is signed (untested: no release is published yet).
- [Assurance case](docs/ASSURANCE-CASE.md) — threat model, trust boundaries, and why the security requirements are met.

## Not ported

KyIdentity device sign-on for other apps, and the passkey provider. Both depend on Android
system services.
