# Building Drosera

## Requirements

- CMake 3.22 or newer, and Ninja
- macOS: Xcode command line tools
- Windows: Visual Studio 2022 with the C++ tools. Run the commands from a Developer Command Prompt

CMake downloads JUCE 8.0.9 the first time it configures.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

The plugins land in `build/Drosera_artefacts/Release/`. They are not copied to the system plugin folders.

To build a universal macOS binary, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` when configuring. To build only
the DSP and its tests, with no download, add `-DDROSERA_BUILD_PLUGIN=OFF`.

## Test

On Windows, add `.exe` to the program names.

```bash
build/render_test
```

Renders the engine offline. It covers finiteness and bounds at several block sizes, MIDI pitch by dominant partial,
the resampler, the tape echo, every preset, and that rendering never allocates.

```bash
build/plugin_host_test_artefacts/Release/plugin_host_test build/Drosera_artefacts/Release/VST3/Drosera.vst3 tests/fixtures/state-baseline.bin
```

Loads the built VST3 as a host would. It covers levels at several sample rates and block sizes, input, MIDI,
latency, presets, parameter identity, and that a saved project from the baseline build still loads.

The fixture comes from `plugin_host_test <vst3> --write-state <file>`. Do not regenerate it to make a test pass: it
exists to catch changes that break saved projects.

```bash
tools/hardware-comparison/check_reference.sh build/render_cli
```

macOS arm64 only. Renders 10 presets and compares them with `tools/hardware-comparison/reference/SHA256SUMS`, which
proves the engine still renders exactly the same.

## Other tools

- `build/pitch_probe [body] [first] [last] [step]` prints the pitch measured for a range of MIDI notes.
- `editor_snapshot <out.png> [paramId=value | program=N | note=N ...]` draws the editor to an image.
- `tools/hardware-comparison/` compares the plugin against a recording of a real unit. See its `PROTOCOL.md`.
