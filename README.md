# Kyoto VST3 Quick Builder 1.0.3

Windows-native JUCE VST3 builder/host.

## Drop support
- GitHub/CMake project folder: build, cache, and launch the first VST3 output.
- ZIP containing a GitHub/CMake project: extract, locate `CMakeLists.txt`, build, cache, and launch.
- `.vst3` folder: load it directly.
- ZIP containing a `.vst3`: extract and load it.
- `.wav`: play immediately and loop continuously. Press Escape to stop the WAV.

## Plugin display
When a VST3 is loaded, the builder UI is hidden and only the plugin editor is shown inside the window. Press **Escape** to close the plugin and return to the builder.

## Build requirements
Windows 10/11, CMake, and a Visual Studio/MSVC toolchain. The GitHub workflow uses MSVC x64 and Ninja.
