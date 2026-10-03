# Agent Notes

## Required skills

- Load and follow `stop-slop` when drafting or editing prose.
- Load and follow `graphify` for codebase questions and architecture exploration. Query an existing graph before rebuilding it.
- Load and follow `ponytail` for coding, debugging, refactoring, and code review. Use full intensity unless the user requests another level.

## Project notes

- Release packaging and tag/retry instructions are documented in `docs/release.md`.

- This is a C++20/CMake project for a raw-domain photo preprocessing app; the first milestones are CLI/core proof-of-concepts, not GUI work.
- Use the configured CMake presets: `cmake --preset dev`, `cmake --build --preset dev`, and `ctest --preset dev`.
- Current targets are `rawlab_core`, `rawlab_desktop_support`, `rawlab_cli` with output binary `rawlab`, `rawlab_preview` with output binary `rawlab-preview`, and the native tests.
- LibRaw is the first external dependency and is discovered through `pkg-config`; use `rawlab inspect <raw-file>` for the Milestone 1 raw metadata PoC.
- libtiff is used by the production `rawlab process` command for Bayer CFA DNG export. Synthetic DNG and diagnostic commands belong in tests or developer-only tools, not the shipped CLI.
- ONNX Runtime is used through the C API by the production GUI and CLI. The stable CLI surface is `rawlab inspect`, `rawlab process`, and `rawlab mask`; model-contract and pipeline probes belong in tests or developer-only tools.
- Use `RawProcessingPipeline` for full-image RawNIND denoise, optional clipped-highlight preservation, post-inference blend, and masked CFA composition; CLI commands should stay thin proof harnesses around core pipeline code.
- The stable end-to-end CLI command is `rawlab process <input.raw> <output.dng> [--denoise <model.onnx>] [--mask <mask.png>] [--blend 0..1] [--clipped-highlights]`; without `--denoise` it directly exports decoded CFA, and denoise-only options require a model.
- SAM 2.1 preview segmentation uses `rawlab mask <preview.png> <mask.png> --encoder <encoder.onnx> --decoder <decoder.onnx> [--point x y] [--negative-point x y]`; encoder embeddings and previous-mask feedback live in `Sam2Segmenter` for future interactive GUI reuse.
- The editor uses React in a Tauri shell. The native worker uses Qt 6 Core, Gui, and Network; do not add Qt Widgets or Concurrent back.
- Preserve the raw-domain architecture in `docs/`: CFA/Bayer data is the processing truth, RGB previews are derived later, and final export targets non-demosaiced Bayer CFA DNG.
- Use `CfaRestoreModel` for Darktable-style raw denoise adapters: packed Bayer input may produce temporary camRGB, but it must re-mosaic to CFA before Bayer DNG export.
- Keep user blend-with-original outside ONNX inference: cache/produce denoised CFA first, then blend original and denoised CFA in C++ so blend changes do not rerun the model.
- Temporary CLI proof commands have been removed; keep debug-only experiments under tests or a separate developer-only tool.
- GPLv3 is acceptable; Darktable code may be copied/adapted only with GPL-compatible project licensing and attribution.
- The editor keeps Global first with ordered local masks beneath it. Local masks may combine SAM output with manual paint/erase strokes; processing eye state and `Show mask` overlay state are independent. Cache denoised CFA by model and keep layer blend, inversion, enablement, and ordering outside ONNX inference.
- Native desktop support persists multiple named SAM model folders with one globally active encoder/decoder pair, multiple named denoise models, a path-reference picture library, cached thumbnails, and per-picture JSON recipes. Global composes first and local mask denoise outputs always compose afterward.
- Apply the project's stop-slop discipline to UI work: prefer focused workflows over persistent controls, remove duplicate or decorative UI, and verify that every visible action serves a concrete user task. If a local stop-slop skill is available, load and follow it before frontend changes.
