use serde::Serialize;
use std::path::{Path, PathBuf};
use tauri_plugin_fs::FsExt;

use crate::engine::ProcessingEngine;

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ExternalSession {
    mode: String,
    sources: Vec<String>,
    output: Option<String>,
    recipe_source: Option<String>,
    request: Option<String>,
}

fn absolute_file(value: &str, label: &str) -> Result<String, String> {
    let path = PathBuf::from(value);
    if !path.is_absolute() || !path.is_file() {
        return Err(format!("{label} must be an existing absolute file"));
    }
    Ok(path.canonicalize().map_err(|error| error.to_string())?.to_string_lossy().into_owned())
}

fn output_path(value: &str, input: &str) -> Result<String, String> {
    let path = PathBuf::from(value);
    let parent = path.parent().ok_or("External output needs a parent directory")?;
    if !path.is_absolute() || !parent.is_dir() || path.exists() || PathBuf::from(format!("{value}.xmp")).exists() {
        return Err("External output must be a new absolute path in an existing directory".into());
    }
    if path == PathBuf::from(input) { return Err("External output cannot replace its input".into()); }
    Ok(path.to_string_lossy().into_owned())
}

fn decode_hex(value: &str) -> Result<String, String> {
    if value.is_empty() || value.len() % 2 != 0 || !value.bytes().all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte)) {
        return Err("Invalid Darktable batch path encoding".into());
    }
    let bytes = (0..value.len()).step_by(2).map(|index| u8::from_str_radix(&value[index..index + 2], 16).unwrap()).collect::<Vec<_>>();
    let decoded = String::from_utf8(bytes).map_err(|_| "Darktable batch paths must be UTF-8")?;
    if decoded.contains('\0') || !Path::new(&decoded).is_absolute() { return Err("Darktable batch paths must be absolute".into()); }
    Ok(decoded)
}

fn read_batch(path: &str) -> Result<Vec<String>, String> {
    let bytes = std::fs::read(path).map_err(|error| error.to_string())?;
    if bytes.len() > 4 * 1024 * 1024 || !bytes.ends_with(b"\n") { return Err("Invalid Darktable batch request size or newline".into()); }
    let text = std::str::from_utf8(&bytes).map_err(|_| "Darktable batch request must be UTF-8")?;
    let mut lines = text.lines();
    if lines.next() != Some("rawlab-batch-v1") { return Err("Unsupported Darktable batch protocol".into()); }
    let mut sources = Vec::new();
    for line in lines {
        if sources.len() >= 10_000 { return Err("Darktable batch contains too many jobs".into()); }
        let (source, output) = line.split_once('\t').ok_or("Invalid Darktable batch record")?;
        let source = decode_hex(source)?;
        let output = decode_hex(output)?;
        absolute_file(&source, "Batch source")?;
        output_path(&output, &source)?;
        sources.push(source);
    }
    if sources.is_empty() { return Err("Darktable batch is empty".into()); }
    Ok(sources)
}

pub fn parse_args() -> Result<Option<ExternalSession>, String> {
    parse_arguments(std::env::args().skip(1).collect())
}

fn parse_arguments(arguments: Vec<String>) -> Result<Option<ExternalSession>, String> {
    let value = |flag: &str| -> Result<Option<String>, String> {
        if let Some(index) = arguments.iter().position(|argument| argument == flag) {
            return arguments.get(index + 1).cloned().map(Some).ok_or_else(|| format!("{flag} requires a value"));
        }
        Ok(None)
    };
    let input = value("--external-edit")?;
    let output = value("--output")?;
    let recipe_source = value("--apply-settings-from")?;
    let request = value("--external-batch")?;
    if input.is_none() && output.is_none() && recipe_source.is_none() && request.is_none() { return Ok(None); }
    if request.is_some() {
        if input.is_some() || output.is_some() || recipe_source.is_some() { return Err("--external-batch must be used on its own".into()); }
        let request = absolute_file(request.as_deref().unwrap(), "Batch request")?;
        let sources = read_batch(&request)?;
        return Ok(Some(ExternalSession { mode: "batch".into(), sources, output: None, recipe_source: None, request: Some(request) }));
    }
    let input = input.ok_or("--external-edit and --output must be used together")?;
    let output = output.ok_or("--external-edit and --output must be used together")?;
    let input = absolute_file(&input, "External input")?;
    let output = output_path(&output, &input)?;
    let recipe_source = recipe_source.map(|path| absolute_file(&path, "Settings source")).transpose()?;
    Ok(Some(ExternalSession { mode: "single".into(), sources: vec![input], output: Some(output), recipe_source, request: None }))
}

#[tauri::command]
pub fn external_session(app: tauri::AppHandle, session: tauri::State<'_, Option<ExternalSession>>) -> Result<Option<ExternalSession>, String> {
    let Some(session) = session.inner().clone() else { return Ok(None); };
    for source in &session.sources { app.fs_scope().allow_file(source).map_err(|error| error.to_string())?; }
    if let Some(path) = &session.recipe_source { app.fs_scope().allow_file(path).map_err(|error| error.to_string())?; }
    if let Some(path) = &session.output { app.fs_scope().allow_file(path).map_err(|error| error.to_string())?; }
    Ok(Some(session))
}

#[tauri::command]
pub async fn run_external_batch(app: tauri::AppHandle, engine: tauri::State<'_, ProcessingEngine>, request: String, recipe: serde_json::Value) -> Result<serde_json::Value, String> {
    let state = engine.inner().0.clone();
    tauri::async_runtime::spawn_blocking(move || state.request(&app, serde_json::json!({"operation":"external_batch_run", "requestPath":request, "recipe":recipe})))
        .await.map_err(|error| error.to_string())?
}

#[tauri::command]
pub fn finish_external(app: tauri::AppHandle, code: i32) {
    app.exit(code);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validates_single_and_batch_launches() {
        let directory = tempfile::tempdir().unwrap();
        let input = directory.path().join("input raw.dng");
        std::fs::write(&input, b"raw").unwrap();
        let output = directory.path().join("output.dng");
        let single = parse_arguments(vec![
            "--external-edit".into(), input.to_string_lossy().into_owned(),
            "--output".into(), output.to_string_lossy().into_owned(),
        ]).unwrap().unwrap();
        assert_eq!(single.mode, "single");
        assert_eq!(single.output.as_deref(), Some(output.to_string_lossy().as_ref()));

        let request = directory.path().join("request");
        let line = format!("rawlab-batch-v1\n{}\t{}\n",
            input.to_string_lossy().as_bytes().iter().map(|byte| format!("{byte:02x}")).collect::<String>(),
            output.to_string_lossy().as_bytes().iter().map(|byte| format!("{byte:02x}")).collect::<String>());
        std::fs::write(&request, line).unwrap();
        let batch = parse_arguments(vec!["--external-batch".into(), request.to_string_lossy().into_owned()]).unwrap().unwrap();
        assert_eq!(batch.mode, "batch");
        assert_eq!(batch.sources.len(), 1);
    }

    #[test]
    fn rejects_incomplete_external_edit() {
        assert!(parse_arguments(vec!["--external-edit".into(), "/missing".into()]).is_err());
    }
}
