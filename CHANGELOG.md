# Changelog

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Three-string polyphony: up to three notes at once as separate feedback loops, with Coupling between them.
- POLY, LOCK, Coupling, Interval 2 and Interval 3. LOCK chooses between a tuned chord and the free drone.
- Three presets: Open Fifths, Sympathetic and Chord Keys.
- Echo Pong: a second echo, off by default, whose repeats bounce between left and right and saturate, with PONG, Drive, Tone, Width and Level.
- The Ember Pong preset.

### Changed

- A new window, on one page with no tabs: the plant in the centre with Frequency and Body beside it, one row of FILTER,
  REVERB, PONG and ECHO below, and a SETUP panel for Strings, Output and MIDI. Red marks the controls inside the
  feedback loop, amber those after it.
- The plant is drawn as a sundew whose tentacles unfurl with the sound, and marks the strings' notes under POLY.
- An arc over Highpass and Lowpass draws the band that survives in the loop, and one over Tone draws the echo's band-pass.
  A dot beside Echo Time blinks at the real time between repeats.
- The window scales from 75 % to 150 %, and the scale is saved with the project in place of the open tab.
- Interval readouts show the interval a voice really plays when the note range holds it back, tagged LIMIT.
- The interface's fonts, Inter and Cormorant Garamond, are embedded, under the SIL Open Font License.

### Fixed

- The echo's first repeats after loading, or after the engine recovers from a bad sample, came late and warped:
  its delay line started at 5 s and glided towards the knob for seconds, settling up to 3 ms short of it. It now
  starts at the knob. The three reference renders with echo change throughout, the seven without are byte-identical,
  so `tools/hardware-comparison/reference/SHA256SUMS` was regenerated.
- Bypass delays the input by the declared latency, so hosts at rates other than 48 kHz keep the bypassed audio aligned.
- A host that prepares with a sample rate of 0 no longer makes the resampling buffers unbounded.
- The noise generator multiplied a signed integer past its range, which is undefined behaviour: it now wraps
  as unsigned. Same sound.
- The parameter registry no longer depends on the standard library's hash-map order: its handlers run in a
  fixed order, the one the reference renders were made with.
- The Audio Unit is built and validated with `auval` and `pluginval` in CI.
- JUCE is fetched by commit, not by tag.
- A restored project takes its parameters at once instead of gliding from the defaults, so it settles on the
  same drone every time it loads. Presets chosen in the window still glide.
- The manual says how far Reverb Mix detunes low MIDI notes, from measurement.

## [0.1.1] - 2026-11-01

### Fixed

- Echo Time's readout with WARP on follows the knob when the host is not processing audio.
- A long MIDI message, such as a sysex, no longer allocates on the audio thread.
- VoiceOver can name the Feedback knob.
- A preset chosen in the window updates the host's own preset display.

### Known limits

- macOS only. The release is not notarized, so a downloaded plugin needs the unblocking command in `INSTALL.txt`.
- The universal binary's Intel part is built, but has not been run.
- Not yet compared with a hardware unit.

## [0.1.0] - 2026-11-01

First release.

### Added

- VST3 plugin for macOS 11 or newer, universal.
- The Audrey II firmware's engine, at a fixed 48 kHz, with bridging for other host sample rates.
- Optional audio input that excites the strings.
- MIDI Pitch and MIDI Gate.
- 12 factory presets.
- An interface built around a sundew that shows the real spectrum, with readouts of what really sounds.

### Known limits

- macOS only. The release is not notarized, so a downloaded plugin needs the unblocking command in `INSTALL.txt`.
- The universal binary's Intel part is built, but has not been run.
- Not yet compared with a hardware unit.
