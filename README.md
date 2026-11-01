# Drosera

Feedback drone synth as a VST3 / AU / Standalone plugin.

Derived from the MIT-licensed firmware of [Audrey II](https://github.com/Synthux-Academy/Audrey-II), by Nick
Donaldson (Synthux Academy), for Daisy Seed. Drosera is an independent project, not affiliated with or endorsed
by Synthux Academy, and uses none of their artwork.

With the plugin-only features off, the DSP is the firmware's own code at a fixed 48 kHz.

## Plugin-only features

- **Audio input:** optional, excites the strings. Off until the host connects the input bus.
- **MIDI:** PITCH puts the drone on the played note; moving Frequency takes pitch back once no key is held.
  GATE opens the feedback only while a key is down.
- **TAPE:** swaps the echo for the octamad tape echo, adding Wow and Age.
- **Presets:** 14 factory presets, exposed to the host as programs.

## Readouts

Knob readouts show what the engine really uses, not the knob's position. Echo Feedback is 100 % at unity with
the firmware echo. With TAPE on the same knob follows the tape echo's steeper law, so its readout is only a
guide: repeats die off quickly below about 60 % of the knob's travel and self-oscillate above about 70 %.

WARP is the firmware's delay switch. It makes the echo 2.8 to 3.9 times shorter, and the readout does not
show the change.

## Build

Needs CMake 3.22+, Ninja and a C++17 compiler. CMake downloads JUCE 8.0.9.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

Plugins land in `build/Drosera_artefacts/Release/`. They are not copied to the system plugin folders.

## Tests

```bash
build/render_test
```

```bash
build/plugin_host_test_artefacts/Release/plugin_host_test "build/Drosera_artefacts/Release/VST3/Drosera.vst3"
```

`render_test` covers the engine, MIDI pitch, the tape echo and every preset. `plugin_host_test` loads the
real VST3 bundle. `hardware-comparison/` holds the protocol and tool for comparing against a real unit.

## Status

Not yet compared with hardware, validated with `pluginval` or `auval`, or tested across DAWs. Binaries are
not signed with a Developer ID.

## License

AGPLv3, see `LICENSE`. Third-party code keeps its own terms: see `THIRD_PARTY_LICENSES.md`.
