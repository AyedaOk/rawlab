#!/usr/bin/env bash
set -euo pipefail

: "${GITHUB_REF_NAME:=0.1.0}"
: "${ONNXRUNTIME_LIB_DIR:?ONNXRUNTIME_LIB_DIR must point to the ONNX Runtime lib directory}"
root="$(cd "$(dirname "$0")/.." && pwd)"
app="$root/build/release/src/rawlab_app/rawlab-app.app"
out="$root/dist"
mkdir -p "$out"
"${QT_ROOT_DIR}/bin/macdeployqt" "$app" \
  -libpath="$ONNXRUNTIME_LIB_DIR" \
  -libpath="$(brew --prefix libraw)/lib" \
  -libpath="$(brew --prefix libtiff)/lib" \
  -libpath="$(brew --prefix libpng)/lib" \
  -dmg
mv "$root/build/release/src/rawlab_app"/*.dmg "$out/RawLab-${GITHUB_REF_NAME}-macOS-arm64.dmg"
