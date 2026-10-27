# Contributing to KyAuth for Linux

KyAuth for Linux provides TOTP codes and KyIdentity Push MFA approvals for
postmarketOS and Ubuntu Touch phones. Device sign-on and passkeys are not ported.
Contributions must keep authenticator secrets on the device and keep every approval
behind an explicit human action.

## Before contributing

Read [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) and [SECURITY.md](SECURITY.md),
the [README](README.md), and any AGENTS.md in the subtree you are about to
touch. Open an issue before proposing a feature larger than a bug fix or
documentation change.

Keep changes small, reuse existing code, and fix the shared root cause rather
than the reported symptom. New non-trivial logic ships with a runnable test or
self-check.

## Security requirements

- TOTP seeds and private push-approval keys stay on the device; never transmit them off-device or write them to logs or crash reports.
- A sign-in or MFA approval happens only on an explicit user action, never automatically; a notification alone is not consent.
- Do not add telemetry, phone-home, analytics, or unconfigured third-party
  services.
- Do not add silent security fallbacks; fail closed with a readable error and a
  remediation step.
- Name and document any security trade-off in [SECURITY.md](SECURITY.md) before
  it merges.

## Verification

Run the checks that apply to the files you changed, and the repository's
full required checks before hand-off. Record the exact commands and their
results in the pull request.

The `unifiedpush` CTest uses a private D-Bus daemon with no activation directories
and an owned mock distributor process. Run `ctest --test-dir build -R unifiedpush
--output-on-failure`; the executable refuses accidental direct execution. It must
exercise the real connector and keep token/current-owner refusal and transport
bounds asserted. Do not use a workstation or phone's session bus as a fixture.

The Flatpak's build-only D-Bus 1.16.2 emits GCC 15.2 `-Wnull-dereference`
diagnostics in `dbus-message.c` (cache/new-message paths) and `bus/services.c`
(owner-list paths). These are false positives for the inspected pinned source:
new-message allocation returns on null, the cache asserts a non-null entry, and
owner removal/replacement requires and asserts the relevant list membership and
successor. Build its assertions enabled (`-Dasserts=true`); retain the diagnostics
and revisit this assessment when the source pin or compiler changes. These tools
are removed before delivery. Application compiler warnings remain subject to the
normal fix-or-document policy.

## AI attribution

AI-assisted contributions are welcome; undisclosed ones are not. Disclose
material AI assistance in the pull request — the tool, what it changed, and what
you personally ran, read, and verified — and add a commit trailer naming any
tool that materially contributed. You are the author of the code you submit for
every purpose, regardless of how it was produced.

## Pull requests

Branch off `master` with a focused name, and use a Conventional Commit subject
(`fix:`, `feat:`, `docs:`, `refactor:`, `test:`, or `chore:`). The pull request
explains why the change exists, the verification performed, the security impact,
and any known limitations.

Every pull request gets an adversarial review pass; see
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md#hostile-reviews-are-about-code-never-people)
for what that is and the boundary it keeps.

## Licence

KyAuth for Linux is licensed under the MIT License. See [LICENSE.txt](LICENSE.txt).

