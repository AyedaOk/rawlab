# Agent Notes

- This is a C++20/CMake project for a raw-domain photo preprocessing app; the first milestones are CLI/core proof-of-concepts, not GUI work.
- Use the configured CMake presets: `cmake --preset dev`, `cmake --build --preset dev`, and `ctest --preset dev`.
- Current targets are `rawlab_core`, `rawlab_cli` with output binary `rawlab`, and `rawlab_smoke_test`.
- LibRaw is the first external dependency and is discovered through `pkg-config`; use `rawlab inspect <raw-file>` for the Milestone 1 raw metadata PoC.
- libtiff is used for the isolated Milestone 3 Bayer CFA DNG writer; use `rawlab write-synthetic-dng <output.dng>` for a tiny validation DNG and `rawlab export-cfa-dng <input.raw> <output.dng>` for real raw-derived CFA DNG export.
- ONNX Runtime is used through the C API for Milestone 4; use `rawlab inspect-onnx <model.onnx>` to verify model inputs/outputs, `rawlab test-rawnind-tile <input.raw> <model.onnx> <output.dng>` for the temporary single-tile RawNIND PoC, and `rawlab raw-denoise <input.raw> <model.onnx> <output.dng> [--blend 0..1] [--clipped-highlights]` for the full-image PoC.
- Milestone 5 mask PoC uses raw-size and preview-size masks with nearest-neighbor sampling; use temporary `rawlab test-mask-blend <input.raw> <model.onnx> <output.dng>` or `rawlab test-preview-mask-blend <input.raw> <model.onnx> <output.dng>` to run RawNIND, apply a synthetic horizontal mask, and export masked Bayer CFA DNG. Use `rawlab test-mask-split <input.raw> <model.onnx> <output.dng>` and `rawlab test-preview-mask-split <input.raw> <model.onnx> <output.dng>` as hard left-original/right-denoised diagnostics.
- Use `RawProcessingPipeline` for full-image RawNIND denoise, optional clipped-highlight preservation, post-inference blend, and masked CFA composition; CLI commands should stay thin proof harnesses around core pipeline code.
- Milestone 6 end-to-end prototype is `rawlab process <input.raw> <output.dng> [--denoise <model.onnx>] [--mask <mask.png>] [--blend 0..1] [--clipped-highlights]`; without `--denoise` it directly exports decoded CFA, and denoise-only options require a model.
- SAM 2.1 preview segmentation uses `rawlab mask <preview.png> <mask.png> --encoder <encoder.onnx> --decoder <decoder.onnx> [--point x y] [--negative-point x y]`; encoder embeddings and previous-mask feedback live in `Sam2Segmenter` for future interactive GUI reuse.
- Do not add Qt or Exiv2 until their milestone starts and licensing has been checked.
- Preserve the raw-domain architecture in `docs/`: CFA/Bayer data is the processing truth, RGB previews are derived later, and final export targets non-demosaiced Bayer CFA DNG.
- Use `CfaRestoreModel` for Darktable-style raw denoise adapters: packed Bayer input may produce temporary camRGB, but it must re-mosaic to CFA before Bayer DNG export.
- Keep user blend-with-original outside ONNX inference: cache/produce denoised CFA first, then blend original and denoised CFA in C++ so blend changes do not rerun the model.
- PoC CLI commands may be temporary; before GUI/user-facing work, move debug-only commands under `debug` or remove them once covered by tests.
- GPLv3 is acceptable; Darktable code may be copied/adapted only with GPL-compatible project licensing and attribution.
- Do not start GUI scaffolding until raw decoding, CFA preservation, DNG export, ONNX inference, and mask-to-raw mapping proof-of-concepts are underway or proven.
