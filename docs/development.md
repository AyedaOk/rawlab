# Developer reference

## Components

- `desktop/`: React editor and Tauri shell.
- `src/rawlab_preview/`: persistent native worker for previews and processing.
- `src/rawlab_core/`: RAW decoding, model inference, masks, composition, and DNG export.
- `src/rawlab_desktop/`: settings, model registrations, library, recipes, and batch services.
- `src/rawlab_cli/`: `inspect`, `process`, and `mask` commands.
- `tests/` and `desktop/tests/`: native and frontend checks.

Use the [desktop bridge contract](desktop-preview-bridge.md) when changing worker
messages, and the [Darktable batch protocol](darktable-batch-protocol.md) when
changing the Lua handoff. See [release instructions](release.md) for packaging.

## Processing rules

Preserve source CFA samples through decoding, denoising, and layer composition.
Derive RGB previews from that composition; never use display pixels as export input.
Use `RawProcessingPipeline` for shared processing and keep CLI commands thin.

`CfaRestoreModel` adapters own model-specific normalization and tensor contracts.
The RawNIND Bayer adapter packs float32 `[1, 4, T, T]` in R/G1/G2/B order after
black subtraction, range and white-balance normalization. It shifts to the red
origin for RGGB input and mirror-pads within the effective crop. Its temporary
camRGB output has shape `[1, 3, 2T, 2T]`; gain matching, inverse normalization, and
re-mosaicing produce sensor-order CFA again.

Cache denoised CFA by model. Apply highlight preservation, blend-with-original,
and mask composition outside ONNX inference. Compose Global first, then local
layers bottom-to-top; the local layer nearest Global applies last. Keep processing
enablement separate from mask-overlay visibility.

Track active-area offsets, CFA phase, scaling, and orientation explicitly through
`PreviewGeometry`. Store recipe points in normalized oriented-preview coordinates
and resample soft masks when dimensions change. `Sam2Segmenter` caches encoder
embeddings and previous-mask feedback for repeated point prompts.

Fast previews use half-size Bayer demosaicing. High Quality uses the same path at
scales of one half or less, and AMaZE above that. Linear RGB export uses
full-resolution AMaZE. Retain the Darktable and original author attribution in
`AmazeDemosaic.cpp`.

Keep source files read-only. Persist source references, derived thumbnails, and
per-picture JSON recipes; do not persist large raw model-output caches.

## DNG export

Bayer CFA export retains sensor-order samples and source orientation. Linear RGB
export writes white-balanced linear sRGB with a RawLab identity profile,
XYZ-D65-to-sRGB matrix, D65 illuminant, unity neutral/analog balance, and explicit
three-channel black/white levels. Sharpening and Lensfun optical corrections apply
to Linear RGB output, not Bayer CFA output.

Preserve black/white levels, CFA layout, active area, camera identity, white balance,
color matrices, and calibration illuminants. Export available exposure and lens
metadata, plus JPEG thumbnails and display previews. Do not copy GPS, MakerNotes,
source XMP, or arbitrary structural TIFF tags.

Read exposure compensation through LibRaw's EXIF callback: some RAW files have a
metadata-only TIFF directory that libtiff cannot open. Keep EXIF ExposureBiasValue
separate from DNG BaselineExposure. Preserve the latter from input DNGs or use the
zero default. For DNG inputs, retain embedded color matrices and illuminants even
when the generic camera-table matrix is empty.

Run `cmake --preset dev`, `cmake --build --preset dev`, and `ctest --preset dev`
for native checks. Use `npm --prefix desktop run build` and
`npm --prefix desktop test` for frontend checks. Validate representative DNGs with
`tiffinfo`, `exiftool`, Darktable, and an independent reader such as RawTherapee;
check geometry, color, metadata, and both export modes.

## Open defects and validation gaps

- **macOS recipe restoration:** the packaged-worker bridge test on 2026-10-04
  restored empty local-layer points and category strokes. Investigate persistence
  versus test isolation. DNG export passed in the same
  [Actions run](https://github.com/AyedaOk/rawlab/actions/runs/37239571508).
- **CUDA library loading:** the tested Arch host required
  `LD_PRELOAD=/usr/lib/libcudnn.so.9`. Investigate runtime dependency loading before
  treating this host-specific workaround as a general setup instruction.
- **Sharpening:** a user reported softer previews at 100%. Reproduce with Normal
  and DeepSharpen, comparing previews and exported Linear RGB output.
- **Blend responsiveness:** an earlier report described full-resolution
  recomposition lag. Recheck against the current cached RGB endpoint path.
- **Packaged-host checks:** exercise close/reopen persistence, interrupted batch
  recovery, actual model inference, and a Darktable round trip. Check clean Linux
  hosts, Wayland, high-DPI, and small-screen layouts.
- **Lens geometry:** verify corrected previews, comparison, 1:1 views, point and
  brush masks against a calibrated wide-angle image. Mask editing uses unwarped
  coordinates; check alignment when returning to the corrected view.
