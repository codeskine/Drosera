# Changelog

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

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
