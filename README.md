# RawLab

RawLab prepares RAW photographs for editing. It preserves Bayer CFA data through
denoising, sharpening, masking, and DNG export. The desktop editor uses React and
Tauri; the native worker uses C++ and Qt.

## Features

- RAW import with Fast and High Quality previews
- Global and local denoise layers
- SAM 2.1 masks with point prompts and manual brush editing
- Normal and DeepSharpen processing
- Bayer CFA and Linear RGB DNG export
- Lensfun chromatic-aberration and lens corrections
- Batch processing with recovery
- Darktable external-editor and batch integration
- CPU and supported GPU inference through ONNX Runtime

## Build From Source

Install C++20, CMake 3.25+, Ninja, Qt 6 Core/Gui/Network, LibRaw, libtiff,
libpng, Lensfun, and the ONNX Runtime C API. The ONNX Runtime installation must
provide `onnxruntime_c_api.h` and its shared library.

Install Node.js 20.19+, npm, Rust/Cargo, and WebKitGTK 4.1 development files for
the desktop editor.

Build and test the native code:

```bash
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev
```

Build and run the desktop editor:

```bash
cd desktop
npm ci
npm run build
npm test
cargo test --manifest-path src-tauri/Cargo.toml
npm run desktop
```

The development build finds the worker at
`build/dev/src/rawlab_preview/rawlab-preview`. Set
`RAWLAB_ENGINE_SOURCE` to an absolute worker path when using another build.

## AppImage

Build the release worker with a packaged ONNX Runtime installation:

```bash
cmake --preset release \
  -DRAWLAB_DEPENDENCY_ROOT=/path/to/onnxruntime
cmake --build --preset release --parallel 2
ONNXRUNTIME_LIB_DIR=/path/to/onnxruntime/lib \
  bash scripts/stage-tauri-linux.sh
cd desktop
npm ci
npm run desktop:bundle:linux
```

The AppImage appears under
`desktop/src-tauri/target/release/bundle/appimage/`. GitHub Actions builds the
same package from `.github/workflows/release.yml`.

The published AppImage uses CPU ONNX Runtime. GPU inference requires a
CUDA-enabled ONNX Runtime build, compatible NVIDIA drivers, and host CUDA/cuDNN
libraries. On the tested Arch host, launch with:

```bash
LD_PRELOAD=/usr/lib/libcudnn.so.9 ./RawLab_0.1.0_amd64.AppImage
```

Select `Auto` or `CUDA` in **Settings > Model Manager** when the runtime exposes
the CUDA execution provider.

## Models and Data

RawLab does not include model weights. Use **Settings > Model Manager** or add
local models. Model licenses remain separate from the RawLab license.

RawLab never modifies source photographs. It stores recipes, presets, models, and
recovery queues in its application data directory. On Linux, this is normally
`~/.local/share/RawLab/RawLab`.

## Darktable

Install `packaging/darktable/RawLab.lua` in Darktable's Lua configuration and
follow `docs/darktable-batch-protocol.md` for external editing and batch export.

## Acknowledgments

RawLab acknowledges these projects and publications:

- [RapidRAW](https://github.com/CyberTimon/RapidRAW), used as a visual and workflow reference. RawLab does not copy its code or branding.
- [Lensfun](https://lensfun.github.io/), used for lens and chromatic-aberration correction data and processing.
- [RawForge](https://github.com/rymuelle/RawForge), by rymuelle.
- [darktable](https://darktable.org/), whose raw-processing work informs parts of the pipeline.
- [Raw Natural Image Noise Dataset](https://doi.org/10.14428/DVN/DEQCIM), by Benoit Brummer and Christophe De Vleeschouwer.
- [NAFNet](https://doi.org/10.48550/arXiv.2208.04677), by Liangyu Chen and contributors.

## License

RawLab is licensed under GPLv3-or-later. Model files have their own licensing
terms. See `docs/model-licensing.md` before redistributing model weights.
