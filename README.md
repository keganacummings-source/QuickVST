# Kyoto VST3 Quick Builder

A Windows x64 native VST3 player and ultra-fast CMake/JUCE VST3 builder.

## Two modes

### BUILD MODE
1. Start `KyotoVST3QuickBuilder.exe`.
2. Leave **BUILD MODE** enabled.
3. Drag an extracted GitHub/CMake VST3 project folder onto the window.
4. The app runs CMake, builds Release x64, finds the `.vst3` bundle, saves it under its build cache, and opens it immediately.
5. If compilation fails, the exact CMake/compiler output remains in the build console.

The cache lives under:

`%APPDATA%\KyotoVST3QuickBuilder\Builds\`

If the source signature has not changed, a previously built VST3 is launched without recompiling.

### PLAYER MODE
Turn **BUILD MODE** off.

Drop a `.vst3` folder directly onto the window. The app scans it as a native VST3, creates its editor, and routes it through the host audio device.

## Optimized for the uploaded Kyoto/KYOTRIPPAH project

The uploaded project is a CMake + JUCE project and contains `CMakeLists.txt`, multiple native C++ sources, and a VST3 build script. This application uses that same CMake/JUCE workflow, but keeps the build directory and JUCE dependency available between builds.

## Local development

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 with Desktop development with C++
- CMake 3.22+
- Git
- Internet access for the first JUCE fetch

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

The GitHub Actions workflow uses Ninja for faster parallel builds.

## Important VST3 limitation

A VST3 is a plugin, not a standalone EXE. The Quick Builder therefore includes a native VST3 host/player. "Open and run" means the freshly built VST3 is instantiated inside this host immediately after compilation.

## Security

The builder does not execute JavaScript, PowerShell, batch files, or arbitrary installer files from a dropped project. It intentionally builds through CMake only.

## License

The app source is intended for the project owner. JUCE remains subject to its own licensing terms.
