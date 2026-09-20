# Release Packaging

RawLab ships three desktop packages, each produced by its own workflow:

- Linux AppImage (`.github/workflows/release.yml`): React editor, `rawlab-preview`
  worker, ONNX Runtime CPU, Qt runtime plugins, Lensfun profiles, and the
  Darktable Lua integration.
- macOS DMG (`.github/workflows/release-macos.yml`, Apple Silicon): worker built
  against Homebrew Qt/LibRaw/LibTIFF/LibPNG/Lensfun; `dylibbundler` rewrites and
  collects every non-system dylib next to the worker, and the Lensfun database is
  bundled into `Contents/Resources/lensfun`.
- Windows NSIS installer (`.github/workflows/release-windows.yml`, x64): worker
  built against Qt (aqtinstall) and vcpkg LibRaw/LibTIFF/LibPNG; `windeployqt`
  and the vcpkg runtime DLLs are staged next to the worker. Lensfun is not
  available on Windows, so lens and chromatic-aberration corrections are greyed
  out in that build.

All three workflows run manually or for tags matching `v*.*.*`. Every run uploads
an artifact; tagged runs also attach the package to a GitHub release.

## macOS signing

The macOS build is **ad-hoc signed** (`bundle.macOS.signingIdentity: "-"`).
Ad-hoc signing is mandatory on Apple Silicon: without a valid signature macOS
reports the app as "damaged" and refuses to launch it. Ad-hoc signing gives every
executable and dylib a valid local signature, so the "damaged" error is gone.

Ad-hoc signing is not notarization, so on first launch macOS shows an
"unidentified developer" warning. Users open it once via **right-click → Open**
(or System Settings → Privacy & Security → "Open Anyway"). To strip the download
quarantine flag instead:

```bash
xattr -dr com.apple.quarantine /Applications/RawLab.app
```

A clean, warning-free build requires an Apple Developer account plus
notarization (`APPLE_SIGNING_IDENTITY`, `APPLE_CERTIFICATE`, `APPLE_ID`, …); that
is future work.

## GitHub Actions

`.github/workflows/release.yml` builds the worker with the CPU ONNX Runtime
package, stages its runtime dependencies with `scripts/stage-tauri-linux.sh`, and
runs `npm run desktop:bundle:linux` in `desktop/`.

## Local Build

Build the release worker before staging the package:

```bash
cmake --preset release
cmake --build --preset release --parallel 2
ONNXRUNTIME_LIB_DIR=/path/to/onnxruntime/lib bash scripts/stage-tauri-linux.sh
cd desktop
npm ci
npm run desktop:bundle:linux
```

The resulting package is under
`desktop/src-tauri/target/release/bundle/appimage/`.

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
