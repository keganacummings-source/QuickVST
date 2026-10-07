# Kyoto VST3 Quick Host

A deliberately small Windows VST3 host focused on fast startup and quick plugin loading.

## Features

- Drag a `.vst3` folder/bundle into the window to load it.
- Drag a folder containing a `.vst3` bundle; the first VST3 found is loaded.
- Browser scans:
  - `C:\Program Files\Common Files\VST3`
  - `C:\Program Files (x86)\Common Files\VST3`
- Plugin paths are cached under `%APPDATA%\Kyoto\VST3QuickHost\plugins.txt`.
- Startup uses the cached list immediately and refreshes the filesystem index afterward.
- Plugins open in their native JUCE-hosted editor.
- No web UI, Electron, Chromium, or embedded browser is required.

## Important performance note

The host itself is intentionally lightweight. It cannot make an individual VST3's DSP cheaper: a heavy synth/effect can still consume substantial CPU/RAM. The host avoids unnecessary work, keeps scanning separate from plugin instantiation, and uses the normal realtime audio callback.

## Build

The included GitHub Actions workflow builds the Windows x64 Release package.

1. Create a GitHub repository.
2. Upload this project with the same folder structure.
3. Open **Actions**.
4. Run **Build Kyoto VST3 Quick Host**.
5. Download the Windows artifact.

The build downloads JUCE during CMake configuration.
