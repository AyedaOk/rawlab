# RawLab

RawLab is a desktop RAW photo preprocessing application developed
through “vibe coding”. It keeps Bayer CFA data intact while applying
AI denoise and sharpening, then exports a non-demosaiced CFA DNG
for use in a RAW editor.

## Features

- Global and local denoise layers
- SAM 2.1 masks with point prompts and manual masking
- Normal or RawForge's DeepSharpen sharpening after denoising
- Batch editing
- CPU and compatible GPU inference
- Model Manager for installing SAM, denoise, and sharpen models

## Screenshots

![RawLab editor with before-and-after comparison](docs/screenshots/Screenshot_20260827_012134.png)

## Build From Source

Development requirements are C++20, CMake 3.25+, Ninja, Qt 6, LibRaw,
libtiff, libpng, and the ONNX Runtime C API headers. ONNX Runtime must provide
`onnxruntime_c_api.h` and its shared library.

Development build:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Release GUI build:

```bash
cmake --preset release
cmake --build --preset release
```

The desktop application is `build/release/src/rawlab_app/rawlab-app`.

## Basic Workflow

1. Import RAW files or a folder in the Library.
2. Open a picture and choose a denoise model.
3. Press **Update** to generate the denoised preview.
4. Adjust blend and sharpening. 
5. Add mask layers if needed.
6. Export a CFA DNG.

**Update** checks Global and every enabled mask layer, reusing cached results.
In **Presets**, choose saved settings, adjust them, then press Update. Presets
include masks and brush strokes; copied masks retain their relative positions
and do not automatically track a subject in another picture. Save with the same
name to replace a preset, or a different name to create a copy.

Use **Edit > Undo / Redo** (Ctrl+Z / Ctrl+Shift+Z on Linux) for settings and
preset selection, brush paint/erase strokes, mask points, shapes, clearing masks,
and adding, deleting, renaming, enabling or reordering layers.
A slider drag or completed brush stroke counts as one change. Restored recipes wait for
Update Preview. History holds up to 20 recipe snapshots for the current image
and resets when another image is loaded. Undo/redo waits for running processing
and unfinished brush strokes to complete. Redo restores saved masks without
rerunning SAM.

For batch processing, select several pictures before opening them. The batch export
uses the settings from the picture currently being edited and applies them to every
selected picture. 

Batch export saves a recoverable queue. Progress shows the current
picture and processing stage, including inference tile counts. **Cancel** stops
at the next processing checkpoint; decoding, a running inference tile, or preview
rendering may need to finish first. Completed DNGs are kept.

Use **File > Resume / Retry Batch** (or the completion dialog) to process only
failed and unfinished pictures. The queue retains its original recipe, model
settings, and output paths even if you edit another image. New batches assign
numbered filenames when necessary; retries never overwrite existing output.
Starting another batch replaces the retained queue after confirmation.
After closing or restarting RawLab, use **File > Resume / Retry Batch** to recover
unfinished work. No image needs to be open. An interrupted image starts again;
completed images are not reprocessed. The recipe (including masks), model settings,
job states and output paths are saved under `batch/` in RawLab's application data.
Only one standalone RawLab window can own the recovery queue at a time.
Darktable's **Edit first image, apply settings to all** mode uses the same processing
worker with a temporary queue. It does not load or replace standalone recovery data.

To test restart recovery:
1. Start a batch of at least three images into a new output folder.
2. After one DNG finishes, cancel, wait for processing to stop, and close RawLab.
3. Reopen RawLab normally and choose **File > Resume / Retry Batch**.
4. Confirm only unfinished images are processed, with the original settings and
   filenames. The completed DNG's modification time should remain unchanged.
5. For a crash test, repeat with a new batch, force-stop RawLab using your system
   monitor while an image is processing, then reopen and resume. The interrupted
   image restarts from the beginning. Keep sources and models in their original locations.

## High Quality preview

High Quality uses AMaZE on the main canvas, with area-weighted downsampling in
linear camera RGB to reduce aliasing. It adds no display-only sharpening. Before
and After use the same quality setting; original previews are cached per image
for Fast and High Quality. Existing zoom and comparison controls stay on the
main canvas. Zooming to 100% still refers to preview pixels, not full sensor resolution.

## Models

Model weights are not included in the source repository or application. Use
**Settings > Model Manager** to download available SAM 2.1, denoise, and
sharpen models, or add local models manually. Model licenses are separate from
the RawLab license.

## Acceleration

RawLab uses the execution providers exposed by the installed ONNX Runtime.
Select **Auto** to use the best available provider with CPU fallback. GPU
acceleration requires compatible hardware drivers and matching runtime
libraries.

## Output

RawLab exports non-demosaiced Bayer CFA DNG files. The editor preview is a
display-only RGB view. Output files can be opened by RAW applications that
 support CFA DNG workflows.

Original files are never modified. RawLab stores library references,
recipes, presets, and downloaded models in its application data directory.

Recipes, presets and models use Qt's application data location (on Linux,
normally under `$XDG_DATA_HOME/RawLab/RawLab`, or `~/.local/share/RawLab/RawLab`).
Disposable thumbnails use the application cache location instead. Back up the
whole `presets` folder: JSON files reference PNG mask assets stored alongside them.

## CLI

The optional CLI provides stable automation commands:

```bash
rawlab inspect input.ARW
rawlab process input.ARW output.dng
rawlab process input.ARW output.dng --denoise model.onnx --blend 1.0
rawlab mask preview.png mask.png --encoder encoder.onnx --decoder decoder.onnx --point 790 430
```

## Darktable integration

The Linux GUI supports an external-editor mode for the `RawLab.lua` Darktable script:

```bash
RawLab.AppImage --external-edit /absolute/input.ARW --output /absolute/input-rawlab.dng
```

RawLab opens the original sensor RAW for interactive editing. In this mode,
**Export and Return to Darktable** writes the requested DNG without overwriting
an existing file and exits only after a successful export. 

For **Edit first image, apply settings to all**, update both RawLab and `RawLab.lua`
from `DT_custom_script`. Select several photographs in Darktable and launch RawLab.
Edit or select a preset for the first image, press Update, then **Start Batch and
Return to Darktable**. The entire selection runs in one session using a temporary queue.

```bash
RawLab.AppImage --external-batch /absolute/darktable-config/rawlab-batch-request.txt
```

Progress is shown in RawLab. On return (including cancellation), the script imports
each completed DNG and groups it with its original. Existing imports are not reset
or duplicated. Import happens when RawLab returns, not live during inference.

There is no Lua resume feature. Cancellation from the Darktable job or RawLab
progress stops at a processing checkpoint. Completed outputs are imported and
temporary request/receipt files are removed. Select unfinished images to start
another batch. Legacy pending-request files from older plugins are ignored.

Standalone recovery queues and their locks do not block Darktable batches.
With the batch checkbox off, images continue
to open individually. `--apply-settings-from` remains available for older callers.

### Development checks

Linux CI builds the GUI and CLI and runs tests on pushes and pull requests.
Tests use synthetic sensor data and cached processing fixtures; model downloads
and proprietary GPU drivers are not required. Actual ONNX inference and the
installed Darktable/Lua workflow still need integration testing on a workstation.

For a user-owned RAW exposure-metadata regression check after building:

```bash
build/dev/tests/rawlab_exposure_metadata_test /absolute/photo.ARW
```

This writes a temporary DNG and checks that EXIF exposure compensation survives.
It never changes the RAW. EXIF exposure compensation and DNG BaselineExposure
are kept separate. Re-export older DNGs to pick up metadata-reader fixes.

## Acknowledgments

RawLab builds on and uses work from:

- [RawForge](https://github.com/rymuelle/RawForge), by rymuelle.
- [Raw Natural Image Noise Dataset](https://doi.org/10.14428/DVN/DEQCIM), by Benoit Brummer and Christophe De Vleeschouwer.
- [NAFNet: Simple Baselines for Image Restoration](https://doi.org/10.48550/arXiv.2208.04677), by Liangyu Chen and contributors.
- [darktable](https://darktable.org/).

## License

RawLab is licensed under GPLv3-or-later. Model files have their own licensing
terms. See `docs/model-licensing.md` before redistributing model weights.
