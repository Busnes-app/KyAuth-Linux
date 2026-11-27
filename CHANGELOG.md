# Changelog

## 0.6.1 — pending

- Prepare application version `0.6.1` (from `0.6.0`) for the compatible dependency refresh. Rebuild or install the updated application; this version bump requires no additional data, account, key or configuration migration.
- Qualify native TLS hybrid key exchange on Ubuntu 26.04 CI (Qt 5 and Qt 6, OpenSSL >=3.5): require X25519MLKEM768 with capable servers and verify classical fallback. Older Ubuntu Touch runtimes retain their existing support without a PQ claim; no account or vault migration is required.
