$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$tag = if ($env:GITHUB_REF_NAME) { $env:GITHUB_REF_NAME } else { "0.1.0" }
$out = Join-Path $root "dist"
New-Item -ItemType Directory -Force $out | Out-Null
$app = Join-Path $root "build\release\src\rawlab_app\rawlab-app.exe"
windeployqt --release --compiler-runtime --no-translations $app
$stage = Join-Path $out "RawLab-$tag-Windows-x86_64"
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item (Join-Path (Split-Path $app) "*") $stage -Recurse
Copy-Item (Join-Path $env:RUNNER_TEMP "onnx\lib\onnxruntime.dll") $stage
Copy-Item (Join-Path $env:VCPKG_INSTALLATION_ROOT "installed\x64-windows\bin\*.dll") $stage
Compress-Archive -Path $stage -DestinationPath (Join-Path $out "RawLab-$tag-Windows-x86_64.zip")
