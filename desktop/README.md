# RawLab Desktop

RawLab's React editor runs in a Tauri desktop window or in a browser. The
desktop shell provides RAW viewing and processing through a persistent C++
worker and shared native settings, library, model, and batch services.

## Process a RAW

Rebuild `rawlab_preview` using the commands below, then restart the desktop app.
Configure denoise, sharpening, SAM, and depth models in the desktop settings. Missing
models produce an error, not a fake result.

1. Open a RAW, select a denoise model and set blend, sharpening and highlights.
2. Press Update. The worker applies all enabled layers and returns real before/after
   previews. Source CFA and denoise/sharpen outputs stay in the C++ worker; repeated
   blend changes reuse inference results for the current document/model.
3. Zoom in and click-drag to pan. 1:1 preserves the viewed location; clicking it
   again at 100% keeps the same view. Fit restores centering. In mask editing, hold
   Space to pan after focusing the image. Drag the comparison divider to move it.
4. Add a mask to start editing, and draw Brush/Erase, Gradient or Ellipse marks.
    **Point** uses the active SAM model configured in Settings (Shift-click for a
   negative point). Done keeps the mask. The red overlay previews feather, blur and
   inversion while editing. After the first Update, mask changes recompose cached denoising.
   Brush and Erase each keep a Size setting; `[` / `]` or Shift + mouse wheel
   over the image resize the current tool. Wheel up increases size; down decreases it.
   Size is the diameter as a percentage of the short image edge. Each stroke keeps
   its own size. The circular cursor previews that diameter. Ellipse and Gradient
   show drag guides even with Show mask off; hold Space for the pan cursor.
5. Export DNG opens a native save dialog and writes Bayer CFA or Linear RGB with metadata
   and JPEG previews. Existing files are not overwritten. Cancel requests a stop at
   a processing checkpoint; an in-flight inference or demosaic may need to finish.

Ctrl+Z / Ctrl+Shift+Z (Cmd on macOS) restore up to 20 setting/layer/mask snapshots
per photograph. Undo keeps the selected mask open if it still exists. The mask
editor also provides Undo, Redo and Clear mask; you can undo Clear mask.

Local masks override Global and the local layers below them. Set **Blend with
denoised** to 0% to restore the original inside the mask, or 100% for full denoising.
Mask fades control the transition to the lower layers. Turn off the layer's
processing eye to reveal those layers throughout the mask.

### Depth masks

Choose **Depth** in Edit Mask to estimate relative depth with the active Depth
Anything V2 model. The single track runs from Near to Far. Move the two downward
arrows above it to set the fully selected range, and the upward arrows below it
to set the fade-in and fade-out limits. The track shading previews both fades.
The percentages describe relative depth within this photograph; hover a knob
to see its name and value.
Drag a handle past its partner to move both. RawLab keeps the start pair before
the end pair.

Dragging previews the mask without rerunning inference. Release to commit one
undo step. Brush and Erase can refine the selection; Show mask and Invert mask
work as they do for other mask tools. Clear mask removes the selection and manual
marks, and Undo restores them. After the first Update, range and inversion changes
refresh processing previews without rerunning denoising.
RawLab saves depth maps and ranges in the recipe, so reopening a selection does
not require the model. Mask editing uses the unwarped preview coordinates.

To prepare the Apache-2.0 **Small FP32** model for local use or catalog upload:

```bash
python3 scripts/prepare-depth-model.py
```

Run this from the repository root with Python 3.11 or later. The script verifies
the ONNX files against the pinned Hugging Face revision's SHA-256 hashes. It writes
the model, preprocessing config, license, attribution, and RawLab manifest under
`dist/depth-model/models/mask-depth-anything-v2-small/1.0/`. In Preferences, choose
**Depth** under Add local model and select `model.onnx`. Keep `model.onnx_data`
beside it, then select the active depth model in Processing settings.

For publishing, upload that model directory to `AyedaOk/rawlab-models` and merge
`dist/depth-model/catalog-entry.json` into the current catalog's `models` array.
The generated `catalog.json` contains a merged snapshot from the time of download.
The script does not upload files. The Small weights use Apache-2.0; retain the
included license and notices when redistributing them.

Normal sharpening works with Raw and Linear RGB output and starts a preview update
when selected. AI sharpening requires Linear RGB output. Sharpening defaults to Off.
The editor shows “Updating preview…” while applying strength changes, without an Update button.
Strength changes reuse cached denoised CFA and keep the last 1:1 tile visible
until the browser decodes its replacement. Detail rendering composes CFA in 256-pixel tiles,
including local masks, then applies optical corrections to the requested region.
High Quality 1:1 uses the export's AMaZE demosaicing; Fast 1:1 uses bilinear demosaicing.
Neither adds display-only sharpening. Overview adjustments first show cached RGB
composites, then a CFA-derived reference after the controls settle.
Small pans reuse overlapping tiles. The editor prioritizes visible detail over
the overview and loads original detail only when you open Before / After.
New preview requests cancel superseded renders between processing stages.

The worker retains up to two decoded photographs within a 256 MiB budget and
keeps model outputs across photo switches within a separate 512 MiB budget.
It evicts unused Normal-sharpening results before model outputs. An oversized
image or an in-use working set can exceed these budgets until eviction is possible.
Desktop presets persist through the native worker. Browser-mode controls
remain a non-processing demo.

## Enable RAW viewing

From the repository root, build the native worker using the existing C++ dependencies:

```bash
cmake --preset dev
cmake --build --preset dev --target rawlab_preview --parallel 2
cd desktop
npm ci
npm run desktop
```

`npm run desktop` configures and incrementally rebuilds the development worker,
stages it for Tauri, then starts Vite. Restart the app after native edits.
Development launches prefer `build/dev`, then `build/preview`, then the sibling
worker. For an optimized preview
or another build location, set `RAWLAB_PREVIEW_ENGINE` to the **absolute executable
path** before launching; this uses your supplied worker and skips the automatic build. Release builds use the
sibling worker. The C++ helper needs its Qt/LibRaw/PNG runtime libraries installed.

Use ONNX Runtime 1.20 or newer. In Preferences, enter the full path to the runtime
library on the machine running RawLab. The worker requests API 20 even when you
compile it with newer ONNX headers. If you downloaded a model with an older worker,
restart with the rebuilt worker and use **Refresh catalog** to register the installed
files, then select the active depth model and save processing settings.

In Preferences, **Remove from list** keeps model files on disk. **Remove** also
deletes the verified catalog download.

Import RAW pictures or a folder using the native buttons. Thumbnails are rendered
at 400 pixels with Fast demosaicing. Open a RAW in Edit to render up to 4096 pixels;
High Quality uses the existing AMaZE pipeline and area-weighted reduction. Uncheck
High Quality for Fast mode. Metadata includes camera, sensor dimensions, ISO,
aperture, shutter, focal length and exposure compensation. Lens information is
available in the metadata tooltip.

Preview work runs outside the UI thread, one RAW decode at a time. The desktop
caches at most eight encoded previews / 64 MiB, keyed by source and engine file
size/modification time, dimensions and quality. Stale responses cannot replace the
currently selected image. Refresh RAW preview retries errors or rereads a changed
source. Unsupported sensors (including non-Bayer layouts) display an error; choosing
a RAW extension alone does not guarantee decoder/model support.

Before a processing update, both comparison sides show the original RAW. After an
update, the processed side uses the returned CFA composition. JPEG/PNG/WebP and
browser viewing remain available. The stable inspect/process/mask CLI surface is
unchanged; `rawlab-preview --serve` is a private desktop bridge.

## Desktop launch on Linux

Requirements: Node.js 20.19+, npm, current stable Rust/Cargo (via
[rustup](https://rustup.rs)), a C/C++ compiler, pkg-config and WebKitGTK 4.1 development files.

Ubuntu/Debian:

```bash
sudo apt install build-essential pkg-config libwebkit2gtk-4.1-dev libssl-dev librsvg2-dev
```

Arch Linux:

```bash
sudo pacman -S --needed base-devel webkit2gtk-4.1 openssl librsvg
```

After installing Rust, open a new terminal so `cargo` is on PATH. In `desktop`:

```bash
npm ci
npm run desktop:prepare
npm run desktop
```

This opens **RawLab**, not a browser tab. Stop any existing
`npm run dev` server first: desktop development manages its own server on port 5173.
Run npm and Cargo as your normal user, not with sudo. Initial Rust compilation
can take several minutes. On KDE, keep your distribution's desktop portal services
enabled for native file dialogs.

Build a standalone executable with bundled frontend assets:

```bash
npm run desktop:build
install -m755 ../build/release/src/rawlab_preview/rawlab-preview src-tauri/target/release/rawlab-preview
./src-tauri/target/release/rawlab-desktop
```

Build the portable package with `npm run desktop:bundle:linux` after running
`scripts/stage-tauri-linux.sh` from the repository root. See `docs/release.md`.

### Try the desktop shell

1. Launch with `npm run desktop`.
2. Use Import Pictures to pick RAW files or JPEG/PNG/WebP images in a native dialog.
3. Use Import Folder and check nested photographs appear. Cancel a picker to
   confirm the existing collection is preserved.
4. Open a picture, adjust interface controls, resize the window and try comparison.
5. For a RAW, toggle High Quality, check orientation/metadata, process and export
   to a new DNG. Source RAW files remain unchanged.

Native imports are read-only and scoped to paths selected in the dialogs. Unsupported
files and symlinks are skipped. The desktop caps one import at 500 images,
50 MiB per raster-preview file and 256 MiB total raster import data. RAW files are
passed by path to the decoder rather than loaded into JavaScript. Directory scans are bounded.

## Run on Linux

Install Node.js 20.19+ and npm, then from the repository root:

```bash
cd desktop
npm ci
npm run dev
```

Open the localhost URL printed by Vite (normally http://127.0.0.1:5173).
Stop with Ctrl+C. No CMake rebuild is needed for browser mode.

## What to review

- Library/Edit navigation with the rounded charcoal containers from the supplied screenshot.
- Library with a slim icon-only import rail on the left and no adjustments panel or zoom controls. Import Pictures supports
  multiple files; Import Folder reads browser-supported previews from a directory.
  Browser mode skips RAW files; desktop mode renders supported RAWs through the bridge. Files are deduplicated within
  the session by relative name, size and modification time.
- Click to select and Ctrl/Shift-click to extend selection. Double-click or Enter
  opens a photograph directly; there is no Open button in the footer. A filter-icon
  button opens filename, minimum-rating and file-type controls, with Reset filters.
- Edit has a right adjustments panel and filmstrip, with no left sidebar.
  Export appears only in that panel, not the top header.
- Separate collapsible Layers, Denoise and Sharpen sections, with contextual mask editing.
  Preserve highlights is under Denoise; sharpening mode and strength are under Sharpen.
- Layer actions (Add Mask, Edit Mask and Delete) sit together below the layer list;
  Edit/Delete are disabled for Global. Edit Mask reveals tools and feather, blur,
  inversion and overlay controls directly below Layers. Done hides the panel while
  keeping settings. Changing layers or images closes editing.
- A Presets icon beside Reset Settings opens saved presets, highlights the selected
  one, and offers Save current settings and Delete selected preset. Deleting a
  preset leaves image settings intact; applying one waits for Update.
- Save current settings opens a naming dialog, prefilled with the selected preset name.
  Saving with the same name replaces that session preset; Cancel/Escape leave it unchanged.
  Preserve highlights uses an Off/On segmented control and is included in presets.
- Desktop-local presets, star rating, zoom relative to Fit. Drag the
  comparison divider directly on the image (mouse or touch); focus it and use
  Left/Right arrows for keyboard adjustments. There is no comparison range slider.
- Small-window layouts, keyboard focus, local preview loading and drag/drop.

The browser demo starts with an original SVG landscape illustration. Importing
your photographs replaces it. The desktop uses your saved library and shows an
import prompt when it is empty.
Open your own JPEG, PNG or WebP for a photographic example. Images are displayed
locally through browser object URLs; there are no uploads, accounts, external fonts
or remote image assets. Browser mode never simulates denoise with a CSS filter.
Real processing requires a RAW opened in the desktop app. Imported documents and
history are session-local in browser mode. The desktop app persists its library and recipes.

## Verification

```bash
npm run build             # TypeScript check + production bundle
npx playwright install chromium webkit
# Linux may also require: npx playwright install-deps chromium webkit
npm test                 # interaction, local image and responsive-layout tests
```

## Remaining validation

Validate the AppImage, model management, batch recovery, and Darktable handoff on
a clean Linux host, including Wayland and high-DPI behavior.

## Licensing

Prototype source, styling and demo illustration: GPL-3.0-or-later, as RawLab.
React/React DOM and Vite: MIT; TypeScript: Apache-2.0; Lucide icons: ISC;
Playwright: Apache-2.0; Prettier: MIT. Dependency notices ship in their packages.
Tauri and its official dialog/filesystem plugins: MIT or Apache-2.0. The desktop
icon is generated from RawLab's existing `packaging/rawlab.png`.
RapidRAW is a visual reference; its AGPL application code and branding are not copied.
