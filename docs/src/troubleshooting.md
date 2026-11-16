# Troubleshooting

- **No push on Nura:** check that a UnifiedPush distributor is installed and running. With several distributors, `UNIFIEDPUSH_DISTRIBUTOR` selects one; otherwise the alphabetically first is used.
- **No camera scanner:** Qt 6 builds do not have the Qt 5 QZXing camera filter. Paste the pairing text instead.
- **Pairing expired:** create a new code in KyIdentity and retry within 90 seconds.
- **Codes rejected:** check the phone's clock and the account's TOTP settings before deleting anything.
- **Forgotten passphrase:** stop guessing. Five consecutive wrong passcodes erase local data. Recover access through the enrolled services and KyIdentity.

When reporting a problem, include the product version, platform, failed task and a redacted error. Exclude pairing links, device secrets, recovery codes, custodian shares and private content.
