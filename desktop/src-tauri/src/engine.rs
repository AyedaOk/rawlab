use std::{
    io::{BufRead, BufReader, Write},
    path::PathBuf,
    process::{Child, ChildStdin, ChildStdout, Command, Stdio},
    sync::{Arc, Mutex, atomic::{AtomicBool, AtomicU64, Ordering}},
};
use tauri::Emitter;
use tauri_plugin_fs::FsExt;
use std::time::UNIX_EPOCH;

struct Worker {
    child: Child,
    input: ChildStdin,
    output: BufReader<ChildStdout>,
}
#[derive(Default)]
struct CancellationState {
    operation: Option<String>,
    path: Option<PathBuf>,
}
impl Drop for Worker {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}
#[derive(Default, Clone)]
pub struct Engine {
    worker: Arc<Mutex<Option<Worker>>>,
    cancellation: Arc<Mutex<CancellationState>>,
    pending_processing_cancel: Arc<AtomicBool>,
    queued_processing: Arc<AtomicU64>,
    pending_batch_cancel: Arc<AtomicBool>,
    queued_batch: Arc<AtomicU64>,
}
#[derive(Default, Clone)]
pub struct PreviewEngine(pub Engine);
#[derive(Default, Clone)]
pub struct ProcessingEngine(pub Engine, pub Arc<AtomicU64>, pub Arc<AtomicU64>, pub Arc<AtomicU64>);
#[derive(Default, Clone)]
pub struct ActivePreviewEngine {
    pub engine: Engine,
    pub generation: Arc<AtomicU64>,
}
impl Engine {
    pub fn request(
        &self,
        app: &tauri::AppHandle,
        request: serde_json::Value,
    ) -> Result<serde_json::Value, String> {
        self.request_guarded(app, request, None)
    }
    pub fn request_guarded(
        &self,
        app: &tauri::AppHandle,
        mut request: serde_json::Value,
        generation: Option<(&AtomicU64, u64)>,
    ) -> Result<serde_json::Value, String> {
        let mut guard = self.worker.lock().map_err(|e| e.to_string())?;
        if generation.is_some_and(|(current, expected)| current.load(Ordering::Acquire) != expected) {
            return Err("Preview superseded".into());
        }
        if guard.is_none() {
            let mut command = Command::new(crate::preview::engine_path()?);
            #[cfg(target_os = "macos")]
            if let Some(directory) = crate::preview::engine_path()?.parent() {
                command.env("QT_PLUGIN_PATH", directory.join("../Resources/qt-plugins"));
            }
            if let Some(appdir) = std::env::var_os("APPDIR") {
                let mut plugin_paths = vec![PathBuf::from(appdir).join("usr/plugins")];
                if let Some(existing) = std::env::var_os("QT_PLUGIN_PATH") {
                    plugin_paths.extend(std::env::split_paths(&existing));
                }
                if let Ok(value) = std::env::join_paths(plugin_paths) {
                    command.env("QT_PLUGIN_PATH", value);
                }
            }
            let mut child = command.arg("--serve")
                .stdin(Stdio::piped())
                .stdout(Stdio::piped())
                .stderr(Stdio::inherit())
                .spawn()
                .map_err(|e| e.to_string())?;
            let input = child.stdin.take().unwrap();
            let output = BufReader::new(child.stdout.take().unwrap());
            *guard = Some(Worker {
                child,
                input,
                output,
            });
        }
        let directory = tempfile::tempdir().map_err(|e| e.to_string())?;
        let cancel_path = directory.path().join("cancel");
        request["cancelPath"] = serde_json::json!(cancel_path);
        let operation = request["operation"].as_str().unwrap_or_default().to_string();
    let cancellable = ["process", "recompose", "export", "export_linear", "segment"].contains(&operation.as_str());
        *self.cancellation.lock().map_err(|e| e.to_string())? = CancellationState {
            operation: Some(operation), path: Some(cancel_path.clone()),
        };
        if cancellable && self.pending_processing_cancel.swap(false, Ordering::AcqRel) {
            std::fs::write(&cancel_path, b"cancel").map_err(|e| e.to_string())?;
        }
        if (request["operation"] == "batch_run" || request["operation"] == "external_batch_run") && self.pending_batch_cancel.swap(false, Ordering::AcqRel) {
            std::fs::write(&cancel_path, b"cancel").map_err(|e| e.to_string())?;
        }
        let result = (|| {
            let worker = guard.as_mut().unwrap();
            writeln!(worker.input, "{}", request)
                .and_then(|_| worker.input.flush())
                .map_err(|e| e.to_string())?;
            loop {
                let mut line = String::new();
                if worker
                    .output
                    .read_line(&mut line)
                    .map_err(|e| e.to_string())?
                    == 0
                {
                    return Err("RAW engine stopped unexpectedly; retry to restart it".into());
                }
                let response: serde_json::Value =
                    serde_json::from_str(&line).map_err(|e| e.to_string())?;
                if response.get("progress").is_some() {
                    let _ = app.emit("rawlab-progress", &response);
                    continue;
                }
                if let Some(error) = response["error"].as_str() {
                    return Err(error.to_string());
                }
                return Ok(response);
            }
        })();
        *self.cancellation.lock().map_err(|e| e.to_string())? = CancellationState::default();
        // Application errors leave the line-oriented worker usable. Restart only
        // when the child actually exited so one stale cache ID cannot flush every
        // other decoded or processed result.
        if result.is_err() && guard.as_mut()
            .and_then(|worker| worker.child.try_wait().ok().flatten()).is_some() {
                *guard = None;
        }
        result
    }
}

pub(crate) fn source_version(path: &std::path::Path) -> Result<String, String> {
    let info = path.metadata().map_err(|e| e.to_string())?;
    let base = format!("{}:{}", info.len(), info.modified().ok()
        .and_then(|time| time.duration_since(UNIX_EPOCH).ok())
        .map(|duration| duration.as_nanos()).unwrap_or(0));
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        return Ok(format!("{base}:{}:{}:{}:{}", info.dev(), info.ino(), info.ctime(), info.ctime_nsec()));
    }
    #[cfg(not(unix))]
    Ok(base)
}

#[tauri::command]
pub fn raw_source_version(app: tauri::AppHandle, path: String) -> Result<String, String> {
    let input = PathBuf::from(&path);
    let canonical = input.canonicalize().map_err(|e| e.to_string())?;
    if !app.fs_scope().is_allowed(&input) || !app.fs_scope().is_allowed(&canonical) {
        return Err("Select the source using Import first".into());
    }
    source_version(&canonical)
}

#[tauri::command]
pub fn cancel_processing(engine: tauri::State<'_, ProcessingEngine>) -> Result<(), String> {
    let cancellation = engine.0.cancellation.lock().map_err(|e| e.to_string())?;
    let cancellable = cancellation.operation.as_deref()
        .is_some_and(|operation| ["process", "recompose", "export", "export_linear", "segment"].contains(&operation));
    if cancellable {
        if let Some(path) = cancellation.path.as_ref() {
            std::fs::write(path, b"cancel").map_err(|e| e.to_string())?;
        }
    } else if engine.0.queued_processing.load(Ordering::Acquire) > 0 {
        engine.0.pending_processing_cancel.store(true, Ordering::Release);
    }
    Ok(())
}

#[tauri::command]
pub fn cancel_batch_processing(engine: tauri::State<'_, Engine>) -> Result<(), String> {
    let cancellation = engine.cancellation.lock().map_err(|e| e.to_string())?;
    if matches!(cancellation.operation.as_deref(), Some("batch_run" | "external_batch_run")) {
        if let Some(path) = cancellation.path.as_ref() {
            std::fs::write(path, b"cancel").map_err(|e| e.to_string())?;
        }
    } else if engine.queued_batch.load(Ordering::Acquire) > 0 {
        engine.pending_batch_cancel.store(true, Ordering::Release);
    }
    Ok(())
}

#[tauri::command]
pub async fn processing_models(
    app: tauri::AppHandle,
    engine: tauri::State<'_, ProcessingEngine>,
) -> Result<serde_json::Value, String> {
    let engine = engine.inner().0.clone();
    tauri::async_runtime::spawn_blocking(move || {
        engine.request(&app, serde_json::json!({"operation":"models"}))
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn desktop_control(app: tauri::AppHandle, engine: tauri::State<'_,Engine>, request: serde_json::Value) -> Result<serde_json::Value,String> {
    let operation=request["operation"].as_str().ok_or("Missing operation")?;
    if !["settings","settings_save","model_add","model_remove","catalog","model_download","library_list","library_add","library_remove","library_metadata","library_metadata_save","preset_list","preset_load","preset_save","preset_delete","recipe_load","recipe_save","batch_create","batch_status","batch_update","batch_retry","batch_cancel","batch_discard","batch_run"].contains(&operation) {return Err("Unsupported desktop operation".into());}
    if ["model_add","recipe_load","recipe_save"].contains(&operation) {
        let path=request["path"].as_str().ok_or("Missing path")?;
        if !app.fs_scope().is_allowed(path) {return Err("Select the file/folder first".into());}
    }
    if operation == "batch_create" {
        for path in request["sources"].as_array().ok_or("Missing sources")? {
            if !app.fs_scope().is_allowed(path.as_str().ok_or("Invalid source path")?) {return Err("Select the batch sources first".into());}
        }
        let directory = request["outputDirectory"].as_str().ok_or("Missing output directory")?;
        if !app.fs_scope().is_allowed(directory) {return Err("Select the batch output directory first".into());}
    }
    if operation == "library_metadata" || operation == "library_metadata_save" {
        let path = request["path"].as_str().ok_or("Missing metadata path")?;
        if !app.fs_scope().is_allowed(path) {return Err("Select the photograph first".into());}
    }
    if operation == "batch_update" {
        // The native store validates job identity and the output hash before completion.
        if request["index"].as_i64().is_none() || request["state"].as_i64().is_none() {return Err("Missing batch update fields".into());}
    }
    if operation=="library_add"||operation=="library_remove" {
        for path in request["paths"].as_array().ok_or("Missing paths")? {
            if !app.fs_scope().is_allowed(path.as_str().ok_or("Invalid path")?) {return Err("Select the photographs first".into());}
        }
    }
    let restore=operation=="library_list";
    let batch_run=operation=="batch_run";
    let engine=engine.inner().clone();
    if batch_run { engine.queued_batch.fetch_add(1, Ordering::AcqRel); }
    tauri::async_runtime::spawn_blocking(move|| {
        let result=engine.request(&app,request);
        if batch_run && engine.queued_batch.fetch_sub(1, Ordering::AcqRel) == 1 {
            engine.pending_batch_cancel.store(false, Ordering::Release);
        }
        let response=result?;
        if restore {if let Some(paths)=response["paths"].as_array(){for path in paths {if let Some(p)=path.as_str(){app.fs_scope().allow_file(p).map_err(|e|e.to_string())?;}}}}
        Ok(response)
    }).await.map_err(|e|e.to_string())?
}

#[tauri::command]
pub async fn process_raw(
    app: tauri::AppHandle,
    engine: tauri::State<'_, ProcessingEngine>,
    path: String,
    recipe: serde_json::Value,
    high_quality: bool,
    output: Option<String>,
    output_kind: Option<String>,
    points: Option<serde_json::Value>,
    operation: Option<String>,
    preview_size: Option<i32>,
    result_id: Option<String>,
) -> Result<serde_json::Value, String> {
    let input = PathBuf::from(&path);
    let canonical = input.canonicalize().map_err(|e| e.to_string())?;
    if !app.fs_scope().is_allowed(&input) || !app.fs_scope().is_allowed(&canonical) {
        return Err("Select the source using Import first".into());
    }
    if let Some(ref destination) = output {
        if !PathBuf::from(destination).is_absolute() || !app.fs_scope().is_allowed(destination) {
            return Err("Choose the DNG destination using the Save dialog".into());
        }
    }
    if recipe.to_string().len() > 8 * 1024 * 1024 {
        return Err("Recipe is too large".into());
    }
    let operation = if points.is_some() {
        "segment"
    } else if output.is_some() {
        if output_kind.as_deref() == Some("linear-rgb") { "export_linear" } else { "export" }
    } else if operation.as_deref() == Some("recompose") {
        "recompose"
    } else {
        "process"
    };
    let version = source_version(&canonical)?;
    let size = preview_size.unwrap_or(4096).clamp(256, 4096);
    let request = serde_json::json!({"operation":operation, "path":canonical.clone(), "recipe":recipe, "size":size, "quality":if high_quality{"high"}else{"fast"}, "output":output, "points":points, "resultId":result_id});
    let state = engine.inner();
    state.1.fetch_add(1, Ordering::AcqRel);
    state.2.fetch_add(1, Ordering::AcqRel);
    state.3.fetch_add(1, Ordering::AcqRel);
    let engine = state.0.clone();
    engine.queued_processing.fetch_add(1, Ordering::AcqRel);
    tauri::async_runtime::spawn_blocking(move || {
        let result = engine.request(&app, request).and_then(|mut response| {
            let current = source_version(&canonical)?;
            if current != version { return Err("Source changed while processing; retry".into()); }
            response["sourceVersion"] = serde_json::json!(current);
            Ok(response)
        });
        if engine.queued_processing.fetch_sub(1, Ordering::AcqRel) == 1 {
            engine.pending_processing_cancel.store(false, Ordering::Release);
        }
        result
    })
        .await
        .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn render_processed(
    app: tauri::AppHandle,
    engine: tauri::State<'_, ProcessingEngine>,
    path: String,
    high_quality: bool,
    result_id: String,
    recipe: serde_json::Value,
    size: u32,
) -> Result<serde_json::Value, String> {
    let input = PathBuf::from(&path);
    let canonical = input.canonicalize().map_err(|e| e.to_string())?;
    if !app.fs_scope().is_allowed(&input) || !app.fs_scope().is_allowed(&canonical) {
        return Err("Select the source using Import first".into());
    }
    let version = source_version(&canonical)?;
    let state = engine.inner();
    let generation = state.3.fetch_add(1, Ordering::AcqRel) + 1;
    let current_generation = state.3.clone();
    let engine = state.0.clone();
    tauri::async_runtime::spawn_blocking(move || {
      let mut response = engine.request_guarded(&app, serde_json::json!({
          "operation": "processed_preview", "path": canonical.clone(), "size": size.clamp(128, 4096),
          "quality": if high_quality { "high" } else { "fast" }, "resultId": result_id,
          "recipe": recipe
      }), Some((&current_generation, generation)))?;
       let current = source_version(&canonical)?;
       if current != version { return Err("Source changed while rendering; retry".into()); }
       response["sourceVersion"] = serde_json::json!(current);
      Ok(response)
    }).await.map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn render_raw_detail(
    app: tauri::AppHandle,
    engine: tauri::State<'_, ProcessingEngine>,
    path: String,
    target: String,
    result_id: Option<String>,
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    recipe: serde_json::Value,
) -> Result<serde_json::Value, String> {
    if width == 0 || height == 0 || u64::from(width) * u64::from(height) > 16 * 1024 * 1024 {
        return Err("Invalid source preview region".into());
    }
    if x > i32::MAX as u32 || y > i32::MAX as u32 || width > i32::MAX as u32 || height > i32::MAX as u32 {
        return Err("Source preview region is out of range".into());
    }
    if target != "original" && target != "processed" {
        return Err("Invalid source preview target".into());
    }
    let input = PathBuf::from(&path);
    let canonical = input.canonicalize().map_err(|e| e.to_string())?;
    if !app.fs_scope().is_allowed(&input) || !app.fs_scope().is_allowed(&canonical) {
        return Err("Select the source using Import first".into());
    }
    let version = source_version(&canonical)?;
    let current_generation = if target == "processed" { engine.inner().1.clone() } else { engine.inner().2.clone() };
    let generation = current_generation.fetch_add(1, Ordering::AcqRel) + 1;
    let engine = engine.inner().0.clone();
    tauri::async_runtime::spawn_blocking(move || {
      let mut response = engine.request_guarded(&app, serde_json::json!({
        "operation":"detail", "path":canonical.clone(), "target":target, "resultId":result_id,
        "x":x, "y":y, "width":width, "height":height, "recipe":recipe
      }), Some((&current_generation, generation)))?;
      let current = source_version(&canonical)?;
      if current != version { return Err("Source changed while rendering detail; retry".into()); }
      response["sourceVersion"] = serde_json::json!(current);
      Ok(response)
    }).await.map_err(|e| e.to_string())?
}
