# Architecture

RawLab is a cross-platform desktop application for raw photo preprocessing. Linux/KDE Plasma is the primary target, but core code should avoid platform-specific assumptions.

The first implementation phase is not GUI-first. The core risks are raw decoding, CFA preservation, Darktable-style CFA restore, non-demosaiced CFA DNG export, ONNX inference, and mask-to-raw coordinate mapping.

## Components

- `rawlab_core`: reusable raw-domain processing library.
- `rawlab_cli`: proof-of-concept command-line entrypoint for decode, inspect, process, and export experiments.
- `rawlab_preview`: persistent native worker exposing `rawlab_core` to the desktop shell.
- `rawlab_desktop`: shared settings, library, recipe, model, and batch services.
- `desktop`: React editor hosted by Tauri.
- `tests`: automated checks for deterministic core behavior.

## Pipeline Direction

```text
RAW file
  -> raw decoder preserving CFA samples
  -> canonical RawImage representation
  -> global CFA modules
  -> local CFA modules with masks
  -> CFA-domain blend/composition
  -> non-demosaiced Bayer CFA DNG export
```

Demosaiced RGB previews are derived views only. They must not become the internal processing truth.

`RawProcessingPipeline` is the current core pipeline skeleton. It owns full-image RawNIND Bayer orchestration, optional clipped-highlight preservation, post-inference blend-with-original, and masked CFA blending. CLI commands should call this core pipeline instead of owning denoise tiling or local-mask composition directly.

`Sam2Segmenter` owns the interactive segmentation context: the RGB preview encoder runs once, embeddings are cached, and decoder calls can reuse those embeddings and previous-mask feedback as point prompts change. Mask PNGs are preview-space interchange artifacts; CFA composition maps them back to raw coordinates.

The Tauri editor keeps one permanent Global denoise layer first,
with reorderable local layers beneath it. Local layers contain a SAM base mask,
optional manual brush corrections, model path, blend amount, invert state,
processing-enabled state, and independent overlay visibility. Denoised CFA
results are cached by model path; layer reorder, mask, inversion, and blend
changes only recompose cached CFA results. The RGB canvas is rendered from that
composition and never becomes the export source.

Global composition is a separate core stage. `composeDenoiseLayers()` creates
the Global base first, then applies local inputs in explicit bottom-to-top
order. Each local model output is blended with original CFA before its mask is
composited, so Global cannot overwrite an available local result.

Application settings use stable model IDs: multiple named SAM 2.1 folders with
one globally active encoder/decoder pair, plus multiple named denoise model
paths. The picture library stores canonical
source references in place and caches derived thumbnails. JSON recipes under
the application-data directory retain editable per-picture layer state; raw
model outputs are intentionally not persisted because of their size.

## CfaRestoreModel

`CfaRestoreModel` is the model adapter name for denoisers/restorers that consume CFA-derived input and produce CFA-compatible output for export.

The preferred RawNIND Bayer path follows Darktable's approach:

```text
source CFA
  -> black-subtract, range-normalize, optional WB normalization
  -> pack Bayer as 4-channel R/G1/G2/B tensor, forcing RGGB when required
  -> run ONNX model
  -> gain-match model output when required
  -> re-mosaic temporary camRGB output back to one CFA sample per pixel
  -> cache/produce denoised CFA
  -> blend original CFA with denoised CFA in C++
  -> write Bayer CFA DNG
```

The temporary camRGB output is an implementation detail of some `CfaRestoreModel` backends. The app-level result remains CFA-compatible data.

Blend-with-original is not part of inference. The denoised CFA result should be reusable so UI/module blend changes can recompute only the CFA blend, not the ONNX model.

## Module Model

- A global module applies to the whole raw image.
- A local module owns its mask, model selection, settings, and blend-with-original amount.
- Multiple local mask modules must be supported.
- Module outputs are composed in CFA space for the Bayer DNG export path.
- Global applies first. Local modules composite bottom-to-top; the local mask nearest Global is applied last.

Recipe coordinates are normalized in oriented preview space, and soft raster
masks are resampled when preview dimensions differ. This lets a library batch
apply one complete layer recipe to differently sized selected pictures while
keeping full-resolution CFA decode, inference, composition, and DNG export as
the processing path.
