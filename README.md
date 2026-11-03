# Drosera

[![CI](https://github.com/codeskine/Drosera/actions/workflows/ci.yml/badge.svg)](https://github.com/codeskine/Drosera/actions/workflows/ci.yml)
[![License: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0-blue.svg)](LICENSE)

![Drosera's window: a sundew whose tentacles show the sound's spectrum, Feedback at its heart, Frequency and Body beside it, and one row of filter, reverb and echo controls below](docs/images/drosera.png)

A feedback drone synth for macOS, as a VST3, an Audio Unit and a standalone app. A noise floor feeds two string
resonators inside a saturating loop, so it sounds on its own: you steer what the loop settles on with its knobs, with
audio, or with MIDI, up to three notes at once.

## Requirements

- macOS 11 or newer, on Apple silicon or Intel
- An administrator password, to install
- A host that loads VST3 or Audio Unit instruments, such as Logic Pro, Ableton Live or Reaper. The standalone app needs
  none.

To build from source: the Xcode command line tools, CMake 3.22 or newer, and Ninja. See [Building](#building-from-source).

## Installation

1. Download `Drosera-X.Y.Z-macos.pkg` from [Releases](https://github.com/codeskine/Drosera/releases).
2. Open it and follow the installer. It installs for every user on the Mac:

   | Format | Installed to |
   |---|---|
   | VST3 | `/Library/Audio/Plug-Ins/VST3/Drosera.vst3` |
   | Audio Unit | `/Library/Audio/Plug-Ins/Components/Drosera.component` |
   | Standalone app | `/Applications/Drosera.app` |

   All three are selected. To leave one out, click **Customize** at the Installation Type step.
3. Rescan plugins in your host. Drosera loads as an instrument.

### If macOS blocks the installer

Releases are not notarized by Apple yet, so the first time you open the installer macOS says it could not verify that
the installer is free of malware, and does not open it. Then:

1. Click **Done** in that warning. Do not move the file to the Bin.
2. Open **System Settings → Privacy & Security** and scroll down to Security. A line says the installer was blocked.
   Click **Open Anyway** and confirm with your password or Touch ID.
3. The installer opens. Install as above.

You do this once per downloaded installer. The plugins and the app it installs open normally, with no Terminal
command.

### Upgrading from 0.1.x

Version 0.1.x came as a zip, and its VST3 was copied by hand to `~/Library/Audio/Plug-Ins/VST3/`, in your home
folder. Delete `Drosera.vst3` there, or your host finds two copies of Drosera. `~/Library` is hidden in Finder: use
**Go → Go to Folder** and type the path.

Later versions install over the earlier one. Open the new installer and nothing else is needed.

### Uninstalling

Delete these three items, and empty the Bin:

- `/Library/Audio/Plug-Ins/VST3/Drosera.vst3`
- `/Library/Audio/Plug-Ins/Components/Drosera.component`
- `/Applications/Drosera.app`

The standalone app also keeps its audio settings in `~/Library/Application Support/Drosera.settings`. Delete it too to
leave nothing behind. Projects saved in your host keep their own copy of Drosera's settings.

## Quickstart

Once installed, this takes about two minutes. **Turn your speakers or headphones down first:** Drosera starts sounding
as soon as it loads, and the drone can swell.

### In the standalone app

1. Open **Drosera** from Applications. macOS asks whether it may use the microphone: either answer works for this
   quickstart.
2. Click **Options** at the top left, choose **Audio/MIDI Settings…**, and pick your speakers or headphones as the
   output. Close the window.
3. On first launch the "Default" preset is loaded and already droning. Turn **Frequency**, left of the plant, slowly: the drone moves to another
   pitch, and the plant's tentacles unfurl where the sound is.
4. Turn **Feedback**, in the plant's heart, down and back up. Low, the loop dies away; high, it sustains and
   saturates.
5. Open the preset selector at the top left and choose **Cathedral Bloom**, then **Ember Pong**, whose echo bounces
   between left and right.

### In a host

1. In Logic Pro, create a Software Instrument track and, in its instrument slot, choose **AU Instruments → codeskine →
   Drosera**. In another host, load **Drosera** as a VST3 instrument on a new track.
2. Select or arm the track. Drosera drones with no notes played.
3. Follow steps 3 to 5 above.
4. To play it from a keyboard, choose the **Keys (play notes)** preset. It turns MIDI Pitch and MIDI Gate on, in
   **SETUP** at the top right, so Drosera is silent until you hold a key, and then sounds that note.

The [manual](docs/MANUAL.md) explains every control, MIDI, the standalone app, and the known limits.

## Project structure

```text
.
├── src/
│   ├── dsp/                  # The engine, with no JUCE: the Audrey II firmware's loop and the plugin-only features
│   │                         #   DroseraCore.h is its host-independent front end, fixed at 48 kHz
│   └── plugin/               # The JUCE plugin
│       ├── PluginProcessor.cpp   # Parameters, MIDI, resampling to and from 48 kHz
│       ├── PluginEditor.*        # The window: assembles the interface and runs its timer
│       ├── Presets.h             # The 16 factory presets
│       └── ui/                   # Theme, fonts, knobs, the plant, header, control row, setup panel
├── tests/                    # render_test, voice_allocator_test, ui_test, plugin_host_test, and their fixtures
├── assets/fonts/             # The interface's fonts, embedded in the plugin, with their licences
├── third_party/DaisySP/      # Vendored DSP library; local changes are listed in its PATCHES.txt
├── tools/
│   ├── package/              # Builds and checks the installer, and holds its screens
│   ├── hardware-comparison/  # Comparison with a real unit, and the reference render checksums
│   └── release_check.py      # Checks a release tag against the version and the changelog
├── docs/                     # Manual, build and release guides, images
│   └── superpowers/          # Design specs and implementation plans
├── review/                   # Code review notes and sound design measurements
├── .github/workflows/        # CI on every push, and the release pipeline
└── CMakeLists.txt            # The build; downloads JUCE 8.0.9 on first configure
```

## Building from source

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

The first configure downloads JUCE, so it takes a few minutes. The VST3, the Audio Unit and the standalone app land in
`build/Drosera_artefacts/Release/`. [docs/BUILDING.md](docs/BUILDING.md) covers the tests, the installer package and
the other tools.

## Contributing

Contributions are welcome, under the project's licence, AGPLv3.

1. Fork [codeskine/Drosera](https://github.com/codeskine/Drosera) on GitHub and clone your fork.
2. Create a branch from `main` named for the change:

   ```bash
   git switch -c my-change
   ```

3. Make the change, and build and run the tests as [docs/BUILDING.md](docs/BUILDING.md) describes.
4. Add user-visible changes to the `Unreleased` section of [CHANGELOG.md](CHANGELOG.md).
5. Push the branch to your fork and open a pull request against `main`. CI must pass.

Read [CONTRIBUTING.md](CONTRIBUTING.md) first. Its rules protect the audio thread, the reference renders and saved
projects. [docs/RELEASING.md](docs/RELEASING.md) describes how releases are made.

## Status

Validated on macOS by automated tests, by `pluginval` on the VST3 and the Audio Unit, and by Apple's `auval` on the Audio
Unit. Not yet compared with a hardware unit. Releases are not yet signed and notarized by Apple.

## Credits

Derived from the MIT-licensed firmware of [Audrey II](https://github.com/Synthux-Academy/Audrey-II), by Nick Donaldson
(Synthux Academy), for Daisy Seed. Drosera is an independent project, not affiliated with or endorsed by Synthux
Academy, and uses none of their artwork.

## Licence

Copyright 2026 codeskine. Drosera is free software under the GNU Affero General Public License, version 3: see
[LICENSE](LICENSE). Third-party code and fonts keep their own terms: see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
