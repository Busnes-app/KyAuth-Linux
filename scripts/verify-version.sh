#!/bin/sh
# Fails unless the tag, CMakeLists.txt and the AppStream metadata name the same version.
# Usage: verify-version.sh v1.2.3
set -eu
tag="${1#v}"
cmake=$(sed -n 's/^project(kyauth VERSION \([0-9.]*\) .*/\1/p' CMakeLists.txt)
# The newest <release> comes first.
metainfo=$(sed -n 's/.*<release version="\([^"]*\)".*/\1/p' packaging/nura/app.busnes.kyauth.metainfo.xml | head -n 1)
echo "tag=$tag cmake=$cmake metainfo=$metainfo"
if [ "$tag" != "$cmake" ] || [ "$tag" != "$metainfo" ]; then
    echo "Version mismatch: bump CMakeLists.txt and add a <release> to the metainfo." >&2
    exit 1
fi
