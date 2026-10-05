# Linux installation

## Run the AppImage

Download the Linux x64 AppImage from [Releases](https://github.com/AyedaOk/rawlab/releases).
In the download directory, use the filename for your version:

```sh
chmod +x RawLab_0.2.1_amd64.AppImage
./RawLab_0.2.1_amd64.AppImage
```

The package includes the native worker and CPU ONNX Runtime. Download model weights
through Preferences. You do not need the build dependencies below to use the AppImage.
The x64 package requires an x86-64 Linux host; it does not run natively on ARM.

If your system reports a missing FUSE library, install your distribution's FUSE 2
compatibility package, or extract and run the AppImage:

```sh
./RawLab_0.2.1_amd64.AppImage --appimage-extract
./squashfs-root/AppRun
```

Keep the extracted directory intact. The Linux release workflow builds on Ubuntu
24.04; older distributions may lack compatible system libraries even with FUSE
installed. Build from source on your host if the package cannot run there.

## Build from source

Run the following build commands from the repository root, as your normal user.
They work in Bash and fish. Use `sudo` only for system packages and installation.

### Dependencies

You need a C++20 compiler, CMake 3.25+, Ninja, pkg-config, Qt 6 Core/Gui/Network,
LibRaw, libtiff, libpng, OpenSSL, and WebKitGTK 4.1 development files. Install Lensfun
for lens corrections. You also need Node.js 22.12+ with npm, current stable
Rust/Cargo, and ONNX Runtime headers and a shared library.

Package names vary by distribution release. The examples below install the native
dependencies; install Node.js and Rust separately if your distribution's versions
are too old. Use the [Node.js installation guide](https://nodejs.org/en/download)
and [rustup](https://rustup.rs/) for current toolchains.

**Ubuntu / Debian**

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config curl \
  qt6-base-dev qt6-base-dev-tools libraw-dev libtiff-dev libpng-dev \
  liblensfun-dev libssl-dev libwebkit2gtk-4.1-dev \
  libayatana-appindicator3-dev librsvg2-dev libxdo-dev
```

Use a release that provides CMake 3.25+ and WebKitGTK 4.1, such as Ubuntu 24.04
or Debian 12 and newer. Ubuntu 22.04's default CMake is too old.

**Fedora**

```sh
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config curl \
  qt6-qtbase-devel LibRaw-devel libtiff-devel libpng-devel lensfun-devel \
  openssl-devel webkit2gtk4.1-devel libappindicator-gtk3-devel \
  librsvg2-devel libXdo-devel
```

**Arch Linux**

```sh
sudo pacman -S --needed base-devel cmake ninja pkgconf curl qt6-base \
  libraw libtiff libpng lensfun openssl webkit2gtk-4.1 \
  libappindicator-gtk3 librsvg xdotool
```

Confirm the build tools are on your PATH:

```sh
cmake --version
node --version
npm --version
rustc --version
cargo --version
```

### ONNX Runtime

Install a distribution package that provides `onnxruntime_c_api.h` and
`libonnxruntime.so`, or extract an official Linux archive from
[ONNX Runtime releases](https://github.com/microsoft/onnxruntime/releases).
Choose the archive for your CPU architecture. RawLab's v0.2.1 Linux packaging uses
CPU ONNX Runtime 1.20.1.

Keep the extracted runtime in a permanent location, for example
`$HOME/.local/opt/onnxruntime`. That directory should contain `include/` and `lib/`.
For this layout, configure with:

```sh
cmake --preset release \
  -DONNXRuntime_INCLUDE_DIR="$HOME/.local/opt/onnxruntime/include"
```

If the headers are already in a system location that CMake discovers, use
`cmake --preset release` instead. RawLab loads the shared library at runtime;
the header path alone does not configure runtime discovery. In Preferences, select
the absolute path to `lib/libonnxruntime.so` from your installation if RawLab does
not find it. Keep that library and its accompanying files installed.

GPU execution requires a compatible GPU-enabled runtime, drivers, and provider
dependencies. Start with the CPU runtime to check the installation.

### Compile

After configuring CMake:

```sh
cmake --build --preset release --parallel 2
npm --prefix desktop ci
npm --prefix desktop run desktop:build
```

The desktop build prepares the optimized worker automatically. If you moved the
checkout or copied a build directory from another machine, configure a fresh build
directory instead of reusing its CMake cache.

## Install the native build

Quit RawLab before replacing its executables. From the repository root:

```sh
sudo install -d /opt/rawlab /usr/local/bin
sudo install -m755 desktop/src-tauri/target/release/rawlab-desktop /opt/rawlab/rawlab-desktop
sudo install -m755 build/release/src/rawlab_preview/rawlab-preview /opt/rawlab/rawlab-preview
sudo install -m644 desktop/src-tauri/icons/128x128.png /opt/rawlab/rawlab.png
sudo ln -sfn /opt/rawlab/rawlab-desktop /usr/local/bin/rawlab
mkdir -p ~/.local/share/applications
```

Keep both executables together. This installation uses your host's shared libraries;
it does not copy the AppImage's bundled dependencies. Leave `/usr/bin` to the
distribution package manager.

Save this text as `~/.local/share/applications/rawlab.desktop`:

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

Launch RawLab from the application menu or run `/usr/local/bin/rawlab`.
No development server is needed. On desktops such as KDE or GNOME, keep your
desktop portal services enabled for native file dialogs.

If startup fails, run it from a terminal and check for missing linked libraries:

```sh
ldd /opt/rawlab/rawlab-desktop
ldd /opt/rawlab/rawlab-preview
```

Resolve any `not found` entries using your package manager. ONNX Runtime loads
dynamically, so check its availability in Preferences as well.

## Update or uninstall

For an AppImage, quit RawLab and replace the downloaded file. If you extracted it,
extract the new version into a separate directory and launch its `AppRun`.

For a native source installation, update your checkout, repeat the configure and
build steps, quit RawLab, and repeat the installation commands.

To remove the native installation described above:

```sh
sudo rm /usr/local/bin/rawlab
sudo rm /opt/rawlab/rawlab-desktop /opt/rawlab/rawlab-preview /opt/rawlab/rawlab.png
sudo rmdir /opt/rawlab
rm ~/.local/share/applications/rawlab.desktop
```

These commands preserve user settings, downloaded models, the library, and recipes.
