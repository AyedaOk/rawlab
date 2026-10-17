#!/usr/bin/env python3
"""Download the pinned Apache-2.0 Small model and prepare a RawLab catalog upload."""
import argparse
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen

REPO = "onnx-community/depth-anything-v2-small-ONNX"
REVISION = "c3b67641fd837b2368757101311e5d21e511441e"
MODEL_ID = "mask-depth-anything-v2-small"
BASE = f"https://huggingface.co/{REPO}/resolve/{REVISION}/"
CATALOG = "https://huggingface.co/AyedaOk/rawlab-models/resolve/main/catalog.json"


def metadata(path):
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"name": path.name, "size": path.stat().st_size, "sha256": digest}


def download(url, path, expected=None):
    if not path.exists():
        temporary = path.with_name(path.name + ".part")
        try:
            with urlopen(url, timeout=120) as source, temporary.open("wb") as target:
                while chunk := source.read(1024 * 1024):
                    target.write(chunk)
            if expected:
                actual = metadata(temporary)
                if actual["size"] != expected["size"] or actual["sha256"] != expected["oid"]:
                    raise ValueError(f"Download checksum mismatch: {path.name}")
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
    if expected:
        actual = metadata(path)
        if actual["size"] != expected["size"] or actual["sha256"] != expected["oid"]:
            raise ValueError(f"Existing file checksum mismatch: {path}")
    print(f"{'Verified' if expected else 'Ready'} {path}")


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", default="dist/depth-model")
    args = parser.parse_args()
    root = Path(args.output).resolve()
    target = root / "models" / MODEL_ID / "1.0"
    target.mkdir(parents=True, exist_ok=True)
    with urlopen(f"https://huggingface.co/api/models/{REPO}/tree/{REVISION}/onnx", timeout=60) as response:
        files = {entry["path"]: entry for entry in json.load(response)}
    for name in ("model.onnx", "model.onnx_data"):
        download(BASE + "onnx/" + name, target / name, files["onnx/" + name]["lfs"])
    for name in ("config.json", "preprocessor_config.json"):
        download(BASE + name, target / name)
    download(BASE + "README.md", target / "UPSTREAM.md")
    download("https://www.apache.org/licenses/LICENSE-2.0.txt", target / "LICENSE.txt")
    (target / "ATTRIBUTION.txt").write_text(
        "Depth Anything V2 Small\n"
        "Authors: Lihe Yang, Bingyi Kang, Zilong Huang, Zhen Zhao, Xiaogang Xu,\n"
        "Jiashi Feng, and Hengshuang Zhao.\n"
        "Original model: https://huggingface.co/depth-anything/Depth-Anything-V2-Small\n"
        "Project: https://github.com/DepthAnything/Depth-Anything-V2\n"
        "ONNX conversion: ONNX Community / Xenova\n"
        f"Source: https://huggingface.co/{REPO}/tree/{REVISION}\n"
        "License: Apache-2.0 (see LICENSE.txt). Model files are unmodified.\n",
        encoding="utf-8",
    )
    manifest = {"schema": 1, "id": MODEL_ID, "name": "Depth Anything V2 Small",
                "version": "1.0", "task": "depth", "adapter": "depth-anything-v2-v1",
                "available": True, "license": {"spdx": "Apache-2.0", "file": "LICENSE.txt"},
                "source": {"repository": REPO, "revision": REVISION},
                "files": [metadata(target / name) for name in
                          ("model.onnx", "model.onnx_data", "config.json", "preprocessor_config.json",
                           "LICENSE.txt", "UPSTREAM.md", "ATTRIBUTION.txt")]}
    write_json(target / "model.json", manifest)
    info = metadata(target / "model.json")
    entry = {key: manifest[key] for key in ("id", "name", "version", "task", "adapter", "available")}
    entry.update(license="Apache-2.0", manifest=(target / "model.json").relative_to(root).as_posix(),
                 manifestSize=info["size"], manifestSha256=info["sha256"])
    with urlopen(CATALOG, timeout=60) as response:
        catalog = json.load(response)
    catalog["models"] = [item for item in catalog["models"] if item["id"] != MODEL_ID] + [entry]
    write_json(root / "catalog.json", catalog)
    write_json(root / "catalog-entry.json", entry)
    print(f"Ready: {root}\nUpload models/{MODEL_ID}/ and merge catalog-entry.json into the current catalog.")


if __name__ == "__main__":
    main()
