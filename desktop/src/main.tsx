import React, { useEffect, useMemo, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import {
  Aperture,
  ArrowDownToLine,
  ArrowLeft,
  Check,
  ChevronDown,
  Columns2,
  Eye,
  EyeOff,
  FolderOpen,
  Grid2X2,
  ImagePlus,
  Layers,
  Maximize,
  Minus,
  Plus,
  RotateCcw,
  Settings2,
  SlidersHorizontal,
  Sparkles,
  Star,
  Trash2,
} from "lucide-react";
import "./style.css";
import {
  isDesktop,
  chooseNativePreviews,
  renderRaw,
  renderProcessed,
  renderRawDetail,
  rawSourceVersion,
  type RawPreview,
  configuredModels,
  type ModelCatalog,
  processRaw,
  recomposeRaw,
  exportDestination,
  exportLinearDestination,
  chooseOutputDirectory,
  cancelProcessing,
  processBatch,
  batchStatus as loadBatchStatus,
  runBatch,
  retryBatch,
  cancelBatch,
  discardBatch,
  cancelBatchProcessing,
  type BatchStatus,
  segmentRaw,
  listLibrary,
  libraryMetadata,
  saveLibraryMetadata,
  addLibraryPaths,
  loadRecipe,
  saveRecipe,
  listPresets,
  loadPreset,
  savePreset,
  deletePreset,
  getExternalSession,
  runExternalBatch,
  finishExternal,
  type ExternalSession,
} from "./desktop";
import ModelManager from "./ModelManager";
import { PersistenceQueue } from "./persistence";

type Mark = { tool: string; points: [number, number][] };
type Layer = {
  marks: Mark[];
  points: [number, number, boolean][];
  aiMask?: string;
  modelId: string;
  id: number;
  name: string;
  enabled: boolean;
  blend: number;
  sharpen: string;
  strength: number;
  feather: number;
  blur: number;
  inverted: boolean;
  showMask: boolean;
};
type Photo = {
  processed?: { url: string; before: string; key: string; high: boolean; resultId: string; sourceVersion?: string };
  rawPath?: string;
  metadata?: RawPreview["metadata"];
  previewWidth?: number;
  previewHeight?: number;
  error?: string;
  id: number;
  name: string;
  url: string;
  identity: string;
  layers: Layer[];
  rating: number;
  labels?: string[];
  sharpenModelId?: string;
  dirty: boolean;
  highlights: boolean;
  chromaticAberration: boolean;
  lensCorrection: boolean;
  preset: string;
  kind: string;
};
const initialLayer = (): Layer => ({
  marks: [],
  points: [],
  modelId: "",
  id: 0,
  name: "Global",
  enabled: true,
  blend: 75,
  sharpen: "Off",
  strength: 30,
  feather: 0,
  blur: 0,
  inverted: false,
  showMask: true,
});
const demo: Photo = {
  id: 0,
  name: "Alpine lake · demo illustration",
  url: "/demo.svg",
  identity: "demo",
  layers: [initialLayer()],
  rating: 0,
  dirty: false,
  highlights: true,
  chromaticAberration: false,
  lensCorrection: false,
  preset: "",
  kind: "demo",
};

function IconButton({
  label,
  children,
  onClick,
  pressed,
}: {
  label: string;
  children: React.ReactNode;
  onClick: () => void;
  pressed?: boolean;
}) {
  return (
    <button
      className={`icon ${pressed ? "active" : ""}`}
      aria-label={label}
      title={label}
      aria-pressed={pressed}
      onClick={onClick}
    >
      {children}
    </button>
  );
}
function App() {
  const importing = useRef(false);
  const [photos, setPhotos] = useState<Photo[]>([demo]);
  const [currentId, setCurrentId] = useState(0);
  const [selection, setSelection] = useState<number[]>([]);
  const [library, setLibrary] = useState(true);
  const [batchView, setBatchView] = useState(false);
  const libraryRef = useRef(library);
  libraryRef.current = library;
  const [layerId, setLayerId] = useState(0);
  const [zoom, setZoom] = useState(1);
  const [pixelZoom, setPixelZoom] = useState(false);
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const panPosition = useRef({ x: 0, y: 0 });
  const [panning, setPanning] = useState(false);
  const [spacePan, setSpacePan] = useState(false);
  const [liveMark, setLiveMark] = useState<Mark | null>(null);
  const mark = useRef<Mark | null>(null);
  const liveStroke = useRef<SVGPolylineElement | null>(null);
  useEffect(() => {
    const down = (e: KeyboardEvent) => {
      if (
        e.code === "Space" &&
        !(e.target as HTMLElement).closest("input,textarea,select,button")
      ) {
        e.preventDefault();
        setSpacePan(true);
      }
    };
    const up = (e: KeyboardEvent) => {
      if (e.code === "Space") setSpacePan(false);
    };
    const blur = () => setSpacePan(false);
    window.addEventListener("keydown", down);
    window.addEventListener("keyup", up);
    window.addEventListener("blur", blur);
    return () => {
      window.removeEventListener("keydown", down);
      window.removeEventListener("keyup", up);
      window.removeEventListener("blur", blur);
    };
  }, []);
  const panStart = useRef<{
    x: number;
    y: number;
    originX: number;
    originY: number;
  } | null>(null);
  const panFrame = useRef<number | null>(null);
  const wheelFrame = useRef<number | null>(null);
  const wheelPending = useRef<{ delta: number; x: number; y: number } | null>(null);
  const [compare, setCompare] = useState(false);
  const [split, setSplit] = useState(50);
  const [quality, setQuality] = useState(false);
  const selectionAnchor = useRef<number | null>(null);
  const [exportKind, setExportKind] = useState<"cfa" | "linear-rgb">("cfa");
  const [rawRefresh, setRawRefresh] = useState(0);
  const [rawView, setRawView] = useState<{
    id: number;
    high: boolean;
    url: string;
    sourceWidth: number;
    sourceHeight: number;
    sourceVersion?: string;
  } | null>(null);
  const [maskGeometryView, setMaskGeometryView] = useState<{ key: string; url: string } | null>(null);
  const rawViewRef = useRef(rawView);
  rawViewRef.current = rawView;
  const [detailView, setDetailView] = useState<{
    key: string; url: string; beforeUrl?: string; x: number; y: number; width: number; height: number;
  } | null>(null);
  const [rawBusy, setRawBusy] = useState(false);
  const [rawError, setRawError] = useState("");
  const [processing, setProcessing] = useState(false);
  const processingRequest = useRef(false);
  const recomposeTimer = useRef<number | undefined>(undefined);
  const recomposeToken = useRef(0);
  const samRequest = useRef(0);
  const [batchBusy, setBatchBusy] = useState(false);
  const [batchDialogOpen, setBatchDialogOpen] = useState(false);
  const [batchQueue, setBatchQueue] = useState<BatchStatus | null>(null);
  const [batchRecoveryError, setBatchRecoveryError] = useState("");
  const [progress, setProgress] = useState("");
  const [catalog, setCatalog] = useState<ModelCatalog>({
    denoise: [],
    sharpen: [],
    provider: "cpu",
  });
  const [defaultSharpenModelId, setDefaultSharpenModelId] = useState("");
  const [externalSession, setExternalSession] = useState<ExternalSession | null>(null);
  const externalSessionRef = useRef(externalSession);
  externalSessionRef.current = externalSession;
  const photosRef = useRef(photos);
  photosRef.current = photos;
  const history = useRef(new Map<number, { undo: Photo[]; redo: Photo[] }>());
  const photoRevisions = useRef(new Map<number, number>());
  const metadataRevisions = useRef(new Map<number, number>());
  const loadedRecipes = useRef(new Set<number>());
  const loadedMetadata = useRef(new Set<number>());
  const persistence = useRef<PersistenceQueue | null>(null);
  const closing = useRef(false);
  const [restoreEpoch, setRestoreEpoch] = useState(0);
  const recipeRevision = (id: number) => photoRevisions.current.get(id) ?? 0;
  useEffect(() => () => {
    if (recomposeTimer.current !== undefined) window.clearTimeout(recomposeTimer.current);
    void persistence.current?.flushAll();
  }, []);
  const loadingRecipes = useRef(new Set<number>());
  const failedRecipeLoads = useRef(new Set<number>());
  const recipeLoadTokens = useRef(new Map<number, number>());
  const rawPreviewCache = useRef(new Map<string, RawPreview>());
  const processedPreviewCache = useRef(new Map<string, RawPreview>());
  const processedPreviewRequests = useRef(new Map<string, Promise<RawPreview>>());
  const detailCache = useRef(new Map<string, string>());
  const detailRequest = useRef(0);
  useEffect(() => {
    const keydown = (event: KeyboardEvent) => {
      if (closing.current) return;
      if (
        !(event.ctrlKey || event.metaKey) ||
        event.key.toLowerCase() !== "z" ||
        (event.target as HTMLElement).closest("input,textarea,select") ||
        processing
      )
        return;
      const h = history.current.get(photo.id);
      if (!h) return;
      const from = event.shiftKey ? h.redo : h.undo,
        to = event.shiftKey ? h.undo : h.redo;
      const saved = from.pop();
      if (!saved) return;
      event.preventDefault();
      photoRevisions.current.set(photo.id, recipeRevision(photo.id) + 1);
      to.push(photo);
      setPhotos((ps) =>
        ps.map((p) =>
          p.id === photo.id
            ? {
                ...saved,
                url: p.url,
                metadata: p.metadata,
                processed: undefined,
                dirty: true,
              }
            : p,
        ),
      );
      setLayerId(0);
      setEditingMask(false);
    };
    window.addEventListener("keydown", keydown);
    return () => window.removeEventListener("keydown", keydown);
  });
  useEffect(() => {
    if (!isDesktop()) return;
    configuredModels()
      .then((models) => {
        setCatalog(models);
        setDefaultSharpenModelId(models.sharpen.find((m) => m.available)?.id ?? "");
      })
      .catch((error) => setNotice(String(error)));
    let stopped = false;
    let cleanup: (() => void) | undefined;
    import("@tauri-apps/api/event")
      .then(async ({ listen }) => {
        const unlisten = await listen<{ progress: number; total: number; stage?: string; jobs?: BatchStatus["jobs"]; snapshot?: string; completed?: number; remaining?: number }>(
          "rawlab-progress",
          (e) => {
            if (e.payload.jobs && e.payload.snapshot)
              setBatchQueue({snapshot:e.payload.snapshot,jobs:e.payload.jobs,
                completed:e.payload.completed ?? 0,remaining:e.payload.remaining ?? 0});
            const stage = e.payload.stage ?? "Processing tile";
            setProgress(e.payload.total > 1
              ? `${stage} ${e.payload.progress} of ${e.payload.total}`
              : `${stage}…`);
          },
        );
        if (stopped) unlisten();
        else cleanup = unlisten;
      })
      .catch(() => {});
    return () => {
      stopped = true;
      cleanup?.();
    };
  }, []);
  useEffect(() => {
    if (!isDesktop()) return;
    let cancelled = false;
    void Promise.all([getExternalSession(), listLibrary(), listPresets()])
      .then(async ([external, libraryResult, presetResult]) => {
        if (cancelled) return;
        setExternalSession(external);
        if (!external)
          void loadBatchStatus().then((status) => { setBatchQueue(status); setBatchRecoveryError(""); })
            .catch((error) => { setBatchRecoveryError(String(error)); });
        const persisted = (external?.sources ?? libraryResult.paths).filter((path) => /\.(arw|cr2|cr3|dng|nef|nrw|orf|rw2|pef|raf|srw|raw)$/i.test(path));
        const restored: Photo[] = [];
        for (const path of persisted) {
          const id = nextPhoto.current++;
          const name = path.split(/[\\/]/).pop() || path;
            restored.push({ id, name, identity: path, rawPath: path, url: "/raw-placeholder.svg", layers: [initialLayer()], rating: 0, labels: [], dirty: false, highlights: true, chromaticAberration: false, lensCorrection: false, preset: "", kind: "raw" });
        }
        if (restored.length) {
           setPhotos((photos) => [...photos.filter((photo) => photo.kind !== "demo"), ...restored]);
            setSelection(external ? restored.map((photo) => photo.id) : []);
            if (external) {
              setCurrentId(restored[0].id);
              setLibrary(false);
              setNotice(external.mode === "batch" ? "Darktable batch · adjust the first photo, then export" : "Darktable external edit");
            }
           for (const photo of restored) {
             if (external?.recipeSource && photo === restored[0]) {
               const preview = await renderRaw(photo.rawPath!, false, 400, true);
               const stored = (await loadRecipe(external.recipeSource, preview.width, preview.height)).recipe;
               if (stored && !cancelled) {
                 loadedRecipes.current.add(photo.id);
                 setPhotos((items) => items.map((item) => item.id === photo.id ? {
                   ...item, layers: stored.layers.map((layer, index) => ({ ...initialLayer(), id: index, ...(layer as Partial<Layer>) })),
                   highlights: stored.highlights !== false, sharpenModelId: stored.sharpenModelId,
                   chromaticAberration: stored.chromaticAberration === true,
                   lensCorrection: stored.lensCorrection === true,
                 } : item));
               }
             } else await restorePhotoRecipe(photo, () => cancelled);
             try {
              const metadataRevision = metadataRevisions.current.get(photo.id) ?? 0;
              const stored = await libraryMetadata(photo.rawPath!);
              if (!cancelled && (metadataRevisions.current.get(photo.id) ?? 0) === metadataRevision)
                setPhotos((items) => items.map((item) => item.id === photo.id ? { ...item, rating: stored.rating, labels: stored.labels } : item));
            } catch {
              // Optional metadata must not prevent RAW restoration.
            } finally {
              loadedMetadata.current.add(photo.id);
            }
          }
        }
        if (!cancelled) {
          const names = presetResult.names;
          const shared: typeof presets = [];
          for (const name of names) {
            try {
              const loaded = await loadPreset(name);
              const recipe = loaded.recipe;
              if (recipe && Array.isArray(recipe.layers)) shared.push({ name, layers: recipe.layers.map((layer) => ({ ...initialLayer(), ...(layer as Partial<Layer>) })), highlights: recipe.highlights !== false, sharpenModelId: recipe.sharpenModelId });
            } catch {
              // Keep the remaining presets available if one legacy entry is invalid.
            }
          }
          setPresets(shared);
        }
      })
      .catch((error) => setNotice(`Could not restore desktop library: ${String(error)}`));
    return () => { cancelled = true; };
  }, []);
  const [notice, setNotice] = useState(
    "Ready · Import pictures or a folder to begin",
  );
  const [failedPersistenceKey, setFailedPersistenceKey] = useState("");
  if (!persistence.current) persistence.current = new PersistenceQueue((key, error) => {
    setFailedPersistenceKey(key);
    setNotice(`Could not save ${key.startsWith("recipe:") ? "recipe" : key.startsWith("preset:") ? "preset" : "photo metadata"}: ${String(error)}`);
  }, (key) => setFailedPersistenceKey((failed) => {
    if (failed !== key) return failed;
    return persistence.current?.failedKeys().find((candidate) => candidate !== key) ?? "";
  }));
  useEffect(() => {
    if (!isDesktop()) return;
    let unlisten: (() => void) | undefined;
    void import("@tauri-apps/api/window").then(async ({ getCurrentWindow }) => {
      const currentWindow = getCurrentWindow();
      unlisten = await currentWindow.onCloseRequested(async (event) => {
        event.preventDefault();
        if (closing.current) return;
        closing.current = true;
        setNotice("Saving changes before closing...");
        let timeout: number | undefined;
        try {
          const save = (async () => {
            while (loadingRecipes.current.size)
              await new Promise((resolve) => window.setTimeout(resolve, 10));
            if (failedRecipeLoads.current.size)
              throw new Error("One or more recipes could not be restored safely");
            for (const item of photosRef.current) enqueuePhotoPersistence(item, 0);
            return persistence.current?.flushAll();
          })();
          const timeoutReached = new Promise<"timeout">((resolve) => {
            timeout = window.setTimeout(() => resolve("timeout"), 10_000);
          });
          const saved = await Promise.race([save, timeoutReached]);
          if (timeout) window.clearTimeout(timeout);
          if (saved === "timeout") {
            setNotice("Closing after the save timed out. Some recent changes may not be saved.");
            await currentWindow.destroy();
          } else if (saved) {
            if (externalSessionRef.current) await finishExternal(3);
            else await currentWindow.destroy();
          } else setNotice("Could not save all changes. Fix the storage error and close again.");
        } catch (error) {
          if (timeout) window.clearTimeout(timeout);
          setNotice(`Could not close safely: ${String(error)}`);
        } finally {
          closing.current = false;
        }
      });
    }).catch((error) => setNotice(`Could not install safe-close handling: ${String(error)}`));
    return () => unlisten?.();
  }, []);
  const [settings, showSettings] = useState(false);
  const [presets, setPresets] = useState<
    {
      name: string;
      layers: Layer[];
      highlights: boolean;
      sharpenModelId?: string;
    }[]
  >(() => {
    if (isDesktop()) return [];
    try {
      const saved = JSON.parse(
        localStorage.getItem("rawlab-desktop-presets-v1") ?? "[]",
      );
      return Array.isArray(saved)
        ? saved
            .filter(
              (p) => typeof p.name === "string" && Array.isArray(p.layers),
            )
            .map((p) => ({
              ...p,
              layers: p.layers.map((l: Partial<Layer>) => ({
                ...initialLayer(),
                ...l,
              })),
            }))
        : [];
    } catch {
      return [];
    }
  });
  useEffect(() => {
    if (!isDesktop())
      try {
        localStorage.setItem(
          "rawlab-desktop-presets-v1",
          JSON.stringify(presets),
        );
      } catch {
        setNotice("Could not save desktop presets: storage is full.");
      }
  }, [presets]);
  const [presetName, setPresetName] = useState("");
  const [search, setSearch] = useState("");
  const [minimumRating, setMinimumRating] = useState(0);
  const [labelFilter, setLabelFilter] = useState("");
  const [fileType, setFileType] = useState("");
  const [filtersOpen, setFiltersOpen] = useState(false);
  const [savingPreset, setSavingPreset] = useState(false);
  const [presetsOpen, setPresetsOpen] = useState(false);
  const [editingMask, setEditingMask] = useState(false);
  const [maskTool, setMaskTool] = useState("AI");
  const presetPanel = useRef<HTMLDivElement>(null);
  const presetButton = useRef<HTMLButtonElement>(null);
  const filterPanel = useRef<HTMLDivElement>(null);
  const filterButton = useRef<HTMLButtonElement>(null);
  const presetDialog = useRef<HTMLDialogElement>(null);
  const filesInput = useRef<HTMLInputElement>(null);
  const folderInput = useRef<HTMLInputElement>(null);
  const frame = useRef<HTMLDivElement>(null);
  const canvas = useRef<HTMLDivElement>(null);
  const [canvasSize, setCanvasSize] = useState({ width: 800, height: 600 });
  const [imageRatio, setImageRatio] = useState(1600 / 1060);
  const [imageSize, setImageSize] = useState({ width: 1600, height: 1060 });
  function boundPan(x: number, y: number, zoomValue = zoom, pixelMode = pixelZoom) {
    const fitWidth = Math.min(canvasSize.width, canvasSize.height * imageRatio);
    const width = pixelMode && rawView?.id === currentId ? rawView.sourceWidth
      : pixelMode ? imageSize.width : fitWidth;
    const height = pixelMode && rawView?.id === currentId ? rawView.sourceHeight : width / imageRatio;
    const maxX = Math.max(0, (width * zoomValue - canvasSize.width) / 2);
    const maxY = Math.max(
      0,
      (height * zoomValue - canvasSize.height) / 2,
    );
    return {
      x: Math.max(-maxX, Math.min(maxX, x)),
      y: Math.max(-maxY, Math.min(maxY, y)),
    };
  }
  function applyPanVisual(value: { x: number; y: number }, zoomValue = zoom) {
    panPosition.current = value;
    if (panFrame.current !== null) return;
    panFrame.current = requestAnimationFrame(() => {
      panFrame.current = null;
      if (frame.current)
        frame.current.style.transform = `translate(${panPosition.current.x}px, ${panPosition.current.y}px) scale(${zoomValue})`;
    });
  }
  function flushPanVisual() {
    if (panFrame.current !== null) {
      cancelAnimationFrame(panFrame.current);
      panFrame.current = null;
    }
    if (frame.current)
      frame.current.style.transform = `translate(${panPosition.current.x}px, ${panPosition.current.y}px) scale(${zoom})`;
  }
  function handlePreviewWheel(event: React.WheelEvent<HTMLDivElement>) {
    if (!event.ctrlKey && !event.metaKey) return;
    event.preventDefault();
    const pending = wheelPending.current;
    wheelPending.current = {
      delta: (pending?.delta ?? 0) + event.deltaY,
      x: event.clientX,
      y: event.clientY,
    };
    if (wheelFrame.current !== null) return;
    wheelFrame.current = requestAnimationFrame(() => {
      wheelFrame.current = null;
      const next = wheelPending.current;
      wheelPending.current = null;
      if (!next || !frame.current) return;
      const currentPan = panPosition.current;
      const currentZoom = zoom;
      const minimumZoom = pixelZoom && rawView?.id === currentId
        ? Math.min(1, Math.min(canvasSize.width, canvasSize.height * imageRatio) / rawView.sourceWidth)
        : 0.5;
      let nextZoom = Math.max(minimumZoom, Math.min(3, currentZoom * Math.pow(1.0015, -next.delta)));
      if (nextZoom === currentZoom) return;
      const bounds = frame.current.getBoundingClientRect();
      const originX = bounds.left + bounds.width / 2 - currentPan.x;
      const originY = bounds.top + bounds.height / 2 - currentPan.y;
      const contentX = (next.x - originX - currentPan.x) / currentZoom;
      const contentY = (next.y - originY - currentPan.y) / currentZoom;
      let nextPixelZoom = pixelZoom;
      let nextPan = boundPan(
        next.x - originX - contentX * nextZoom,
        next.y - originY - contentY * nextZoom,
        nextZoom,
        pixelZoom,
      );
      if (!pixelZoom && rawView?.id === currentId) {
        const fitWidth = Math.min(canvasSize.width, canvasSize.height * imageRatio);
        if (fitWidth * nextZoom >= imageSize.width) {
          nextZoom = fitWidth * nextZoom / rawView.sourceWidth;
          nextPixelZoom = true;
          nextPan = boundPan(nextPan.x, nextPan.y, nextZoom, true);
        }
      }
      setPixelZoom(nextPixelZoom);
      setZoom(nextZoom);
       applyPanVisual(nextPan, nextZoom);
      setPan(nextPan);
    });
  }
  useEffect(() => {
    setPan((p) => boundPan(p.x, p.y));
  }, [zoom, pixelZoom, imageSize.width, canvasSize.width, canvasSize.height, imageRatio]);
  useEffect(() => {
    panPosition.current = pan;
  }, [pan]);
  useEffect(() => () => {
    if (panFrame.current !== null) cancelAnimationFrame(panFrame.current);
    if (dividerFrame.current !== null) cancelAnimationFrame(dividerFrame.current);
    if (wheelFrame.current !== null) cancelAnimationFrame(wheelFrame.current);
  }, []);
  const dialog = useRef<HTMLDialogElement>(null);
  const urls = useRef<string[]>([]);
  const nextPhoto = useRef(1);
  const nextLayer = useRef(1);
  const dragging = useRef(false);
  const dividerRect = useRef<DOMRect | null>(null);
  const dividerValue = useRef(50);
  const dividerFrame = useRef<number | null>(null);
  useEffect(() => {
    folderInput.current?.setAttribute("webkitdirectory", "");
    return () => urls.current.forEach((url) => URL.revokeObjectURL(url));
  }, []);
  useEffect(() => {
    if (settings) dialog.current?.showModal();
  }, [settings]);
  useEffect(() => {
    if (savingPreset) presetDialog.current?.showModal();
  }, [savingPreset]);
  useEffect(() => {
    setEditingMask(false);
    setMaskTool("AI");
    setPresetsOpen(false);
  }, [layerId, currentId, library]);
  useEffect(() => {
    if (!presetsOpen) return;
    const dismiss = (event: PointerEvent) => {
      if (!presetPanel.current?.contains(event.target as Node))
        setPresetsOpen(false);
    };
    const escape = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        setPresetsOpen(false);
        presetButton.current?.focus();
      }
    };
    document.addEventListener("pointerdown", dismiss);
    document.addEventListener("keydown", escape);
    return () => {
      document.removeEventListener("pointerdown", dismiss);
      document.removeEventListener("keydown", escape);
    };
  }, [presetsOpen]);
  useEffect(() => {
    if (!filtersOpen) return;
    const dismiss = (event: PointerEvent) => {
      if (!filterPanel.current?.contains(event.target as Node))
        setFiltersOpen(false);
    };
    const escape = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        setFiltersOpen(false);
        filterButton.current?.focus();
      }
    };
    document.addEventListener("pointerdown", dismiss);
    document.addEventListener("keydown", escape);
    return () => {
      document.removeEventListener("pointerdown", dismiss);
      document.removeEventListener("keydown", escape);
    };
  }, [filtersOpen]);
  useEffect(() => {
    if (!canvas.current) return;
    const observer = new ResizeObserver(([entry]) =>
      setCanvasSize({
        width: entry.contentRect.width,
        height: entry.contentRect.height,
      }),
    );
    observer.observe(canvas.current);
    return () => observer.disconnect();
  }, [library]);
  const photo = photos.find((p) => p.id === currentId) ?? photos[0];
  const selectedRawCount = selection.filter((id) => photos.find((item) => item.id === id)?.rawPath).length;
  const active = photo.layers.find((l) => l.id === layerId) ?? photo.layers[0];
  const previewDimension = Math.max(128, Math.min(4096, Math.ceil(
    Math.max(canvasSize.width, canvasSize.height) * Math.min(window.devicePixelRatio || 1, 2))));
  function retainPreview(cache: Map<string, RawPreview>, key: string, value: RawPreview, limit: number) {
    cache.delete(key);
    cache.set(key, value);
    while (cache.size > limit) cache.delete(cache.keys().next().value!);
  }
  useEffect(() => {
    if (library || !photo.rawPath) {
      setRawBusy(false);
      setRawError("");
      return;
    }
    let obsolete = false;
    const id = photo.id;
    const requestedSize = previewDimension;
    const cacheKey = `${photo.rawPath}:${quality ? "high" : "fast"}:${requestedSize}:${rawRefresh}`;
    setRawBusy(true);
    setRawError("");
    const cached = rawPreviewCache.current.get(cacheKey);
    const timer = window.setTimeout(() => {
      const request = cached
        ? rawSourceVersion(photo.rawPath!)
            .then((version) => version && cached.sourceVersion === version
              ? cached : renderRaw(photo.rawPath!, quality, requestedSize))
        : renderRaw(photo.rawPath!, quality, requestedSize);
      void request.then((result) => {
        if (obsolete) return;
        if (result !== cached) retainPreview(rawPreviewCache.current, cacheKey, result, 8);
        setRawView({
          id,
          high: quality,
          url: `data:image/png;base64,${result.png}`,
          sourceWidth: result.sourceWidth ?? result.metadata.rawWidth ?? result.width,
          sourceHeight: result.sourceHeight ?? result.metadata.rawHeight ?? result.height,
          sourceVersion: result.sourceVersion,
        });
        setPhotos((ps) =>
          ps.map((p) =>
            p.id === id
               ? { ...p, metadata: result.metadata, previewWidth: result.width, previewHeight: result.height,
                   processed: p.processed?.sourceVersion && result.sourceVersion
                     && p.processed.sourceVersion !== result.sourceVersion ? undefined : p.processed,
                   dirty: p.processed?.sourceVersion && result.sourceVersion
                     && p.processed.sourceVersion !== result.sourceVersion ? true : p.dirty,
                   error: undefined }
              : p,
          ),
        );
        setRawBusy(false);
      })
      .catch((error) => {
        if (!obsolete) {
          setRawError(String(error));
          setRawBusy(false);
        }
      });
    }, 40);
    return () => {
      obsolete = true;
      window.clearTimeout(timer);
    };
  }, [library, photo.id, photo.rawPath, quality, rawRefresh, canvasSize.width, canvasSize.height]);
  const originalUrl =
    photo.rawPath && rawView?.id === photo.id && rawView.high === quality
      ? rawView.url
      : photo.url;
  function recipeFor(p: Photo, outputKind?: "cfa" | "linear-rgb") {
    return {
      layers: p.layers.map((l, index) => ({
        ...l,
        modelId:
          l.modelId ||
          (index === 0 ? catalog.denoise.find((m) => m.available)?.id || "" : ""),
      })),
      highlights: p.highlights,
      chromaticAberration: p.chromaticAberration,
      lensCorrection: p.lensCorrection,
      ...(outputKind ? { outputKind } : {}),
      sharpenModelId: p.sharpenModelId ?? defaultSharpenModelId,
    };
  }
  function previewEndpointKey(recipe: ReturnType<typeof recipeFor>) {
    return JSON.stringify({
      ...recipe,
      layers: recipe.layers.map((layer) => ({...layer, blend: 0, strength: 0})),
    });
  }
  function canRecomposePreview(processed: NonNullable<Photo["processed"]>) {
    try {
      return previewEndpointKey(JSON.parse(processed.key) as ReturnType<typeof recipeFor>)
        === previewEndpointKey(recipeFor(photo, exportKind));
    } catch {
      return false;
    }
  }
  function enqueuePhotoPersistence(item: Photo, delay: number) {
    if (!isDesktop() || !item.rawPath) return;
    const path = item.rawPath;
    if (loadedRecipes.current.has(item.id) && !loadingRecipes.current.has(item.id)) {
      const snapshot = recipeFor(item);
      persistence.current!.schedule(`recipe:${path}`, delay, () =>
        saveRecipe(path, snapshot, item.previewWidth ?? 1024, item.previewHeight ?? 1024));
    }
    if (loadedMetadata.current.has(item.id)) {
      const labels = [...(item.labels ?? [])];
      persistence.current!.schedule(`metadata:${path}`, delay, () =>
        saveLibraryMetadata(path, item.rating, labels));
    }
  }
  const recipeKey = useMemo(
    () => JSON.stringify(recipeFor(photo, exportKind)),
    [photo, catalog.denoise, defaultSharpenModelId, exportKind],
  );
  useEffect(() => {
    if (!isDesktop() || !photo.rawPath || !loadedRecipes.current.has(photo.id) || loadingRecipes.current.has(photo.id)) return;
    const path = photo.rawPath;
     const snapshot = recipeFor(photo);
     const width = photo.previewWidth ?? 1024;
     const height = photo.previewHeight ?? 1024;
     persistence.current!.schedule(`recipe:${path}`, 700, () => saveRecipe(path, snapshot, width, height));
  }, [photo.id, photo.rawPath, photo.previewWidth, photo.previewHeight, recipeKey, restoreEpoch]);
  useEffect(() => {
    if (!isDesktop() || !photo.rawPath || !loadedMetadata.current.has(photo.id)) return;
    const path = photo.rawPath;
    const rating = photo.rating;
    const labels = [...(photo.labels ?? [])];
    persistence.current!.schedule(`metadata:${path}`, 300, () => saveLibraryMetadata(path, rating, labels));
  }, [photo.id, photo.rawPath, photo.rating, photo.labels]);
  const processedCurrent =
    photo.processed?.key === recipeKey
      && (!rawView?.sourceVersion || !photo.processed.sourceVersion
        || rawView.sourceVersion === photo.processed.sourceVersion)
      ? photo.processed
      : undefined;
  const rendered = processedCurrent?.high === quality ? processedCurrent : undefined;
  const correctedGeometry = exportKind === "linear-rgb"
    && (photo.chromaticAberration || photo.lensCorrection);
  const maskGeometryKey = `${photo.id}:${photo.processed?.resultId ?? ""}:${quality}:${previewDimension}`;
  const displayUrl = editingMask && correctedGeometry
    ? maskGeometryView?.key === maskGeometryKey ? maskGeometryView.url : originalUrl
    : rendered?.url ?? photo.processed?.url ?? originalUrl;
  useEffect(() => {
    if (!editingMask || !correctedGeometry || !photo.rawPath || !photo.processed?.resultId) {
      setMaskGeometryView(null);
      return;
    }
    let cancelled = false;
    void renderProcessed(photo.rawPath, quality, photo.processed.resultId, recipeFor(photo, "cfa"), previewDimension)
      .then((result) => {
        if (!cancelled) setMaskGeometryView({key: maskGeometryKey, url: `data:image/png;base64,${result.png}`});
      })
      .catch((error) => { if (!cancelled) setNotice(String(error)); });
    return () => { cancelled = true; };
  }, [editingMask, correctedGeometry, photo.id, photo.rawPath, photo.processed?.resultId,
      quality, maskGeometryKey, previewDimension]);
  useEffect(() => {
    if (!rendered?.before) return;
    const image = new Image();
    image.src = rendered.before;
    void image.decode().catch(() => {});
  }, [rendered?.before]);
  async function updateProcessing(exporting = false, outputKind: "cfa" | "linear-rgb" = exportKind) {
    if (!isDesktop() || !photo.rawPath) {
      if (exporting) showSettings(true);
      else {
        patchPhoto({ dirty: false });
        setNotice(
          "Preview interaction confirmed. No denoise or sharpening was performed.",
        );
      }
      return;
    }
    if (processingRequest.current) return;
    if (recomposeTimer.current !== undefined) {
      window.clearTimeout(recomposeTimer.current);
      recomposeTimer.current = undefined;
      recomposeToken.current++;
    }
    if (exporting && externalSession?.mode === "batch") {
      processingRequest.current = true;
      setProcessing(true);
      setProgress("Processing Darktable batch…");
      try {
        enqueuePhotoPersistence(photo, 0);
        await persistence.current?.flushAll();
        const result = await runExternalBatch(externalSession.request!, recipeFor(photo, outputKind));
        await finishExternal(result.complete ? 0 : 3);
      } catch (error) {
        setRawError(String(error));
        setNotice(String(error));
      } finally {
        processingRequest.current = false;
        setProcessing(false);
        setProgress("");
      }
      return;
    }
    processingRequest.current = true;
    const id = photo.id,
      path = photo.rawPath,
      recipe = recipeFor(photo, outputKind),
      key = JSON.stringify(recipe),
      high = quality;
    setProcessing(true);
    setProgress(
      exporting ? "Preparing DNG export…" : "Processing enabled layers…",
    );
    setRawError("");
    try {
      const destination = exporting
        ? externalSession?.mode === "single"
          ? externalSession.output
          : outputKind === "linear-rgb"
          ? await exportLinearDestination(photo.name)
          : await exportDestination(photo.name)
        : undefined;
      if (exporting && !destination) return;
      const result = await processRaw(
        path,
        recipe,
        high,
        destination ?? undefined,
        outputKind,
        previewDimension,
      );
       if (exporting) {
         enqueuePhotoPersistence(photo, 0);
         await persistence.current?.flushAll();
         setNotice(`DNG exported: ${result.output}`);
         if (externalSession?.mode === "single") await finishExternal(0);
       }
      else {
        setPhotos((ps) =>
          ps.map((p) =>
            p.id === id
              ? {
                  ...p,
                   dirty: JSON.stringify(recipeFor(p, outputKind)) !== key
                     || Boolean(result.sourceVersion && rawViewRef.current?.sourceVersion
                       && result.sourceVersion !== rawViewRef.current.sourceVersion),
                   processed: {
                     url: `data:image/png;base64,${result.png}`,
                     before: `data:image/png;base64,${result.before}`,
                     key,
                     high,
                     resultId: result.resultId ?? "",
                     sourceVersion: result.sourceVersion,
                   },
                 }
               : p,
          ),
        );
        setNotice(
          "Processing complete. Before / After compares original and processed RAW.",
        );
      }
    } catch (error) {
      setRawError(String(error));
      setNotice(String(error));
    } finally {
      processingRequest.current = false;
      setProcessing(false);
      setProgress("");
    }
  }
  useEffect(() => {
    // Quality changes only re-render the existing processed CFA. Pending edits
    // still require Update so a preview toggle cannot apply them accidentally.
    const processed = photo.processed;
    if (
      !isDesktop() ||
      !photo.rawPath ||
      photo.dirty ||
      !processed ||
      processed.key !== recipeKey ||
      processed.high === quality ||
      processing ||
      rawView?.id !== photo.id ||
      rawView.high !== quality
    ) return;
    const cacheKey = `${processed.resultId}:${recipeKey}:${quality ? "high" : "fast"}:${previewDimension}`;
     let cancelled = false;
     const cached = processedPreviewCache.current.get(cacheKey);
     let request = cached ? Promise.resolve(cached) : processedPreviewRequests.current.get(cacheKey);
     if (!request) {
       request = renderProcessed(photo.rawPath, quality, processed.resultId, recipeFor(photo, exportKind), previewDimension);
       processedPreviewRequests.current.set(cacheKey, request);
       void request.then(
         (result) => {
           retainPreview(processedPreviewCache.current, cacheKey, result, 4);
           processedPreviewRequests.current.delete(cacheKey);
         },
         () => processedPreviewRequests.current.delete(cacheKey),
       );
     }
     void request
       .then((after) => {
          if (cancelled) return;
          if (!processedPreviewCache.current.has(cacheKey))
            retainPreview(processedPreviewCache.current, cacheKey, after, 4);
          setPhotos((items) => items.map((item) => item.id === photo.id ? {
            ...item,
            processed: {
              url: `data:image/png;base64,${after.png}`,
               before: after.before ? `data:image/png;base64,${after.before}`
                 : rawView?.id === photo.id && rawView.high === quality ? rawView.url : processed.before,
              key: recipeKey,
              high: quality,
              resultId: processed.resultId,
              sourceVersion: after.sourceVersion ?? processed.sourceVersion,
            },
          } : item));
        })
       .catch((error) => { if (!cancelled) setNotice(String(error)); });
     return () => { cancelled = true; };
  }, [photo.id, photo.rawPath, photo.dirty, photo.processed?.key, photo.processed?.high,
       photo.processed?.resultId, recipeKey, quality, processing, rawView?.id, rawView?.high,
       rawView?.url, previewDimension]);
  useEffect(() => {
    if (!pixelZoom || !photo.rawPath || rawView?.id !== photo.id) {
      setDetailView(null);
      return;
    }
    const sourceWidth = rawView.sourceWidth;
    const sourceHeight = rawView.sourceHeight;
    const sourceScale = Math.max(zoom, 0.0001);
    const overscan = 96 / sourceScale;
    const frameLeft = (canvasSize.width - sourceWidth * sourceScale) / 2 + pan.x;
    const frameTop = (canvasSize.height - sourceHeight * sourceScale) / 2 + pan.y;
    const x = Math.max(0, Math.floor(-frameLeft / sourceScale - overscan));
    const y = Math.max(0, Math.floor(-frameTop / sourceScale - overscan));
    const width = Math.min(sourceWidth - x, Math.ceil(canvasSize.width / sourceScale + overscan * 2), 4096);
    const height = Math.min(sourceHeight - y, Math.ceil(canvasSize.height / sourceScale + overscan * 2), 4096);
    if (width <= 0 || height <= 0) return;
    const target = rendered?.resultId ? "processed" as const : "original" as const;
    const identity = rendered?.resultId || rawView.sourceVersion || photo.rawPath;
    const detailRecipe = editingMask && correctedGeometry
      ? recipeFor(photo, "cfa") : recipeFor(photo, exportKind);
    const geometryKey = JSON.stringify(detailRecipe);
    const detailKey = `${identity}:${target}:${geometryKey}:${x}:${y}:${width}:${height}`;
    const beforeKey = target === "processed"
      ? `${rawView.sourceVersion || photo.rawPath}:original:${geometryKey}:${x}:${y}:${width}:${height}` : detailKey;
    const key = `${detailKey}:${beforeKey}`;
    const cached = detailCache.current.get(detailKey);
    const cachedBefore = detailCache.current.get(beforeKey);
    if (cached && cachedBefore) {
      setDetailView({key, url: cached, beforeUrl: cachedBefore, x, y, width, height});
      return;
    }
    setDetailView((current) => current?.key === key ? current : null);
    const token = ++detailRequest.current;
    const timer = window.setTimeout(() => {
      const load = (cacheKey: string, requestTarget: "original" | "processed", resultId?: string) => {
        const hit = detailCache.current.get(cacheKey);
        if (hit) return Promise.resolve({url: hit, x, y, width, height, cacheKey: undefined as string | undefined});
        return renderRawDetail(photo.rawPath!, requestTarget, resultId, x, y, width, height, detailRecipe).then((result) => {
          if (result.sourceVersion && rawView.sourceVersion && result.sourceVersion !== rawView.sourceVersion)
            throw new Error("Source changed while rendering detail; retry");
          const url = `data:image/png;base64,${result.png}`;
          return {url, x: result.x, y: result.y, width: result.width, height: result.height, cacheKey};
        });
      };
      const detail = load(detailKey, target, rendered?.resultId);
      void Promise.all([
        detail,
        target === "processed" ? load(beforeKey, "original") : detail,
      ])
        .then(([result, before]) => {
          if (detailRequest.current !== token) return;
          if (result.cacheKey) detailCache.current.set(result.cacheKey, result.url);
          if (before.cacheKey) detailCache.current.set(before.cacheKey, before.url);
          let bytes = [...detailCache.current.values()].reduce((sum, value) => sum + value.length, 0);
          while (detailCache.current.size > 12 || bytes > 32 * 1024 * 1024) {
            const oldest = detailCache.current.keys().next().value;
            if (!oldest) break;
            bytes -= detailCache.current.get(oldest)?.length ?? 0;
            detailCache.current.delete(oldest);
          }
          setDetailView({key, url: result.url, beforeUrl: before.url,
            x: result.x, y: result.y, width: result.width, height: result.height});
        })
        .catch((error) => {
          if (detailRequest.current === token) setRawError(String(error));
        });
    }, 60);
    return () => {
      window.clearTimeout(timer);
      if (detailRequest.current === token) detailRequest.current++;
    };
  }, [pixelZoom, photo.id, photo.rawPath, rawView?.id, rawView?.sourceWidth,
      rawView?.sourceHeight, rawView?.sourceVersion, rendered?.resultId,
      canvasSize.width, canvasSize.height, pan.x, pan.y, zoom, editingMask,
      correctedGeometry, recipeKey, exportKind]);
  function patchPhoto(values: Partial<Photo>) {
    if (closing.current) return;
    photoRevisions.current.set(photo.id, (photoRevisions.current.get(photo.id) ?? 0) + 1);
    if (values.rating !== undefined || values.labels !== undefined)
      metadataRevisions.current.set(photo.id, (metadataRevisions.current.get(photo.id) ?? 0) + 1);
    if (
      values.layers ||
      values.highlights !== undefined ||
      values.preset !== undefined
    ) {
      let h = history.current.get(photo.id);
      if (!h) {
        h = { undo: [], redo: [] };
        history.current.set(photo.id, h);
      }
      h.undo.push(photo);
      if (h.undo.length > 20) h.undo.shift();
      h.redo = [];
    }
    setPhotos((ps) =>
      ps.map((p) => (p.id === photo.id ? { ...p, ...values } : p)),
    );
  }
  function change(values: Partial<Layer>) {
    const autoRecompose = values.blend !== undefined || values.strength !== undefined;
    const nextLayers = photo.layers.map((l) =>
      l.id === active.id ? { ...l, ...values } : l,
    );
    patchPhoto({
      layers: nextLayers,
      dirty: true,
    });
    if (autoRecompose && isDesktop() && photo.rawPath && photo.processed
      && canRecomposePreview(photo.processed)) {
      if (recomposeTimer.current !== undefined) window.clearTimeout(recomposeTimer.current);
      const id = photo.id;
      const path = photo.rawPath;
      const recipe = recipeFor({ ...photo, layers: nextLayers }, exportKind);
      const key = JSON.stringify(recipe);
      const token = ++recomposeToken.current;
      const runRecompose = () => {
        recomposeTimer.current = undefined;
        if (processingRequest.current) {
          recomposeTimer.current = window.setTimeout(runRecompose, 100);
          return;
        }
        processingRequest.current = true;
        setProcessing(true);
        setProgress("Updating preview…");
        const previewSize = Math.min(2048, Math.max(1024,
          Math.ceil(Math.max(canvasSize.width, canvasSize.height) * Math.min(window.devicePixelRatio || 1, 2))));
        void recomposeRaw(path, recipe, quality, previewSize, photo.processed!.resultId)
          .then((result) => {
            if (token !== recomposeToken.current) return;
            setPhotos((items) => items.map((item) => {
               if (item.id !== id || JSON.stringify(recipeFor(item, exportKind)) !== key) return item;
              return { ...item, dirty: false, processed: {
                url: `data:image/png;base64,${result.png}`,
                before: `data:image/png;base64,${result.before ?? item.processed?.before ?? ""}`,
                key, high: quality, resultId: result.resultId ?? "", sourceVersion: result.sourceVersion,
              }};
            }));
          })
          .catch((error) => {
            if (token === recomposeToken.current) setNotice(String(error));
          })
          .finally(() => {
            processingRequest.current = false;
            if (token === recomposeToken.current) {
              setProcessing(false);
              setProgress("");
            }
          });
      };
      recomposeTimer.current = window.setTimeout(runRecompose, 180);
    }
  }
  function changeOptics(key: "chromaticAberration" | "lensCorrection", enabled: boolean) {
    const nextPhoto = { ...photo, [key]: enabled, dirty: true };
    patchPhoto({ [key]: enabled, dirty: true });
    if (!isDesktop() || exportKind !== "linear-rgb" || !photo.rawPath || !photo.processed?.resultId
      || photo.processed.key !== JSON.stringify(recipeFor(photo, exportKind))) return;
    if (recomposeTimer.current !== undefined) window.clearTimeout(recomposeTimer.current);
    const id = photo.id;
    const path = photo.rawPath;
    const resultId = photo.processed.resultId;
    const recipe = recipeFor(nextPhoto, exportKind);
    const recipeIdentity = JSON.stringify(recipe);
    const token = ++recomposeToken.current;
    const refresh = () => {
      recomposeTimer.current = undefined;
      if (processingRequest.current) {
        recomposeTimer.current = window.setTimeout(refresh, 100);
        return;
      }
      processingRequest.current = true;
      setProcessing(true);
      setProgress("Updating optics preview...");
      void renderProcessed(path, quality, resultId, recipe, previewDimension)
        .then((result) => {
          if (token !== recomposeToken.current) return;
          setPhotos((items) => items.map((item) => {
            if (item.id !== id || JSON.stringify(recipeFor(item, exportKind)) !== recipeIdentity) return item;
            return { ...item, dirty: false, processed: {
              url: `data:image/png;base64,${result.png}`,
              before: `data:image/png;base64,${result.before ?? item.processed?.before ?? ""}`,
              key: recipeIdentity, high: quality, resultId,
              sourceVersion: result.sourceVersion ?? item.processed?.sourceVersion,
            }};
          }));
        })
        .catch((error) => { if (token === recomposeToken.current) setNotice(String(error)); })
        .finally(() => {
          processingRequest.current = false;
          if (token === recomposeToken.current) {
            setProcessing(false);
            setProgress("");
          }
        });
    };
    recomposeTimer.current = window.setTimeout(refresh, 80);
  }
  async function restorePhotoRecipe(selected: Photo, cancelled = () => false) {
    if (!selected.rawPath || loadedRecipes.current.has(selected.id)
      || loadingRecipes.current.has(selected.id)) return;
    const id = selected.id;
    const path = selected.rawPath;
    const revision = recipeRevision(id);
    const token = (recipeLoadTokens.current.get(id) ?? 0) + 1;
    recipeLoadTokens.current.set(id, token);
    loadingRecipes.current.add(id);
    let loadSucceeded = false;
    try {
      let width = selected.previewWidth;
      let height = selected.previewHeight;
      if (!width || !height) {
        const preview = await renderRaw(path, false, 400, true);
        width = preview.width;
        height = preview.height;
        if (!cancelled() && recipeLoadTokens.current.get(id) === token)
          setPhotos((items) => items.map((item) => item.id === id && item.rawPath === path
            ? { ...item, url: `data:image/png;base64,${preview.png}`, metadata: preview.metadata, previewWidth: width, previewHeight: height }
            : item));
      }
      if (cancelled()) return;
      const stored = (await loadRecipe(path, width, height)).recipe;
      loadSucceeded = true;
      failedRecipeLoads.current.delete(id);
      if (!stored || !Array.isArray(stored.layers) || recipeRevision(id) !== revision
        || recipeLoadTokens.current.get(id) !== token || cancelled()) return;
      setPhotos((items) => items.map((item) => item.id === id && item.rawPath === path ? {
        ...item,
        layers: stored.layers.map((layer, index) => {
          const storedLayer = layer as Partial<Layer>;
          return { ...initialLayer(), id: index, ...storedLayer, marks: Array.isArray(storedLayer.marks) ? storedLayer.marks : [], points: Array.isArray(storedLayer.points) ? storedLayer.points : [] };
        }),
        highlights: stored.highlights !== false,
        chromaticAberration: stored.chromaticAberration === true,
        lensCorrection: stored.lensCorrection === true,
        sharpenModelId: stored.sharpenModelId,
      } : item));
    } catch (error) {
      if (recipeLoadTokens.current.get(id) === token) {
        failedRecipeLoads.current.add(id);
        if (!cancelled()) setNotice(`Could not restore recipe: ${String(error)}`);
      }
    } finally {
      if (recipeLoadTokens.current.get(id) === token) {
        loadingRecipes.current.delete(id);
        if (loadSucceeded) {
          loadedRecipes.current.add(id);
          setRestoreEpoch((value) => value + 1);
        }
      }
    }
  }
  function openPhoto(id: number) {
    setBatchView(false);
    if (photo.rawPath && photo.id !== id) {
      void persistence.current?.flush(`recipe:${photo.rawPath}`);
      void persistence.current?.flush(`metadata:${photo.rawPath}`);
    }
    setPan({ x: 0, y: 0 });
    panStart.current = null;
    setPanning(false);
    setCurrentId(id);
    setLayerId(0);
    setZoom(1);
    setPixelZoom(false);
    setSplit(50);
    setLibrary(false);
    setFiltersOpen(false);
    const selected = photos.find((item) => item.id === id);
    if (selected?.rawPath && isDesktop()) void restorePhotoRecipe(selected);
  }
  function importFiles(files: FileList | File[]) {
    const existing = new Set(photos.map((p) => p.identity));
    const imported: Photo[] = [];
    let skipped = 0;
    for (const file of Array.from(files)) {
      if (!["image/jpeg", "image/png", "image/webp"].includes(file.type)) {
        skipped++;
        continue;
      }
      const identity = `${file.webkitRelativePath || file.name}:${file.size}:${file.lastModified}`;
      if (existing.has(identity)) continue;
      existing.add(identity);
      const url = URL.createObjectURL(file);
      urls.current.push(url);
      imported.push({
        id: nextPhoto.current++,
        name: file.webkitRelativePath || file.name,
        url,
        identity,
        layers: [initialLayer()],
        rating: 0,
        dirty: false,
        highlights: true,
        chromaticAberration: false,
        lensCorrection: false,
        preset: "",
        kind: file.type.split("/")[1],
      });
    }
    if (imported.length) {
      setPhotos((ps) => [...ps, ...imported]);
      setSelection(imported.map((p) => p.id));
    }
    setNotice(
      `Imported ${imported.length} preview${imported.length === 1 ? "" : "s"}${skipped ? ` · ${skipped} unsupported files skipped (RAW decoding comes later)` : ""}. Images stay on this device.`,
    );
  }
  async function choosePictures(folder: boolean) {
    if (!isDesktop()) {
      (folder ? folderInput : filesInput).current?.click();
      return;
    }
    if (importing.current) return;
    importing.current = true;
    try {
      const result = await chooseNativePreviews(folder);
      if (result) {
        importFiles(result.files);
        const existing = new Set(photos.map((p) => p.identity));
        const rawPhotos: Photo[] = result.raws
          .filter((raw) => !existing.has(raw.identity))
          .map((raw) => ({
            id: nextPhoto.current++,
            name: raw.name,
            identity: raw.identity,
            rawPath: raw.path,
            url: "/raw-placeholder.svg",
            layers: [initialLayer()],
            rating: 0,
            dirty: false,
            highlights: true,
            chromaticAberration: false,
            lensCorrection: false,
            preset: "",
            kind: "raw",
          }));
         if (rawPhotos.length) {
           setPhotos((ps) => [...ps, ...rawPhotos]);
           setSelection(rawPhotos.map((p) => p.id));
           void addLibraryPaths(rawPhotos.map((p) => p.rawPath!)).catch((error) => setNotice(`Could not save library: ${String(error)}`));
           for (const raw of rawPhotos) {
             const metadataRevision = metadataRevisions.current.get(raw.id) ?? 0;
             void libraryMetadata(raw.rawPath!).then((stored) => {
               if ((metadataRevisions.current.get(raw.id) ?? 0) !== metadataRevision) return;
               setPhotos((items) => items.map((item) => item.id === raw.id
                 ? { ...item, rating: stored.rating, labels: stored.labels } : item));
             }).catch(() => {}).finally(() => loadedMetadata.current.add(raw.id));
           }
         }
        let failures = 0;
         for (const [index, raw] of rawPhotos.entries()) {
          if (!libraryRef.current) break;
          setNotice(
            `Reading RAW thumbnail ${index + 1} of ${rawPhotos.length} · ${raw.name}`,
          );
          try {
            const preview = await renderRaw(raw.rawPath!, false, 400, true);
            setPhotos((ps) =>
              ps.map((p) =>
                p.id === raw.id
                  ? {
                      ...p,
                      url: `data:image/png;base64,${preview.png}`,
                      metadata: preview.metadata,
                      previewWidth: preview.width,
                      previewHeight: preview.height,
                    }
                  : p,
              ),
            );
          } catch (error) {
            failures++;
            setPhotos((ps) =>
              ps.map((p) =>
                p.id === raw.id ? { ...p, error: String(error) } : p,
              ),
            );
            setNotice(`Could not read ${raw.name}: ${String(error)}`);
          }
        }
        if (rawPhotos.length && !failures)
          setNotice(
            `Imported ${rawPhotos.length} RAW photographs. Open one in Edit to view its full preview.`,
          );
        if (result.skipped)
          setNotice(
            `Imported previews · ${result.skipped} unsupported, unreadable or oversized entries skipped.`,
          );
      }
    } catch (error) {
      setNotice(`Could not open previews: ${String(error)}`);
    } finally {
      importing.current = false;
    }
  }
  async function exportSelectedBatch() {
    if (!isDesktop()) {
      setNotice("Batch export requires the desktop application.");
      return;
    }
    const selectedPhotos = selection
      .map((id) => photos.find((item) => item.id === id))
      .filter((item): item is Photo => Boolean(item?.rawPath));
    const sources = selectedPhotos.map((item) => item.rawPath!);
    if (!sources.length) {
      setNotice("Select at least one RAW photograph first.");
      return;
    }
    const directory = await chooseOutputDirectory();
    if (!directory) return;
    setBatchDialogOpen(true);
    setBatchBusy(true);
    try {
      const result = await processBatch(
        sources,
        directory,
        selectedPhotos.map((item) => recipeFor(item, exportKind)),
        quality,
        (status) => { setBatchQueue(status); setNotice(`Batch export: ${status.completed}/${status.jobs.length} complete`); },
      );
      setBatchQueue(result);
      setNotice(`Batch export finished: ${result.completed}/${result.jobs.length} complete`);
    } catch (error) {
      setNotice(`Batch export failed: ${String(error)}`);
    } finally {
      setBatchBusy(false);
    }
  }
  async function closeBatchDialog() {
    if (batchBusy) {
      await stopBatch();
      return;
    }
    try { await discardBatch(); } catch { /* the export result remains visible if cleanup fails */ }
    setBatchQueue(null);
    setBatchDialogOpen(false);
  }
  async function continueBatch(retry = false) {
    setBatchBusy(true);
    try {
      if (retry) setBatchQueue(await retryBatch());
      const result = await runBatch();
      setBatchQueue(result);
      setBatchRecoveryError("");
      setNotice(`Batch export: ${result.completed}/${result.jobs.length} complete`);
    } catch (error) {
      setNotice(`Batch export failed: ${String(error)}`);
      void loadBatchStatus().then(setBatchQueue).catch(() => {});
    } finally {
      setBatchBusy(false);
    }
  }
  async function discardBrokenBatch() {
    try {
      setBatchQueue(await discardBatch());
      setBatchRecoveryError("");
      setNotice("Damaged batch recovery data was discarded.");
    } catch (error) {
      setNotice(`Could not discard batch recovery data: ${String(error)}`);
    }
  }
  async function stopBatch() {
    try {
      if (batchBusy) await cancelBatchProcessing();
      else setBatchQueue(await cancelBatch());
      setNotice(batchBusy ? "Cancelling batch after the current processing checkpoint…" : "Batch cancelled.");
    } catch (error) {
      setNotice(`Could not cancel batch: ${String(error)}`);
    }
  }
  function pick(id: number, event: React.MouseEvent) {
    const additive = event.ctrlKey || event.metaKey;
    const range = event.shiftKey;
    const orderedIds = visiblePhotos.map((item) => item.id);
    const anchor = selectionAnchor.current;
    if (range && anchor !== null && orderedIds.includes(anchor)) {
      const start = orderedIds.indexOf(anchor);
      const end = orderedIds.indexOf(id);
      const rangeIds = orderedIds.slice(Math.min(start, end), Math.max(start, end) + 1);
      setSelection((ids) => additive ? [...new Set([...ids, ...rangeIds])] : rangeIds);
      return;
    }
    selectionAnchor.current = id;
    setSelection((ids) => {
      return additive
        ? ids.includes(id) ? ids.filter((item) => item !== id) : [...ids, id]
        : [id];
    });
  }
  function applySelectedMetadata(values: Partial<Pick<Photo, "rating" | "labels">>) {
    if (closing.current) return;
    const ids = new Set(selection);
    const updates = photosRef.current
      .filter((item) => ids.has(item.id))
      .map((item) => ({ ...item, ...values }));
    for (const next of updates) {
      metadataRevisions.current.set(next.id, (metadataRevisions.current.get(next.id) ?? 0) + 1);
      if (next.rawPath) {
        const path = next.rawPath;
        const labels = [...(next.labels ?? [])];
        persistence.current!.schedule(`metadata:${path}`, 0, () => saveLibraryMetadata(path, next.rating, labels));
      }
    }
    const byId = new Map(updates.map((item) => [item.id, item]));
    setPhotos((items) => items.map((item) => byId.get(item.id) ?? item));
  }
  function applyDividerVisual(value: number) {
    const clamped = Math.max(2, Math.min(98, value));
    dividerValue.current = clamped;
    if (dividerFrame.current !== null) return;
    dividerFrame.current = requestAnimationFrame(() => {
      dividerFrame.current = null;
      frame.current?.style.setProperty("--comparison-split", `${dividerValue.current}%`);
    });
  }
  function flushDividerVisual() {
    if (dividerFrame.current !== null) {
      cancelAnimationFrame(dividerFrame.current);
      dividerFrame.current = null;
    }
    frame.current?.style.setProperty("--comparison-split", `${dividerValue.current}%`);
  }
  function moveDivider(clientX: number, rect = dividerRect.current) {
    if (rect?.width) applyDividerVisual(((clientX - rect.left) / rect.width) * 100);
  }
  function maskPoint(x: number, y: number): [number, number] {
    const r = frame.current!.getBoundingClientRect();
    return [
      Math.max(0, Math.min(1, (x - r.left) / r.width)),
      Math.max(0, Math.min(1, (y - r.top) / r.height)),
    ];
  }
  async function addSamPoint(point: [number, number], foreground: boolean) {
    if (!photo.rawPath || !isDesktop()) {
      setNotice("AI masks require a RAW image in the desktop application.");
      return;
    }
    const id = photo.id,
      layer = active.id,
      revision = recipeRevision(photo.id),
      points: [number, number, boolean][] = [
        ...active.points,
        [...point, foreground],
      ];
    const token = ++samRequest.current;
    setProcessing(true);
    setProgress("Generating SAM mask…");
    try {
      const result = await segmentRaw(photo.rawPath, points);
      if (samRequest.current !== token || recipeRevision(id) !== revision
        || !photosRef.current.find((item) => item.id === id)?.layers.some((item) => item.id === layer)) return;
      setPhotos((ps) =>
        ps.map((p) =>
          p.id === id
            ? {
                ...p,
                dirty: true,
                layers: p.layers.map((l) =>
                  l.id === layer ? { ...l, points, aiMask: result.mask } : l,
                ),
              }
            : p,
        ),
      );
    } catch (error) {
      setNotice(String(error));
      setRawError(String(error));
    } finally {
      if (samRequest.current === token) setProcessing(false);
    }
  }
  const filtersActive = !!search.trim() || minimumRating > 0 || !!fileType || !!labelFilter;
  const visiblePhotos = photos.filter(
    (p) =>
      p.name.toLowerCase().includes(search.trim().toLowerCase()) &&
      p.rating >= minimumRating &&
      (!labelFilter || (p.labels ?? []).includes(labelFilter)) &&
      (!fileType || p.kind === fileType),
  );
  return (
    <div className="app">
      <input
        ref={filesInput}
        aria-label="Import picture files"
        hidden
        type="file"
        multiple
        accept="image/jpeg,image/png,image/webp"
        onChange={(e) => {
          if (e.target.files) importFiles(e.target.files);
          e.target.value = "";
        }}
      />
      <input
        ref={folderInput}
        aria-label="Import folder files"
        hidden
        type="file"
        multiple
        onChange={(e) => {
          if (e.target.files) importFiles(e.target.files);
          e.target.value = "";
        }}
      />
      <header className="topbar">
        <div className="brand">
          <Aperture size={22} />
          <span>RawLab</span>
          <small>DESIGN PREVIEW</small>
        </div>
        <nav aria-label="Workspace">
          <button
            className={library ? "tab selected" : "tab"}
             onClick={() => { setBatchView(false); setLibrary(true); }}
          >
            <Grid2X2 size={16} /> Library
          </button>
          <button
             className={!library && !batchView ? "tab selected" : "tab"}
            onClick={() => openPhoto(currentId)}
          >
            <SlidersHorizontal size={16} /> Edit
         </button>
        </nav>
        <IconButton label="Preferences" onClick={() => showSettings(true)}>
          <Settings2 size={18} />
        </IconButton>
      </header>
      <div
         className={`workspace ${library ? "library-workspace" : batchView ? "batch-workspace" : "editor-workspace"}`}
      >
        {library ? (
          <>
            <aside className="import-rail" aria-label="Library imports">
              <IconButton
                label="Import Pictures"
                onClick={() => void choosePictures(false)}
              >
                <ImagePlus size={20} />
              </IconButton>
              <IconButton
                label="Import Folder"
                onClick={() => void choosePictures(true)}
              >
                <FolderOpen size={20} />
              </IconButton>
            </aside>
            <main
              className="library-panel"
              onDragOver={(e) => e.preventDefault()}
              onDrop={(e) => {
                e.preventDefault();
                importFiles(e.dataTransfer.files);
              }}
            >
              <div className="library-header">
                <div>
                  <h1>Library</h1>
                  <p>
                    {photos.length} photograph{photos.length === 1 ? "" : "s"} ·
                    Preview collection
                  </p>
                </div>
                 <div className="library-actions" ref={filterPanel}>
                   <button
                    ref={filterButton}
                    className={`icon filter-toggle ${filtersActive ? "active" : ""}`}
                    aria-label="Filter photographs"
                    title="Filter photographs"
                    aria-expanded={filtersOpen}
                    aria-controls="library-filters"
                    onClick={() => setFiltersOpen(!filtersOpen)}
                  >
                    <SlidersHorizontal size={19} />
                    {filtersActive && <span className="filter-dot" />}
                  </button>
                  {filtersOpen && (
                    <div
                      className="filter-popover"
                      id="library-filters"
                      role="region"
                      aria-label="Photograph filters"
                    >
                      <strong>Filters</strong>
                      <label>
                        Filename
                        <input
                          autoFocus
                          aria-label="Filter filename"
                          placeholder="Contains…"
                          value={search}
                          onChange={(e) => setSearch(e.target.value)}
                        />
                      </label>
                      <label>
                        Minimum rating
                        <select
                          aria-label="Minimum rating"
                          value={minimumRating}
                          onChange={(e) => setMinimumRating(+e.target.value)}
                        >
                          <option value={0}>Any rating</option>
                          {[1, 2, 3, 4, 5].map((n) => (
                            <option key={n} value={n}>
                              {n} star{n === 1 ? "" : "s"} or more
                            </option>
                          ))}
                        </select>
                      </label>
                      <label>
                        File type
                        <select
                          aria-label="File type"
                          value={fileType}
                          onChange={(e) => setFileType(e.target.value)}
                        >
                          <option value="">All types</option>
                          <option value="jpeg">JPEG</option>
                          <option value="png">PNG</option>
                          <option value="webp">WebP</option>
                          <option value="demo">Demo illustration</option>
                          <option value="raw">RAW</option>
                        </select>
                      </label>
                      <label>
                        Color label
                        <select aria-label="Color label" value={labelFilter} onChange={(e) => setLabelFilter(e.target.value)}>
                          <option value="">Any label</option>
                          {["red", "yellow", "green", "blue", "purple"].map((label) => <option key={label} value={label}>{label}</option>)}
                        </select>
                      </label>
                      <button
                        className="subtle wide"
                        onClick={() => {
                          setSearch("");
                           setMinimumRating(0);
                           setFileType("");
                           setLabelFilter("");
                         }}
                      >
                        Reset filters
                      </button>
                    </div>
                  )}
                </div>
              </div>
              {false && batchRecoveryError && (
                <section className="batch-queue" aria-label="Batch recovery error">
                  <div className="batch-summary">
                    <div><strong>Batch recovery needs attention</strong><span>{batchRecoveryError}</span></div>
                    <button className="button" onClick={() => void discardBrokenBatch()}>Discard recovery</button>
                  </div>
                </section>
              )}
              {false && batchQueue?.snapshot && batchQueue!.jobs.length > 0 && (
                <section className="batch-queue" aria-label="Batch queue">
                  <div className="batch-summary">
                    <div><strong>Batch queue</strong><span>{batchQueue!.completed}/{batchQueue!.jobs.length} complete</span>{batchBusy && <span>{progress}</span>}</div>
                    <div className="batch-actions">
                      {batchQueue!.remaining > 0 && !batchBusy && <button className="button" onClick={() => void continueBatch(false)}>Resume</button>}
                      {batchQueue!.jobs.some((job) => job.state === 3 || job.state === 4) && !batchBusy
                        && <button className="button" onClick={() => void continueBatch(true)}>Retry failed</button>}
                      {batchQueue!.remaining > 0 && <button className="button secondary" onClick={() => void stopBatch()}>{batchBusy ? "Cancel running" : "Cancel queue"}</button>}
                    </div>
                  </div>
                  <div className="batch-jobs">
                    {batchQueue!.jobs.map((job) => (
                      <div className="batch-job" key={`${job.source}:${job.output}`}>
                        <span className={`batch-state state-${job.state}`}>{["Pending","Running","Completed","Failed","Cancelled"][job.state]}</span>
                        <span title={job.source}>{job.source.split(/[\\/]/).pop()}</span>
                        <span className="batch-output" title={job.output}>{job.output.split(/[\\/]/).pop()}</span>
                        {job.message && <span className="batch-error">{job.message}</span>}
                      </div>
                    ))}
                  </div>
                </section>
              )}
              <div className="library-grid">
                {visiblePhotos.map((p) => (
                  <button
                    key={p.id}
                    className={`photo-card ${selection.includes(p.id) ? "selected" : ""}`}
                    aria-label={`Select ${p.name}`}
                    aria-pressed={selection.includes(p.id)}
                     onClick={(e) => pick(p.id, e)}
                    onDoubleClick={() => openPhoto(p.id)}
                    onKeyDown={(e) => {
                      if (e.key === "Enter") {
                        e.preventDefault();
                        openPhoto(p.id);
                      }
                    }}
                  >
                    <img src={p.url} alt="" loading="lazy" />
                    <span className="card-name">{p.name}</span>
                    {(p.labels?.length || p.rating > 0) ? <span className="thumb-metadata">{p.rating ? `${"*".repeat(p.rating)} ` : ""}{(p.labels ?? []).join(" / ")}</span> : null}
                    {p.error && (
                      <span className="card-error" title={p.error}>
                        Preview unavailable
                      </span>
                    )}
                    {selection.includes(p.id) && (
                      <i className="selection-check">
                        <Check size={14} />
                      </i>
                    )}
                  </button>
                ))}
                {!visiblePhotos.length && (
                  <p className="empty-state">
                    No photographs match this filter.
                  </p>
                )}
              </div>
              <div className="library-footer">
                <span>
                  {selection.length} selected{" "}
                  <span className="muted">
                    · Double-click a photograph to edit
                  </span>
                </span>
              </div>
            </main>
          </>
        ) : batchView ? (
          <section className="batch-panel" role="region" aria-label="Batch queue">
            <div className="batch-view-header">
              <div><h1>Batch queue</h1><p>Recoverable exports and processing jobs</p></div>
              <div className="batch-actions">
                {batchQueue?.remaining && !batchBusy && <button className="button" onClick={() => void continueBatch(false)}>Resume</button>}
                {batchQueue?.jobs.some((job) => job.state === 3 || job.state === 4) && !batchBusy && <button className="button" onClick={() => void continueBatch(true)}>Retry failed</button>}
                {batchQueue?.remaining ? <button className="button secondary" onClick={() => void stopBatch()}>{batchBusy ? "Cancel running" : "Cancel queue"}</button> : null}
                {batchQueue?.snapshot && !batchBusy && <button className="button secondary" onClick={() => void discardBrokenBatch()}>Clear queue</button>}
              </div>
            </div>
            {batchRecoveryError && <div className="batch-alert" role="region" aria-label="Batch recovery error"><strong>Recovery unavailable</strong><span>{batchRecoveryError}</span><button className="button" onClick={() => void discardBrokenBatch()}>Discard recovery</button></div>}
            {batchQueue && batchQueue.snapshot ? <>
              <div className="batch-progress" role="status"><strong>{batchQueue.completed}/{batchQueue.jobs.length} complete</strong>{batchBusy && <span>{progress}</span>}</div>
              <div className="batch-jobs batch-jobs-large">{batchQueue.jobs.map((job) => <div className="batch-job" key={`${job.source}:${job.output}`}>
                <span className={`batch-state state-${job.state}`}>{["Pending","Running","Completed","Failed","Cancelled"][job.state]}</span><span title={job.source}>{job.source.split(/[\\/]/).pop()}</span><span className="batch-output" title={job.output}>{job.output.split(/[\\/]/).pop()}</span>{job.message && <span className="batch-error">{job.message}</span>}
              </div>)}</div>
            </> : !batchRecoveryError && <div className="batch-empty"><Layers size={28}/><strong>No saved batch queue</strong><span>Select photographs in the Library and choose Batch export.</span></div>}
          </section>
        ) : (
          <>
            <main className="main">
              <div className="canvas-toolbar">
                <div className="breadcrumb">
                  <IconButton
                    label="Back to library"
                    onClick={() => setLibrary(true)}
                  >
                    <ArrowLeft size={17} />
                  </IconButton>
                  <span>{photo.name}</span>
                </div>
                <div className="tools">
                  <IconButton
                    label="Before and after"
                    pressed={compare}
                    onClick={() => setCompare(!compare)}
                  >
                    <Columns2 size={18} />
                  </IconButton>
                  <span className="separator" />
                   <IconButton
                     label="Zoom out"
                     onClick={() => { setPixelZoom(false); setZoom((z) => Math.max(0.5, z - 0.25)); }}
                  >
                    <Minus size={17} />
                  </IconButton>
                   <button className="zoom" onClick={() => { setPixelZoom(false); setZoom(1); }}>
                     {pixelZoom ? "100%" : zoom === 1 ? "Fit" : `${Math.round(zoom * 100)}%`}
                   </button>
                   <IconButton
                     label="Zoom in"
                     onClick={() => { setPixelZoom(false); setZoom((z) => Math.min(3, z + 0.25)); }}
                  >
                    <Plus size={17} />
                  </IconButton>
                   <IconButton label="Fit image" onClick={() => { setPixelZoom(false); setZoom(1); setPan({ x: 0, y: 0 }); }}>
                     <Maximize size={17} />
                   </IconButton>
                   <IconButton
                     label="1:1"
                     pressed={pixelZoom}
                      onClick={() => { setPixelZoom(true); setZoom(1); setPan({ x: 0, y: 0 }); }}
                   >
                     <span className="zoom-one-to-one">1:1</span>
                   </IconButton>
                </div>
              </div>
              <div
                  className={`canvas ${zoom > 1 || pixelZoom ? "can-pan" : ""} ${panning ? "panning" : ""} ${editingMask && active.id !== 0 ? "mask-pointing" : ""}`}
                 ref={canvas}
                 onWheel={handlePreviewWheel}
                 onPointerDown={(e) => {
                  if (
                    e.button !== 0 ||
                     (zoom <= 1 && !pixelZoom) ||
                    (e.target as HTMLElement).closest("button,input")
                  )
                    return;
                  e.preventDefault();
                  e.currentTarget.setPointerCapture(e.pointerId);
                  panPosition.current = pan;
                  panStart.current = {
                    x: e.clientX,
                    y: e.clientY,
                    originX: pan.x,
                    originY: pan.y,
                  };
                  setPanning(true);
                }}
                onPointerMove={(e) => {
                  const start = panStart.current;
                  if (start)
                    applyPanVisual(boundPan(
                      start.originX + e.clientX - start.x,
                      start.originY + e.clientY - start.y,
                    ));
                }}
                onPointerUp={(e) => {
                  panStart.current = null;
                  setPanning(false);
                  flushPanVisual();
                  setPan(panPosition.current);
                  if (e.currentTarget.hasPointerCapture(e.pointerId))
                    e.currentTarget.releasePointerCapture(e.pointerId);
                }}
                onPointerCancel={() => {
                  panStart.current = null;
                  setPanning(false);
                  flushPanVisual();
                  setPan(panPosition.current);
                }}
                onLostPointerCapture={() => {
                  panStart.current = null;
                  setPanning(false);
                  flushPanVisual();
                  setPan(panPosition.current);
                }}
              >
                <div
                  ref={frame}
                  className={`photo-frame ${pixelZoom ? "pixel-zoom" : ""}`}
                  tabIndex={0}
                  onPointerDown={(e) => {
                    if (
                      !editingMask ||
                      active.id === 0 ||
                      spacePan ||
                      processing ||
                      e.button !== 0 ||
                      (e.target as HTMLElement).closest("button")
                    )
                      return;
                    e.stopPropagation();
                    e.preventDefault();
                    e.currentTarget.focus();
                    const p = maskPoint(e.clientX, e.clientY);
                    if (maskTool === "AI") {
                      void addSamPoint(p, !e.shiftKey);
                      return;
                    }
                    e.currentTarget.setPointerCapture(e.pointerId);
                    mark.current = { tool: maskTool, points: [p] };
                    setLiveMark({ ...mark.current });
                  }}
                   onPointerMove={(e) => {
                     if (mark.current) {
                       e.stopPropagation();
                       const p = maskPoint(e.clientX, e.clientY);
                       mark.current.points =
                        mark.current.tool === "Brush" ||
                        mark.current.tool === "Erase"
                          ? [...mark.current.points, p]
                          : [mark.current.points[0], p];
                       if (mark.current.tool === "Brush" || mark.current.tool === "Erase") {
                         liveStroke.current?.setAttribute(
                           "points",
                           mark.current.points.map((point) => `${point[0] * 1000},${(point[1] * 1000) / imageRatio}`).join(" "),
                         );
                       } else setLiveMark({ ...mark.current });
                    }
                  }}
                  onPointerUp={(e) => {
                    if (mark.current) {
                      e.stopPropagation();
                      change({ marks: [...active.marks, mark.current] });
                      mark.current = null;
                      setLiveMark(null);
                      if (e.currentTarget.hasPointerCapture(e.pointerId))
                        e.currentTarget.releasePointerCapture(e.pointerId);
                    }
                  }}
                  onPointerCancel={() => {
                    mark.current = null;
                    setLiveMark(null);
                  }}
                  style={{
                    "--comparison-split": `${split}%`,
                    transform: `translate(${pan.x}px, ${pan.y}px) scale(${zoom})`,
                     width: pixelZoom && rawView?.id === photo.id ? rawView.sourceWidth
                       : pixelZoom ? imageSize.width : Math.min(canvasSize.width, canvasSize.height * imageRatio),
                  } as React.CSSProperties}
                >
                  <img
                    className="main-photo"
                    draggable={false}
                     onLoad={(e) => {
                       setImageRatio(e.currentTarget.naturalWidth / e.currentTarget.naturalHeight);
                       setImageSize({ width: e.currentTarget.naturalWidth, height: e.currentTarget.naturalHeight });
                     }}
                     src={displayUrl}
                     alt={photo.name}
                   />
                   {pixelZoom && detailView && (
                     <img
                       className="detail-tile"
                       src={detailView.url}
                       alt="Source-resolution detail"
                       draggable={false}
                       style={{left: detailView.x, top: detailView.y, width: detailView.width, height: detailView.height}}
                     />
                   )}
                  {compare && (
                    <>
                      <span className="before-tag">Before</span>
                      <span className="after-tag">
                        {rendered ? "After" : "After · no processing yet"}
                      </span>
                      <div
                        className="comparison-shade"
                        style={{
                          width: "100%",
                          clipPath: "inset(0 calc(100% - var(--comparison-split)) 0 0)",
                        }}
                      >
                        <img
                          src={rendered?.before ?? originalUrl}
                          alt="Original before processing"
                          draggable={false}
                        />
                        {pixelZoom && detailView?.beforeUrl && (
                          <img
                            className="detail-tile"
                            src={detailView.beforeUrl}
                            alt="Original source-resolution detail"
                            draggable={false}
                            style={{left: detailView.x, top: detailView.y, width: detailView.width, height: detailView.height}}
                          />
                        )}
                      </div>
                      <button
                        className="split-line"
                        style={{ left: "var(--comparison-split)" }}
                        role="separator"
                        aria-label="Before after divider"
                        aria-orientation="vertical"
                        aria-valuemin={2}
                        aria-valuemax={98}
                        aria-valuenow={Math.round(split)}
                        onPointerDown={(e) => {
                          dragging.current = true;
                          dividerRect.current = frame.current?.getBoundingClientRect() ?? null;
                          e.currentTarget.setPointerCapture(e.pointerId);
                          moveDivider(e.clientX);
                        }}
                        onPointerMove={(e) => {
                          if (dragging.current) moveDivider(e.clientX);
                        }}
                        onPointerUp={(e) => {
                          dragging.current = false;
                          flushDividerVisual();
                          setSplit(dividerValue.current);
                          dividerRect.current = null;
                          e.currentTarget.releasePointerCapture(e.pointerId);
                        }}
                        onPointerCancel={() => {
                          dragging.current = false;
                          flushDividerVisual();
                          setSplit(dividerValue.current);
                          dividerRect.current = null;
                        }}
                        onLostPointerCapture={() => {
                          dragging.current = false;
                          flushDividerVisual();
                          setSplit(dividerValue.current);
                          dividerRect.current = null;
                        }}
                        onKeyDown={(e) => {
                          if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
                            e.preventDefault();
                            const next = split + (e.key === "ArrowLeft" ? -2 : 2);
                            applyDividerVisual(next);
                            flushDividerVisual();
                            setSplit(dividerValue.current);
                          }
                        }}
                      >
                        <Columns2 size={17} />
                      </button>
                    </>
                  )}
                  {editingMask && active.id !== 0 && active.showMask && (
                    <svg
                      className="mask-overlay"
                      viewBox={`0 0 1000 ${1000 / imageRatio}`}
                      preserveAspectRatio="none"
                    >
                      <defs>
                        <mask
                          id="paint-mask"
                          maskUnits="userSpaceOnUse"
                          x="0"
                          y="0"
                          width="1000"
                          height={1000 / imageRatio}
                        >
                          <rect
                            width="1000"
                            height={1000 / imageRatio}
                            fill="black"
                          />
                          {active.aiMask && (
                            <image
                              href={`data:image/png;base64,${active.aiMask}`}
                              width="1000"
                              height={1000 / imageRatio}
                            />
                          )}
                          {[
                            ...active.marks,
                            ...(liveMark ? [liveMark] : []),
                          ].map((m, i) => {
                            const a = m.points[0],
                              b = m.points[m.points.length - 1];
                            if (m.tool === "Ellipse")
                              return (
                                <ellipse
                                  key={i}
                                  cx={(a[0] + b[0]) * 500}
                                  cy={((a[1] + b[1]) * 500) / imageRatio}
                                  rx={Math.abs(a[0] - b[0]) * 500}
                                  ry={
                                    (Math.abs(a[1] - b[1]) * 500) / imageRatio
                                  }
                                  fill="white"
                                />
                              );
                            if (m.tool === "Gradient")
                              return (
                                <g key={i}>
                                  <linearGradient
                                    id={`gradient-${i}`}
                                    gradientUnits="userSpaceOnUse"
                                    x1={a[0] * 1000}
                                    y1={(a[1] * 1000) / imageRatio}
                                    x2={b[0] * 1000}
                                    y2={(b[1] * 1000) / imageRatio}
                                  >
                                    <stop stopColor="black" />
                                    <stop offset="1" stopColor="white" />
                                  </linearGradient>
                                  <rect
                                    width="1000"
                                    height={1000 / imageRatio}
                                    fill={`url(#gradient-${i})`}
                                  />
                                </g>
                              );
                            return (
                               <polyline
                                 ref={(element) => {
                                   if (i === active.marks.length) liveStroke.current = element;
                                 }}
                                 key={i}
                                points={(m.points.length === 1
                                  ? [m.points[0], m.points[0]]
                                  : m.points
                                )
                                  .map(
                                    (p) =>
                                      `${p[0] * 1000},${(p[1] * 1000) / imageRatio}`,
                                  )
                                  .join(" ")}
                                fill="none"
                                stroke={m.tool === "Erase" ? "black" : "white"}
                                strokeWidth={
                                  0.06 * Math.min(1000, 1000 / imageRatio)
                                }
                                strokeLinecap="round"
                                strokeLinejoin="round"
                              />
                            );
                          })}
                        </mask>
                        <mask
                          id="paint-mask-inverted"
                          maskUnits="userSpaceOnUse"
                          x="0"
                          y="0"
                          width="1000"
                          height={1000 / imageRatio}
                        >
                          <rect
                            width="1000"
                            height={1000 / imageRatio}
                            fill="white"
                          />
                          <rect
                            width="1000"
                            height={1000 / imageRatio}
                            fill="black"
                            mask="url(#paint-mask)"
                          />
                        </mask>
                      </defs>
                      <rect
                        width="1000"
                        height={1000 / imageRatio}
                         fill="#e34f5f"
                        opacity=".4"
                        mask={
                          active.inverted
                            ? "url(#paint-mask-inverted)"
                            : "url(#paint-mask)"
                        }
                      />
                    </svg>
                  )}
                </div>
                {(processing || rawBusy || rawError) && (
                  <div className="raw-progress" role="status">
                    {processing
                      ? progress
                      : rawBusy
                        ? `Rendering ${quality ? "High Quality" : "Fast"} RAW preview…`
                        : rawError}
                    {processing && (
                      <button
                        className="subtle"
                        onClick={() => {
                          setProgress("Cancelling at the next checkpoint…");
                          void cancelProcessing().catch((error) =>
                            setNotice(String(error)),
                          );
                        }}
                      >
                        Cancel
                      </button>
                    )}
                  </div>
                )}
                {(photo.dirty || (photo.rawPath && !processedCurrent)) &&
                  !processing && (
                    <div className="update-pill">
                      <span className="pending-dot" /> Changes pending
                      <button onClick={() => void updateProcessing()}>
                        <RotateCcw size={13} /> Update
                      </button>
                    </div>
                  )}
              </div>
              <div className="photo-info">
                {photo.metadata ? (
                  <span
                    title={[
                      photo.metadata.make,
                      photo.metadata.model,
                      photo.metadata.lens,
                    ]
                      .filter(Boolean)
                      .join(" · ")}
                  >
                    {photo.metadata.make} {photo.metadata.model} ·{" "}
                    {photo.metadata.rawWidth} × {photo.metadata.rawHeight}
                    {photo.metadata.iso != null &&
                      ` · ISO ${photo.metadata.iso}`}
                    {photo.metadata.aperture != null &&
                      ` · f/${photo.metadata.aperture.toFixed(1)}`}
                    {photo.metadata.exposureBiasEv != null &&
                      ` · ${photo.metadata.exposureBiasEv > 0 ? "+" : ""}${photo.metadata.exposureBiasEv.toFixed(1)} EV`}
                    {photo.metadata.exposureSeconds != null &&
                      photo.metadata.exposureSeconds > 0 &&
                      ` · ${photo.metadata.exposureSeconds < 1 ? `1/${Math.round(1 / photo.metadata.exposureSeconds)}` : photo.metadata.exposureSeconds.toFixed(1)} s`}
                    {photo.metadata.focalLengthMm != null &&
                      ` · ${Math.round(photo.metadata.focalLengthMm)} mm`}
                  </span>
                ) : (
                  <span>Preview only · Zoom is relative to Fit</span>
                )}
                <div className="photo-info-controls">
                  {photo.rawPath && (
                    <IconButton
                      label="Refresh RAW preview"
                      onClick={() => setRawRefresh((n) => n + 1)}
                    >
                      <RotateCcw size={14} />
                    </IconButton>
                  )}
                  <button
                    type="button"
                    role="switch"
                    aria-checked={quality}
                    className={`quality-toggle ${quality ? "on" : ""}`}
                    onClick={() => setQuality((enabled) => !enabled)}
                  >
                    <span className="quality-track"><span /></span>
                    <span>High Quality</span>
                  </button>
                  <div className="rating" aria-label="Rating">
                    {Array.from({ length: 5 }, (_, i) => (
                      <button
                        key={i}
                        aria-label={`Rate ${i + 1} stars`}
                        onClick={() =>
                          patchPhoto({
                            rating: i + 1 === photo.rating ? 0 : i + 1,
                          })
                        }
                      >
                        <Star
                          size={14}
                          fill={i < photo.rating ? "currentColor" : "none"}
                        />
                      </button>
                    ))}
                  </div>
                  <div className="color-labels" aria-label="Color labels">
                    {(["red", "yellow", "green", "blue", "purple"] as const).map((label) => {
                      const active = (photo.labels ?? []).includes(label);
                      return (
                        <button
                          key={label}
                          type="button"
                          className={`color-label color-label-${label} ${active ? "active" : ""}`}
                          aria-label={`${active ? "Remove" : "Add"} ${label} color label`}
                          aria-pressed={active}
                          onClick={() => patchPhoto({ labels: active ? (photo.labels ?? []).filter((item) => item !== label) : [...(photo.labels ?? []), label] })}
                        />
                      );
                    })}
                  </div>
                </div>
              </div>
              <div className="filmstrip">
                {photos.map((p) => (
                  <button
                    key={p.id}
                   className={`thumb ${photo.id === p.id ? "selected" : ""}`}
                    aria-label={`Edit ${p.name}`}
                    aria-current={photo.id === p.id ? "true" : undefined}
                    aria-description={selection.includes(p.id) ? "Selected for export" : undefined}
                    onClick={() => openPhoto(p.id)}
                  >
                    <img src={p.url} alt="" />
                    <span>{p.name}</span>
                    {selection.includes(p.id) && <i className="filmstrip-selection-check" aria-label="Selected for export"><Check size={12} /></i>}
                  </button>
                ))}
              </div>
            </main>
            <aside className="edit-panel" aria-label="Adjustments">
              <div className="edit-heading">
                <h2>Adjustments</h2>
                <div className="adjustment-actions" ref={presetPanel}>
                  <button
                    ref={presetButton}
                    className={`icon ${presetsOpen ? "active" : ""}`}
                    aria-label="Presets"
                    title="Presets"
                    aria-expanded={presetsOpen}
                    aria-controls="preset-popover"
                    onClick={() => setPresetsOpen(!presetsOpen)}
                  >
                    <Star size={17} />
                  </button>
                  {presetsOpen && (
                    <div
                      className="preset-popover"
                      id="preset-popover"
                      role="region"
                      aria-label="Saved presets"
                    >
                      <strong>Presets</strong>
                      <div className="preset-list">
                        {presets.length ? (
                          presets.map((p) => (
                            <button
                              className={`preset-choice ${photo.preset === p.name ? "selected" : ""}`}
                              key={p.name}
                              aria-pressed={photo.preset === p.name}
                              onClick={() => {
                                const apply = (recipe: { layers: unknown[]; highlights?: boolean; sharpenModelId?: string } | null) => {
                                  const source = recipe && Array.isArray(recipe.layers) ? recipe : p;
                                  patchPhoto({
                                    layers: source.layers.map((layer, index) => {
                                      const storedLayer = layer as Partial<Layer>;
                                      return { ...initialLayer(), id: index, ...storedLayer, marks: Array.isArray(storedLayer.marks) ? storedLayer.marks : [], points: Array.isArray(storedLayer.points) ? storedLayer.points : [] };
                                    }),
                                     highlights: source.highlights !== false,
                                     sharpenModelId: source.sharpenModelId,
                                     preset: p.name,
                                     dirty: true,
                                   });
                                  setLayerId(0);
                                  setEditingMask(false);
                                  setPresetsOpen(false);
                                };
                                if (isDesktop()) void loadPreset(p.name, photo.previewWidth ?? 1024, photo.previewHeight ?? 1024).then((result) => apply(result.recipe)).catch(() => apply(null));
                                else apply(p);
                              }}
                            >
                              <span>{p.name}</span>
                              {photo.preset === p.name && <Check size={14} />}
                            </button>
                          ))
                        ) : (
                          <p className="hint">No saved presets yet.</p>
                        )}
                      </div>
                      <button
                        className="subtle wide"
                        onClick={() => {
                          setPresetName(photo.preset);
                          setPresetsOpen(false);
                          setSavingPreset(true);
                        }}
                      >
                        Save current settings…
                      </button>
                      <button
                        className="subtle wide"
                        disabled={!photo.preset}
                        onClick={() => {
                          const name = photo.preset;
                          setPresets((ps) => ps.filter((p) => p.name !== name));
                           if (isDesktop()) persistence.current!.schedule(`preset:${name}`, 0, () => deletePreset(name));
                          setPhotos((ps) =>
                            ps.map((p) =>
                              p.preset === name ? { ...p, preset: "" } : p,
                            ),
                          );
                          setNotice(
                            `Preset “${name}” deleted. Image settings kept.`,
                          );
                        }}
                      >
                        <Trash2 size={14} /> Delete selected preset
                      </button>
                    </div>
                  )}
                  <IconButton
                    label="Reset settings"
                    onClick={() => {
                      patchPhoto({
                        layers: [initialLayer()],
                        dirty: false,
                        highlights: true,
                        chromaticAberration: false,
                        lensCorrection: false,
                        preset: "",
                      });
                      setLayerId(0);
                    }}
                  >
                    <RotateCcw size={17} />
                  </IconButton>
                </div>
              </div>
              <div className="edit-scroll">
                <Section title="Layers" icon={<Layers size={16} />}>
                  <div className="layer-list">
                    {photo.layers.map((layer) => (
                      <div
                        key={layer.id}
                        className={`layer ${active.id === layer.id ? "selected" : ""}`}
                      >
                        <button
                          className="layer-select"
                          onClick={() => setLayerId(layer.id)}
                        >
                          <Aperture size={17} />
                          <span>
                            {layer.name}
                            <small>
                              {layer.id === 0
                                ? "Entire photograph"
                                : "Local adjustment"}
                            </small>
                          </span>
                        </button>
                        <IconButton
                          label={
                            layer.enabled
                              ? `Disable ${layer.name}`
                              : `Enable ${layer.name}`
                          }
                          onClick={() =>
                            patchPhoto({
                              layers: photo.layers.map((l) =>
                                l.id === layer.id
                                  ? { ...l, enabled: !l.enabled }
                                  : l,
                              ),
                              dirty: true,
                            })
                          }
                        >
                          {layer.enabled ? (
                            <Eye size={16} />
                          ) : (
                            <EyeOff size={16} />
                          )}
                        </IconButton>
                      </div>
                    ))}
                  </div>
                  <div className="layer-actions">
                    <button
                      className="subtle"
                      onClick={() => {
                        const id = nextLayer.current++;
                        patchPhoto({
                          layers: [
                            ...photo.layers,
                            { ...initialLayer(), id, name: `Mask ${id}` },
                          ],
                          dirty: true,
                        });
                        setLayerId(id);
                      }}
                    >
                      <Plus size={14} /> Add mask
                    </button>
                    <button
                      className="subtle"
                      disabled={active.id === 0}
                      onClick={() => setEditingMask(true)}
                    >
                      <Sparkles size={14} /> Edit Mask
                    </button>
                    <button
                      className="icon"
                      aria-label="Delete selected mask"
                      title="Delete selected mask"
                      disabled={active.id === 0}
                      onClick={() => {
                        patchPhoto({
                          layers: photo.layers.filter(
                            (l) => l.id !== active.id,
                          ),
                          dirty: true,
                        });
                        setLayerId(0);
                      }}
                    >
                      <Trash2 size={15} />
                    </button>
                  </div>
                </Section>
                {editingMask && active.id !== 0 && (
                  <section
                    className="mask-edit-panel"
                    aria-label="Mask editing"
                  >
                    <div className="mask-edit-heading">
                      <strong>Editing {active.name}</strong>
                      <button
                        className="subtle"
                        onClick={() => setEditingMask(false)}
                      >
                        <Check size={14} /> Done
                      </button>
                    </div>
                    <div
                      className="mask-tools"
                      role="group"
                      aria-label="Mask tools"
                    >
                      {["AI", "Brush", "Erase", "Gradient", "Ellipse"].map(
                        (tool) => (
                          <button
                            className={maskTool === tool ? "chosen" : ""}
                            aria-pressed={maskTool === tool}
                            key={tool}
                            onClick={() => {
                              setMaskTool(tool);
                              setNotice(
                                "Draw on the image. Hold Space to pan; Shift-click adds a negative AI point.",
                              );
                            }}
                          >
                            {tool}
                          </button>
                        ),
                      )}
                    </div>
                    <p className="hint">
                      {maskTool} tool selected · Hold Space to pan. AI uses the
                      SAM model configured in Settings.
                      {correctedGeometry && " Optical corrections resume when mask editing ends."}
                    </p>
                    <Slider
                      label="Feather"
                      value={active.feather}
                      change={(feather) => change({ feather })}
                    />
                    <Slider
                      label="Blur"
                      value={active.blur}
                      change={(blur) => change({ blur })}
                    />
                    <label className="check">
                      <input
                        type="checkbox"
                        checked={active.inverted}
                        onChange={(e) => change({ inverted: e.target.checked })}
                      />{" "}
                      Invert mask
                    </label>
                    <label className="check">
                      <input
                        type="checkbox"
                        checked={active.showMask}
                        onChange={(e) => change({ showMask: e.target.checked })}
                      />{" "}
                      Show mask
                    </label>
                  </section>
                )}
                <Section title="Denoise" icon={<SlidersHorizontal size={16} />}>
                  <label className="field-label">Denoise model</label>
                  <select
                    aria-label="Denoise model"
                    value={
                      isDesktop()
                        ? active.modelId ||
                          catalog.denoise.find((m) => m.available)?.id ||
                          ""
                        : active.modelId || "demo"
                    }
                    onChange={(e) => change({ modelId: e.target.value })}
                  >
                    {isDesktop() ? (
                      <>
                        <option value="">Select configured model</option>
                        {catalog.denoise.map((m) => (
                          <option key={m.id} value={m.id}>
                            {m.name}
                            {m.available ? "" : " (missing)"}
                          </option>
                        ))}
                      </>
                    ) : (
                      <option value="demo">RawNIND · Bayer</option>
                    )}
                  </select>
                  <Slider
                    label="Blend with denoised"
                    value={active.blend}
                    change={(blend) => change({ blend })}
                  />
                  <div className="field-label">Preserve highlights</div>
                  <div
                    className="segmented"
                    role="group"
                    aria-label="Preserve highlights"
                  >
                    {[false, true].map((enabled) => (
                      <button
                        key={String(enabled)}
                        aria-pressed={photo.highlights === enabled}
                        className={photo.highlights === enabled ? "chosen" : ""}
                        onClick={() =>
                          patchPhoto({ highlights: enabled, dirty: true })
                        }
                      >
                        {enabled ? "On" : "Off"}
                      </button>
                    ))}
                  </div>
                </Section>
                <Section title="Sharpen" icon={<Sparkles size={16} />}>
                  <fieldset className="output-dependent" disabled={exportKind === "cfa"}>
                  {exportKind === "cfa" && <p className="hint">Available with Linear RGB output.</p>}
                  {isDesktop() && active.sharpen === "AI" && (
                    <select
                      aria-label="AI sharpening model"
                      value={photo.sharpenModelId ?? defaultSharpenModelId}
                      onChange={(e) => {
                        patchPhoto({ sharpenModelId: e.target.value, dirty: true });
                      }}
                    >
                      <option value="">Select model</option>
                      {catalog.sharpen.map((m) => (
                        <option key={m.id} value={m.id}>
                          {m.name}
                          {m.available ? "" : " (missing)"}
                        </option>
                      ))}
                    </select>
                  )}
                  <div className="field-label">Mode</div>
                  <div
                    className="segmented"
                    role="group"
                    aria-label="Sharpening mode"
                  >
                    {["Off", "Normal", "AI"].map((mode) => (
                      <button
                        key={mode}
                        aria-pressed={active.sharpen === mode}
                        className={active.sharpen === mode ? "chosen" : ""}
                        onClick={() => change({ sharpen: mode })}
                      >
                        {mode}
                      </button>
                    ))}
                  </div>
                  <Slider
                    label="Strength"
                    value={active.strength}
                    disabled={active.sharpen === "Off"}
                    change={(strength) => change({ strength })}
                  />
                  </fieldset>
                </Section>
                <Section title="Optics" icon={<Aperture size={16} />}>
                  <fieldset className="output-dependent" disabled={exportKind === "cfa"}>
                    {exportKind === "cfa" && <p className="hint">Available with Linear RGB output.</p>}
                    {([
                      ["Chromatic aberration", "chromaticAberration"],
                      ["Lens correction", "lensCorrection"],
                    ] as const).map(([label, key]) => {
                      const available = !isDesktop() || (key === "chromaticAberration"
                        ? photo.metadata?.optics?.chromaticAberrationAvailable
                        : photo.metadata?.optics?.lensCorrectionAvailable) === true;
                      return <div className="optics-control" key={key}>
                      <div className="field-label">{label}</div>
                      <div className="segmented" role="group" aria-label={label}>
                        {[false, true].map((enabled) => <button key={String(enabled)}
                          disabled={enabled && !available}
                          aria-pressed={photo[key] === enabled}
                          className={photo[key] === enabled ? "chosen" : ""}
                          onClick={() => changeOptics(key, enabled)}>
                          {enabled ? "On" : "Off"}
                        </button>)}
                      </div>
                    </div>})}
                    {isDesktop() && exportKind === "linear-rgb" &&
                      <p className="hint optics-status">{photo.metadata?.optics?.message ?? "Checking lens profile..."}</p>}
                  </fieldset>
                </Section>
              </div>
              <div className="export-footer">
                <div className="export-format">
                  <span>Export format</span>
                  <div className="segmented" role="group" aria-label="Export format">
                    <button
                      className={exportKind === "cfa" ? "chosen" : ""}
                      aria-pressed={exportKind === "cfa"}
                      onClick={() => { setExportKind("cfa"); patchPhoto({ dirty: true }); }}
                    >Raw</button>
                    <button
                      className={exportKind === "linear-rgb" ? "chosen" : ""}
                      aria-pressed={exportKind === "linear-rgb"}
                      onClick={() => { setExportKind("linear-rgb"); patchPhoto({ dirty: true }); }}
                    >Linear RGB</button>
                  </div>
                </div>
                <button
                  className="primary wide"
                  disabled={processing || rawBusy}
                  onClick={() => externalSession ? void updateProcessing(true, exportKind) : selectedRawCount > 1 ? void exportSelectedBatch() : void updateProcessing(true, exportKind)}
                >
                  <ArrowDownToLine size={16} /> {externalSession?.mode === "batch" ? `Export batch to Darktable (${externalSession.sources.length})` : externalSession ? "Export and return to Darktable" : selectedRawCount > 1 ? `Export selected (${selectedRawCount})` : "Export DNG"}
                </button>
              </div>
            </aside>
          </>
        )}
      </div>
      {batchDialogOpen && batchQueue && (
        <div className="modal-backdrop" role="presentation">
          <section className="export-dialog" role="dialog" aria-modal="true" aria-labelledby="export-dialog-title">
            <div className="export-dialog-header">
              <div><h2 id="export-dialog-title">Exporting {batchQueue.jobs.length} photographs</h2><p>{batchBusy ? progress || "Preparing export…" : `${batchQueue.completed} complete`}</p></div>
              <button className="subtle" onClick={() => void closeBatchDialog()}>{batchBusy ? "Cancel" : "Close"}</button>
            </div>
            <div className="export-dialog-jobs">
              {batchQueue.jobs.map((job) => <div className="export-dialog-job" key={`${job.source}:${job.output}`}>
                <span className={`batch-state state-${job.state}`}>{["Pending","Running","Completed","Failed","Cancelled"][job.state]}</span>
                <span>{job.source.split(/[\\/]/).pop()}</span>
                {job.message && <small>{job.message}</small>}
              </div>)}
            </div>
            {!batchBusy && batchQueue.remaining > 0 && <div className="dialog-actions"><button className="button" onClick={() => void continueBatch(true)}>Retry unfinished</button><button className="button secondary" onClick={() => void closeBatchDialog()}>Close</button></div>}
          </section>
        </div>
      )}
      <footer className="status" aria-live="polite">
        <span>{notice}</span>
        {failedPersistenceKey && <button className="subtle" onClick={() => {
          setNotice("Retrying save...");
          void persistence.current?.retry(failedPersistenceKey).then((saved) => {
            if (saved) setFailedPersistenceKey("");
          });
        }}>Retry save</button>}
        <span className={`status-state ${photo.dirty ? "pending" : "ready"}`}>
          {progress || (photo.dirty ? "Changes pending" : "Visual preview ready")}
        </span>
      </footer>
      {savingPreset && (
        <dialog
          ref={presetDialog}
          aria-labelledby="save-preset-title"
          className="modal-backdrop"
          onCancel={() => setSavingPreset(false)}
          onClick={() => setSavingPreset(false)}
        >
          <form
            className="modal"
            onClick={(e) => e.stopPropagation()}
            onSubmit={(e) => {
              e.preventDefault();
              const name = presetName.trim();
              if (!name) return;
              const preset = {
                sharpenModelId: photo.sharpenModelId ?? defaultSharpenModelId,
                name,
                layers: photo.layers.map((l) => ({ ...l })),
                highlights: photo.highlights,
              };
              setPresets((ps) =>
                ps.some((p) => p.name === name)
                  ? ps.map((p) => (p.name === name ? preset : p))
                  : [...ps, preset],
              );
               if (isDesktop()) {
                 const snapshot = recipeFor({ ...photo, preset: name });
                 const width = photo.previewWidth ?? 1024;
                 const height = photo.previewHeight ?? 1024;
                 persistence.current!.schedule(`preset:${name}`, 0, () => savePreset(name, snapshot, width, height));
               }
              patchPhoto({ preset: name });
              setSavingPreset(false);
              setNotice(`Preset “${name}” saved${isDesktop() ? " to RawLab" : " for this session"}.`);
            }}
          >
            <h2 id="save-preset-title">Save preset</h2>
            <label className="preset-name-label">
              Preset name
              <input
                autoFocus
                aria-label="Preset name"
                value={presetName}
                onChange={(e) => setPresetName(e.target.value)}
              />
            </label>
            {presets.some((p) => p.name === presetName.trim()) && (
              <p>This will replace the existing preset.</p>
            )}
            <div className="dialog-actions">
              <button
                type="button"
                className="subtle"
                onClick={() => setSavingPreset(false)}
              >
                Cancel
              </button>
              <button
                type="submit"
                className="primary"
                disabled={!presetName.trim()}
              >
                Save
              </button>
            </div>
          </form>
        </dialog>
      )}
      {settings && (
        <dialog
          ref={dialog}
           aria-labelledby="settings-title"
          className="modal-backdrop"
          onCancel={() => showSettings(false)}
          onClick={() => showSettings(false)}
        >
          <section className="modal settings-modal" onClick={(e) => e.stopPropagation()}>
             {isDesktop() ? (
               <ModelManager
                 onChanged={() => {
                   configuredModels()
                     .then((models) => {
                       setCatalog(models);
                        setDefaultSharpenModelId(
                         models.sharpen.find((m) => m.available)?.id ?? "",
                       );
                     })
                     .catch((error) => setNotice(String(error)));
                 }}
               />
             ) : (
               <>
                 <Aperture size={30} />
                  <h2 id="settings-title">Desktop model settings</h2>
                 <p>
                   Model management is available in the Linux desktop build.
                   Browser mode displays JPEG, PNG and WebP previews only.
                 </p>
               </>
             )}
             <div className="preferences-footer">
               <button className="subtle" onClick={() => showSettings(false)}>Close</button>
             </div>
          </section>
        </dialog>
      )}
    </div>
  );
}
function Section({
  title,
  icon,
  children,
}: {
  title: string;
  icon: React.ReactNode;
  children: React.ReactNode;
}) {
  return (
    <details className="section" open>
      <summary>
        {icon}
        <span>{title}</span>
        <ChevronDown size={14} />
      </summary>
      <div className="section-content">{children}</div>
    </details>
  );
}
function Slider({
  label,
  value,
  change,
  disabled = false,
}: {
  label: string;
  value: number;
  change: (value: number) => void;
  disabled?: boolean;
}) {
  return (
    <label className={`slider-field ${disabled ? "disabled" : ""}`}>
      <span>
        {label}
        <output>{value}%</output>
      </span>
      <input
        type="range"
        aria-label={label}
        min="0"
        max="100"
        value={value}
        disabled={disabled}
        onChange={(e) => change(+e.target.value)}
      />
    </label>
  );
}
createRoot(document.getElementById("root")!).render(<App />);
