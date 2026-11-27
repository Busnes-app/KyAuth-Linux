#!/usr/bin/env bash
# Generate the GPG key that signs KyAuth's Flatpak repository, once. See docs/DISTRIBUTION.md.
#
#   packaging/flatpak/gen-signing-key.sh            # generate, print the secret commands
#   packaging/flatpak/gen-signing-key.sh --upload   # generate and set the repo secrets
#
# Passphraseless: CI signs unattended, and a passphrase kept as a second secret protects
# nothing. Bounded expiry (default 2y): keys can be rotated with zero user disruption via
# OSTree dual-key rollover (FLATPAK_GPG_*_NEXT); see docs/DISTRIBUTION.md.
set -euo pipefail

REPO_SLUG="${REPO_SLUG:-Busnes-app/KyAuth-Linux}"
KEY_NAME="KyAuth Flatpak Signing Key"
KEY_EMAIL="${KEY_EMAIL:-kyauth-flatpak@busnes.app}"
KEY_EXPIRE="${KEY_EXPIRE-2y}"
OUT_DIR="${OUT_DIR:-$HOME/kyauth-flatpak-signing-key}"

case "$KEY_EXPIRE" in
    0|never|NEVER|"")
        echo "ERROR: KEY_EXPIRE must be a bounded lifetime (e.g. 2y, 18m, 365d), not '$KEY_EXPIRE'." >&2
        exit 1
        ;;
esac
if ! [[ "$KEY_EXPIRE" =~ ^[1-9][0-9]*[dwmy]$ ]]; then
    echo "ERROR: invalid KEY_EXPIRE '$KEY_EXPIRE' (expected <N>[d|w|m|y], e.g. 2y)." >&2
    exit 1
fi

if [ -e "$OUT_DIR" ]; then
    echo "$OUT_DIR already exists. Refusing to overwrite a signing key: losing it strands every install." >&2
    exit 1
fi

GNUPGHOME="$(mktemp -d)"
export GNUPGHOME
trap 'rm -rf "$GNUPGHOME"' EXIT
chmod 700 "$GNUPGHOME"

gpg --batch --generate-key <<EOF
%no-protection
Key-Type: RSA
Key-Length: 4096
Key-Usage: sign
Name-Real: $KEY_NAME
Name-Email: $KEY_EMAIL
Expire-Date: $KEY_EXPIRE
%commit
EOF

KEY_ID="$(gpg --list-secret-keys --with-colons "$KEY_EMAIL" | awk -F: '/^fpr:/ {print $10; exit}')"
KEY_EXP_EPOCH="$(gpg --list-keys --with-colons --fixed-list-mode "$KEY_ID" | awk -F: '$1=="pub" {print $7; exit}')"
KEY_EXP_DATE="$(date -u -d "@$KEY_EXP_EPOCH" +%Y-%m-%d 2>/dev/null || echo "$KEY_EXP_EPOCH")"
mkdir -m 700 "$OUT_DIR"
gpg --armor --export-secret-keys "$KEY_ID" > "$OUT_DIR/private.asc"
gpg --armor --export "$KEY_ID" > "$OUT_DIR/public.asc"
printf '%s\n' "$KEY_ID" > "$OUT_DIR/fingerprint.txt"
printf '%s\n' "$KEY_EXP_DATE" > "$OUT_DIR/expires.txt"
chmod 600 "$OUT_DIR"/*
echo "Fingerprint: $KEY_ID"
echo "Expires:     $KEY_EXP_DATE (UTC)"
echo "Written to:  $OUT_DIR"

if [ "${1:-}" = "--upload" ]; then
    gh secret set FLATPAK_GPG_PRIVATE_KEY --repo "$REPO_SLUG" < "$OUT_DIR/private.asc"
    printf '%s' "$KEY_ID" | gh secret set FLATPAK_GPG_KEY_ID --repo "$REPO_SLUG"
    echo "Secrets FLATPAK_GPG_PRIVATE_KEY and FLATPAK_GPG_KEY_ID set on $REPO_SLUG."
else
    echo "# Initial setup:"
    echo "gh secret set FLATPAK_GPG_PRIVATE_KEY --repo $REPO_SLUG < $OUT_DIR/private.asc"
    echo "printf '%s' '$KEY_ID' | gh secret set FLATPAK_GPG_KEY_ID --repo $REPO_SLUG"
    echo "# Or stage a dual-key rollover (see docs/DISTRIBUTION.md):"
    echo "gh secret set FLATPAK_GPG_PRIVATE_KEY_NEXT --repo $REPO_SLUG < $OUT_DIR/private.asc"
    echo "printf '%s' '$KEY_ID' | gh secret set FLATPAK_GPG_KEY_ID_NEXT --repo $REPO_SLUG"
fi

echo
echo ">>> Back up $OUT_DIR/private.asc offline now. GitHub secrets cannot be read back. <<<"
echo ">>> Start the rollover runbook in docs/DISTRIBUTION.md at least 90 days before $KEY_EXP_DATE. <<<"
