# Linux-first reliability roadmap

RawLab remains a local GPLv3-or-later RAW preprocessor for Darktable. CPU processing,
local models, and non-demosaiced CFA DNG are the baseline workflow.

## Implemented reliability foundations

- Separate EXIF exposure compensation from DNG rendering calibration.
- Read metadata from Sony RAW directories rejected by libtiff.
- Update readiness covers every enabled layer, independent of selection.
- Selecting a preset defers composition even when model outputs are cached.
- Preset names and recipes are published in a single atomic JSON save.
- Missing preset mask files fail instead of silently changing the recipe.
- New recipes record the sharpening model identity instead of depending on list order.
- Automatic external-editor runs use noninteractive errors and wait for valid composition.
- Linux build/test CI and synthetic metadata, editor and external-edit regression tests.
- Session batch queue with fixed recipe/settings, reserved output paths, processing
  stage progress, checkpoint cancellation and Resume / Retry for unfinished jobs.
- Atomic on-disk recovery checkpoints and recipe/mask snapshots, restored on
  standalone startup. A process lock prevents simultaneous queue writers; output
  hashes identify publication completed immediately before a crash.
- Single-session Darktable batches use temporary queues independent of standalone
  recovery, with cancellation and duplicate-safe imports on return to RawLab.lua.

## Larger follow-up work

1. Bundle the matching Lua script with releases and consider live completion imports
   (currently imports happen on return from RawLab).
2. A document/job controller with cooperative cancellation and immutable composition
   snapshots, followed by bounded caches and coordinated inference/thread limits.
3. Preset rename and portable import/export bundles with model compatibility checks.
4. Model-by-model license/source inventories and a verified freely licensed default
   catalog. Separate optional vendor-specific acceleration from the FOSS CPU baseline.
5. Linux desktop installation, Wayland/high-DPI testing, and a small-screen editor
   layout review. Keep AppImage as the primary portable Linux package.

Model inference benchmarks and actual Darktable UI import tests require suitable
models and hardware; passing the synthetic CI suite does not establish those results.
