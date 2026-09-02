# RawLab

RawLab is a desktop RAW photo preprocessing application developed
through “vibe coding”. It keeps Bayer CFA data intact while applying
AI denoise and sharpening, then exports a non-demosaiced CFA DNG
for use in a RAW editor.

## Features

- RAW photo library that references your original files
- Global and local denoise layers
- SAM 2.1 masks with point prompts and manual masking
- Normal or RawForge's DeepSharpen sharpening after denoising
- Batch editing
- CPU and compatible GPU inference
- Model Manager for installing SAM, denoise, and sharpen models

## Screenshots

![RawLab editor with before-and-after comparison](docs/screenshots/Screenshot_20260827_012134.png)

## Build From Source

Development requirements are C++20, CMake 3.25+, Ninja, Qt 6, LibRaw,
libtiff, libpng, and the ONNX Runtime C API headers. ONNX Runtime must provide
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
4. Adjust blend, highlights and sharpening. Choose Normal for the built-in
   strength slider or DeepSharpen AI for the model-based blend slider.
5. Add mask layers if needed.
6. Export a CFA DNG.

For batch processing, select several pictures before opening them. The batch export
uses the settings from the picture currently being edited and applies them to every
selected picture. It copies the global denoise model and blend, enabled/disabled
layers, local masks, mask inversion and blend, feather and blur, DeepSharpen settings,
and clipped-highlight setting.

## Models

Model weights are not included in the source repository or application. Use
**Settings > Model Manager** to download available SAM 2.1, denoise, and
sharpen models, or add local models manually. Model licenses are separate from
the RawLab license.

## Acceleration

RawLab uses the execution providers exposed by the installed ONNX Runtime.
Select **Auto** to use the best available provider with CPU fallback. GPU
acceleration requires compatible hardware drivers and matching runtime
libraries.

## Output

RawLab exports non-demosaiced Bayer CFA DNG files. The editor preview is a
display-only RGB view. Output files can be opened by RAW applications that
 support CFA DNG workflows.

Preview rendering is Fast by default. Fast uses direct 2x2 Bayer-block
demosaicing; High Quality is an opt-in, slower standalone AMaZE preview path
adapted from Darktable. The
adaptation supplies active-area, rawprepare-style normalized and WB-adjusted
CFA samples without depending on Darktable's internal image buffers or runtime.
Both modes use black/white normalization,
camera white balance and color matrix data, protect over-white highlights for
display, and apply only modest display-only luminance sharpening. Preview
changes never alter CFA export. The PPG adaptation is GPLv3 code attributed to
the Darktable developers in `RawPreview.cpp`.

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

## Darktable integration

The Linux GUI supports an external-editor mode for the `RawLab.lua` Darktable script:

```bash
RawLab.AppImage --external-edit /absolute/input.ARW --output /absolute/input-rawlab.dng
```

RawLab opens the original sensor RAW for interactive editing. In this mode,
**Export and Return to Darktable** writes the requested DNG without overwriting
an existing file and exits only after a successful export. Closing RawLab
without exporting returns a nonzero status, so Darktable does not import an
incomplete result.

## Acknowledgments

RawLab builds on and uses work from:

- [RawForge](https://github.com/rymuelle/RawForge), by rymuelle.
- [Raw Natural Image Noise Dataset](https://doi.org/10.14428/DVN/DEQCIM), by Benoit Brummer and Christophe De Vleeschouwer.
- [NAFNet: Simple Baselines for Image Restoration](https://doi.org/10.48550/arXiv.2208.04677), by Liangyu Chen and contributors.
- [darktable](https://darktable.org/).

Please review the relevant licenses and attribution requirements before redistributing model files.

## License

RawLab is licensed under GPLv3-or-later. Model files have their own licensing
terms. See `docs/model-licensing.md` before redistributing model weights.
