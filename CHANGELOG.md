# Changelog

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Three-string polyphony: up to three notes at once as separate feedback loops, with Coupling between them.
- The STRINGS tab, with POLY, LOCK, Coupling, Interval 2 and Interval 3. LOCK chooses between a tuned chord and the free drone.
- Three presets: Open Fifths, Sympathetic and Chord Keys.
- Echo Pong: a second echo, off by default, whose repeats bounce between left and right and saturate, with PONG, Drive, Tone, Width and Level in its own PONG tab.
- The Ember Pong preset.

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
