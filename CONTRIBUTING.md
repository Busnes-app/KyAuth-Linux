# Contributing to KyAuth for Linux

KyAuth holds TOTP secrets and the device key KyIdentity trusts to approve sign-ins. A
contribution must keep the contracts in [AGENTS.md](AGENTS.md) and the argument in
[docs/ASSURANCE-CASE.md](docs/ASSURANCE-CASE.md) true.

## How to contribute

- **Bugs and ideas:** open an issue at <https://github.com/Busnes-app/KyAuth-Linux/issues>.
  Report vulnerabilities privately instead: the repository's **Security** tab, **Report a
  vulnerability** ([policy](https://github.com/Busnes-app/.github/blob/main/SECURITY.md)).
- **Changes:** branch off `master` and open a pull request against `master`. CI must be green.
- **Docs follow the DOX chain:** read [AGENTS.md](AGENTS.md) before editing, and update it when
  a contract changes.
- A change that moves a trust boundary or a security requirement updates
  [docs/ASSURANCE-CASE.md](docs/ASSURANCE-CASE.md) in the same pull request.

## Tests

New protocol, crypto or lock behaviour needs a test in `tests/`. Protocol and crypto rules live
in `src/core` so they can be tested without a network or a screen. Build and run everything:

```sh
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

CI (`.github/workflows/ci.yml`) runs the same on Qt 5 and Qt 6 and builds the Ubuntu Touch
click. `release.yml` builds and smoke-tests the Flatpak on both architectures. `codeql.yml` runs
CodeQL on the C++ and the workflows; fix or explain its findings before merging.

## Coding standard

| Language | Style guide | Enforced by |
|---|---|---|
| C++17 | The [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines), with Qt's naming (`m_` members, `camelCase` functions) | Compiler warnings from `CMakeLists.txt`: `-Wall -Wextra -Werror=format-security`, plus `QT_NO_CAST_FROM_ASCII`, in the `test` jobs of `ci.yml` |
| QML | Qt's [QML coding conventions](https://doc.qt.io/qt-6/qml-codingconventions.html) | The `qml_smoke` test, which fails on any QML warning |

Only format-string warnings fail the build; fix the others too. There is no formatter or
linter. Keep to the style of the file you are editing, keep OpenSSL objects in the
`unique_ptr` wrappers of `src/core/crypto.cpp`, and show server-supplied text with
`Text.PlainText`.

## Licence

KyAuth is MIT-licensed ([LICENSE.txt](LICENSE.txt)). By contributing you agree that your
contribution is licensed the same way.
