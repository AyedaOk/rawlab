import { chmodSync, copyFileSync, existsSync, mkdirSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { dirname, resolve } from "node:path";

const root = resolve(import.meta.dirname, "../..");
const releaseSource = resolve(root, "build/release/src/rawlab_preview/rawlab-preview");
const previewSource = resolve(root, "build/preview/src/rawlab_preview/rawlab-preview");
const performanceSource = resolve(root, "build/performance/src/rawlab_preview/rawlab-preview");
const debugSource = resolve(root, "build/dev/src/rawlab_preview/rawlab-preview");
const allowDebug = process.env.RAWLAB_ALLOW_DEBUG_ENGINE === "1";
const configuredSource = process.env.RAWLAB_ENGINE_SOURCE
  ? resolve(process.env.RAWLAB_ENGINE_SOURCE) : null;
const source = (configuredSource && existsSync(configuredSource) ? configuredSource : null)
  ?? [releaseSource, previewSource, performanceSource].find(existsSync)
  ?? (allowDebug ? debugSource : null);
if (!source || !existsSync(source)) {
  throw new Error(`Optimized native preview engine not found at ${releaseSource}. Build it with 'cmake --build --preset release' or set RAWLAB_ALLOW_DEBUG_ENGINE=1 explicitly for development.`);
}

const targetTriple = execFileSync("rustc", ["--print", "host-tuple"], {encoding: "utf8"}).trim();
if (!targetTriple) throw new Error("Could not determine the Rust host target");
const extension = process.platform === "win32" ? ".exe" : "";
const target = resolve(import.meta.dirname,
  `../src-tauri/binaries/rawlab-preview-${targetTriple}${extension}`);
mkdirSync(dirname(target), { recursive: true });
copyFileSync(source, target);
if (process.platform !== "win32") chmodSync(target, 0o755);
console.log(`Prepared native preview engine from ${source}`);
