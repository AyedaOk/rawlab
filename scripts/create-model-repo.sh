#!/usr/bin/env bash

set -euo pipefail

DEFAULT_MODEL_SOURCE="${MODEL_SOURCE_DIR:-}"
DEFAULT_SAM_SOURCE="$HOME/.local/share/darktable/models/mask-object-sam21-large"
DEFAULT_NIND_SOURCE="$HOME/.local/share/darktable/models/rawdenoise-nind"

MODEL_SOURCE_DIR="${1:-${DEFAULT_MODEL_SOURCE:?Pass the model source directory as the first argument or set MODEL_SOURCE_DIR}}"
OUTPUT_DIR="${2:-$(dirname "$MODEL_SOURCE_DIR")/rawlab-model}"
SAM_SOURCE_DIR="${3:-$DEFAULT_SAM_SOURCE}"
NIND_SOURCE_DIR="${NIND_SOURCE_DIR:-$DEFAULT_NIND_SOURCE}"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
GPL_LICENSE_FILE="$REPO_ROOT/LICENSE"
APACHE_LICENSE_FILE="${APACHE_LICENSE_FILE:-/usr/share/licenses/spdx/Apache-2.0.txt}"
HF_REPO_ID="${HF_REPO_ID:-YOUR_NAME/rawlab-model}"

if [[ ! -d "$MODEL_SOURCE_DIR" ]]; then
  printf 'Model source directory does not exist: %s\n' "$MODEL_SOURCE_DIR" >&2
  exit 1
fi

if [[ ! -d "$SAM_SOURCE_DIR" ]]; then
  printf 'SAM source directory does not exist: %s\n' "$SAM_SOURCE_DIR" >&2
  exit 1
fi

for file in config.json encoder.onnx decoder.onnx; do
  if [[ ! -f "$SAM_SOURCE_DIR/$file" ]]; then
    printf 'SAM source is missing %s: %s\n' "$file" "$SAM_SOURCE_DIR" >&2
    exit 1
  fi
done

for file in config.json model_bayer.onnx; do
  if [[ ! -f "$NIND_SOURCE_DIR/$file" ]]; then
    printf 'RawNIND source is missing %s: %s\n' "$file" "$NIND_SOURCE_DIR" >&2
    exit 1
  fi
done

if [[ ! -f "$GPL_LICENSE_FILE" ]]; then
  printf 'RawLab GPL license file does not exist: %s\n' "$GPL_LICENSE_FILE" >&2
  exit 1
fi

if [[ ! -f "$APACHE_LICENSE_FILE" ]]; then
  printf 'Apache-2.0 license text does not exist: %s\n' "$APACHE_LICENSE_FILE" >&2
  printf 'Set APACHE_LICENSE_FILE to a complete Apache License 2.0 text.\n' >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  printf 'python3 is required to build the model repository.\n' >&2
  exit 1
fi

if [[ -e "$OUTPUT_DIR" ]]; then
  printf 'Output already exists; refusing to overwrite it: %s\n' "$OUTPUT_DIR" >&2
  exit 1
fi

OUTPUT_PARENT="$(dirname "$OUTPUT_DIR")"
mkdir -p "$OUTPUT_PARENT"
STAGING_DIR="$(mktemp -d "$OUTPUT_PARENT/.rawlab-model.XXXXXX")"

cleanup() {
  rm -rf -- "$STAGING_DIR"
}
trap cleanup EXIT

export MODEL_SOURCE_DIR SAM_SOURCE_DIR NIND_SOURCE_DIR GPL_LICENSE_FILE APACHE_LICENSE_FILE HF_REPO_ID
export REPOSITORY_ROOT="$STAGING_DIR/repository"

python3 <<'PY'
from __future__ import annotations

import hashlib
import json
import os
import shutil
import stat
import zipfile
from pathlib import Path, PurePosixPath


MODEL_SOURCE = Path(os.environ["MODEL_SOURCE_DIR"])
SAM_SOURCE = Path(os.environ["SAM_SOURCE_DIR"])
NIND_SOURCE = Path(os.environ["NIND_SOURCE_DIR"])
GPL_LICENSE_FILE = Path(os.environ["GPL_LICENSE_FILE"])
APACHE_LICENSE_FILE = Path(os.environ["APACHE_LICENSE_FILE"])
REPOSITORY_ROOT = Path(os.environ["REPOSITORY_ROOT"])
HF_REPO_ID = os.environ["HF_REPO_ID"]
COPY_MODE = os.environ.get("RAWLAB_MODEL_COPY_MODE", "copy")

if COPY_MODE not in {"copy", "hardlink"}:
    raise SystemExit("RAWLAB_MODEL_COPY_MODE must be 'copy' or 'hardlink'")


APACHE_2_LICENSE = APACHE_LICENSE_FILE.read_text(encoding="utf-8")

MIT_LICENSE = """MIT License

Copyright (c) 2025 rymuelle

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""


RAWFORGE_MODELS = (
    {
        "archive": "rawdenoise-rawforge-shadowweightedl1.dtmodel",
        "id": "rawdenoise-rawforge-shadowweightedl1",
        "name": "TreeNet Denoise",
        "description": "Standard RawForge TreeNet denoise model.",
        "task": "denoise",
    },
    {
        "archive": "rawdenoise-rawforge-shadowweightedl1-light.dtmodel",
        "id": "rawdenoise-rawforge-shadowweightedl1-light",
        "name": "TreeNet Denoise Light",
        "description": "Lightweight RawForge TreeNet denoise model.",
        "task": "denoise",
    },
    {
        "archive": "rawdenoise-rawforge-shadowweightedl1-super-light.dtmodel",
        "id": "rawdenoise-rawforge-shadowweightedl1-super-light",
        "name": "TreeNet Denoise Super Light",
        "description": "Smallest RawForge TreeNet denoise model.",
        "task": "denoise",
    },
    {
        "archive": "rawdenoise-rawforge-shadowweightedl1-heavy.dtmodel",
        "id": "rawdenoise-rawforge-shadowweightedl1-heavy",
        "name": "TreeNet Denoise Heavy",
        "description": "Heavy RawForge TreeNet denoise model.",
        "task": "denoise",
    },
    {
        "archive": "rawdenoise-rawforge-deblur-deep-24.dtmodel",
        "id": "rawdenoise-rawforge-deblur-deep-24",
        "name": "DeepSharpen",
        "description": "RawForge DeepSharpen restoration model.",
        "task": "sharpen",
    },
)


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content.rstrip() + "\n", encoding="utf-8")


def write_json(path: Path, content: object) -> None:
    write_text(path, json.dumps(content, indent=2, sort_keys=False))


def file_metadata(path: Path) -> dict[str, object]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
            size += len(chunk)
    return {"name": path.name, "size": size, "sha256": digest.hexdigest()}


def copy_large_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if COPY_MODE == "hardlink":
        os.link(source, destination)
    else:
        shutil.copyfile(source, destination)


def validate_archive_member(info: zipfile.ZipInfo, expected_root: str) -> None:
    name = info.filename
    if "\\" in name or "\x00" in name:
        raise ValueError(f"unsafe archive path: {name!r}")
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ValueError(f"unsafe archive path: {name!r}")
    if path.parts[0] != expected_root:
        raise ValueError(f"unexpected archive root in {name!r}")
    mode = info.external_attr >> 16
    kind = stat.S_IFMT(mode)
    if kind not in {0, stat.S_IFREG, stat.S_IFDIR}:
        raise ValueError(f"unsupported archive entry type: {name!r}")


def extract_rawforge_model(spec: dict[str, str]) -> dict[str, object]:
    archive_path = MODEL_SOURCE / spec["archive"]
    if not archive_path.is_file():
        raise FileNotFoundError(f"required model package is missing: {archive_path}")

    expected_root = spec["id"]
    expected_files = {
        f"{expected_root}/config.json",
        f"{expected_root}/model.onnx",
    }
    with zipfile.ZipFile(archive_path) as archive:
        file_members: dict[str, zipfile.ZipInfo] = {}
        for info in archive.infolist():
            validate_archive_member(info, expected_root)
            if not info.is_dir():
                file_members[info.filename] = info
        if set(file_members) != expected_files:
            missing = sorted(expected_files - set(file_members))
            unexpected = sorted(set(file_members) - expected_files)
            raise ValueError(
                f"unexpected contents in {archive_path.name}; "
                f"missing={missing}, unexpected={unexpected}"
            )

        config_bytes = archive.read(file_members[f"{expected_root}/config.json"])
        upstream_config = json.loads(config_bytes)
        if upstream_config.get("id") != expected_root:
            raise ValueError(f"model id mismatch in {archive_path.name}")
        if upstream_config.get("version") != "1.0":
            raise ValueError(f"unsupported model version in {archive_path.name}")

        target = REPOSITORY_ROOT / "models" / expected_root / "1.0"
        target.mkdir(parents=True)
        with archive.open(file_members[f"{expected_root}/model.onnx"]) as source:
            with (target / "model.onnx").open("wb") as destination:
                shutil.copyfileobj(source, destination, length=1024 * 1024)

    model_file = file_metadata(target / "model.onnx")
    conditioning_normalization = (
        "iso / 6400"
        if spec["task"] == "sharpen"
        else "min(iso, 65535) / 6400"
    )
    model_manifest = {
        "schema": 1,
        "id": expected_root,
        "name": spec["name"],
        "description": spec["description"],
        "version": "1.0",
        "task": spec["task"],
        "adapter": "rawforge-rgb-v1",
        "available": True,
        "license": {
            "spdx": "MIT",
            "file": "licenses/MIT.txt",
            "source": "https://github.com/rymuelle/RawForge",
            "status": "publisher-declared",
        },
        "source": {
            "project": "RawForge",
            "url": "https://github.com/rymuelle/RawForge",
            "package": spec["archive"],
        },
        "input": {
            "kind": "linear-rec2020-rgb",
            "dtype": "float16",
            "layout": "nchw",
            "inputName": "input",
            "shape": ["batch", 3, "height", "width"],
            "tileSize": 256,
            "tileOverlap": 64,
            "conditioning": {
                "kind": "iso-v1",
                "inputName": "cond",
                "dtype": "float16",
                "shape": ["batch", 1],
                "normalization": conditioning_normalization,
            },
        },
        "output": {
            "kind": "linear-rec2020-rgb",
            "dtype": "float16",
            "layout": "nchw",
            "outputName": "output",
            "shape": ["batch", 3, "height", "width"],
        },
        "files": [model_file],
    }
    write_json(target / "model.json", model_manifest)
    return catalog_entry(model_manifest, target)


def build_sam_model() -> dict[str, object]:
    upstream_config = json.loads((SAM_SOURCE / "config.json").read_text(encoding="utf-8"))
    if upstream_config.get("id") != "mask-object-sam21-large":
        raise ValueError("installed SAM config has an unexpected model id")
    if upstream_config.get("version") != "1.0":
        raise ValueError("installed SAM config has an unsupported version")

    target = REPOSITORY_ROOT / "models" / "mask-object-sam21-large" / "1.0"
    target.mkdir(parents=True)
    copy_large_file(SAM_SOURCE / "encoder.onnx", target / "encoder.onnx")
    copy_large_file(SAM_SOURCE / "decoder.onnx", target / "decoder.onnx")

    manifest = {
        "schema": 1,
        "id": "mask-object-sam21-large",
        "name": "SAM 2.1 Large",
        "description": "SAM 2.1 Hiera Large interactive object segmentation.",
        "version": "1.0",
        "task": "mask",
        "adapter": "sam21-split-v1",
        "available": True,
        "license": {
            "spdx": "Apache-2.0",
            "file": "licenses/Apache-2.0.txt",
            "source": "https://github.com/facebookresearch/sam2",
            "status": "upstream-declared",
        },
        "source": {
            "project": "Segment Anything Model 2.1",
            "url": "https://github.com/facebookresearch/sam2",
            "distribution": "https://huggingface.co/AyedaOk/darktable-sam21-large",
            "trainingData": ["SA-V", "SA-1B"],
            "trainingDataTerms": "SA-V is CC BY 4.0; SA-1B has separate Meta terms",
        },
        "input": {
            "kind": "srgb-image",
            "dtype": "float32",
            "layout": "nchw",
            "imageSize": 1024,
        },
        "output": {"kind": "interactive-mask", "dtype": "float32"},
        "files": [
            file_metadata(target / "encoder.onnx"),
            file_metadata(target / "decoder.onnx"),
        ],
    }
    write_json(target / "model.json", manifest)
    return catalog_entry(manifest, target)


def build_rawnind_model() -> dict[str, object]:
    target = REPOSITORY_ROOT / "models" / "rawdenoise-nind" / "1.0"
    target.mkdir(parents=True)
    manifest = {
        "schema": 1,
        "id": "rawdenoise-nind",
        "name": "RawNIND Bayer",
        "description": "RawNIND Bayer UtNet2 denoiser.",
        "version": "1.0",
        "task": "denoise",
        "adapter": "rawnind-bayer-v1",
        "available": True,
        "license": {
            "spdx": "GPL-3.0",
            "file": "licenses/GPL-3.0.txt",
            "source": "https://github.com/trougnouf/rawnind_jddc",
            "status": "upstream-declared",
        },
        "source": {
            "project": "RawNIND",
            "url": "https://github.com/trougnouf/rawnind_jddc",
            "paper": "https://arxiv.org/abs/2501.08924",
            "author": "Benoit Brummer (UCLouvain)",
            "trainingData": "Raw Natural Image Noise Dataset; real-world raw noise/clean pairs",
            "trainingDataLicense": "CC BY 4.0 / CC0 per image",
        },
        "input": {
            "kind": "packed-bayer",
            "dtype": "float32",
            "layout": "nchw",
            "tileSize": 512,
            "edgePad": "mirror-cropped",
            "whiteBalance": "none",
        },
        "output": {"kind": "camera-rgb", "dtype": "float32", "scale": 2,
                   "shape": ["batch", 3, 1024, 1024]},
        "files": [],
    }
    copy_large_file(NIND_SOURCE / "model_bayer.onnx", target / "model_bayer.onnx")
    manifest["files"] = [file_metadata(target / "model_bayer.onnx")]
    write_json(target / "model.json", manifest)
    return catalog_entry(manifest, target)


def catalog_entry(manifest: dict[str, object], target: Path) -> dict[str, object]:
    manifest_file = file_metadata(target / "model.json")
    relative_manifest = (target / "model.json").relative_to(REPOSITORY_ROOT).as_posix()
    return {
        "id": manifest["id"],
        "name": manifest["name"],
        "version": manifest["version"],
        "task": manifest["task"],
        "adapter": manifest["adapter"],
        "available": manifest["available"],
        "license": manifest["license"]["spdx"],
        "manifest": relative_manifest,
        "manifestSize": manifest_file["size"],
        "manifestSha256": manifest_file["sha256"],
    }


def write_repository_documents() -> None:
    licenses = REPOSITORY_ROOT / "licenses"
    write_text(licenses / "Apache-2.0.txt", APACHE_2_LICENSE)
    write_text(licenses / "MIT.txt", MIT_LICENSE)
    shutil.copyfile(GPL_LICENSE_FILE, licenses / "GPL-3.0.txt")

    license_link = f"https://huggingface.co/{HF_REPO_ID}/blob/main/README.md#licenses"
    write_text(
        REPOSITORY_ROOT / "README.md",
        f"""---
license: other
license_name: multiple-model-licenses
license_link: {license_link}
library_name: onnx
pipeline_tag: image-to-image
tags:
  - rawlab
  - raw-processing
  - image-restoration
---

# RawLab Models

Supported ONNX models for RawLab. RawLab reads `catalog.json`, then downloads
the files declared by each versioned `model.json` manifest.

| Model | Task | Adapter | Available | License |
|---|---|---|---|---|
| SAM 2.1 Large | Mask segmentation | `sam21-split-v1` | Yes | Apache-2.0 |
| RawNIND Bayer | Raw denoise | `rawnind-bayer-v1` | Yes | GPL-3.0 |
| TreeNet Denoise | Denoise | `rawforge-rgb-v1` | Yes | MIT |
| TreeNet Denoise Light | Denoise | `rawforge-rgb-v1` | Yes | MIT |
| TreeNet Denoise Super Light | Denoise | `rawforge-rgb-v1` | Yes | MIT |
| TreeNet Denoise Heavy | Denoise | `rawforge-rgb-v1` | Yes | MIT |
| DeepSharpen | Sharpen | `rawforge-rgb-v1` | Yes | MIT |

## Sources and Attribution

- SAM 2.1 is developed by Meta: https://github.com/facebookresearch/sam2
- RawForge and RawRefinery are developed by rymuelle: https://github.com/rymuelle/RawForge
- RawNIND is developed by Benoit Brummer and Christophe De Vleeschouwer: https://github.com/trougnouf/rawnind_jddc

No endorsement by these projects is implied. Preserve the source and license
metadata from each `model.json` when redistributing an artifact.

SAM 2.1 was trained on SA-V and SA-1B. SA-V is CC BY 4.0; SA-1B has separate
Meta terms. The RawForge license is publisher-declared in the source model
repository, but detailed training-data provenance is not included in the
redistributed packages. RawNIND uses the Bayer artifact from the installed
Darktable model package and is available for download.

## Licenses

This is a mixed-license collection; no single license applies to every model.
Complete license texts are provided in `licenses/`:

- `licenses/Apache-2.0.txt` for SAM 2.1 Large
- `licenses/GPL-3.0.txt` for the RawNIND placeholder
- `licenses/MIT.txt` for the RawForge models
""",
    )


REPOSITORY_ROOT.mkdir(parents=True)
write_repository_documents()

catalog_models = [build_sam_model()]
catalog_models.append(build_rawnind_model())
for rawforge_spec in RAWFORGE_MODELS:
    catalog_models.append(extract_rawforge_model(rawforge_spec))

catalog_models.sort(key=lambda model: str(model["id"]))
write_json(
    REPOSITORY_ROOT / "catalog.json",
    {
        "schema": 1,
        "repository": HF_REPO_ID,
        "models": catalog_models,
    },
)
PY

mv -- "$REPOSITORY_ROOT" "$OUTPUT_DIR"
rmdir -- "$STAGING_DIR"
trap - EXIT

printf 'Created RawLab model repository: %s\n' "$OUTPUT_DIR"
if [[ "$HF_REPO_ID" == YOUR_NAME/* ]]; then
  printf 'Warning: set HF_REPO_ID before rebuilding to generate the final Hugging Face license URL.\n' >&2
fi
