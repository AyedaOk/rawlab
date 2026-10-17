# Desktop RAW viewing bridge v1

## Persistent processing mode

`rawlab-preview --serve` accepts newline-delimited JSON requests on stdin and replies
on stdout. It supports previews, detail regions, processing, recomposition,
segmentation, CFA and Linear RGB export, settings, recipes, presets, recoverable
batches, and temporary Darktable batches. This is a private desktop protocol.
Progress objects (`progress`, `total`) may precede the final result. Exceptions
return an `error` object. Tauri owns the worker and a temporary cancellation marker;
the worker checks it between stages and inference tiles. Closing stdin ends the worker.

`ProcessingSession` retains up to two decoded sources within 256 MiB, denoise/sharpen
CFA within 512 MiB, and SAM state. Cache keys include the source fingerprint, model
version, adapter, and execution settings. The worker evicts unused Normal-sharpening
results before model outputs and retains in-use CFA until a later insertion can evict it.
It reads the existing Qt ApplicationSettingsStore. Processing
recipes use the frontend schema (layer blend/strength percentages, normalized mask
marks, embedded SAM mask PNG) and convert through `RecipeAdapter`. Qt and Tauri share
the native recipe, settings, preset, and library stores. Manual mask softening is performed at a 1024-pixel
working size before mapping to CFA. The frontend renders softened overlays in a worker.

`export` additionally accepts a destination path authorized by the native save dialog.
It writes a temporary CFA DNG with embedded JPEG previews and publishes without
replacing an existing file. `export_linear` applies enabled Lensfun corrections after
demosaicing and writes Linear RGB DNG. The Rust side does not permit arbitrary shell commands.
Tauri uses separate serialized workers for background previews, active previews, and
processing. Rust validates filesystem scope before requests reach the worker. Temporary
Darktable batches use explicit destinations and never touch the recoverable queue.

For `batch_create`, Tauri stages the per-photo `recipes` array in its request-scoped
temporary directory and sends `recipesPath` to the worker. The worker validates the
array and copies it into the recovery snapshot before replying; Tauri then removes
the temporary file. Depth maps can therefore exceed 16 MiB across a batch without
exceeding the pipe request limit. Callers cannot supply their own staging path.

`depth` runs the active Depth Anything V2 model on the oriented, unwarped RAW
preview. It returns `key`, `png`, and `modelId`; Tauri adds `sourceVersion` and
rejects a result if the source changed during inference. The worker retains four
encoded depth maps keyed by source, model graph and external-weight fingerprints,
provider, device, and runtime. Cancellation checkpoints surround inference.

Recipe v11 stores each referenced map once in `depthMaps[key] = {png, modelId}`.
A local layer selects it with `depth: {map: key, range: [a,b,c,d]}`. Values are
ordered percentages from near to far: fade-in start, full-strength start,
full-strength end, fade-out end. Equal handles give a hard edge, including the
0/100 endpoints. The PNG packs normalized 16-bit depth into R (high byte) and G
(low byte), with B zero. Decode and bilinearly resample numerical depths before
applying the range; resizing packed PNG colors would corrupt depth values.
The frontend mask worker and native `depthRangeMask` use the same linear ramps.
Apply manual marks after depth selection, then blur and inversion. Depth masks
ignore spatial Feather because its thresholding would replace the selected fades.
Recipes, presets, and batch snapshots retain the map; export needs no depth model.

Processed preview requests include the viewport target size. Scales at or below one half
use the half-size Bayer path before Lensfun for both quality settings; larger High Quality
previews may use AMaZE. The persistent worker caches bounded linear RGB endpoints, so
blend, sharpening-strength, and mask changes can show draft composites from cached
demosaiced previews. Preserve clipped highlights in CFA before creating these endpoints.
Use the same 1024-pixel mask geometry as export. After controls settle, request
`processed_preview` to compose CFA from cached model output and render the reference.
Normal sharpening can generate a missing endpoint from cached denoised CFA.
Interactive recomposition retains the current result ID. Source-detail requests compose
CFA in 256-pixel tiles, then apply Lensfun to the requested region. A 512 MiB RGB cache
holds overview endpoints and detail tiles; panning reuses overlapping tiles. Lensfun
geometry uses a separate four-entry cache. Detail requests accept `quality`:
`high` (default) uses AMaZE with aligned context crops; `fast` uses bilinear demosaicing.
Neither adds display sharpening. Export composes the full CFA image.

The frontend requests visible detail first, original detail when comparison is open,
then the overview. Tauri uses one generation counter for processed previews, detail,
and recomposition. It drops obsolete queued requests and signals active previews through
the cancellation marker. Native code checks between tiles, rendering stages, and PNG
encoding. Explicit processing and export retain their own Cancel action.

## Read-only compatibility mode

`rawlab_preview` is a separate CMake target (`rawlab-preview` executable), not an
addition to the stable `rawlab inspect/process/mask` CLI interface.

Arguments: `INPUT MAX_DIMENSION fast|high`. Dimensions must be 128–4096. The bridge
decodes CFA with LibRawDecoder and renders with RawPreview, using exactly the Qt
application's orientation, white balance, matrix, AMaZE and downsampling code.
It accepts no output path and writes nothing to the input or adjacent directories.

stdout contains one JSON object: `version:1`, `width`, `height`, `quality`,
`png` (base64 PNG) and `metadata` (camera, lens, dimensions, capture exposure, and
Lensfun profile/correction availability).
Absent numerical EXIF fields are null, not zero. Diagnostics go to stderr and
failure returns nonzero. Do not interpret decode failure as a blank successful image.

Tauri validates the input against the filesystem scope granted by native dialogs,
then launches the fixed helper with separate arguments (no shell). Resolve the
helper via `RAWLAB_PREVIEW_ENGINE`. Without that override, development builds prefer
`build/dev/src/rawlab_preview/rawlab-preview`, then `build/preview`, then a sibling
executable; release builds use the sibling. `npm run desktop` rebuilds and stages
the development worker unless `RAWLAB_PREVIEW_ENGINE` specifies a supplied executable.
Build the development worker directly with
`cmake --preset dev` and `cmake --build --preset dev --target rawlab_preview`. Serialized
background jobs limit concurrent decoder memory; eight encoded responses are cached
with a combined 64 MiB limit. Keys include input/engine modification time and size,
preview dimensions and quality. This first step does not retain full CFA buffers
between processes; changing quality can decode again. In-flight decodes finish even
if selection changes, but stale responses are discarded by the frontend.

Tests: `rawlab_preview_bridge_test` checks both quality paths, orientation, PNG
decoding, exposure metadata, argument validation and unchanged input bytes.
Frontend native-bridge tests cover thumbnails, metadata display and quality changes.
Rendering real Bayer fixtures and checking the native UI on the host remain useful
acceptance checks. `rawlab_processing_session_test` uses deterministic cached CFA
fixtures for blend, sharpening, highlight, mask and DNG round-trip regressions. It also accepts a local
RawNIND model path and optional ONNX Runtime library path to verify real inference,
normal sharpening, cached blend changes, local masks, and reuse across photo switches.
`rawlab_preview_regions_test` checks crop offsets, orientation, tile seams, Lensfun
region parity, and cancellation. It accepts the same optional model arguments to
compare masked, corrected detail against a full CFA composition.
`rawlab_depth_mask_test` checks depth ramps, packed PNG precision, persistence, and
oriented CFA blending. Pass a Small ONNX model path, runtime library path, and
optional RAW path to check inference and cache reuse with the downloaded model.
That run also checks runtime probing and provider discovery. To reproduce an SDK/
runtime version mismatch, build with ONNX 1.29 headers and pass a 1.22 runtime library;
RawLab should still request its supported API 20 and generate the depth map.
