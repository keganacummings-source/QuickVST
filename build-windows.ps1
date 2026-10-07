param(
  [string]$Source = "."
)

$ErrorActionPreference = "Stop"
$Build = Join-Path $PSScriptRoot "build"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  throw "CMake is not installed or not on PATH."
}

$Generator = "Visual Studio 17 2022"
if (Get-Command ninja -ErrorAction SilentlyContinue) {
  $Generator = "Ninja"
}

if ($Generator -eq "Ninja") {
  cmake -S $Source -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build $Build --parallel
} else {
  cmake -S $Source -B $Build -G $Generator -A x64
  cmake --build $Build --config Release --parallel
}

$exe = Get-ChildItem $Build -Recurse -Filter "KyotoVST3QuickBuilder.exe" |
  Select-Object -First 1

if (-not $exe) { throw "Build finished without producing KyotoVST3QuickBuilder.exe" }

Write-Host "READY: $($exe.FullName)" -ForegroundColor Green
