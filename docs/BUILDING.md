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

The VST3, the Audio Unit and the standalone app land in `build/Drosera_artefacts/Release/`, in `VST3/`, `AU/` and
`Standalone/`. They are not copied to the system plugin folders. Each is signed ad hoc once complete, so macOS loads it on
this Mac.

To build a universal macOS binary, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` when configuring. To build only
the DSP and its tests, with no download, add `-DDROSERA_BUILD_PLUGIN=OFF`.

## Test

```bash
build/render_test
```

Renders the engine offline. It covers finiteness and bounds at several block sizes, MIDI pitch by dominant partial,
the resampler, every preset, and that rendering never allocates.

```bash
build/voice_allocator_test
```

Checks how MIDI notes are assigned to the three voices, with no JUCE.

```bash
build/ui_test_artefacts/Release/ui_test
```

Covers the interface: the spectrum path to the plant, the plant's shape and how it follows the sound, the filter and
Tone arcs, the readouts, the setup panel, the window's scale, and what the editor shows while MIDI, POLY and WARP change
what sounds. It opens no window on screen.

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

## Package

The release is an installer package, built from the three bundles:

```bash
tools/package/build_pkg.sh build/Drosera_artefacts/Release 0.0.0 build/Drosera.pkg
```

```bash
tools/package/check_pkg.sh build/Drosera.pkg
```

`build_pkg.sh <artefacts dir> <version> <output.pkg>` makes one component package per format and combines them under
`tools/package/resources/distribution.xml`, with the installer's screens from the same folder. The Read Me screen is
generated from the licence files, so it always holds their current text.

`check_pkg.sh` expands a package without installing it. It checks:

- the three components, with their identifiers and install paths;
- that no bundle can be relocated;
- the minimum macOS version and the architectures the installer declares (the text, not the binaries);
- each bundle's code signature;
- that the packaged VST3 loads and renders in `plugin_host_test`, which must be built.

If the package is signed, it also checks the Installer signature, the notarization, the stapled ticket and the
hardened runtime. With `REQUIRE_SIGNED=1` it refuses an unsigned package.

```bash
tools/package/test_package.sh build/Drosera_artefacts/Release
```

Proves the two scripts: it builds good and deliberately broken packages, and checks that each is accepted or refused
as it should be.

A package built from a plain local build holds only this Mac's architecture. Release packages come from CI, which builds
universal.

After a change to the plugin's options in `CMakeLists.txt`, reconfigure and rebuild with `--clean-first`. CMake rewrites
the bundles' `Info.plist` when it configures, and an incremental build does not sign them again, so `check_pkg.sh`
then rejects their signatures.

`build_pkg.sh` signs and notarizes when all five of these are set, and refuses to run with only some of them:

| Variable | Holds |
|---|---|
| `DEVELOPER_ID_APPLICATION` | The full name of the Developer ID Application identity, such as `Developer ID Application: Name (TEAMID)` |
| `DEVELOPER_ID_INSTALLER` | The full name of the Developer ID Installer identity |
| `APPLE_ID` | The Apple ID used for notarization |
| `APPLE_TEAM_ID` | The team's ID |
| `APPLE_APP_PASSWORD` | An app-specific password for that Apple ID |

See [RELEASING.md](RELEASING.md#signing-and-notarization) for getting them.

## Other tools

- `build/pitch_probe [body] [first] [last] [step]` prints the pitch measured for a range of MIDI notes.
- `editor_snapshot <out.png> [paramId=value | program=N | note=N | sound=1 | setup=1 | scale=N ...]` draws the window
  to an image. `sound=1` renders a second of audio first, so the plant shows real sound. `setup=1` opens the setup panel,
  and `scale=N` draws the window at N times 900×560.
- `tools/hardware-comparison/` compares the plugin against a recording of a real unit. See its `PROTOCOL.md`.
