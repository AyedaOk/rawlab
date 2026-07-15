# Raw Pipeline Notes

The project must preserve raw-domain CFA/Bayer data from decode through export. Darktable's rawdenoise path is the preferred reference for RawNIND Bayer integration.

## Milestone 1 Decoder Goal

Use LibRaw first to inspect and extract:

- raw CFA buffer without demosaicing
- raw dimensions and active area
- CFA pattern and CFA phase
- black level and white level
- bit depth and sample type
- camera make/model and orientation metadata needed later for DNG

RawSpeed may be evaluated later after the LibRaw baseline is understood.

## Coordinate Rules

Mask and preview coordinates must map back to raw coordinates explicitly. Active-area offsets, crops, rotations, scaling, and CFA phase cannot be implicit.

The first mask proof-of-concept uses masks whose dimensions directly match raw pixels. Pixel `(x, y)` samples mask value `(x, y)` with nearest-neighbor lookup.

Preview-sized masks map raw pixels back to mask pixels explicitly with nearest-neighbor sampling:

```text
mask_x = floor(raw_x * mask_width / raw_width)
mask_y = floor(raw_y * mask_height / raw_height)
```

The result is clamped at mask edges so the last raw pixel maps to the last mask pixel.

## RawNIND Bayer Contract

The Darktable RawNIND Bayer path uses this contract:

- Input tensor: `[1, 4, T, T]` float32, packed half-resolution Bayer planes.
- Input channel order: `R, G1, G2, B`.
- Input values: `(raw - black[site]) / range[site] * wb_norm[channel]`.
- Orientation: force RGGB by shifting to the CFA red origin for non-RGGB sensors.
- Edge handling: mirror padding inside the effective RGGB-cropped rectangle.
- Output tensor: `[1, 3, 2T, 2T]` float32 temporary camRGB.
- Postprocess: gain-match if required, undo WB/range normalization, re-mosaic to one CFA value per sensor pixel.

This means RawNIND Bayer is a valid `CfaRestoreModel` backend even though the ONNX output is temporary 3-channel camRGB.

RawLab's first ONNX Runtime proof command is:

```bash
./build/dev/src/rawlab_cli/rawlab inspect-onnx /path/to/model_bayer.onnx
```

For the Darktable RawNIND Bayer model, this should report input `[1, 4, 512, 512]` and output `[1, 3, 1024, 1024]`.

The temporary single-tile inference proof command is:

```bash
./build/dev/src/rawlab_cli/rawlab test-rawnind-tile input.raw model_bayer.onnx output.dng
```

It crops one red-origin-aligned 1024x1024 CFA tile, packs it to `[1, 4, 512, 512]`, runs RawNIND, gain-matches the output, re-mosaics temporary camRGB back to CFA, and writes a 1024x1024 Bayer CFA DNG.

The current full-image proof command is:

```bash
./build/dev/src/rawlab_cli/rawlab raw-denoise input.raw model_bayer.onnx output.dng --blend 1.0 --clipped-highlights
```

It runs RawNIND over the Bayer working area with 512x512 packed tiles, 32 packed-pixel overlap, mirror padding inside the Bayer-aligned working rectangle, per-tile `match_gain`, re-mosaic to CFA, and overlap-weight accumulation into a full-size denoised Bayer CFA buffer. The current seam blending is a first PoC implementation and not Darktable's exact strip-buffer seam algorithm yet.

`--blend` is applied after inference by blending original CFA with denoised CFA in C++. This must remain separate from ONNX inference so changing blend amount later does not require rerunning the model.

Full-image RawNIND denoise and mask composition now live in `rawlab::pipeline::RawProcessingPipeline`; the CLI remains a proof harness around the core raw-domain pipeline.

## End-To-End CLI Prototype

Milestone 6 adds the first consolidated processing command:

```bash
./build/dev/src/rawlab_cli/rawlab process input.raw output.dng
./build/dev/src/rawlab_cli/rawlab process input.raw output.dng \
  --denoise model_bayer.onnx --mask mask.png --blend 1.0 --clipped-highlights
```

Without `--denoise`, it decodes the source CFA and exports it directly. With `--denoise`, it runs RawNIND through `RawProcessingPipeline`, optionally preserves clipped highlights, applies post-inference blend, and exports the resulting Bayer CFA DNG. `--mask` loads an 8-bit grayscale preview-space PNG and limits denoise composition to that mask using nearest-neighbor preview-to-raw mapping. `--mask`, `--blend`, and `--clipped-highlights` require `--denoise`.

## SAM Preview Masks

SAM 2.1 mask generation currently accepts an existing RGB preview PNG:

```bash
./build/dev/src/rawlab_cli/rawlab mask preview.png mask.png \
  --encoder encoder.onnx --decoder decoder.onnx \
  --point 790 430 --negative-point 1000 500
```

The preview is resized so its longest side is 1024 pixels, zero-padded, ImageNet-normalized, and encoded once. The decoder receives all positive (`label=1`) and negative (`label=0`) points plus the required SAM padding point (`label=-1`). Encoder embeddings and the previous low-resolution mask are retained by `Sam2Segmenter` so a future GUI can rerun only the decoder as points change. The CLI performs one decoder pass using all supplied points and writes a soft 8-bit grayscale mask at preview dimensions.

The temporary local mask proof command is:

```bash
./build/dev/src/rawlab_cli/rawlab test-mask-blend input.raw model_bayer.onnx output.dng
./build/dev/src/rawlab_cli/rawlab test-mask-split input.raw model_bayer.onnx output.dng
```

It runs the full-image RawNIND PoC, creates a synthetic raw-size horizontal gradient or hard vertical split mask, applies masked CFA blending, and writes a Bayer CFA DNG. Masked blending uses:

```text
alpha = blend_amount * mask(x, y)
output = original * (1 - alpha) + processed * alpha
```

The temporary preview-mask proof command is:

```bash
./build/dev/src/rawlab_cli/rawlab test-preview-mask-blend input.raw model_bayer.onnx output.dng
./build/dev/src/rawlab_cli/rawlab test-preview-mask-split input.raw model_bayer.onnx output.dng
```

It creates a quarter-size synthetic horizontal or hard vertical split mask, maps it back to raw pixels with nearest-neighbor lookup, applies the same CFA masked blend, and writes a Bayer CFA DNG. The split commands are diagnostics: left half should remain original/noisy and center/right should be fully denoised.

## Clipped Highlight Handling

Raw denoise has a boolean option for clipped or near-white CFA samples:

- flag absent: use model output everywhere.
- `--clipped-highlights`: keep original CFA values for clipped sites.

This happens after inference and re-mosaic, before final `--blend`, not inside ONNX. A site is treated as clipped when the original CFA sample is at the source white level within float tolerance.

## Milestone 2 Tests

Use synthetic CFA images to test:

- CFA color lookup for RGGB, BGGR, GRBG, and GBRG.
- Red-origin detection and force-RGGB packing.
- Packed Bayer plane order `R, G1, G2, B`.
- Per-site black/range normalization and inverse conversion.
- Re-mosaic from temporary camRGB back to CFA positions.
- Blend-with-original in CFA space.

## Model I/O Risk

ONNX models may require packed Bayer planes, temporary camRGB output, linear Rec.2020 tiles, camera RGB, or camera-specific metadata. Keep model input/output contracts explicit in `CfaRestoreModel` adapters instead of hiding them in generic tensor code.
