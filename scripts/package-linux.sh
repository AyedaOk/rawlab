#!/usr/bin/env bash
set -euo pipefail

: "${GITHUB_REF_NAME:=0.1.0}"
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/dist"
tools="${TMPDIR:-/tmp}/rawlab-linuxdeploy"
rm -rf "$out" "$root/AppDir"
rm -rf "$tools"
mkdir -p "$out"
mkdir -p "$tools"
cmake --install "$root/build/release" --prefix "$root/AppDir/usr"
cp "$root/packaging/rawlab.desktop" "$root/AppDir/rawlab.desktop"

qmake_bin="${QMAKE:-qmake6}"
qt_plugins="$($qmake_bin -query QT_INSTALL_PLUGINS)"
mkdir -p "$root/AppDir/usr/plugins/tls"
if [[ ! -d "$qt_plugins/tls" ]]; then
  echo "Qt TLS plugins not found under $qt_plugins" >&2
  exit 1
fi
cp -a "$qt_plugins/tls/"*.so "$root/AppDir/usr/plugins/tls/"

: "${ONNXRUNTIME_LIB_DIR:?ONNXRUNTIME_LIB_DIR must point to the ONNX Runtime lib directory}"
mkdir -p "$root/AppDir/usr/lib"
cp -a "$ONNXRUNTIME_LIB_DIR"/libonnxruntime.so* "$root/AppDir/usr/lib/"

copy_system_library()
{
  local name="$1"
  local path
  path="$(ldconfig -p | awk -v library="$name" '$1 == library {print $NF; exit}')"
  if [[ -z "$path" || ! -f "$path" ]]; then
    echo "Required system library not found: $name" >&2
    exit 1
  fi
  cp -L "$path" "$root/AppDir/usr/lib/"
}

copy_system_library libssl.so.3
copy_system_library libcrypto.so.3

curl -L --fail --retry 3 -o "$tools/linuxdeploy-x86_64.AppImage" \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
curl -L --fail --retry 3 -o "$tools/linuxdeploy-plugin-qt-x86_64.AppImage" \
  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x "$tools"/*.AppImage

"$tools/linuxdeploy-x86_64.AppImage" --appdir "$root/AppDir" \
  --desktop-file "$root/packaging/rawlab.desktop" \
  --icon-file "$root/packaging/rawlab.png" \
  --plugin qt --output appimage
mv "$root"/RawLab*.AppImage "$out/RawLab-${GITHUB_REF_NAME}-Linux-x86_64.AppImage"
