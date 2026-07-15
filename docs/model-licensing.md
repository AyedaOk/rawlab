# Model And Dependency Licensing

Verify license compatibility before adding or redistributing dependencies, model code, model weights, copied source code, or sample raw files.

RawLab may copy or adapt Darktable rawdenoise/DNG code. Darktable is GPL-3.0-or-later, and the RawNIND package used by Darktable declares GPL-3.0. Treat RawLab as a GPL-3.0-or-later project unless this changes deliberately.

## Initial Dependencies To Verify

- LibRaw license and selected build options.
- libtiff license.
- ONNX Runtime license and execution-provider-specific licenses.
- Exiv2 license before adding metadata support.
- RawSpeed license before evaluation.

## Models To Verify

- RawNIND / `rawnind_jddc` code, weights, datasets, and redistribution rights.
- RawForge-style model code, weights, datasets, and redistribution rights.
- SAM 2.1 code, weights, and redistribution rights. The local Darktable model manifest declares Apache-2.0 weights; its SA-1B training-data note references separate Meta research-only terms, so keep weights external until redistribution review is complete.

Keep pretrained weights external to the repository until redistribution terms are clear.

RawNIND Bayer model I/O has been verified locally with ONNX Runtime: `model_bayer.onnx` uses float input `[1, 4, 512, 512]` and float output `[1, 3, 1024, 1024]`.

The local SAM 2.1 Large encoder/decoder contracts have also been verified: RGB encoder input `[1, 3, 1024, 1024]`, three cached embedding outputs, and decoder point/previous-mask inputs producing mask logits and IoU predictions.

## Darktable Code Candidates

Copy or adapt only the focused pieces needed for the standalone raw path:

- Bayer packing and red-origin detection.
- Force-RGGB handling and mirror-cropped edge padding.
- Black/white/range normalization and WB normalization modes.
- RawNIND tiled inference postprocessing, gain match, and re-mosaic math.
- libtiff-based Bayer CFA DNG writer logic.

Do not copy GTK UI, catalog/database, Lua, image cache, or full pixelpipe infrastructure unless a later milestone proves it is necessary.
