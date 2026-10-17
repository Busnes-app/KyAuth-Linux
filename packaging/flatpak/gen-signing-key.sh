#!/usr/bin/env bash
# Generate the GPG key that signs KyAuth's Flatpak repository, once. See docs/DISTRIBUTION.md.
#
#   packaging/flatpak/gen-signing-key.sh            # generate, print the secret commands
#   packaging/flatpak/gen-signing-key.sh --upload   # generate and set the repo secrets
#
# Passphraseless: CI signs unattended, and a passphrase kept as a second secret protects
# nothing. Never expires: an expired key makes `flatpak update` reject the repo for every
# installed user.
set -euo pipefail

REPO_SLUG="${REPO_SLUG:-Busnes-app/KyAuth-Linux}"
KEY_NAME="KyAuth Flatpak Signing Key"
KEY_EMAIL="${KEY_EMAIL:-kyauth-flatpak@busnes.app}"
OUT_DIR="${OUT_DIR:-$HOME/kyauth-flatpak-signing-key}"

if [ -e "$OUT_DIR" ]; then
    echo "$OUT_DIR already exists. Refusing to overwrite a signing key: losing it strands every install." >&2
    exit 1
fi

GNUPGHOME="$(mktemp -d)"
export GNUPGHOME
trap 'rm -rf "$GNUPGHOME"' EXIT

gpg --batch --generate-key <<EOF
%no-protection
Key-Type: RSA
Key-Length: 4096
Key-Usage: sign
Name-Real: $KEY_NAME
Name-Email: $KEY_EMAIL
Expire-Date: 0
%commit
EOF

KEY_ID="$(gpg --list-secret-keys --with-colons "$KEY_EMAIL" | awk -F: '/^fpr:/ {print $10; exit}')"
mkdir -m 700 "$OUT_DIR"
gpg --armor --export-secret-keys "$KEY_ID" > "$OUT_DIR/private.asc"
gpg --armor --export "$KEY_ID" > "$OUT_DIR/public.asc"
printf '%s\n' "$KEY_ID" > "$OUT_DIR/fingerprint.txt"
chmod 600 "$OUT_DIR"/*
echo "Fingerprint: $KEY_ID"
echo "Written to:  $OUT_DIR"

if [ "${1:-}" = "--upload" ]; then
    gh secret set FLATPAK_GPG_PRIVATE_KEY --repo "$REPO_SLUG" < "$OUT_DIR/private.asc"
    printf '%s' "$KEY_ID" | gh secret set FLATPAK_GPG_KEY_ID --repo "$REPO_SLUG"
    echo "Secrets FLATPAK_GPG_PRIVATE_KEY and FLATPAK_GPG_KEY_ID set on $REPO_SLUG."
else
    echo "gh secret set FLATPAK_GPG_PRIVATE_KEY --repo $REPO_SLUG < $OUT_DIR/private.asc"
    echo "printf '%s' '$KEY_ID' | gh secret set FLATPAK_GPG_KEY_ID --repo $REPO_SLUG"
fi

echo
echo ">>> Back up $OUT_DIR/private.asc offline now. GitHub secrets cannot be read back. <<<"
