import { execFileSync, spawn } from "node:child_process";
import { resolve } from "node:path";

const root = resolve(import.meta.dirname, "../..");
const options = { cwd: root, stdio: "inherit" };
if (!process.env.RAWLAB_PREVIEW_ENGINE) {
  execFileSync("cmake", ["--preset", "dev"], options);
  execFileSync("cmake", ["--build", "--preset", "dev", "--target", "rawlab_preview", "--parallel", "2"], options);
}
const engine = process.env.RAWLAB_PREVIEW_ENGINE
  ?? resolve(root, `build/dev/src/rawlab_preview/rawlab-preview${process.platform === "win32" ? ".exe" : ""}`);
execFileSync(process.execPath, [resolve(import.meta.dirname, "prepare-engine.mjs")], {
  ...options, env: { ...process.env, RAWLAB_ENGINE_SOURCE: engine },
});
const vite = spawn(process.execPath, [resolve(root, "desktop/node_modules/vite/bin/vite.js"),
  "--host", "127.0.0.1", "--port", "5173", "--strictPort"], { cwd: resolve(root, "desktop"), stdio: "inherit" });
for (const signal of ["SIGINT", "SIGTERM"]) process.on(signal, () => vite.kill(signal));
vite.on("exit", (code) => process.exit(code ?? 0));
