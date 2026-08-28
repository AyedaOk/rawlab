# Milestones

## Milestone 0: Repository Foundation 

- Status: Completed
- CMake project skeleton.
- `rawlab_core`, `rawlab_cli`, and smoke-test targets.
- Initial design notes in `docs/`.
- No external dependencies yet.

## Milestone 1: Raw Decode Inspection

- Status: Completed
- Add LibRaw dependency.
- Open real raw files without demosaicing.
- Print raw metadata and CFA properties from the CLI.

## Milestone 2: RawImage Representation

- Status: Completed
- Define the canonical raw-domain image and metadata structures.
- Convert LibRaw output into the internal representation.
- Add CFA pattern/origin detection, force-RGGB packing, and packed `R/G1/G2/B` plane conversion.
- Add re-mosaic helpers for temporary camRGB back to CFA samples.
- Test CFA coordinate, packed-plane, normalization, and re-mosaic behavior with synthetic data.

## Milestone 3: Experimental CFA DNG Writer

- Status: Completed
- Copy/adapt Darktable's GPLv3-compatible Bayer CFA DNG writer logic from `src/imageio/imageio_dng.c`, especially `dt_imageio_dng_write_cfa_bayer()`.
- Keep the writer isolated in RawLab's DNG module; do not copy Darktable UI, catalog, image cache, Lua, or pixelpipe infrastructure.
- Write synthetic `uint16` Bayer CFA data to non-demosaiced DNG.
- Preserve source-style CFA pattern, black/white levels, `AsShotNeutral`, `ColorMatrix1`, active area/crop, and camera make/model metadata.
- Validate with `tiffinfo`, `exiftool`, darktable, and RawTherapee where available.

## Milestone 4: ONNX CfaRestoreModel Sandbox

- Status: Completed
- Add ONNX Runtime wrapper and verify model I/O through the core API and tests.
- Integrate RawNIND Bayer as a `CfaRestoreModel` after license review.
- Add full-image RawNIND Bayer inference, clipped-highlight preservation, post-inference CFA blend-with-original, and CFA DNG export through `RawProcessingPipeline`.
- Refine Darktable-style preprocessing, tiled inference, gain match, re-mosaic, seam blending, and blend-with-original behavior.

## Milestone 5: Masks And Local Modules

- Status: Completed
- Implement raw-space mask representation and nearest-neighbor sampling tests.
- Add nearest-neighbor preview-mask-to-raw coordinate mapping tests.
- Add `RawProcessingPipeline` as the first core skeleton for global RawNIND denoise, optional clipped-highlight preservation, post-inference blend, and local masked CFA composition.
- Add SAM 2.1 encoder/decoder segmentation from RGB preview PNGs with positive and negative point prompts.
- Add grayscale mask PNG interchange and stable `process --mask <mask.png>` support.
- Add fuller global/local module stack concepts.
- Implement raw-domain masked blend-with-original behavior.

## Milestone 6: CLI End-To-End Prototype

- Status: Completed
- Add `rawlab process <input.raw> <output.dng> [--denoise <model.onnx>] [--mask <mask.png>] [--blend 0..1] [--clipped-highlights]` as the first stable end-to-end prototype command.
- Decode raw input and export it directly when no model is selected.
- Optionally run the RawNIND `CfaRestoreModel` through `RawProcessingPipeline`.
- Export non-demosaiced CFA DNG in both paths.


## Milestone 7: Qt 6 GUI

- Status: Completed
- Add a Qt 6 Widgets desktop application after the raw pipeline proof-of-concepts.
- Use a single-image editor backed by a lightweight persistent file-reference library, not a managed-original catalog database.
- Open a RAW file and generate an RGB preview through LibRaw.
- Keep Bayer CFA data as the processing truth; RGB is only a display and SAM input.
- Display the preview with a mask overlay.
- Add positive and negative SAM point interaction.
- Run the SAM encoder once and reuse cached embeddings while points change.
- Re-run only the SAM decoder when prompts change.
- Run RawNIND through `RawProcessingPipeline`.
- Provide blend controls without rerunning inference in the GUI process.
- Map preview masks back to raw coordinates and export non-demosaiced Bayer CFA DNG.
- Add a permanent Global denoise layer and reorderable local mask layers.
- Give each local layer independent model, blend, invert, enable, edit, and delete controls.
- Composite overlapping masks in ordered CFA-space layers, with higher layers over lower layers.
- Render a live display-only RGB preview from the current CFA composition.
- Run RAW decode, SAM, RawNIND, composition, and export without blocking the GUI thread.
- Keep Global first and insert reorderable local masks beneath it.
- Separate the eye processing toggle from the per-layer `Show mask` overlay control.
- Add adjustable paint and erase brushes for manual masks and SAM refinements.
- Add File, View, and Settings menus with fit and preview zoom actions.
- Persist multiple named SAM model folders with one globally active pair and multiple named denoise models; automatically run uncached inference.
- Add a separate thumbnail library page that imports files/folders by reference.
- Persist per-picture layer recipes while keeping denoised CFA outputs as session caches.

## Milestone 8: CLI And PoC Command Cleanup

- Status: Completed
- Review all proof-of-concept CLI commands before GUI/user-facing workflows start.
- Keep the stable commands `inspect`, `process`, and `mask` as the shipped CLI surface.
- Remove temporary validation commands once their behavior is covered by automated tests.
- Do not expose PoC/debug commands as final user workflows unless they have clear long-term value.

## Milestone 9: DNG Size And Compatibility

- Evaluate DNG-compatible lossless compression for Bayer CFA output.
- Evaluate tiled DNG layout versus scanline strips.
- Evaluate whether writing only the active area is safe without breaking CFA phase or reader compatibility.
- Compare output compatibility in darktable, RawTherapee, and `exiftool` before making compression/cropping default.
