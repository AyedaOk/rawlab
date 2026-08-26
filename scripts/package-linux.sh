#!/usr/bin/env bash
set -euo pipefail

: "${GITHUB_REF_NAME:=0.1.0}"
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/dist"
rm -rf "$out" "$root/AppDir"
mkdir -p "$out"
cmake --install "$root/build/release" --prefix "$root/AppDir/usr"
cp "$root/packaging/rawlab.desktop" "$root/AppDir/rawlab.desktop"

: "${ONNXRUNTIME_LIB_DIR:?ONNXRUNTIME_LIB_DIR must point to the ONNX Runtime lib directory}"
mkdir -p "$root/AppDir/usr/lib"
cp -a "$ONNXRUNTIME_LIB_DIR"/libonnxruntime.so* "$root/AppDir/usr/lib/"

curl -L --fail --retry 3 -o "$out/linuxdeploy-x86_64.AppImage" \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
curl -L --fail --retry 3 -o "$out/linuxdeploy-plugin-qt-x86_64.AppImage" \
  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x "$out"/*.AppImage

"$out/linuxdeploy-x86_64.AppImage" --appdir "$root/AppDir" \
  --desktop-file "$root/packaging/rawlab.desktop" \
  --icon-file "$root/packaging/rawlab.png" \
  --plugin qt --output appimage
mv "$root"/RawLab*.AppImage "$out/RawLab-${GITHUB_REF_NAME}-Linux-x86_64.AppImage"
