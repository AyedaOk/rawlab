#!/usr/bin/env bash
set -euo pipefail

: "${ONNXRUNTIME_LIB_DIR:?ONNXRUNTIME_LIB_DIR must point to the ONNX Runtime lib directory}"
root="$(cd "$(dirname "$0")/.." && pwd)"
stage="$root/desktop/src-tauri/bundle/linux/usr"
qmake_bin="${QMAKE:-qmake6}"
qt_plugins="$($qmake_bin -query QT_INSTALL_PLUGINS)"
lensfun_data="${LENSFUN_DATA_DIR:-/usr/share/lensfun}"
engine="${RAWLAB_ENGINE_SOURCE:-$root/build/release/src/rawlab_preview/rawlab-preview}"

if [[ ! -x "$engine" ]]; then
  echo "Release worker not found or not executable: $engine" >&2
  exit 1
fi
if ! ldd "$engine" | grep -q 'liblensfun'; then
  echo "Release worker was built without Lensfun support: $engine" >&2
  exit 1
fi
if ! compgen -G "$lensfun_data/version_*" >/dev/null; then
  echo "Lensfun profiles not found under: $lensfun_data" >&2
  exit 1
fi

rm -rf "$root/desktop/src-tauri/bundle/linux"
mkdir -p "$stage/lib" "$stage/plugins" "$stage/share/lensfun" "$stage/share/rawlab/darktable"
cp -a "$ONNXRUNTIME_LIB_DIR"/libonnxruntime.so* "$stage/lib/"
cp -a "$lensfun_data"/version_* "$stage/share/lensfun/"
cp "$root/packaging/darktable/RawLab.lua" "$stage/share/rawlab/darktable/"

for plugin in imageformats tls networkinformation; do
  if [[ -d "$qt_plugins/$plugin" ]]; then cp -a "$qt_plugins/$plugin" "$stage/plugins/"; fi
done

for library in libssl.so.3 libcrypto.so.3; do
  path="$(ldconfig -p | awk -v name="$library" '$1 == name {print $NF; exit}')"
  if [[ -z "$path" || ! -f "$path" ]]; then
    echo "Required system library not found: $library" >&2
    exit 1
  fi
  cp -L "$path" "$stage/lib/"
done
