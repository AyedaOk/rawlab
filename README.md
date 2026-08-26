# RawLab

RawLab is a desktop RAW photo preprocessing application. It keeps Bayer CFA
data intact while applying AI denoise, masks, and sharpening, then exports a
non-demosaiced CFA DNG for use in a RAW editor.

## Features

- RAW photo library that references your original files
- Global and local denoise layers
- SAM 2.1 masks with point prompts and manual editing
- DeepSharpen after denoising
- Blend and clipped-highlight preservation
- Batch editing with a hideable filmstrip
- CPU and compatible GPU inference providers
- Model Manager for installing SAM, denoise, and sharpen models
- Per-picture recipes and cached previews

## Build From Source

Development requirements are C++20, CMake 3.25+, Ninja, Qt 6, LibRaw,
libtiff, libpng, and the C++ ONNX Runtime library. ONNX Runtime must provide
`onnxruntime_c_api.h` and its shared library.

Development build:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Release GUI build:

```bash
cmake --preset release
cmake --build --preset release
```

The desktop application is `build/release/src/rawlab_app/rawlab-app`.

## Basic Workflow

1. Import RAW files or a folder in the Library.
2. Open a picture and choose a denoise model.
3. Press **Run** to generate the denoised preview.
4. Adjust blend, highlights, masks, and DeepSharpen.
5. Export a CFA DNG.

Select several pictures before opening them to edit the selection with the
bottom filmstrip. Click a thumbnail to move between pictures; each picture
keeps its own settings. The export button becomes **Export Selected...** for
multi-picture sessions.

## Models

Model weights are not included in the source repository or application. Use
**Settings > Model Manager** to download available SAM 2.1, denoise, and
sharpen models, or add local models manually. Model licenses are separate from
the RawLab license.

## Acceleration

RawLab uses the execution providers exposed by the installed ONNX Runtime.
Select **Auto** to use the best available provider with CPU fallback. GPU
acceleration requires compatible hardware drivers and matching runtime
libraries; CPU mode works without a supported GPU.

## Output

RawLab exports non-demosaiced Bayer CFA DNG files. The editor preview is a
display-only RGB view. Output files can be opened by RAW applications that
support CFA DNG workflows.

Original files are never copied or modified. RawLab stores library references,
thumbnails, recipes, and downloaded models in its application data directory.

## CLI

The optional CLI provides stable automation commands:

```bash
rawlab inspect input.ARW
rawlab process input.ARW output.dng
rawlab process input.ARW output.dng --denoise model.onnx --blend 1.0
rawlab mask preview.png mask.png --encoder encoder.onnx --decoder decoder.onnx --point 790 430
```

## License

RawLab is licensed under GPLv3-or-later. Model files have their own licensing
terms. See `docs/model-licensing.md` before redistributing model weights.
