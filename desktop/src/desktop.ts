import { isTauri, invoke } from "@tauri-apps/api/core";

export type RawFile = { path: string; name: string; identity: string };
export type RawPreview = {
  before?: string;
  output?: string;
  version: number;
  png: string;
  width: number;
  height: number;
  sourceWidth?: number;
  sourceHeight?: number;
  sourceVersion?: string;
  resultId?: string;
  quality: string;
  metadata: {
    make: string;
    model: string;
    lens: string;
    iso: number | null;
    aperture: number | null;
    exposureSeconds: number | null;
    exposureBiasEv: number | null;
    focalLengthMm: number | null;
    rawWidth: number;
    rawHeight: number;
    optics?: {
      lensfunAvailable: boolean;
      profileMatched: boolean;
      chromaticAberrationAvailable: boolean;
      lensCorrectionAvailable: boolean;
      message: string;
    };
  };
};
export const renderRaw = (path: string, highQuality: boolean, size = 4096, background = false) =>
  invoke<RawPreview>("render_raw_preview", { path, highQuality, size, background });
export const renderProcessed = (path: string, highQuality: boolean, resultId: string, recipe: unknown, size = 4096) =>
  invoke<RawPreview>("render_processed", { path, highQuality, resultId, recipe, size });
export type RawDetail = {version:number;x:number;y:number;width:number;height:number;sourceWidth:number;sourceHeight:number;sourceVersion?:string;png:string};
export const renderRawDetail = (path:string,target:'original'|'processed',resultId:string|undefined,x:number,y:number,width:number,height:number,recipe:unknown) =>
  invoke<RawDetail>("render_raw_detail",{path,target,resultId,x,y,width,height,recipe});
export const rawSourceVersion = (path:string) => invoke<string>("raw_source_version",{path});
const rawExtensions = [
  "arw",
  "cr2",
  "cr3",
  "nef",
  "nrw",
  "dng",
  "orf",
  "rw2",
  "pef",
  "raf",
  "srw",
  "raw",
];

export const isDesktop = () => isTauri();
export const control = <T = Record<string, unknown>>(request: Record<string, unknown>) => invoke<T>('desktop_control', {request});
export type StoredLayer = Record<string, unknown> & {
  id: number;
  name?: string;
  marks?: unknown[];
  points?: unknown[];
};
export type StoredRecipe = { layers: StoredLayer[]; highlights?: boolean; sharpenModelId?: string; chromaticAberration?: boolean; lensCorrection?: boolean };
export type RecipeLoadOutcome = 'loaded' | 'missing' | 'unreadable' | 'invalid' | 'future-version' | 'missing-mask';
const readStoredRecipe = (value: unknown): StoredRecipe | null => {
  if (value === null) return null;
  if (!value || typeof value !== 'object' || !Array.isArray((value as StoredRecipe).layers))
    throw new Error('Invalid stored recipe');
  const recipe = value as StoredRecipe;
  if (!recipe.layers.every((layer) => layer && typeof layer === 'object' && Number.isInteger(layer.id)))
    throw new Error('Invalid stored recipe layer');
  return recipe;
};
export const listLibrary = () => control<{ paths: string[] }>({ operation: 'library_list' });
export const addLibraryPaths = (paths: string[]) => control<{ paths: string[] }>({ operation: 'library_add', paths });
export const removeLibraryPaths = (paths: string[]) => control<{ paths: string[] }>({ operation: 'library_remove', paths });
export const loadRecipe = async (path: string, width = 1024, height = 1024) => {
  const response = await control<{ recipe: unknown; outcome?: RecipeLoadOutcome; error?: string }>({ operation: 'recipe_load', path, width, height });
  const outcome = response.outcome ?? (response.recipe === null ? 'missing' : 'loaded');
  if (outcome !== 'loaded' && outcome !== 'missing') throw new Error(response.error || `Could not load recipe: ${outcome}`);
  return { recipe: readStoredRecipe(response.recipe), outcome };
};
export const saveRecipe = (path: string, recipe: StoredRecipe, width = 1024, height = 1024) =>
  control({ operation: 'recipe_save', path, recipe, width, height });
export const listPresets = () => control<{ names: string[] }>({ operation: 'preset_list' });
export const loadPreset = async (name: string, width = 1024, height = 1024) => {
  const response = await control<{ recipe: unknown; outcome?: RecipeLoadOutcome; error?: string }>({ operation: 'preset_load', name, width, height });
  const outcome = response.outcome ?? (response.recipe === null ? 'missing' : 'loaded');
  if (outcome !== 'loaded' && outcome !== 'missing') throw new Error(response.error || `Could not load preset: ${outcome}`);
  return { recipe: readStoredRecipe(response.recipe), outcome };
};
export const savePreset = (name: string, recipe: StoredRecipe, width = 1024, height = 1024) =>
  control({ operation: 'preset_save', name, recipe, width, height });
export const deletePreset = (name: string) => control({ operation: 'preset_delete', name });
export type LibraryMetadata = { rating: number; labels: string[] };
export const libraryMetadata = (path: string) => control<LibraryMetadata>({ operation: 'library_metadata', path });
export const saveLibraryMetadata = (path: string, rating: number, labels: string[]) =>
  control<LibraryMetadata>({ operation: 'library_metadata_save', path, rating, labels });
export type BatchJob = { source: string; output: string; state: number; message: string; completed: boolean };
export type BatchStatus = { snapshot: string | null; jobs: BatchJob[]; completed: number; remaining: number };
function readBatchStatus(value: unknown): BatchStatus {
  if (!value || typeof value !== 'object') throw new Error('Invalid batch status response');
  const status = value as Partial<BatchStatus>;
  const validCount = (count: unknown) => Number.isInteger(count) && (count as number) >= 0;
  const validJob = (job: unknown): job is BatchJob => {
    if (!job || typeof job !== 'object') return false;
    const candidate = job as Partial<BatchJob>;
    return typeof candidate.source === 'string' && typeof candidate.output === 'string'
      && Number.isInteger(candidate.state) && candidate.state! >= 0 && candidate.state! <= 4
      && typeof candidate.message === 'string' && typeof candidate.completed === 'boolean';
  };
  if (!Array.isArray(status.jobs) || !status.jobs.every(validJob)
    || !validCount(status.completed) || !validCount(status.remaining)
    || !(status.snapshot === null || typeof status.snapshot === 'string'))
    throw new Error('Invalid batch status response');
  return status as BatchStatus;
}
const batchControl = (request: Record<string, unknown>) => control(request).then(readBatchStatus);
export const createBatch = (sources: string[], outputDirectory: string, recipe: unknown, recipes: unknown[] = [], width = 1024, height = 1024) =>
  batchControl({ operation: 'batch_create', sources, outputDirectory, recipe, recipes, width, height });
export const batchStatus = () => batchControl({ operation: 'batch_status' });
export const updateBatchJob = (index: number, state: number, message = '') =>
  batchControl({ operation: 'batch_update', index, state, message });
export const retryBatch = () => batchControl({ operation: 'batch_retry' });
export const cancelBatch = () => batchControl({ operation: 'batch_cancel' });
export const discardBatch = () => batchControl({ operation: 'batch_discard' });
export const cancelBatchProcessing = () => invoke("cancel_batch_processing");
export const runBatch = () => batchControl({ operation: 'batch_run' });
export type ModelCatalog = {
  denoise: { id: string; name: string; path?: string; available: boolean }[];
  sharpen: { id: string; name: string; path?: string; available: boolean }[];
  provider: string;
};
export const configuredModels = async () =>
  (await invoke<{ models: ModelCatalog }>("processing_models")).models;
export const processRaw = (
  path: string,
  recipe: unknown,
  highQuality: boolean,
  output?: string,
  outputKind: 'cfa' | 'linear-rgb' = 'cfa',
  previewSize = 4096,
) =>
  invoke<RawPreview>("process_raw", {
    path,
    recipe,
    highQuality,
    output: output ?? null,
    outputKind,
    previewSize,
  });
export const recomposeRaw = (path: string, recipe: unknown, highQuality: boolean, previewSize: number, resultId: string) =>
  invoke<RawPreview>("process_raw", {
    operation: "recompose",
    path,
    recipe,
    highQuality,
    previewSize,
    resultId,
    output: null,
  });
export const cancelProcessing = () => invoke("cancel_processing");
export type ExternalSession = {
  mode: 'single' | 'batch';
  sources: string[];
  output?: string;
  recipeSource?: string;
  request?: string;
};
export const getExternalSession = () => invoke<ExternalSession | null>('external_session');
export const runExternalBatch = (request: string, recipe: unknown) =>
  invoke<{completed:number;total:number;complete:boolean}>('run_external_batch', {request, recipe});
export const finishExternal = (code: number) => invoke<void>('finish_external', {code});
export async function processBatch(
  sources: string[],
  outputDirectory: string,
  recipes: unknown[],
  highQuality: boolean,
  onProgress?: (status: BatchStatus) => void,
) {
  let status = await createBatch(sources, outputDirectory, recipes[0] ?? {}, recipes);
  onProgress?.(status);
  status = await runBatch();
  onProgress?.(status);
  return status;
}
export const segmentRaw = (path: string, points: [number, number, boolean][]) =>
  invoke<{ mask: string }>("process_raw", {
    path,
    points,
    recipe: {},
    highQuality: false,
    output: null,
  });
export async function exportDestination(name: string) {
  const { save } = await import("@tauri-apps/plugin-dialog");
  return save({
    title: "Export Bayer CFA DNG",
    defaultPath: name.replace(/\.[^.]+$/, "") + "-rawlab.dng",
    filters: [{ name: "Digital Negative", extensions: ["dng"] }],
  });
}
export async function exportLinearDestination(name: string) {
  const { save } = await import("@tauri-apps/plugin-dialog");
  return save({
    title: "Export Linear RGB DNG",
    defaultPath: name.replace(/\.[^.]+$/, "") + "-rawlab-linear.dng",
    filters: [{ name: "Digital Negative", extensions: ["dng"] }],
  });
}
export async function chooseOutputDirectory() {
  const { open } = await import("@tauri-apps/plugin-dialog");
  const selected = await open({ title: "Choose batch output folder", directory: true });
  return typeof selected === "string" ? selected : null;
}
const formats: Record<string, string> = {
  jpg: "image/jpeg",
  jpeg: "image/jpeg",
  png: "image/png",
  webp: "image/webp",
};

// The dialog plugin grants read scope only for explicitly selected paths. No
// blanket home-directory permissions, shell commands or filesystem writes.
export async function chooseNativePreviews(
  folder: boolean,
): Promise<{ files: File[]; raws: RawFile[]; skipped: number } | null> {
  const { open } = await import("@tauri-apps/plugin-dialog");
  const { readDir, readFile, stat } = await import("@tauri-apps/plugin-fs");
  const { join, basename } = await import("@tauri-apps/api/path");
  const selected = await open(
    folder
      ? { title: "Import photograph folder", directory: true, recursive: true }
      : {
          title: "Import photographs",
          multiple: true,
          filters: [
            {
              name: "RAW and preview images",
              extensions: [...rawExtensions, ...Object.keys(formats)],
            },
          ],
        },
  );
  if (!selected) return null;
  const files: File[] = [];
  const raws: RawFile[] = [];
  let skipped = 0;
  let visited = 0;
  let totalBytes = 0;
  const append = async (path: string, relative: string) => {
    const mime = formats[path.split(".").pop()?.toLowerCase() ?? ""];
    const raw = rawExtensions.includes(
      path.split(".").pop()?.toLowerCase() ?? "",
    );
    if ((!mime && !raw) || files.length + raws.length >= 500) {
      skipped++;
      return;
    }
    try {
      const metadata = await stat(path);
      if (raw) {
        raws.push({
          path,
          name: relative || (await basename(path)),
          identity: `${path}:${metadata.size}:${metadata.mtime?.getTime() ?? 0}`,
        });
        return;
      }
      if (
        metadata.size > 50 * 1024 * 1024 ||
        totalBytes + metadata.size > 256 * 1024 * 1024
      ) {
        skipped++;
        return;
      }
      const bytes = await readFile(path);
      totalBytes += bytes.byteLength;
      const file = new File([bytes], await basename(path), {
        type: mime,
        lastModified: metadata.mtime?.getTime() ?? 0,
      });
      Object.defineProperty(file, "webkitRelativePath", { value: relative });
      files.push(file);
    } catch {
      skipped++;
    }
  };
  const walk = async (directory: string, relative: string, depth: number) => {
    if (depth > 32 || visited++ >= 2000 || files.length + raws.length >= 500) {
      skipped++;
      return;
    }
    const entries = (await readDir(directory)).sort((a, b) =>
      a.name.localeCompare(b.name, undefined, { numeric: true }),
    );
    for (const entry of entries) {
      if (entry.isSymlink) {
        skipped++;
        continue;
      }
      const path = await join(directory, entry.name);
      const name = relative ? `${relative}/${entry.name}` : entry.name;
      if (entry.isDirectory) {
        try {
          await walk(path, name, depth + 1);
        } catch {
          skipped++;
        }
      } else if (entry.isFile) await append(path, name);
    }
  };
  for (const path of Array.isArray(selected) ? selected : [selected]) {
    if (folder) await walk(path, await basename(path), 0);
    else await append(path, "");
  }
  return { files, raws, skipped };
}
