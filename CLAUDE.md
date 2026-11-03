# Drosera

Feedback drone synth plugin, built with JUCE. The DSP is the Audrey II firmware's engine plus plugin-only features.

## Commands

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

```bash
build/voice_allocator_test
```

```bash
build/render_test
```

```bash
build/ui_test_artefacts/Release/ui_test
```

```bash
build/plugin_host_test_artefacts/Release/plugin_host_test build/Drosera_artefacts/Release/VST3/Drosera.vst3 tests/fixtures/state-baseline.bin
```

```bash
tools/hardware-comparison/check_reference.sh build/render_cli
```

## Code map

| Path | Holds |
|---|---|
| `src/dsp/` | Engine, no JUCE. `DroseraCore.h` is its host-independent front end, fixed at 48 kHz. `FeedbackVoice` is one feedback loop and `VoiceAllocator` assigns MIDI notes to voices |
| `src/plugin/PluginProcessor.cpp` | Parameters, MIDI, resampling to and from 48 kHz |
| `src/plugin/PluginEditor.*` | The window: assembles the interface and runs its timer |
| `src/plugin/EditorBridge.h` | What the editor reads from the processor, beyond parameters |
| `src/plugin/SpectrumTap.h` | Lock-free FIFO from the audio thread to the display |
| `src/plugin/ui/` | Theme, knob drawing, plant, tabs. `SpectrumBands.h` and `Readouts.h` have no JUCE, so tests use them alone |
| `src/plugin/Presets.h` | Factory presets, plain C++ so tests can use them |
| `tests/` | `render_test`, `voice_allocator_test`, `ui_test`, `plugin_host_test`, `editor_snapshot` (draws the window to a PNG), measuring tools |
| `third_party/` | Vendored DaisySP |
| `tools/hardware-comparison/` | Comparison against a real unit, and the reference checksums |
| `docs/superpowers/` | Design specs and implementation plans |

## Rules

1. No allocation, locks or file access on the audio thread. `render_test` counts allocations.
2. With the plugin-only features off, renders stay byte-identical to `tools/hardware-comparison/reference/SHA256SUMS`.
   A change that alters them needs a stated reason and new checksums.
3. MIDI tuning is judged by the dominant partial, never by a fit to the note's harmonic grid: every partial of a
   note fits that grid, so such a test passes while the wrong harmonic sounds.
4. `third_party/` changes are recorded in that component's `PATCHES.txt`.
5. Parameter IDs never change, or saved projects break. Parameter names stay in English everywhere.

Do not regenerate `tests/fixtures/state-baseline.bin` to make a test pass.
