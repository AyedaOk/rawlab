# Agent Notes

- Release packaging and tag/retry instructions are documented in `docs/release.md`.

- This is a C++20/CMake project for a raw-domain photo preprocessing app; the first milestones are CLI/core proof-of-concepts, not GUI work.
- Use the configured CMake presets: `cmake --preset dev`, `cmake --build --preset dev`, and `ctest --preset dev`.
- Current targets are `rawlab_core`, `rawlab_cli` with output binary `rawlab`, `rawlab_app` with output binary `rawlab-app`, and `rawlab_smoke_test`.
- LibRaw is the first external dependency and is discovered through `pkg-config`; use `rawlab inspect <raw-file>` for the Milestone 1 raw metadata PoC.
- libtiff is used by the production `rawlab process` command for Bayer CFA DNG export. Synthetic DNG and diagnostic commands belong in tests or developer-only tools, not the shipped CLI.
- ONNX Runtime is used through the C API by the production GUI and CLI. The stable CLI surface is `rawlab inspect`, `rawlab process`, and `rawlab mask`; model-contract and pipeline probes belong in tests or developer-only tools.
- Use `RawProcessingPipeline` for full-image RawNIND denoise, optional clipped-highlight preservation, post-inference blend, and masked CFA composition; CLI commands should stay thin proof harnesses around core pipeline code.
- Milestone 6 end-to-end prototype is `rawlab process <input.raw> <output.dng> [--denoise <model.onnx>] [--mask <mask.png>] [--blend 0..1] [--clipped-highlights]`; without `--denoise` it directly exports decoded CFA, and denoise-only options require a model.
- SAM 2.1 preview segmentation uses `rawlab mask <preview.png> <mask.png> --encoder <encoder.onnx> --decoder <decoder.onnx> [--point x y] [--negative-point x y]`; encoder embeddings and previous-mask feedback live in `Sam2Segmenter` for future interactive GUI reuse.
- Qt 6 Widgets is used by the Milestone 7 single-image editor. Do not add Exiv2 until its milestone starts and licensing has been checked.
- Preserve the raw-domain architecture in `docs/`: CFA/Bayer data is the processing truth, RGB previews are derived later, and final export targets non-demosaiced Bayer CFA DNG.
- Use `CfaRestoreModel` for Darktable-style raw denoise adapters: packed Bayer input may produce temporary camRGB, but it must re-mosaic to CFA before Bayer DNG export.
- Keep user blend-with-original outside ONNX inference: cache/produce denoised CFA first, then blend original and denoised CFA in C++ so blend changes do not rerun the model.
- PoC CLI commands may be temporary; before GUI/user-facing work, move debug-only commands under `debug` or remove them once covered by tests.
- GPLv3 is acceptable; Darktable code may be copied/adapted only with GPL-compatible project licensing and attribution.
- The GUI keeps Global first with ordered local masks beneath it. Local masks may combine SAM output with manual paint/erase strokes; processing eye state and `Show mask` overlay state are independent. Cache denoised CFA by model and keep layer blend, inversion, enablement, and ordering outside ONNX inference.
- The Qt app persists multiple named SAM model folders with one globally active encoder/decoder pair, multiple named denoise models, a path-reference picture library, cached thumbnails, and per-picture JSON recipes. Global composes first and local mask denoise outputs always compose afterward.
