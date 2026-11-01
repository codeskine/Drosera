# Rugiada interface design

Sub-project 2 of 2. It builds on `2026-11-01-repository-and-release-design.md`, which creates `src/plugin/`
and the CI that covers this work.

> **Changed after approval, by the owner:** the tape echo is removed, so there is no TAPE, Wow or Age, and the
> table rows about them do not apply. The release is macOS only.

## Goal

Replace today's grid of equal knobs with an interface that gives Drosera its own identity, makes the
instrument clearer to use, and shows the real sound. Scalable windows are not a goal.

## Direction

**Rugiada**: a sundew seen from above is the interface. Each tentacle is a frequency band, and its drop of dew
swells with the sound. The approved mockup is `docs/design/rugiada-layout-mockup.html`, option 2. Its motion is
simulated: this document decides where the two differ.

## Layout

The window is a fixed 760×500, drawn as vectors so it stays sharp on Retina displays.

- **Header:** "DROSERA" on the left. On the right, the preset selector with previous and next arrows.
- **Foreground:** the plant in the centre with **Feedback** as its heart. **Frequency** and **Body** are large
  knobs either side.
- **Tabs below**, one visible at a time:

| Tab | Controls |
|---|---|
| Filters | Lowpass, Highpass |
| Reverb | Mix, Decay |
| Echo | Send, Time, Feedback, WARP, TAPE. With TAPE on, also Wow and Age |
| Output | Volume, Input |
| MIDI | PITCH, GATE |

The open tab is remembered in the plugin state. It is not a parameter, so it is not automatable.

Parameter names are in English everywhere: interface, host, documents and presets. Parameter IDs do not change,
so saved projects keep working.

Colours, from the mockup:

| Use | Colour |
|---|---|
| Background | `#120a10` |
| Panel | `#1c0d15` |
| Plant stalk | `#a33244` |
| Dew and main accent | `#ffc27a` |
| Secondary knob | `#c98a6a` |
| Knob track | `#3a1a24` |
| Text | `#e9d6c8` |
| Secondary text | `#a98f88` |

## The plant

The plant has 54 tentacles, each mapped to one band, log-spaced from 30 Hz to 16 kHz.

A band's level from −72 dBFS to 0 dBFS sets the tentacle's length and the size and brightness of its drop.
Levels rise with a 40 ms attack and fall with a 300 ms release.

There is **no invented motion**. A silent plugin shows a still plant at rest, so its state can be read at a glance.

## Signal path to the display

```text
audio thread                         message thread (30 Hz timer)
processBlock ──► SpectrumTap ──FIFO──► FFT ──► SpectrumBands ──► PlantView
```

- **`SpectrumTap`** (`src/plugin/SpectrumTap.h`): a single-producer, single-consumer lock-free FIFO of 4096 samples,
  holding the mono sum of the final output at the host's rate. It allocates only on preparation. If the FIFO is
  full, new samples are dropped: the display may lose data, audio is never delayed.
- It writes only while an editor is open, signalled by an atomic flag. With the window closed the plugin does no
  display work.
- **FFT** on the message thread: 2048 points, Hann window, using `juce_dsp`.
- **`SpectrumBands`** (`src/plugin/ui/SpectrumBands.h`): a plain function from FFT magnitudes and sample rate to band
  levels, with no JUCE dependency, so it can be tested alone.

## Honest readouts

Knobs show what sounds, not where the knob points. The sources are the following.

| Situation | Display |
|---|---|
| MIDI pitch in control | Frequency shows the note name and Hz, tagged MIDI, and the knob is dimmed |
| MIDI pitch in control | Body shows the real snapped time in ms, tagged SNAP, and the knob is dimmed |
| WARP on | Echo Time shows the real, shortened time |
| TAPE off | Wow and Age are hidden |
| TAPE on | Echo Feedback shows a mark at the tape's unity point, about 69 % of the knob's travel, and its percent readout is hidden because it no longer applies |

`DroseraCore` publishes the sounding note and the real Body and Echo times through relaxed atomics, written where
those values are already computed. These writes do not touch the audio, so rule 2 of the repository design still
holds. The editor reads them on its timer.

## Components

`src/plugin/ui/`, each with one job:

| File | Job |
|---|---|
| `Theme.h` | Colours and fonts |
| `DroseraLookAndFeel.h/.cpp` | Knob and toggle drawing |
| `SpectrumBands.h` | FFT magnitudes to band levels |
| `PlantView.h/.cpp` | Draws the plant and holds Feedback |
| `TabPanel.h/.cpp` | Tab strip and the visible tab's controls |
| `Readouts.h` | Text for each control, from parameters and the published values |

`src/plugin/PluginEditor.*` shrinks to assembling these and running the timer. The processor gains only the tap
and the published values.

## Failure and edge cases

- **Sample rate changes:** `prepareToPlay` resets the tap, and bands are computed from the current rate.
- **Bands above Nyquist:** at low host rates they stay at rest.
- **Non-finite samples:** the tap replaces them with silence, so a NaN cannot reach the display.
- **Editor closed while audio runs:** the flag stops writes, and reopening flushes the FIFO.
- **Several instances:** each has its own tap. Nothing is shared.

## Testing

Automated, in a new `tests/ui_test.cpp` run by CI:

- A sine at a known frequency lights the expected tentacle and its neighbours only.
- Silence puts every band at rest.
- `SpectrumTap` keeps samples in order under a producer and consumer running at once, and drops rather than blocks
  when full.
- Each readout in the table above produces the expected text.

Also:

- `editor_snapshot` gains `tab=N`, and each tab is reviewed from its image.
- `plugin_host_test` and the reference render checks must pass unchanged.

By hand, because they cannot be automated here:

- Using the mouse, in a DAW, on each system.
- GUI CPU use, with a target below 5 % of one core with the window open, on the owner's Mac.
- Whether the plant looks right with real sound. Mapping and ranges may need tuning by ear and eye.

## Out of scope

- Resizable windows and themes.
- New sound features, or any change to the DSP.
- Showing the signal's route on screen.
- User presets.
- Translated labels.
