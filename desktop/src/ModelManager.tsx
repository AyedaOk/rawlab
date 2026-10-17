import React, { useEffect, useState } from "react";
import { X } from "lucide-react";
import { cancelProcessing, control, type ModelCatalog } from "./desktop";

type Settings = ModelCatalog & {
  sam: { id: string; name: string; folder: string; origin?: string }[];
  activeSamModelId: string;
  runtimeLibrary: string;
  deviceId: number;
  providers: { id: string; name: string }[];
  runtimeError: string;
};
type CatalogItem = { id: string; name: string; task: string; installed: boolean };

export default function ModelManager({ onChanged }: { onChanged: () => void }) {
  const [data, setData] = useState<Settings | null>(null);
  const [catalog, setCatalog] = useState<CatalogItem[]>([]);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState("");
  const [task, setTask] = useState("denoise");
  const [adapter, setAdapter] = useState("rawnind-bayer-v1");
  const refresh = async () => {
    setData(await control<Settings>({ operation: "settings" }));
    onChanged();
  };
  const run = async (label: string, operation: () => Promise<void>) => {
    setBusy(label);
    setError("");
    try { await operation(); } catch (caught) { setError(String(caught)); } finally { setBusy(""); }
  };
  useEffect(() => { void run("Reading settings…", refresh); }, []);
  if (!data) return <div role="status">{error || (busy ? "Reading model settings…" : "Model settings unavailable")}
    {error && <button className="icon" aria-label="Dismiss settings error" onClick={() => setError("")}><X size={14} /></button>}
    {!busy && <button className="subtle" onClick={() => void run("Reading settings…", refresh)}>Retry</button>}
  </div>;

  const groups = [
    { label: "Denoise", task: "denoise", models: data.denoise },
    { label: "Sharpen", task: "sharpen", models: data.sharpen },
    { label: "SAM", task: "mask", models: data.sam },
    { label: "Depth", task: "depth", models: data.depth ?? [] },
  ];
  return (
    <div className="model-manager">
      <header className="preferences-header">
        <div>
          <h2 id="settings-title">Preferences</h2>
          <p>Processing hardware and installed models</p>
        </div>
        <span className="provider-badge">{data.provider === "auto" ? "Auto" : data.provider.toUpperCase()} · device {data.deviceId}</span>
      </header>
      {error && <div className="preferences-error dismissible-error" role="alert"><span>{error}</span>
        <button className="icon" aria-label="Dismiss settings error" onClick={() => setError("")}><X size={14} /></button>
      </div>}

      <fieldset className="preferences-section" disabled={!!busy}>
        <legend>Processing</legend>
        <div className="preferences-grid compact-fields">
          <label><span>Provider</span><select value={data.provider} onChange={(event) => setData({ ...data, provider: event.target.value })}>
            <option value="auto">Auto</option><option value="cpu">CPU</option>
            {data.providers.filter((provider) => provider.id !== "cpu" && provider.id !== "auto").map((provider) => <option key={provider.id} value={provider.id}>{provider.name}</option>)}
          </select></label>
          <label><span>Device</span><input aria-label="Processing device" type="number" min="0" value={data.deviceId} onChange={(event) => setData({ ...data, deviceId: Math.max(0, +event.target.value) })} /></label>
          <label className="full-field"><span>ONNX Runtime library</span><input aria-label="ONNX Runtime library" placeholder="Use system default" value={data.runtimeLibrary} onChange={(event) => setData({ ...data, runtimeLibrary: event.target.value })} /></label>
           <label className="full-field"><span>Active SAM model</span><select value={data.activeSamModelId} onChange={(event) => setData({ ...data, activeSamModelId: event.target.value })}>
            <option value="">None</option>{data.sam.map((model) => <option key={model.id} value={model.id}>{model.name}</option>)}
           </select></label>
           <label className="full-field"><span>Active depth model</span><select value={data.activeDepthModelId ?? ""} onChange={(event) => setData({ ...data, activeDepthModelId: event.target.value })}>
             <option value="">None</option>{(data.depth ?? []).map((model) => <option key={model.id} value={model.id}>{model.name}{model.available ? "" : " (missing)"}</option>)}
           </select></label>
        </div>
        {data.runtimeError && <div className="field-error dismissible-error" role="alert"><span>{data.runtimeError}</span>
          <button className="icon" aria-label="Dismiss runtime error" onClick={() => setData({ ...data, runtimeError: "" })}><X size={14} /></button>
        </div>}
        <div className="section-action"><button className="primary" onClick={() => void run("Saving settings…", async () => {
          await control({ operation: "settings_save", provider: data.provider, deviceId: data.deviceId, runtimeLibrary: data.runtimeLibrary, activeSamModelId: data.activeSamModelId, activeDepthModelId: data.activeDepthModelId ?? "" });
          await refresh();
        })}>Save processing settings</button></div>
      </fieldset>

      <fieldset className="preferences-section" disabled={!!busy}>
        <legend>Configured models</legend>
        <div className="model-groups">
          {groups.map((group) => <section className="model-group" key={group.task}>
            <h3>{group.label}<span>{group.models.length}</span></h3>
            {group.models.length === 0 && <p className="empty-models">No models configured</p>}
            {group.models.map((model) => <div className="model-row" key={model.id}>
              <span title={"folder" in model ? model.folder : model.path}>{model.name}{"available" in model && !model.available ? " (missing)" : ""}</span>
              <button className="subtle" title={model.origin === "verified" ? "Delete downloaded model files" : "Keep model files on disk"} onClick={() => void run("Removing model…", async () => {
                await control({ operation: "model_remove", task: group.task, id: model.id }); await refresh();
                setCatalog((items) => items.map((item) => `hf:${item.id}` === model.id ? { ...item, installed: false } : item));
              })}>{model.origin === "verified" ? "Remove" : "Remove from list"}</button>
            </div>)}
          </section>)}
        </div>
        <div className="add-model-row">
          <select aria-label="Local model task" value={task} onChange={(event) => setTask(event.target.value)}><option value="denoise">Denoise</option><option value="sharpen">AI Sharpen</option><option value="mask">SAM folder</option><option value="depth">Depth</option></select>
          {task === "denoise" && <select aria-label="Local model adapter" value={adapter} onChange={(event) => setAdapter(event.target.value)}><option value="rawnind-bayer-v1">RawNIND</option><option value="rawforge-rgb-v1">RawForge</option></select>}
          <button className="subtle" onClick={() => void run("Adding local model…", async () => {
            const { open } = await import("@tauri-apps/plugin-dialog");
            const path = await open({ title: "Select local model", directory: task === "mask", recursive: task === "mask", filters: task === "mask" ? undefined : [{ name: "ONNX model", extensions: ["onnx"] }] });
            if (typeof path === "string") { const name = path.split(/[\\/]/).pop() || task; await control({ operation: "model_add", path, name, task, adapter: task === "mask" ? "sam21-split-v1" : task === "depth" ? "depth-anything-v2-v1" : task === "sharpen" ? "rawforge-rgb-v1" : adapter }); await refresh(); }
          })}>Add local model…</button>
        </div>
      </fieldset>

      <fieldset className="preferences-section catalog-section" disabled={!!busy}>
        <legend>Model catalog</legend>
        <div className="section-action start"><button className="subtle" onClick={() => void run("Fetching catalog…", async () => {
          setCatalog((await control<{ catalog: CatalogItem[] }>({ operation: "catalog" })).catalog);
          await refresh();
        })}>Refresh catalog</button></div>
        {catalog.map((model) => <div className="model-row" key={model.id}><span>{model.name}</span><button className="subtle" disabled={model.installed} onClick={() => void run(`Downloading ${model.name}…`, async () => { await control({ operation: "model_download", id: model.id }); await refresh(); setCatalog((items) => items.map((item) => item.id === model.id ? { ...item, installed: true } : item)); })}>{model.installed ? "Installed" : "Install"}</button></div>)}
      </fieldset>
      {busy && <div className="preferences-status" role="status"><span>{busy}</span><button className="subtle" onClick={() => void cancelProcessing()}>Cancel</button></div>}
    </div>
  );
}
