#!/usr/bin/env bash
# Stages the native runtime for the macOS .app bundle.
#
# The release worker links Homebrew Qt/LibRaw/LibTIFF/LibPNG/Lensfun and the
# ONNX Runtime dylib. dylibbundler rewrites the worker's install names to
# @loader_path/../Resources and collects every non-system dependency, so the
# Tauri bundle only has to drop them into Contents/Resources. The worker is
# copied into the Tauri bundle afterwards by prepare-engine.mjs, which is why
# this runs first.
set -euo pipefail

: "${ONNXRUNTIME_LIB_DIR:?ONNXRUNTIME_LIB_DIR must point to the ONNX Runtime lib directory}"
root="$(cd "$(dirname "$0")/.." && pwd)"
stage="$root/desktop/src-tauri/bundle/macos"
lensfun_stage="$root/desktop/src-tauri/bundle/macos-lensfun"
lensfun_data="${LENSFUN_DATA_DIR:-$(brew --prefix)/share/lensfun}"
engine="${RAWLAB_ENGINE_SOURCE:-$root/build/release/src/rawlab_preview/rawlab-preview}"

if [[ ! -x "$engine" ]]; then
  echo "Release worker not found or not executable: $engine" >&2
  exit 1
fi
if ! command -v dylibbundler >/dev/null 2>&1; then
  echo "dylibbundler is required (brew install dylibbundler)" >&2
  exit 1
fi

rm -rf "$stage" "$lensfun_stage"
mkdir -p "$stage" "$lensfun_stage"

# Rewrite the worker in place and collect its dylibs for Contents/Resources.
dylibbundler -od -b -x "$engine" -d "$stage" -p "@loader_path/../Resources/" >/dev/null

# dylibbundler does not see dependencies that are only loaded lazily, so copy
# the ONNX Runtime dylibs explicitly.
cp -a "$ONNXRUNTIME_LIB_DIR"/libonnxruntime*.dylib "$stage/" 2>/dev/null || true

if compgen -G "$lensfun_data/version_*" >/dev/null; then
  cp -a "$lensfun_data"/version_* "$lensfun_stage/"
else
  echo "Lensfun profiles not found under: $lensfun_data (optics will be unavailable)" >&2
fi

echo "Staged macOS runtime in $stage"
