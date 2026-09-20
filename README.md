# RawLab

RawLab is a vibe-coded RAW photographs pre-processor. It can preserves Bayer CFA data and DNG export.

## Features

- Global and local denoise layers
- SAM 2.1 masks with point prompts and manual brush editing
- Bayer CFA and Linear RGB DNG export
- Sharpening: Normal and DeepSharpen
- Lensfun chromatic-aberration and lens corrections
- Batch processing with recovery
- Darktable integration with Lua script
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

## Models and Data

RawLab does not include model weights. Use **Settings > Model Manager** or add
local models. Model licenses remain separate from the RawLab license.

## Darktable

Install `packaging/darktable/RawLab.lua` in Darktable's Lua configuration.

## Acknowledgments

RawLab acknowledges these projects and publications:

- [RawForge](https://github.com/rymuelle/RawForge), by rymuelle.
- [Raw Natural Image Noise Dataset](https://doi.org/10.14428/DVN/DEQCIM), by Benoit Brummer and Christophe De Vleeschouwer.
- [NAFNet](https://doi.org/10.48550/arXiv.2208.04677), by Liangyu Chen and contributors.
- [darktable](https://darktable.org/), whose raw-processing work informs parts of the pipeline.
- [RapidRAW](https://github.com/CyberTimon/RapidRAW), used as a visual and workflow reference.
- [Lensfun](https://lensfun.github.io/), used for lens and chromatic-aberration correction data and processing.

## License

RawLab is licensed under GPLv3-or-later. Model files have their own licensing
terms (see `docs/model-licensing.md`).
