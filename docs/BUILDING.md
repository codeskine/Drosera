# Building Drosera

## Requirements

- macOS with the Xcode command line tools
- CMake 3.22 or newer, and Ninja

Windows is not supported for now. Commit `4898524` built and passed CI on Windows, and is the place to restart from.

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

```bash
build/render_test
```

Renders the engine offline. It covers finiteness and bounds at several block sizes, MIDI pitch by dominant partial,
the resampler, every preset, and that rendering never allocates.

```bash
build/ui_test_artefacts/Release/ui_test
```

Covers the interface: the spectrum path to the plant, the readouts, tabs, and what the editor shows while MIDI and WARP
change what sounds. It opens no window on screen.

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

On arm64 only. Renders 10 presets and compares them with `tools/hardware-comparison/reference/SHA256SUMS`, which
proves the engine still renders exactly the same.

## Other tools

- `build/pitch_probe [body] [first] [last] [step]` prints the pitch measured for a range of MIDI notes.
- `editor_snapshot <out.png> [paramId=value | program=N | note=N | tab=N | sound=1 ...]` draws the window to an image.
  `sound=1` renders a second of audio first, so the plant shows real sound.
- `tools/hardware-comparison/` compares the plugin against a recording of a real unit. See its `PROTOCOL.md`.
