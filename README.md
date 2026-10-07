# Kyoto VST3 Quick Builder

Windows x64 JUCE native VST3 host + CMake/JUCE builder.

## Build Mode
Drop an extracted GitHub/CMake project folder. The application:
1. Detects `CMakeLists.txt`.
2. Uses Ninja when installed, otherwise Visual Studio 2022.
3. Configures and builds Release.
4. Finds the generated `.vst3`.
5. Copies it into `%APPDATA%\KyotoVST3QuickBuilder\Builds\<project>\dist`.
6. Launches the resulting VST3 immediately.
7. Shows the complete CMake/compiler output if anything fails.

A source signature prevents unnecessary recompilation when the project has not changed.

## Player Mode
Disable BUILD MODE and drop a `.vst3` folder. The app loads it with JUCE's native VST3 host and displays its editor.

## GitHub
The included workflow explicitly selects MSVC x64 and builds with Ninja. JUCE 8 does not support MinGW.

## Requirements for local development
Windows 10/11 x64, Visual Studio 2022 C++ tools, CMake, Git, and Internet access for the initial JUCE download.
