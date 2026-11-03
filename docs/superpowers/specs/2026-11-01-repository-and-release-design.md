# Repository and release design

> The release zip and the non-goals on installers and on AU and Standalone releases are replaced by
> `2026-11-03-macos-installer-design.md`.

Sub-project 1 of 2. The new interface is specified in
`2026-11-01-rugiada-interface-design.md` and is built on top of this work.

> **Changed after approval, by the owner:** Windows is dropped for now and the release is macOS only. The tape echo
> is removed. Sections below that mention Windows or TAPE are kept as the original design and do not apply.

## Goal

An open repository from which anyone can download a working Drosera VST3 for macOS or Windows, or build it
from source. A release is a GitHub Release holding the binaries, with the matching source and install
instructions, as AGPLv3 requires.

## Decisions

| Topic | Decision |
|---|---|
| Platforms | macOS and Windows |
| Published format | VST3 only. AU and Standalone can still be built, but are not released or supported |
| macOS architectures | Universal binary: arm64 and x86_64, minimum macOS 11 |
| macOS signing | Ad hoc for 0.x, with unblocking instructions. Notarization added later without reworking the pipeline |
| Windows signing | None. A VST3 is a library the host loads |
| Documentation language | English |
| Project license | AGPLv3, already in place |

## Repository layout

```text
Drosera/
├── src/
│   ├── dsp/                       engine, no JUCE dependency (today Source/DSP)
│   └── plugin/                    processor, editor, presets (today Source/)
│       └── ui/                    added by sub-project 2
├── tests/
├── third_party/
│   ├── DaisySP/
│   └── tapeecho/
├── tools/
│   └── hardware-comparison/       today hardware-comparison/
├── docs/
│   ├── MANUAL.md
│   ├── BUILDING.md
│   ├── RELEASING.md
│   ├── design/                    interface mockup
│   └── superpowers/specs/
├── .github/workflows/
│   ├── ci.yml
│   └── release.yml
├── CMakeLists.txt
├── README.md
├── CLAUDE.md
├── CHANGELOG.md
├── CONTRIBUTING.md
├── THIRD_PARTY_LICENSES.md
├── LICENSE
└── LICENSE-AudreyII
```

Files move with `git mv`, so history follows them. No code changes beyond include paths and CMake paths.

## Documents

- **README.md**, for musicians first: screenshot, what Drosera is, download and install on each system, the
  macOS unblocking steps, then short links for developers, the licence and the credit to the Audrey II firmware.
- **docs/MANUAL.md**: every control, MIDI behaviour, TAPE and WARP, the known limits. Replaces the "Readouts"
  and "Plugin-only features" sections of today's README.
- **docs/BUILDING.md**: requirements, build and test commands for each system, what each test covers.
- **docs/RELEASING.md**: the release steps below and how to turn on notarization.
- **CLAUDE.md**: build and test commands, a map of the code, and the rules below.
- **CHANGELOG.md**: Keep a Changelog format, with an `Unreleased` section and one section per version.
- **CONTRIBUTING.md**: how to build, the rules below, and that contributions are under AGPLv3.

### Rules recorded in CLAUDE.md and CONTRIBUTING.md

1. No allocation, locks or file access on the audio thread.
2. With the plugin-only features off, renders stay byte-identical to `reference/SHA256SUMS`. A change that
   alters them needs a stated reason and new checksums.
3. MIDI tuning is judged by the dominant partial, never by a fit to the note's harmonic grid: every partial of
   a note fits that grid, so such a test passes while the wrong harmonic sounds.
4. `third_party/` changes are recorded in that component's `PATCHES.txt`.
5. Parameter IDs never change, or saved projects break.

## Continuous integration

`ci.yml` runs on every push and pull request, on `macos-14` and `windows-latest`.

Each job configures with CMake and Ninja, builds, then runs:

1. `render_test`
2. `plugin_host_test` on the built VST3
3. `pluginval` at strictness 5 on the built VST3. pluginval is downloaded at a pinned version in the job.

The macOS job builds universal and runs on arm64. It also regenerates the reference renders with `render_cli`
and checks them against `SHA256SUMS`. This check is macOS arm64 only, because floating-point results can
differ slightly on other architectures.

JUCE is pinned by CMake to tag 8.0.9 and cached between runs.

### Windows port

Drosera has never been compiled on Windows, so this is the largest unknown here. Known work:

- `third_party/tapeecho/cpu.c` uses `__attribute__`, unknown to MSVC. Guard it with a macro and record the
  change in `PATCHES.txt`.
- Define `_USE_MATH_DEFINES` for `M_PI`.
- Build with MSVC and link its runtime statically, so users need no redistributable.
- The post-build `codesign` step is already limited to Apple.

If a test fails only on Windows, it is treated as a real bug unless shown otherwise.

## Release pipeline

`release.yml` runs when a tag `vX.Y.Z` is pushed.

1. Fail if the tag does not match the version in `project()` in `CMakeLists.txt`, or if `CHANGELOG.md` has no
   section for it.
2. Run everything in `ci.yml`.
3. Produce `Drosera-X.Y.Z-macos.zip` and `Drosera-X.Y.Z-windows.zip`, each holding `Drosera.vst3`, `LICENSE`,
   `THIRD_PARTY_LICENSES.md` and a short `INSTALL.txt`.
4. Publish SHA-256 checksums of both.
5. Create a **draft** GitHub Release with the changelog section as its notes. A person reviews and publishes it.

GitHub attaches the tagged source automatically. With JUCE pinned by CMake, that is the corresponding source.

**Notarization** is a step in the macOS job that runs only when these repository secrets exist:
`APPLE_CERTIFICATE`, `APPLE_CERTIFICATE_PASSWORD`, `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`.
Until then the job signs ad hoc and `INSTALL.txt` carries the unblocking steps. The step cannot be tested until
those secrets exist, and `RELEASING.md` says so.

## Order of work

1. Move the project out of the temporary session folder to one the owner chooses.
2. Restructure, keeping build and tests green.
3. Write the documents.
4. Port to Windows and turn on CI.
5. Add the release pipeline and try it on a pre-release tag such as `v0.1.0-rc1`.
6. Sub-project 2: the new interface.
7. Release 0.1.0 against the criteria below.

## Criteria for releasing 0.1.0

- CI green on both systems, `pluginval` included.
- Tried in a real DAW on each system, and listened to by the owner. If no Windows machine is available, the
  release notes say that Windows was only tested by CI.
- "Drosera" checked as a name against existing trademarks and plugins.
- Copyright holder chosen and stated in `README.md`.
- Licence of the tape echo's `TapeEchoBlock` reference checked. If it stays unclear, the TAPE switch is removed
  from 0.1.0 and returns in a later version. `third_party/tapeecho` then leaves the release build.

## Out of scope

- Linux builds, and installers: the release is a zip to unpack into the plugin folder.
- AU and Standalone releases.
- Windows code signing, and an Apple Developer account.
- Automatic updates.
- Moving DaisySP's `ReverbSc` and `Tone` to the upstream LGPL repository. The AGPLv3 release already meets
  their terms, as `THIRD_PARTY_LICENSES.md` explains.
