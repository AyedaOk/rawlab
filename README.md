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
- Qt 6 with Core, Gui, Widgets, and Concurrent
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

The desktop application is:

```bash
./build/dev/src/rawlab_app/rawlab-app
```

## Desktop Editor

The Qt 6 editor uses a single-image filesystem workflow. Open a RAW image,
select a denoise model, and press **Run** to cache a full-image denoised CFA result.
Blend and clipped-highlight changes recompose the preview without rerunning
inference. The canvas displays an activity indicator while decoding, rendering,
segmenting, or denoising.

Additional mask layers behave like an ordered layer stack:

- Each mask layer selects its own denoise model and blend amount.
- The eye beside a layer enables or disables its processing effect.
- Global remains first; local masks are reordered beneath it by dragging.
- **Show mask** controls the selected overlay independently from processing.
- Masks can be inverted, edited with positive and negative SAM points, refined
  with adjustable brush and erase tools, or painted manually from an empty mask.
- **Edit Mask** explicitly enters mask-editing mode; **Done** returns the canvas
  to normal pan and zoom interaction.
- Denoised CFA output is cached by model, so layers sharing a model reuse one
  inference result.
- Global is always composed first; enabled local mask results are applied on
  top, with the local layer nearest Global winning full-strength overlaps.
- Local masks can be renamed with double-click, `F2`, or the Rename button.

Application settings keep multiple named SAM 2.1 model folders and denoise
models. Each SAM folder contains its paired `encoder.onnx` and `decoder.onnx`;
one Small, Large, or custom SAM entry is active globally. Layers select named
denoise models from a combo box; inference only starts when **Run** is pressed.
AI acceleration can use CPU or any compatible execution provider exposed by
the installed ONNX Runtime, with automatic CPU fallback in Auto mode.

The Library page imports RAW files or folders by reference without copying the
originals. It persists paths and cached thumbnails. Per-picture JSON recipes
restore Global settings, mask names/order, selected models, SAM prompts, brush
strokes, inversion, blend, eye state, and `Show mask`; large denoised CFA
results remain session-only caches.

Select multiple pictures in the Library and open the selection to tune the
first picture. **File > Apply to Selected and Export...** scales the complete
recipe and soft masks to every selected picture, exports all selected files to
one chosen folder, and continues past individual failures. Batch output uses
`<source>-rawlab.dng` and never overwrites an existing destination.

The canvas is a display-only demosaic of the current CFA composition. Export
continues to write the non-demosaiced Bayer CFA data.
The Preview toolbar provides Library/Edit switching, fit and 100% zoom,
live zoom percentage, capture metadata, and an optional slower high-quality
1440-pixel CFA preview.

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

Milestones 0 through 7 are completed. Milestone 8 covers cleanup of temporary
CLI proof commands, and Milestone 9 is reserved for DNG size and compatibility
work.

The first library is intentionally lightweight and path-based. It does not
copy originals or require a catalog database.

## Licensing

RawLab is GPLv3-or-later. Darktable-derived code is limited to focused
raw-processing and CFA DNG logic with GPL-compatible licensing and attribution.

Model files are not included in this repository. Verify model licensing and
redistribution terms before distributing model weights.
