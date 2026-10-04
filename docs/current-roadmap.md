# RawLab Current Build Roadmap

Updated: 2026-09-20

## Working approach

Build functional, usable features first. Fix defects that lose edits, produce incorrect
output, or block normal use. Leave polish and exhaustive edge-case validation for later
passes.

This sequence complements the historical core and Qt milestones in `milestones.md`.

## 1. Recipe restoration, masks, and bridge contracts

Status: Functional. Detailed validation can continue later.

- Keep recipe and sharpening state per photograph.
- Reject stale restoration responses.
- Preserve mask operations through native and frontend conversions.
- Return authoritative batch status from native operations.

## 2. Reliable persistence

Status: Functional. Detailed validation can continue later.

- Order and coalesce recipe, metadata, and preset writes.
- Retain failed saves for explicit retry.
- Flush current snapshots during navigation and shutdown.
- Distinguish missing, unreadable, invalid, future, and damaged recipes.
- Publish mask assets and recipe JSON atomically.

Later checks include a packaged desktop close/reopen exercise and broader failure
injection on additional platforms.

- macOS packaged-worker bridge test failed recipe round-trip assertions on 2026-10-04:
  restored local-layer points and category strokes were empty. Investigate test isolation
  and persistence behavior (Actions run 37239571508). DNG export checks passed before this failure.

## 3. Preview performance and worker scheduling

Status: Functional. Detailed validation can continue later.

- Reuse processed CFA and derived previews.
- Avoid repeated RAW decoding and model inference.
- Prioritize the active photograph over background thumbnails.
- Reject superseded preview responses.
- Bound preview and processing cache memory.
- Avoid repeatedly transferring identical large previews where practical.
- Provide genuine source-resolution detail inspection rather than only scaling a
  reduced preview.

Functional gate: rapid switching, quality changes, and editing remain responsive; the
latest request owns the visible preview; repeated rendering does not rerun inference or
grow memory without a bound; and 1:1 displays source-resolution pixels.

## 4. Batch recovery and queue UI

Status: Functional. Detailed validation can continue later.

- Expose native queue state in the desktop frontend.
- Show per-job progress, completion, cancellation, and errors.
- Resume unfinished queues after restart.
- Retry failed jobs.
- Preserve the exact recipe and model/settings snapshot.
- Protect existing output files.
- Validate recovery around interrupted output publication.

Functional gate: interrupt a multi-photo batch, reopen RawLab, and finish the remaining
jobs without losing settings or overwriting outputs.

## 5. Real ONNX models and provider validation

Status: Functional with follow-up defects recorded below.

- Resolve the reported ONNX schema-registration conflict.
- Validate actual denoise, sharpening, and SAM models.
- Establish a dependable CPU baseline.
- Test optional acceleration on available hardware.
- Check model contracts, tiled seams, memory use, and cancellation.
- Record model sources and licenses.
- On the tested Arch host, CUDA inference works when the desktop process starts with
  `LD_PRELOAD=/usr/lib/libcudnn.so.9`; keep this as a host-launch workaround while
  runtime dependency loading is investigated.
- Interactive blend recomposition still performs full-resolution CFA work and does not
  yet respond continuously like a display-pipeline exposure control. Add a display-space
  interaction path while dragging, followed by authoritative CFA composition on release.
- Sharpening requires a correctness pass. A reported 100% setting appears softer in the
  desktop preview; compare Normal and DeepSharpen CFA output, rendered preview, and DNG
  output with real models before treating sharpening as validated.

Functional gate: representative RAW files process and export successfully with real
models, with useful errors for unavailable models and providers.

## 6. Darktable workflow and metadata interoperability

Status: Implemented in Qt and Tauri; packaged-host validation remains.

- Port the existing Qt external-edit and external-batch protocols to Tauri.
- Integrate and package the matching Darktable Lua script.
- Validate single-image and batch handoff.
- Support agreed XMP rating and color-label interoperability.
- Define which application owns each metadata field.
- Keep original RAW files read-only.

Metadata ownership for protocol v1: Darktable owns ratings, rejection state, color
labels, and tags. The Lua integration copies those fields to a newly imported DNG and
Darktable writes any sidecar according to its own preferences. RawLab owns processing
recipes and never modifies the source RAW or source XMP. Protocol v1 does not transport
metadata into RawLab or embed source XMP in exported DNG files.

Functional gate: send photographs from Darktable to RawLab, process them, and return
usable outputs with expected metadata and no duplicate imports.

Automated coverage validates Tauri single-image routing, explicit-destination batch
publication, protocol receipts, and no-replace behavior. The remaining gate is a real
Darktable round trip using the packaged desktop executable and installed Lua script.

## 7. DNG compatibility and output size

Status: Bayer CFA and corrected Linear RGB output were validated on the Arch host.

- Validate Bayer CFA DNG in independent readers.
- Validate Linear RGB DNG in Darktable and another reader.
- Check orientation, white balance, color, and active-area metadata.
- Evaluate lossless compression and tiled output.
- Consider active-area-only export only after compatibility testing.

Functional gate: exported files open correctly in independent applications and preserve
the intended geometry and appearance. Bayer CFA DNG remains the baseline export path.

## 8. Lens corrections and chromatic aberration

Status: Initial implementation complete; calibrated-lens and mask-geometry host validation
remains.

- Sharpening, profile-based lateral chromatic-aberration correction, distortion correction,
  vignetting correction, and automatic scaling are restricted to Linear RGB output.
- Bayer CFA processing and export bypass sharpening and optical corrections while retaining
  their saved settings.
- Lensfun is optional at build time. The editor reports whether its database contains an
  unambiguous profile and enables only corrections supported by that profile.
- Linear previews, single exports, recoverable batches, and Darktable batches carry the
  selected output mode and optical recipe.
- Preview-sized linear RGB endpoints for the original, denoised, and fully sharpened
  results are cached with a 256 MiB bound. Blend and sharpening strength are composed
  from those endpoints, so repeated adjustments do not rerun inference or demosaicing.
- Preview scales at or below one half use the half-size Bayer path even in High Quality,
  matching Darktable's preview policy. Larger previews may use AMaZE; Linear RGB export
  always keeps full-resolution AMaZE.
- Optics toggles refresh the current processed result without rerunning model inference.
  Lensfun profile matches and uncorrected RGB endpoints are reused, and corrected output
  is cropped and rescaled to the original dimensions instead of retaining black borders.
  Changing output mode still requires Update because it changes sharpening eligibility.
- Linear RGB DNG output embeds a JPEG thumbnail and display preview using the same corrected
  display rendering as the exported recipe.
- Before/after and 1:1 rendering use the same optical geometry. Mask editing uses the
  unwarped cached processed image so points and paint remain in original coordinates;
  the corrected view resumes when editing ends.
- Validate preview, 1:1, comparison, point, paint, and mask-overlay geometry with a real
  calibrated wide-angle image before marking this step functional.

Functional gate: supported camera and lens combinations produce predictable corrections
without misaligned masks or broken CFA output.

## 9. Linux packaging and release validation

- The Tauri AppImage configuration, sidecar staging script, and GitHub Actions workflow
  are implemented. The remaining work is clean-host and hardware validation.
- Test installation on a clean host.
- Bundle the Release worker and required resources.
- Validate model discovery and actionable startup errors.
- Exercise actual close/reopen, batch recovery, and Darktable handoff.
- Test Wayland, high-DPI displays, and small-screen layouts.
- Keep AppImage as the primary portable Linux package.

Functional gate: a user can install, import, edit, process, export, close, and reopen
without a development checkout.

## Later usability improvements

- Preset rename.
- Portable preset import/export with masks.
- Model compatibility checks for imported presets.
- Optional live Darktable completion imports.
- Further performance tuning and UI polish.

## Related documents

- `milestones.md`: historical core and Qt implementation milestones.
- [`darktable-batch-protocol.md`](darktable-batch-protocol.md): Darktable handoff contract.
- [`desktop-preview-bridge.md`](desktop-preview-bridge.md): private Tauri/native bridge.
- [`release.md`](release.md): release packaging instructions.
