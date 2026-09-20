#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod engine;
mod integration;
mod preview;

fn main() {
    let external_session = match integration::parse_args() {
        Ok(session) => session,
        Err(error) => { eprintln!("{error}"); std::process::exit(2); }
    };
    tauri::Builder::default()
        .manage(preview::PreviewCache::default())
        .manage(engine::Engine::default())
        .manage(engine::PreviewEngine::default())
        .manage(engine::ProcessingEngine::default())
        .manage(engine::ActivePreviewEngine::default())
        .manage(external_session)
        .invoke_handler(tauri::generate_handler![
            preview::render_raw_preview,
            engine::process_raw,
            engine::render_processed,
            engine::render_raw_detail,
            engine::raw_source_version,
            engine::processing_models,
            engine::cancel_processing,
            engine::cancel_batch_processing,
            engine::desktop_control,
            integration::external_session,
            integration::run_external_batch,
            integration::finish_external
        ])
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .run(tauri::generate_context!())
        .expect("Unable to start RawLab desktop preview");
}
