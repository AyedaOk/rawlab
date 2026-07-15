# RawLab

RawLab is a raw photo pre-processing application.

## Current Features

- RawNIND Bayey denoise through ONNX Runtime
- SAM 2.1 interactive segmentation masks
- Post-inference blend with the original CFA data
- Non-demosaiced Bayer CFA DNG export
- Clipped-highlight preservation

## Requirements

- C++20 compiler
- CMake 3.25 or newer
- Ninja
- LibRaw
- libtiff
- libpng
- ONNX Runtime
- ExifTool and `tiffinfo` are recommended for validation

Build
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
The CLI binary is:
build/dev/src/rawlab_cli/rawlab
Basic Usage
Inspect a RAW file:
./build/dev/src/rawlab_cli/rawlab inspect input.ARW
Export the decoded CFA data directly:
./build/dev/src/rawlab_cli/rawlab process input.ARW output.dng
Run RawNIND denoise:
./build/dev/src/rawlab_cli/rawlab process \
  input.ARW \
  output.dng \
  --denoise model_bayer.onnx \
  --blend 1.0 \
  --clipped-highlights
--blend and --clipped-highlights require --denoise.
SAM Masks
SAM segmentation currently accepts an existing RGB preview PNG.
./build/dev/src/rawlab_cli/rawlab mask \
  preview.png \
  mask.png \
  --encoder /path/to/encoder.onnx \
  --decoder /path/to/decoder.onnx \
  --point 790 430 \
  --negative-point 1000 500
Positive and negative points use pixel coordinates in the preview image.
Use the generated mask during RAW processing:
./build/dev/src/rawlab_cli/rawlab process \
  input.ARW \
  masked-output.dng \
  --denoise model_bayer.onnx \
  --mask mask.png \
  --blend 1.0
The mask is mapped from preview dimensions to raw dimensions using
nearest-neighbor sampling.
