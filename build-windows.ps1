param(
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"

# JUCE 8 requires a supported Microsoft toolchain on Windows.
# Import the Visual Studio developer environment so CMake/Ninja cannot pick MinGW.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "Visual Studio Installer / vswhere.exe was not found. Install Visual Studio with the Desktop development with C++ workload."
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw "No Visual Studio installation with MSVC C++ tools was found."
}

$devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $devCmd)) {
    throw "VsDevCmd.bat was not found at $devCmd"
}

cmd.exe /s /c "`"$devCmd`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {
    if ($_ -match '^(.*?)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake was not found in PATH."
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    throw "Ninja was not found in PATH. Install Ninja or add it to PATH."
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw "MSVC cl.exe was not found after loading Visual Studio."
}

$launcher = $null
if (Get-Command sccache -ErrorAction SilentlyContinue) { $launcher = "sccache" }
elseif (Get-Command ccache -ErrorAction SilentlyContinue) { $launcher = "ccache" }

$jobs = [Environment]::ProcessorCount
if ($jobs -lt 1) { $jobs = 1 }

$configure = @(
    "-S", ".",
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_C_COMPILER=cl.exe",
    "-DCMAKE_CXX_COMPILER=cl.exe",
    "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF",
    "-DFETCHCONTENT_BASE_DIR=$env:LOCALAPPDATA\KyotoVST3QuickBuilder\fetchcontent"
)
if ($launcher) {
    $configure += "-DCMAKE_C_COMPILER_LAUNCHER=$launcher"
    $configure += "-DCMAKE_CXX_COMPILER_LAUNCHER=$launcher"
    Write-Host "Compiler cache: $launcher"
}

cmake @configure
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

cmake --build $BuildDir --parallel $jobs
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

Write-Host "Build complete ($jobs jobs, Ninja, no LTCG)." -ForegroundColor Green
