#!/usr/bin/env bash
set -euo pipefail

: "${GITHUB_REF_NAME:=0.1.0}"
root="$(cd "$(dirname "$0")/.." && pwd)"
app="$root/build/release/src/rawlab_app/rawlab-app.app"
out="$root/dist"
mkdir -p "$out"
"${QT_ROOT_DIR}/bin/macdeployqt" "$app" -dmg
mv "$root/build/release/src/rawlab_app"/*.dmg "$out/RawLab-${GITHUB_REF_NAME}-macOS-arm64.dmg"
