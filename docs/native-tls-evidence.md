# Native TLS PQ evidence

Scope: PQC Wave 0 tail, plan rows #8 / #15, KyAuth for Linux's native HTTPS path.
This qualifies hybrid TLS key exchange, not post-quantum certificates, device
signatures, vault encryption or the external push transport.

## Runtime and policy

`Controller::request` in `src/app/controller.cpp` uses QtNetwork's OpenSSL backend
and `QSslConfiguration`: TLS >=1.2, peer verification, the existing level-2 cipher
policy, filtered trust anchors and pre-data certificate checks. It leaves the
key-exchange group list at OpenSSL defaults. OpenSSL >=3.5 can therefore negotiate
`X25519MLKEM768` while retaining classical groups for older peers. A classical-only
`Groups`/elliptic-curve override would remove that capability.

CI's Qt 5 and Qt 6 test jobs run on `ubuntu-26.04` and set
`KYAUTH_REQUIRE_PQ_TLS=1`. The TLS suite checks **Qt's loaded runtime** is OpenSSL
>=3.5 (`QSslSocket::sslLibraryVersionNumber()`), separately from the library used
by the vault or the `openssl` CLI. Each matrix leg selects its Qt major explicitly.
The `native-tls-qt5` / `native-tls-qt6` artifacts retain CTest's `LastTest.log`,
including Qt/TLS versions and negotiated groups.

The application build/runtime floor remains OpenSSL 3.0 for Ubuntu Touch 24.04.
Without the CI flag, older runtimes skip only the new hybrid qualification cases;
they do not earn a PQ claim. Changing a GitHub runner does not upgrade the Ubuntu
Touch SDK or Flatpak runtime. Release packaging/publishing runners and package
runtimes are outside this runner change. No installed-phone PQ qualification is
claimed here.

## Automated proof

`tests/test_tls.cpp`, `tls13Groups`, calls the real production
`Controller::request`; it does not override the request or restrict client groups.
A throwaway trusted RSA-2048 certificate authenticates a loopback-only OpenSSL
`s_server`, which binds an OS-assigned port and serves a fixed HTTP response.
The test checks the response and the peer's actual TLS 1.3 `ServerHello`
`key_share`, including its named-group ID. Offered/shared groups and cipher names
are not used as proof of negotiation. The peer is reaped even on failure.

- Hybrid-capable peer (`X25519MLKEM768:X25519`): requires `X25519MLKEM768` (4588).
- Hybrid-only peer (`X25519MLKEM768`): requires the same group.
- Classical-only peer (`X25519`): requires X25519 (29), proving fallback.

The existing TLS 1.2/1.3 weak-certificate, trust/name/time, weak-DH, redirect and
pre-send refusal checks remain required. Certificate intermediates use distinct
subjects. DH controls assert the actual ephemeral key size, with fixture-only
group settings so OpenSSL cannot silently substitute a stronger named FFDHE group.

## Reproduce

With Qt development modules, OpenSSL >=3.5 and `dbus-run-session` installed:

```sh
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
PUSH_RELAY_URL= APNS_RELAY_URL= KYAUTH_REQUIRE_PQ_TLS=1 \
  ctest --test-dir build --output-on-failure
```

For the focused transcript:

```sh
PUSH_RELAY_URL= APNS_RELAY_URL= KYAUTH_REQUIRE_PQ_TLS=1 \
  QT_QPA_PLATFORM=offscreen build/test_tls tls13Groups
```

## Observed evidence — 2026-11-26

Local results and limits are recorded below; GitHub-hosted results belong to the
PR's current-head checks and artifacts.

- Native host: Qt 6.12.1, Qt TLS build/runtime OpenSSL 4.1.0. Full CTest: **9/9
  passed**, including every existing TLS control and all three new group cases.
- Stock Ubuntu 26.04 container using CI's development/QML package lists:
  Qt 5.15.18 (TLS build 3.5.3, runtime 3.5.5) and Qt 6.10.2 (TLS build/runtime
  3.5.5). Explicit Qt-major selection, RelWithDebInfo builds and full CTest:
  **9/9 passed for each major**, with `KYAUTH_REQUIRE_PQ_TLS=1` and no PQ skips.
- Each environment reports these actual TLS 1.3 results:

  ```text
  hybrid-preferred:  X25519MLKEM768 (4588)
  hybrid-only:       X25519MLKEM768 (4588)
  classical-fallback: ecdh_x25519 (29)
  ```

- Regression control: temporarily forcing the **production client's** `Groups`
  to `X25519` made `hybrid-preferred` fail with actual group 29 versus required
  4588, despite completing HTTPS. The mutation was removed, rebuilt and followed
  by the passing full suites above.
- Other local checks: seven libFuzzer seed inputs and 335,616 fuzz executions in
  61 seconds under ASan/UBSan, with no crash; mdBook 0.5.4 build and 283 local
  links/fragments across 11 generated HTML pages; binary scan, version consistency
  and workflow YAML syntax checks passed.
- Native host Qt 5 was not built because its QuickControls2 development module is
  absent; the Ubuntu 26.04 Qt 5 build above supplies that coverage. Click/Flatpak
  packages and installed phones were not exercised locally. GitHub-hosted runner
  startup, packaging and CodeQL still require their PR checks; a local container
  pass is not a hosted-runner pass.
