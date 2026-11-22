"""Verify Microsoft's pinned NuGet package and stage its C SDK."""

import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
from zipfile import ZipFile


platform, destination = sys.argv[1:]
libraries = {
    "headers": [],
    "linux-x64": ["libonnxruntime.so", "libonnxruntime_providers_shared.so"],
    "osx-arm64": ["libonnxruntime.dylib"],
    "win-x64": ["onnxruntime.dll", "onnxruntime.lib", "onnxruntime_providers_shared.dll"],
}[platform]
root = Path(destination)
version = "1.20.1"
package = f"microsoft.ml.onnxruntime.{version}.nupkg"
url = f"https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/{version}/{package}"

with TemporaryDirectory() as temporary:
    archive = Path(temporary) / package
    subprocess.run(["curl", "--fail", "--silent", "--show-error", "--location",
                    "--retry", "3", "--output", str(archive), url], check=True)
    digest = hashlib.sha256()
    with archive.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    if digest.hexdigest() != "9c32108e9a1c78b090a95c2a219f244dd5f067d4caf68fb0f967103f3f20839c":
        sys.exit("ONNX Runtime NuGet package checksum mismatch")
    # NuGet does not support signature verification on macOS. Check the same
    # SHA-256 there as on Linux CI, which verifies the signature.
    if sys.platform != "darwin":
        subprocess.run(["dotnet", "nuget", "verify", "--all", str(archive)], check=True)
    with ZipFile(archive) as bundle:
        header_prefix = "build/native/include/"
        files = {
            name: root / "include" / Path(name).name
            for name in bundle.namelist()
            if name.startswith(header_prefix) and name.endswith(".h")
        }
        for name in libraries:
            output = f"{name}.{version}" if name == "libonnxruntime.so" else name
            files[f"runtimes/{platform}/native/{name}"] = root / "lib" / output
        for name, output in files.items():
            output.parent.mkdir(parents=True, exist_ok=True)
            with bundle.open(name) as source, output.open("wb") as target:
                shutil.copyfileobj(source, target)
    if platform == "linux-x64":
        for name in ("libonnxruntime.so", "libonnxruntime.so.1"):
            link = root / "lib" / name
            link.unlink(missing_ok=True)
            link.symlink_to(f"libonnxruntime.so.{version}")
