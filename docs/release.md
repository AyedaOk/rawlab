# Linux Release Packaging

RawLab's portable Linux package is a Tauri AppImage. It includes the React editor,
the `rawlab-preview` native worker, ONNX Runtime CPU, Qt runtime plugins, Lensfun
profiles, and the Darktable Lua integration.

## GitHub Actions

`.github/workflows/release.yml` runs manually or for tags matching `v*.*.*`.
Every run uploads the AppImage as a workflow artifact; tagged runs also attach it
to a GitHub release.

The workflow builds the worker with the CPU ONNX Runtime package, stages its
runtime dependencies with `scripts/stage-tauri-linux.sh`, and runs
`npm run desktop:bundle:linux` in `ui-prototype/`.

## Local Build

Build the release worker before staging the package:

```bash
cmake --preset release
cmake --build --preset release --parallel 2
ONNXRUNTIME_LIB_DIR=/path/to/onnxruntime/lib bash scripts/stage-tauri-linux.sh
cd ui-prototype
npm ci
npm run desktop:bundle:linux
```

The resulting package is under
`ui-prototype/src-tauri/target/release/bundle/appimage/`.

The staging script requires Lensfun and rejects a worker built without Lensfun
support. Set `RAWLAB_ENGINE_SOURCE` when testing a worker outside `build/release`.

## Release Validation

Before publishing, run the AppImage outside the source checkout and verify:

- startup and CPU processing without `RAWLAB_PREVIEW_ENGINE`
- model discovery and useful missing-model errors
- RAW import, preview, mask editing, and Bayer and Linear RGB DNG exports
- close/reopen recipe restoration and batch recovery
- Darktable handoff using the bundled `packaging/darktable/RawLab.lua`
- Wayland, high-DPI, and narrow-window behavior

## Publishing

After the workflow succeeds on `main`, create and push a version tag:

```bash
git tag v0.1.0
git push origin v0.1.0
```

Do not move a published tag. If a release must be corrected, publish a new patch
version after merging the fix.
