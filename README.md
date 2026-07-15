# RawLab

RawLab is a GPLv3-or-later raw photo preprocessing application focused on
preserving Bayer CFA data from RAW decode through non-demosaiced DNG export.
RGB previews are derived views only and are not the processing truth.

## Current Features

- RawNIND Bayer denoise through ONNX Runtime
- SAM 2.1 interactive segmentation masks
- Positive and negative point prompts
- Preview-to-raw mask mapping
- Post-inference blend with original CFA data
- Clipped-highlight preservation
- Non-demosaiced Bayer CFA DNG export

## Requirements

- C++20 compiler
- CMake 3.25 or newer
- Ninja
- LibRaw
- libtiff
- libpng
- ONNX Runtime
- ExifTool and `tiffinfo` are recommended for validation

ONNX Runtime must provide `onnxruntime_c_api.h` and `libonnxruntime.so`.

## Build

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The CLI binary is:

```text
build/dev/src/rawlab_cli/rawlab
```

## Basic Usage

Inspect a RAW file:

```bash
./build/dev/src/rawlab_cli/rawlab inspect input.ARW
```

Export decoded CFA data directly:

```bash
./build/dev/src/rawlab_cli/rawlab process input.ARW output.dng
```

Run RawNIND denoise:

```bash
./build/dev/src/rawlab_cli/rawlab process \
  input.ARW \
  output.dng \
  --denoise model_bayer.onnx \
  --blend 1.0 \
  --clipped-highlights
```

`--blend` and `--clipped-highlights` require `--denoise`.

## SAM Masks

SAM segmentation currently accepts an existing RGB preview PNG:

```bash
./build/dev/src/rawlab_cli/rawlab mask \
  preview.png \
  mask.png \
  --encoder /path/to/encoder.onnx \
  --decoder /path/to/decoder.onnx \
  --point 790 430 \
  --negative-point 1000 500
```

Positive and negative points use pixel coordinates in the preview image.
The encoder runs once per segmenter context; decoder calls use all supplied
points and previous-mask feedback.

Use the generated mask during RAW processing:

```bash
./build/dev/src/rawlab_cli/rawlab process \
  input.ARW \
  masked-output.dng \
  --denoise model_bayer.onnx \
  --mask mask.png \
  --blend 1.0
```

The mask is mapped from preview dimensions to raw dimensions using
nearest-neighbor sampling.

## Project Structure

```text
src/rawlab_core/    Reusable raw-domain processing library
src/rawlab_cli/     CLI proof and end-to-end commands
tests/              Core and CLI regression tests
docs/               Architecture, pipeline, DNG, and licensing notes
```

## Status And Next Steps

Milestones 0 through 6 are completed. Milestones 7 and 8 are intentionally
deferred, and Milestone 9 is reserved for DNG size and compatibility work.

The next implementation milestone is a Qt 6 Widgets single-image editor:

- Open a RAW file and generate an RGB preview through LibRaw.
- Keep CFA data as the processing truth.
- Display the preview with a mask overlay.
- Add positive and negative SAM point interaction.
- Reuse cached SAM encoder embeddings while points change.
- Run RawNIND through `RawProcessingPipeline`.
- Adjust blend without rerunning inference in the GUI process.
- Export a non-demosaiced Bayer CFA DNG.

There is no catalog or project database planned for the first GUI version.

## Licensing

RawLab is GPLv3-or-later. Darktable-derived code is limited to focused
raw-processing and CFA DNG logic with GPL-compatible licensing and attribution.

Model files are not included in this repository. Verify model licensing and
redistribution terms before distributing model weights.
