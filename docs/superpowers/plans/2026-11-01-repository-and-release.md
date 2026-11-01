# Repository and Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the Drosera checkout into an open repository whose CI proves macOS and Windows builds and whose tags produce a draft GitHub Release holding installable VST3 zips.

**Architecture:** Files move with `git mv` into `src/`, `tools/` and `docs/`. One `ci.yml` builds, tests and validates on both systems; `release.yml` reuses it through `workflow_call`, then publishes the zips it packaged. Tag and changelog rules live in one small Python script that runs locally and in CI.

**Tech Stack:** CMake 3.22+, Ninja, JUCE 8.0.9, C++17, MSVC and Apple Clang, GitHub Actions, pluginval 1.0.4, Python 3 standard library.

**Spec:** `docs/superpowers/specs/2026-11-01-repository-and-release-design.md`

## Global Constraints

- Platforms macOS and Windows. Published format VST3 only. AU and Standalone stay buildable.
- macOS universal, arm64 and x86_64, minimum macOS 11. Ad hoc signing for 0.x.
- Documentation is in English. Parameter names stay in English everywhere.
- No allocation, locks or file access on the audio thread.
- With plugin-only features off, renders stay byte-identical to `tools/hardware-comparison/reference/SHA256SUMS`.
- MIDI tuning is judged by the dominant partial, never by a fit to the note's harmonic grid.
- `third_party/` changes are recorded in that component's `PATCHES.txt`.
- Parameter IDs never change.
- Work on branch `repo-release`, cut from `main`. Never commit to `main` directly.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **Owner approval gates:** the first push of the branch, merging to `main`, and every tag are outward-facing. Stop and ask before each one. The repository is public.
- Local tools are in `/private/tmp/claude-501-drosera/env/bin`, and that folder can be wiped. If it is gone: `python3 -m venv` there, then `pip install cmake ninja numpy scipy`. Prefix local commands with `PATH=/private/tmp/claude-501-drosera/env/bin:$PATH`.

Standard local build, used by every task below:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

## Review Focus

1. **A musician downloads the macOS zip through a browser.** The bundle must survive zipping and unzipping with its executable bit and signature, at the top level of the zip. Task 6 verifies the unpacked copy, not the build folder.
2. **A project saved with today's build is reopened in a later one.** Every parameter must return to its saved value. Task 1 pins this with a state file captured before anything moves.
3. **Someone builds from the source zip GitHub attaches to the release, with no `.git`.** AGPLv3 depends on this working. Task 2 builds from `git archive` output.
4. **A release is tagged by mistake**, with a version or changelog that does not match, or a malformed tag. It must fail before any upload. Task 6 tests each case.
5. **A NaN reaches the feedback loop during playback** through the audio input. Sound must recover on its own without allocating. Task 3 tests it.

---

### Task 1: Pin today's behaviour before moving anything

**Files:**
- Modify: `.gitignore`
- Modify: `tests/plugin_host_test.cpp`
- Create: `tests/fixtures/state-baseline.bin`

**Interfaces:**
- Produces: `plugin_host_test <path to .vst3> [state fixture]`. Without the second argument the state check is skipped, so CI must always pass it.

- [ ] **Step 1: Create the branch and fix `.gitignore`**

```bash
git switch -c repo-release
```

Add `.superpowers/` and `.claude/` to `.gitignore`. Then `git status --short` must print only `.gitignore` and `docs/superpowers/plans/`.

- [ ] **Step 2: Build at the new location and confirm the baseline**

Delete nothing outside the repository. Run the standard build, then `build/render_test`, then `plugin_host_test` on the VST3. Both print `ALL OK`. Render the references and check them:

```bash
mkdir -p /private/tmp/claude-501-drosera/ref && build/render_cli hardware-comparison/presets.txt /private/tmp/claude-501-drosera/ref 20 && cd /private/tmp/claude-501-drosera/ref && shasum -a 256 -c /Volumes/Archiviazione/codeskine/drosera/hardware-comparison/reference/SHA256SUMS
```

Expected: 10 lines ending `OK`. If anything fails here, stop: the move changed something and no later result can be trusted.

- [ ] **Step 3: Write the failing parameter and state checks in `plugin_host_test.cpp`**

`checkParameterIdentity(inst)` compares the hosted parameter list, in order, against this golden list of `{id, name}`: `freq Frequency`, `fbgain Feedback`, `body Body`, `lpf Lowpass`, `hpf Highpass`, `revmix Reverb Mix`, `revdecay Reverb Decay`, `echosend Echo Send`, `echotime Echo Time`, `echofb Echo Feedback`, `volume Volume`, `echohalve Echo Warp`, `inputgain Input Level`, `midipitch MIDI Pitch`, `midigate MIDI Gate`, `echotape Echo Tape`, `tapewow Tape Wow`, `tapeage Tape Age`.

Assert the name, and that `HostedAudioProcessorParameter::getParameterID()` equals `juce::String(juce::String(id).hashCode() & 0x7fffffff)`. If that hash form does not match on the current, untouched build, print the observed IDs and hard-code those instead: the values the current build reports are the contract.

`checkSavedState(inst, fixture)` loads the file with `setStateInformation` and asserts these knob values within 0.001: `freq` 0.61, `fbgain` 0.83, `lpf` 0.37, `echotape` 1, `tapewow` 0.52.

- [ ] **Step 4: Generate the fixture from the current, unmoved code**

Add a hidden mode `plugin_host_test <vst3> --write-state <file>` that sets those five values and writes `getStateInformation`. Generate `tests/fixtures/state-baseline.bin`. The mode stays in the file so the fixture can be explained, and `docs/BUILDING.md` mentions it in Task 4.

- [ ] **Step 5: Run and confirm**

Run: `build/plugin_host_test_artefacts/Release/plugin_host_test build/Drosera_artefacts/Release/VST3/Drosera.vst3 tests/fixtures/state-baseline.bin`
Expected: `ALL OK`. Change one golden name temporarily, confirm `SOME FAILED`, then restore it.

- [ ] **Step 6: Commit**

```bash
git add .gitignore docs/superpowers/plans tests/plugin_host_test.cpp tests/fixtures/state-baseline.bin && git commit -m "Pin parameter identity and saved state before restructuring"
```

---

### Task 2: Move to the new layout

**Files:**
- Move: `Source/DSP/*` to `src/dsp/`
- Move: `Source/PluginProcessor.cpp`, `PluginEditor.*`, `Presets.h` to `src/plugin/`
- Move: `hardware-comparison/` to `tools/hardware-comparison/`
- Create: `tools/hardware-comparison/check_reference.sh`
- Modify: `CMakeLists.txt`, `.gitignore`, `src/plugin/PluginProcessor.cpp`

**Interfaces:**
- Produces: `tools/hardware-comparison/check_reference.sh <render_cli path>`, exit 0 when all 10 renders match `reference/SHA256SUMS`.
- Produces: `src/dsp` on the include path of `DroseraDSP`. Plugin code includes `"DroseraCore.h"`, not `"DSP/DroseraCore.h"`.

- [ ] **Step 1: Write `check_reference.sh`**

Quote every path, because this repository has been built from paths with spaces. Use `mktemp -d`, render with `presets.txt` for 20 seconds, then run `shasum -a 256 -c` in that folder against the script's own `reference/SHA256SUMS`. Remove the folder on exit. It must return non-zero on a mismatch.

- [ ] **Step 2: Confirm it detects a mismatch, before moving**

Change one hex digit of `SHA256SUMS`, run it and expect a non-zero exit. Restore the digit and expect exit 0.

- [ ] **Step 3: Move with `git mv`**

Afterwards `git status` shows renames only, and `Source/` no longer exists. Update `CMakeLists.txt` paths, `.gitignore` paths and the header comment at `src/plugin/PluginProcessor.cpp:2`. Change `#include "DSP/DroseraCore.h"` and `"DSP/SincResampler.h"` to the bare names. `render_test` gets include directories `tests` and `src/plugin`. Make no other code change.

- [ ] **Step 4: Build clean and verify**

Remove `build/` and run the standard build. Then:
- `build/render_test` prints `ALL OK`.
- `plugin_host_test` with the fixture prints `ALL OK`.
- `tools/hardware-comparison/check_reference.sh build/render_cli` exits 0.
- `codesign --verify --deep --strict build/Drosera_artefacts/Release/VST3/Drosera.vst3` exits 0.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "Move sources to src/ and tools/, keeping behaviour identical"
```

- [ ] **Step 6: Build from an exported source tree, as a release recipient would**

```bash
D=$(mktemp -d); git archive HEAD | tar -x -C "$D" && cmake -S "$D" -B "$D/b" -G Ninja -DCMAKE_BUILD_TYPE=Release -DDROSERA_BUILD_PLUGIN=OFF && cmake --build "$D/b" && "$D/b/render_test"
```

Expected: `ALL OK`. A failure means a needed file is untracked or ignored, so fix that in a follow-up commit. This proves the DSP and vendored code are complete; Task 6 proves the plugin the same way on a real tag.

---

### Task 3: Make the audio thread honour rule 1

The spec records "no allocation on the audio thread" as a rule, and the code breaks it today: `DroseraCore::Render` calls `Engine::Init` when it meets a NaN, and `Engine::Init` calls `make_unique` five times. Publishing the rule while breaking it is not acceptable, so this is added to the spec's work. It also fixes a real defect if filter state is not cleared, because a NaN in a biquad never leaves.

**Files:**
- Modify: `src/dsp/FeedbackSynthEngine.cpp:10-59`, `src/dsp/BiquadFilters.h`, `src/dsp/TapeEcho.h` if its reset is incomplete
- Test: `tests/render_test.cpp`

**Interfaces:**
- Produces: `Engine::Init` may be called repeatedly, and only its first call allocates.

- [ ] **Step 1: Write the failing tests in `render_test.cpp`**

Replace global `operator new` and `operator delete` with counting versions guarded by a `static std::atomic<bool> gCountAllocations`.

`noAllocationOnRender()`: after `core.Init()`, with counting on, render 2 seconds in blocks of 64 while toggling `SetMidiPitch`, `SetEchoTape`, `SetGate` and `SetEchoWarp` every 100 ms, and call `Render` once with a NaN in `in`. Assert the count is 0.

`recoversFromNan()`: render 1 second of the Default preset, render one block with `in[0] = NaN`, then render 3 seconds with no input. Assert every sample after that block is finite, and the RMS of the last second is above 0.01.

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --target render_test && build/render_test`
Expected: `noAllocationOnRender` reports a non-zero count. `SOME FAILED`.

- [ ] **Step 3: Implement**

In `Engine::Init`, allocate each `echo_delay_`, `verb_` and `tape_echo_` only when the pointer is null, and call `Reset()` on tape echoes that already exist. Ensure `Init` on `LPF12`, `HPF12` and `BPF24` zeroes their delay state, and that `TapeEcho::Reset` zeroes `ring_`, `state_`, `in_`, `out_`, `pos_` and `write_`. Scalar settings such as `fb_gain_` stay as they are, so the sound comes back with the same knobs.

- [ ] **Step 4: Verify**

`build/render_test` prints `ALL OK`, and `tools/hardware-comparison/check_reference.sh build/render_cli` exits 0.

- [ ] **Step 5: Commit**

```bash
git add src/dsp tests/render_test.cpp && git commit -m "Recover from NaN without allocating on the audio thread"
```

---

### Task 4: Write the documents

**Files:**
- Modify: `README.md`, `THIRD_PARTY_LICENSES.md`
- Rewrite in English: `tools/hardware-comparison/PROTOCOL.md`
- Create: `docs/MANUAL.md`, `docs/BUILDING.md`, `CLAUDE.md`, `CHANGELOG.md`, `CONTRIBUTING.md`

**Interfaces:**
- Produces: `CHANGELOG.md` headings in exactly the form `## [Unreleased]` and `## [X.Y.Z] - YYYY-MM-DD`, which Task 6's script parses.

- [ ] **Step 1: Write `docs/MANUAL.md`**

Every control under its final tab group (Filters, Reverb, Echo, Output, MIDI) with what it really does. Include MIDI behaviour, with the limit that a knob moved while a key is held does not retake pitch after release. Include TAPE, WARP (2.8 to 3.9 times shorter), and known limits: notes 16 to 18 and 80 may stay off in some settings, and Echo Feedback with TAPE on follows a steeper law. Move the "Readouts" and "Plugin-only features" sections here from the README. Describe the controls, not the layout: the current interface changes in sub-project 2.

- [ ] **Step 2: Write `docs/BUILDING.md` and `README.md`**

`BUILDING.md`: requirements, build and test commands per system, what each test covers, the `--write-state` mode.

`README.md`, for musicians first: what Drosera is, download, install to `~/Library/Audio/Plug-Ins/VST3/` or `C:\Program Files\Common Files\VST3\`, the macOS unblocking command below, then short links for developers, the licence and the Audrey II credit. Leave the screenshot to sub-project 2.

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Drosera.vst3
```

- [ ] **Step 3: Write `CLAUDE.md`, `CONTRIBUTING.md`, `CHANGELOG.md`**

`CLAUDE.md`: build and test commands, a code map, and the five rules from the spec, with the reference path written in full. Add: CI is the authority for byte-identical checks, if Task 5 finds that local and CI toolchains differ. `CONTRIBUTING.md`: how to build, the same five rules, contributions under AGPLv3. `CHANGELOG.md`: Keep a Changelog format with `## [Unreleased]` listing the current features.

- [ ] **Step 4: Update `THIRD_PARTY_LICENSES.md` and translate `PROTOCOL.md`**

Change `Source/DSP/` to `src/dsp/`. In `PROTOCOL.md` remove the private `/private/tmp` path and the first-person account, and keep every measurement and the SHA-256. Fix the commands to the new paths.

- [ ] **Step 5: Verify the documents against reality**

Run every command in `BUILDING.md` as written. `grep -rn "Source/" README.md THIRD_PARTY_LICENSES.md docs/MANUAL.md docs/BUILDING.md CLAUDE.md CONTRIBUTING.md` prints nothing. No Italian remains in `PROTOCOL.md`.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "Write the manual, build guide, contributor rules and changelog"
```

---

### Task 5: Port to Windows and turn on CI

**Files:**
- Modify: `CMakeLists.txt`, `third_party/tapeecho/cpu.c`, `third_party/tapeecho/PATCHES.txt`, and whatever else MSVC rejects
- Create: `.github/workflows/ci.yml`

**Interfaces:**
- Produces: `ci.yml` callable through `workflow_call` with an optional string input `version`, and `secrets: inherit`. When `version` is set, each job uploads an artifact `drosera-macos` or `drosera-windows` holding `Drosera-<version>-<macos|windows>.zip`.

- [ ] **Step 1: Make the known portability changes**

In `CMakeLists.txt`, before `project()`: `set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")`. After `project()`: `set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")` and, under `MSVC`, `add_compile_definitions(_USE_MATH_DEFINES)`.

In `third_party/tapeecho/cpu.c` define `TE_ATTR_USED` and `TE_NOINLINE` empty under `_MSC_VER`, and use them at lines 188 and 277. Record this in `PATCHES.txt`.

- [ ] **Step 2: Verify macOS is unchanged, as universal**

Configure a separate `build-universal` with `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. Expected: `lipo -archs` on the VST3 binary prints both, `vtool -show-build` shows minos 11.0, `render_test` and `plugin_host_test` pass, and `check_reference.sh` exits 0.

- [ ] **Step 3: Write `ci.yml`**

Triggers: `push`, `pull_request`, `workflow_call`. Permissions `contents: read`. Matrix `macos-14`, `windows-latest`, `fail-fast: false`, `shell: bash`. Steps, in order:

1. `actions/checkout@v4`.
2. `actions/cache@v4` on `.deps`, key `juce-8.0.9-${{ runner.os }}`. Configure with `-DFETCHCONTENT_BASE_DIR=.deps`.
3. Ninja: `brew install ninja`, or `choco install ninja -y`. On Windows, `ilammy/msvc-dev-cmd@v1`.
4. Configure Release with Ninja. On macOS add `-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64`.
5. Build targets `render_test render_cli plugin_host_test Drosera_VST3`.
6. `render_test`.
7. `plugin_host_test` with `tests/fixtures/state-baseline.bin`.
8. Download `pluginval_macOS.zip` or `pluginval_Windows.zip` from `https://github.com/Tracktion/pluginval/releases/download/v1.0.4/`, then run `pluginval --strictness-level 5 --validate-in-process --validate <VST3>`. On macOS the binary is `pluginval.app/Contents/MacOS/pluginval`.
9. macOS only: `check_reference.sh build/render_cli`.
10. Windows only: `dumpbin /dependents` on the inner `Contents/x86_64-win/Drosera.vst3`, failing if `VCRUNTIME` or `MSVCP` appears.
11. If `inputs.version` is set: the packaging step from Task 6.

- [ ] **Step 4: Ask the owner to approve the first push, then push**

```bash
git push -u origin repo-release
```

- [ ] **Step 5: Iterate until green**

Read failures with `gh run view --log-failed`. A failure on Windows only is a real bug unless shown otherwise. Fix code, recording anything in `third_party/` in its `PATCHES.txt`. Fix pluginval findings in the plugin, not by lowering strictness. `--skip-gui-tests` is allowed only if the log shows the runner has no display, and then say so in `BUILDING.md`.

Tuning on Windows is judged by the dominant-partial test already in `render_test`. Do not loosen its thresholds.

- [ ] **Step 6: Resolve the reference check if only CI differs**

`libm` and the compiler differ between the owner's Mac and `macos-14`, so checksums may differ with no change to the DSP. If so, render on both, confirm the largest sample difference is below 1e-4, replace `SHA256SUMS` with the CI values, and state that reason in the commit message. Update `CLAUDE.md`: CI is the authority, and a local mismatch alone is not a failure. A larger difference is a real bug: stop and investigate.

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "Port to Windows and add CI for macOS and Windows"
```

---

### Task 6: Add the release pipeline and try it

**Files:**
- Create: `tools/release_check.py`, `tools/test_release_check.py`, `.github/workflows/release.yml`, `docs/INSTALL.txt`, `docs/RELEASING.md`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Produces: `tools/release_check.py <tag> [--repo-root DIR]`. Exit 0 prints the release notes to stdout. Exit 1 prints the reason to stderr.
- Consumes: Task 5's `version` input and artifacts, and Task 4's changelog headings.

- [ ] **Step 1: Write the failing tests in `tools/test_release_check.py`**

Standard-library `unittest` over temporary folders holding a `CMakeLists.txt` with `project(Drosera VERSION 0.1.0 ...)` and a `CHANGELOG.md`:

| Test | Tag | Changelog | Expected |
|---|---|---|---|
| `test_final_ok` | `v0.1.0` | has `## [0.1.0] - 2026-12-01` with text | exit 0, notes are that section only |
| `test_version_mismatch` | `v0.2.0` | has `[0.2.0]` | exit 1, names both versions |
| `test_missing_section` | `v0.1.0` | only `[Unreleased]` | exit 1 |
| `test_empty_section` | `v0.1.0` | `[0.1.0]` with no text | exit 1 |
| `test_rc_uses_unreleased` | `v0.1.0-rc1` | only `[Unreleased]` with text | exit 0, notes are that section |
| `test_rc_mismatch` | `v0.2.0-rc1` | `[Unreleased]` | exit 1 |
| `test_malformed` | `v0.1`, `0.1.0`, `v0.1.0-beta` | any | exit 1 |

A release candidate cannot match on `project()` as written, because CMake versions hold no suffix, so the comparison uses the part before `-rc`. The spec does not mention this case; this is the decision.

- [ ] **Step 2: Run to verify they fail**

Run: `python3 tools/test_release_check.py`
Expected: FAIL, script not found.

- [ ] **Step 3: Implement `release_check.py`, then run again**

Tag pattern `^v(\d+\.\d+\.\d+)(-rc\d+)?$`. Expected: all pass. Add `python3 tools/test_release_check.py` to `ci.yml` on macOS only.

- [ ] **Step 4: Write `docs/INSTALL.txt` and the packaging step**

`INSTALL.txt`: the folder for each system, the macOS unblocking command, the source URL `https://github.com/codeskine/Drosera`, and that the source for this version is attached to the release.

Each zip holds at its top level `Drosera.vst3`, `LICENSE`, `THIRD_PARTY_LICENSES.md`, `INSTALL.txt`, and a `licenses/` folder with `LICENSE-AudreyII`, `DaisySP-LICENSE` and `tapeecho-LICENSE`. The spec omits that folder, but MIT requires those notices to travel with binaries.

macOS: `ditto -c -k --sequesterRsrc` on a staging folder, to preserve the bundle. Windows: `Compress-Archive`.

After zipping, in the same job: unzip to a new folder, run `plugin_host_test` on the unpacked bundle, and on macOS run `codesign --verify --deep --strict` on it. Upload the zip with `actions/upload-artifact@v4`.

- [ ] **Step 5: Add conditional notarization to the macOS packaging**

Map the five secrets `APPLE_CERTIFICATE`, `APPLE_CERTIFICATE_PASSWORD`, `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD` to `env`, and use `if: env.APPLE_ID != ''`. Steps: import the certificate, `codesign --force --options runtime --timestamp` with Developer ID, `xcrun notarytool submit --wait`, `xcrun stapler staple`, all before zipping. Otherwise the ad hoc signature from the build stands.

- [ ] **Step 6: Write `release.yml` and `docs/RELEASING.md`**

`release.yml`: on tags `v*`. Job `check` runs `release_check.py`, exposes the version without the `v` and whether it is a release candidate. Job `build` uses `./.github/workflows/ci.yml` with `secrets: inherit`. Job `publish` on `ubuntu-latest` with `contents: write` downloads both artifacts, writes `SHA256SUMS.txt`, and runs `gh release create "$TAG" --draft --title "Drosera $VERSION" --notes-file notes.md`, adding `--prerelease` for a release candidate.

`RELEASING.md`: the steps; how to turn on notarization; that the notarization path is **untested until the secrets exist**; and that the x86_64 slice is built but no CI job runs it.

- [ ] **Step 7: Ask the owner to approve the merge and the release candidate tag**

Merge `repo-release` to `main` only after they agree. Then:

```bash
git tag v0.1.0-rc1 && git push origin v0.1.0-rc1
```

- [ ] **Step 8: Verify the draft release**

`gh release view v0.1.0-rc1 --json isDraft,isPrerelease,assets` shows a draft pre-release with three assets. Download both zips into a new empty folder, run `shasum -a 256 -c SHA256SUMS.txt`, and list each zip to confirm `Drosera.vst3` is at the top level. Install the macOS one through the README's steps exactly, and run `plugin_host_test` on it.

- [ ] **Step 9: Build the plugin from the release's source zip**

Download the source zip into a new empty folder, and run the standard build and both tests with the plugin on. Expected: `ALL OK`.

- [ ] **Step 10: Delete the candidate and commit any fixes**

Ask the owner before `gh release delete v0.1.0-rc1 --cleanup-tag`. Commit whatever this task changed:

```bash
git add -A && git commit -m "Add the release pipeline"
```

---

### Task 7: Settle the name, the licence and the copyright holder

These decide whether TAPE reaches the interface in sub-project 2, so they come before it, not at release time.

**Files:**
- Modify: `THIRD_PARTY_LICENSES.md`, `README.md`, `docs/RELEASING.md`

- [ ] **Step 1: Check the tape echo's reference**

Find the `TapeEchoBlock` named in `third_party/tapeecho/README.md`, read its licence, and establish whether any of its code is in `cpu.c` or whether it was a behavioural reference only. Record the source links and finding in `THIRD_PARTY_LICENSES.md`, replacing "licence not checked".

- [ ] **Step 2: Check the name**

Search existing audio plugins, synth products and trademark databases (USPTO, EUIPO) for "Drosera" in class 9 software and musical instruments. Record what was found in `docs/RELEASING.md`, stating that this is a search and not legal advice.

- [ ] **Step 3: Ask the owner for three decisions**

Present the findings, then ask: keep or remove TAPE for 0.1.0 if the licence stays unclear; keep the name if there is a conflict; and the copyright holder's exact wording for the README and `LICENSE` notice. Do not choose these on the owner's behalf.

- [ ] **Step 4: Apply and commit**

State the copyright holder in `README.md`. If TAPE is removed: take the `echotape`, `tapewow` and `tapeage` code paths out of the release build behind a CMake option, and keep the parameter IDs reserved. Then run all checks from Task 2 Step 4.

```bash
git add -A && git commit -m "Record the name and licence checks and the copyright holder"
```
