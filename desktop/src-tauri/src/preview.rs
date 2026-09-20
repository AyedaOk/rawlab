use std::{
    collections::VecDeque,
    path::{Path, PathBuf},
    sync::{Arc, Mutex},
};
use tauri_plugin_fs::FsExt;

#[derive(Default, Clone)]
pub struct PreviewCache(Arc<Mutex<VecDeque<(String, serde_json::Value)>>>);

pub(crate) fn engine_path() -> Result<PathBuf, String> {
    if let Some(path) = std::env::var_os("RAWLAB_PREVIEW_ENGINE") {
        return Ok(PathBuf::from(path));
    }
    if let Ok(exe) = std::env::current_exe() {
        let sibling = exe.with_file_name("rawlab-preview");
        if sibling.is_file() {
            return Ok(sibling);
        }
    }
    // Development convenience. Packaged builds must provide a sibling bridge.
    if cfg!(debug_assertions) {
        let root = Path::new(env!("CARGO_MANIFEST_DIR"))
            .parent()
            .unwrap()
            .parent()
            .unwrap();
        for build in ["preview", "dev"] {
            let path = root.join(format!("build/{build}/src/rawlab_preview/rawlab-preview"));
            if path.is_file() {
                return Ok(path);
            }
        }
    }
    Err("RAW preview engine not found. Build CMake target rawlab_preview, or set RAWLAB_PREVIEW_ENGINE to the rawlab-preview executable and restart the desktop app.".into())
}

#[tauri::command]
pub async fn render_raw_preview(
    app: tauri::AppHandle,
    cache: tauri::State<'_, PreviewCache>,
    interactive: tauri::State<'_, crate::engine::ActivePreviewEngine>,
    background_worker: tauri::State<'_, crate::engine::PreviewEngine>,
    path: String,
    size: u32,
    high_quality: bool,
    background: Option<bool>,
) -> Result<serde_json::Value, String> {
    if !(128..=4096).contains(&size) {
        return Err("Preview size must be 128–4096".into());
    }
    let input = PathBuf::from(&path);
    let canonical = input.canonicalize().map_err(|e| e.to_string())?;
    if !app.fs_scope().is_allowed(&input) || !app.fs_scope().is_allowed(&canonical) {
        return Err("Select this RAW file or its folder using Import first.".into());
    }
    let cache = cache.inner().clone();
    let is_background = background.unwrap_or(false);
    let (worker, generation) = if is_background {
        (background_worker.inner().0.clone(), None)
    } else {
        let interactive = interactive.inner();
        let generation = interactive.generation.fetch_add(1, std::sync::atomic::Ordering::AcqRel) + 1;
        (interactive.engine.clone(), Some((interactive.generation.clone(), generation)))
    };
    tauri::async_runtime::spawn_blocking(move || {
        let info = canonical.metadata().map_err(|e| e.to_string())?;
        let source_version = crate::engine::source_version(&canonical)?;
        if !info.is_file() {
            return Err("Selected path is not a file".into());
        }
        let engine = engine_path()?;
        let engine_info = engine
            .metadata()
            .map_err(|e| format!("Cannot locate RAW preview engine: {e}"))?;
        let key = format!(
            "{:?}:{source_version}:{size}:{high_quality}:{:?}:{}:{:?}",
            canonical,
            engine,
            engine_info.len(),
            engine_info.modified().ok()
        );
        {
            let mut entries = cache.0.lock().map_err(|e| e.to_string())?;
            if let Some(index) = entries.iter().position(|(k, _)| k == &key) {
                let entry = entries.remove(index).unwrap();
                let response = entry.1.clone();
                entries.push_back(entry);
                if crate::engine::source_version(&canonical)? != source_version {
                    return Err("Source changed while loading cached preview; retry".into());
                }
                return Ok(response);
            }
        }
        let request = serde_json::json!({"operation":"preview", "path":canonical, "size":size, "quality":if high_quality{"high"}else{"fast"}});
        let mut response = if let Some((current, expected)) = generation {
            worker.request_guarded(&app, request, Some((&current, expected)))?
        } else {
            worker.request(&app, request)?
        };
        if response["version"] != 1 || !response["png"].is_string() {
            return Err("Unsupported preview bridge response".into());
        }
        if crate::engine::source_version(&canonical)? != source_version {
            return Err("Source changed while rendering preview; retry".into());
        }
        response["sourceVersion"] = serde_json::json!(source_version);
        // Bound encoded preview storage, not just item count (max 64 MiB).
        let cost = response["png"].as_str().unwrap().len();
        let mut entries = cache.0.lock().map_err(|e| e.to_string())?;
        let mut total: usize = entries
            .iter()
            .map(|(_, v)| v["png"].as_str().map_or(0, str::len))
            .sum();
        while !entries.is_empty() && (entries.len() >= 8 || total + cost > 64 * 1024 * 1024) {
            total -= entries.pop_front().unwrap().1["png"]
                .as_str()
                .map_or(0, str::len);
        }
        if cost <= 64 * 1024 * 1024 {
            entries.push_back((key, response.clone()));
        }
        Ok(response)
    })
    .await
    .map_err(|e| e.to_string())?
}
