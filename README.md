# KyAuth for Linux phones

Authenticator codes and KyIdentity push sign-in approvals for **Nura** (postmarketOS) and
**Ubuntu Touch**. A port of KyAuth-Android.

- **Codes:** TOTP (RFC 6238; SHA-1, SHA-256 and SHA-512; 6 to 8 digits). Scan the
  account's QR code (Ubuntu Touch) or paste its `otpauth://totp/` link. An account already
  in the vault is refused.
- **Push sign-in:** pair with KyIdentity once. Each sign-in then arrives on the phone, and you
  approve it by typing the two-digit number shown on the sign-in screen.
- **Passcode lock:** a passcode encrypts everything. The app locks as soon as it leaves the
  screen.

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

## Install

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
OpenSSL 3, and CMake 3.16 or later. On Qt 5, QZXing and Qt Multimedia add QR scanning when
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

## Not ported

KyIdentity device sign-on for other apps, and the passkey provider. Both depend on Android
system services.
