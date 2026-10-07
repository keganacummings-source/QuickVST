# Kyoto VST3 Quick Builder 1.0.4

Windows-native JUCE VST3 builder/host.

## Drop support
- GitHub/CMake project folder: build, cache, and launch the first VST3 output.
- ZIP containing a GitHub/CMake project: extract, locate `CMakeLists.txt`, build, cache, and launch.
- `.vst3` folder: load it directly.
- ZIP containing a `.vst3`: extract and load it.
- `.wav`: play immediately and loop continuously. Press Escape to stop the WAV.

## Plugin display
When a VST3 is loaded, the builder UI is hidden and only the plugin editor is shown inside the window. Press **Escape** to close the plugin and return to the builder.

## Faster compiles
Dropped projects are configured for speed, not for a whole-program Release link:

- Ninja when it is on PATH (`ninja` or `ninja.exe`). The Visual Studio generator is only the fallback, and it gets `/MP`.
- Job count matches the CPU count.
- `ccache` or `sccache` is passed as the compiler launcher when installed.
- Unity builds (`CMAKE_UNITY_BUILD`) batch translation units.
- `/GL` and `/LTCG` are not enabled. Those flags were the slow rebuild path.
- JUCE FetchContent is stored once under `%LOCALAPPDATA%\\KyotoVST3QuickBuilder\\fetchcontent` so each dropped project does not re-clone JUCE.
- Repeat configures set `FETCHCONTENT_UPDATES_DISCONNECTED`.

Install Ninja and ccache (or sccache) on the build machine for the full speedup. The app's own CMakeLists uses the same cache and drops LTCG.

## Build requirements
Windows 10/11, CMake, and a Visual Studio/MSVC toolchain. The GitHub workflow uses MSVC x64 and Ninja.

```
powershell -ExecutionPolicy Bypass -File .\build-windows.ps1
```

FIXED8: JUCE 8 compatibility fixes for WAV looping, ZIP extraction, file detection, and Escape keyboard handling.
