param(
  [string]$OnnxRuntimeLibDir = $env:ONNXRUNTIME_LIB_DIR,
  [string]$VcpkgBinDir = $env:VCPKG_BIN_DIR
)

$ErrorActionPreference = "Stop"

if (-not $OnnxRuntimeLibDir) {
  throw "ONNXRUNTIME_LIB_DIR must point to the ONNX Runtime lib directory"
}

$root = (Resolve-Path "$PSScriptRoot/..").Path
$stage = Join-Path $root "desktop/src-tauri/bundle/windows"
$engine = if ($env:RAWLAB_ENGINE_SOURCE) {
  $env:RAWLAB_ENGINE_SOURCE
} else {
  Join-Path $root "build/release/src/rawlab_preview/rawlab-preview.exe"
}

if (-not (Test-Path $engine)) {
  throw "Release worker not found: $engine"
}

Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item (Join-Path $OnnxRuntimeLibDir "onnxruntime.dll") $stage

if ($VcpkgBinDir -and (Test-Path $VcpkgBinDir)) {
  Copy-Item (Join-Path $VcpkgBinDir "*.dll") $stage
}

$windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
if (-not $windeployqt) {
  throw "windeployqt not found on PATH (Qt is required to stage the runtime)"
}
& $windeployqt.Source --release --no-translations --no-opengl-sw --no-quick-import --dir $stage $engine

Write-Host "Staged Windows runtime in $stage"
