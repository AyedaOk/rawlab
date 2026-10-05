# Build and install on Linux

Run these commands on the Linux host from the repository root. Manual installation
uses `/opt/rawlab` and `/usr/local/bin`; leave `/usr/bin` to the package manager.

## Arch dependencies

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf qt6-base libraw libtiff \
  libpng lensfun webkit2gtk-4.1 libappindicator-gtk3 librsvg openssl nodejs npm rust
```

Use Node.js 20.19 or newer and current stable Rust. Install ONNX Runtime separately:
you need `onnxruntime_c_api.h` and a compatible shared library. Keep the runtime
installed after building; the native installation uses host libraries.

## Release build

```bash
cmake --preset release
cmake --build --preset release --parallel 2
npm --prefix desktop ci
npm --prefix desktop run desktop:build
```

If CMake cannot locate the ONNX headers, configure with your actual include path:

```bash
cmake --preset release -DONNXRuntime_INCLUDE_DIR=/path/to/onnxruntime/include
```

Select an external runtime library in Preferences if the worker cannot discover it.
GPU runtimes also require the matching driver and runtime dependencies.

Check for missing linked libraries:

```bash
ldd desktop/src-tauri/target/release/rawlab-desktop
ldd build/release/src/rawlab_preview/rawlab-preview
```

Resolve any `not found` entries. ONNX Runtime loads at runtime, so `ldd` alone
does not check it. Open Preferences to check its availability.

If a build directory came from a different checkout path, create a fresh build
directory rather than reusing that CMake cache.

## Install

Quit RawLab before replacing its executables. From the repository root:

```bash
sudo install -d /opt/rawlab /usr/local/bin
sudo install -m755 desktop/src-tauri/target/release/rawlab-desktop /opt/rawlab/rawlab-desktop
sudo install -m755 build/release/src/rawlab_preview/rawlab-preview /opt/rawlab/rawlab-preview
sudo install -m644 desktop/src-tauri/icons/128x128.png /opt/rawlab/rawlab.png
sudo ln -sfn /opt/rawlab/rawlab-desktop /usr/local/bin/rawlab
mkdir -p ~/.local/share/applications
```

Save the following as `~/.local/share/applications/rawlab.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=RawLab
Comment=RAW photo preprocessing
Exec=/opt/rawlab/rawlab-desktop
Icon=/opt/rawlab/rawlab.png
Terminal=false
Categories=Graphics;Photography;
StartupNotify=true
```

Start `rawlab` from a terminal or choose RawLab in your application menu. The worker
must remain beside the desktop executable. No development server is needed.

## Update or uninstall

To update, pull the source, repeat the release build, quit the app, and repeat the
installation commands.

To uninstall this manual installation:

```bash
sudo rm /usr/local/bin/rawlab
sudo rm /opt/rawlab/rawlab-desktop /opt/rawlab/rawlab-preview /opt/rawlab/rawlab.png
sudo rmdir /opt/rawlab
rm ~/.local/share/applications/rawlab.desktop
```

These commands leave your user settings, model files, library, and recipes intact.

## Development

After building the release worker:

```bash
npm --prefix desktop run desktop:prepare
npm --prefix desktop run desktop
```

Tauri starts Vite on port 5173. Stop an existing Vite instance if that port is in use.
