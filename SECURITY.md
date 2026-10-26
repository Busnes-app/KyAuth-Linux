# Security Policy

KyAuth for Linux is an authenticator client for KyIdentity. It holds TOTP seeds and
push-approval keys locally and approves MFA on an explicit user action. Android's
device sign-on and passkey provider have not been ported.

## Reporting vulnerabilities

Report security vulnerabilities privately through GitHub Security Advisories:
<https://github.com/Busnes-app/KyAuth-Linux/security/advisories>. Do not open a public issue.
Include the affected area, reproduction steps, impact, and affected versions.
We aim to acknowledge reports within two business days and will coordinate a fix
and disclosure.

## Trust boundaries and limitations

- TOTP import and generation require at least 128 decoded secret bits. Legacy short
  secrets remain stored with their original account metadata, but cannot generate or
  copy a code; the account card explains provider-side re-enrolment. No secrets are
  repaired, rewritten or silently removed. See [migration guidance](README.md#independent-use-and-prerequisites).
- Authenticator secrets live on the device. The app must not upload them or write them to logs or crash reports.
- A push or MFA approval is a deliberate user action. The app must not auto-approve, and a notification alone is not consent.
- The device is the trust boundary: anyone with the unlocked app can use its codes.
  There is no hardware keystore integration. A copied vault can be attacked offline;
  use a strong passphrase and a device lock.

## Operating requirements

- All application requests use QtNetwork's OpenSSL >=3 backend, TLS >=1.2,
  `VerifyPeer`, and `CipherString=DEFAULT@SECLEVEL=2`, applied before dispatch.
  Level 2 checks signature and exchange strength; explicit certificate-key checks
  additionally require RSA/RSA-PSS/DSA/DH >=2048 or EC >=224 (Ed25519/Ed448 are also
  allowed). This avoids OpenSSL versions that round RSA-2047 up to a 112-bit estimate.
  Unknown certificate key types fail closed. Normal CA, hostname and time validation
  remains required; every redirect is refused.
- Startup explicitly loads the platform CA store. Requests filter its weak keys and
  disable on-demand roots; an empty explicitly supplied list never falls back to system
  CAs. Private CAs belong in the platform store. Restart after changing that store.
  The pre-data `encrypted` check rejects weak peer-chain keys; HTTP/2 and TLS session
  sharing/persistence/tickets are disabled for predictable Qt 5.15/Qt 6 checks. The
  dedicated manager can reuse already checked HTTP/1.1 connections. Replace weak
  server/CA keys instead of disabling verification.
- The fixed supported profile replaces inherited cipher/security-level settings,
  including stricter levels. It covers the app's pairing, refresh and MFA requests,
  not the external push transport. No weak-key override is offered. If Qt cannot use
  OpenSSL >=3, networking fails with guidance while local TOTP remains available.
- Keep passcode protection and foreground locking enabled.

## Security-sensitive changes

Changes to secret storage, enrollment, push-approval handling, passkey flows, or logging require tests for both the expected path and the relevant
failure or attack path, plus a description of the trust boundary in the pull
request.

## Related documents

- [CONTRIBUTING.md](CONTRIBUTING.md) — contribution and review requirements
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) — community expectations
- [README.md](README.md) — what this project is and how to run it

