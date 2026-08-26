# Release Packaging

## Current Release

The project version is `0.1.0`. The release workflow is
`.github/workflows/release.yml` and runs only when a tag matching `v*.*.*` is
pushed. It builds Linux x86_64, macOS Apple Silicon, and Windows x86_64
artifacts.

Windows is distributed as a portable ZIP containing the executable and DLLs,
because Qt, ONNX Runtime, and vcpkg runtime DLLs are required beside it.

## Dependency Packaging

- Linux uses Ubuntu Qt packages, LibRaw, libtiff, and libpng. Do not restore
  `install-qt-action` to Linux; aqtinstall cannot resolve the Linux `qt_base`
  archive used here.
- Linux downloads CPU ONNX Runtime and copies its shared libraries into the
  AppImage before linuxdeploy scans dependencies.
- macOS uses the Apple Silicon runner and the CPU ONNX Runtime arm64 package.
  macdeployqt receives the ONNX and Homebrew library paths.
- Windows uses Qt MSVC, the MSVC developer environment, vcpkg libraries, and
  the official CPU ONNX Runtime package.

## Packaging Pitfalls

- `scripts/package-linux.sh` must not pass `--plugin-dir`; the bundled
  linuxdeploy version does not support it.
- `packaging/rawlab.desktop` and `packaging/rawlab.png` must be committed.
  The icon needs `git add -f` because the general `*.png` ignore rule applies.
- `CMakePresets.json` must contain the `release` preset before moving the tag.
- The macOS application target must retain `MACOSX_BUNDLE TRUE`.
- Generated `AppDir/`, `dist/`, linuxdeploy binaries, logs, and AppImages are
  ignored and should not be committed.

## Retrying A Release

After fixing a workflow or packaging change, commit and push `main`, then move
the release tag to the new commit:

```bash
git add <changed-files>
git commit -m "Describe the release fix"
git push origin main
git tag -f v0.1.0
git push origin v0.1.0 --force
```

The tag push triggers the workflow. The release job publishes artifacts after
all three platform jobs succeed.
