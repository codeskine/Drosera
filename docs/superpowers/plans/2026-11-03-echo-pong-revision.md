# Echo Pong Revision Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make PONG a mode of the firmware's echo, whose two lines feed each other so the repeats bounce between left and right, and remove `PongDelay`.

**Architecture:** `EchoDelay` splits `Process` into `Read` and `Write` and gains a movable band-pass center and a shaped read. A new JUCE-free `StereoEcho` owns the two lines, the PONG ramp, Drive, Tone, Width and Level, and runs the firmware's code unchanged when the ramp is at 0. `Engine` holds a `StereoEcho` in place of `echo_delay_` and `pong_delay_`.

**Tech Stack:** C++17, JUCE 8.0.9, DaisySP `7f67768` (vendored), CMake with Ninja.

**Spec:** `docs/superpowers/specs/2026-11-03-echo-pong-revision-design.md`

## Global Constraints

- With PONG off, renders stay byte-identical to `tools/hardware-comparison/reference/SHA256SUMS`. No new checksums. A failure is a bug.
- No allocation, locks or file access on the audio thread. `render_test` counts allocations.
- `tests/fixtures/state-baseline.bin` is not regenerated. `third_party/` does not change.
- Parameter IDs, names and layout order do not change: `echopong`, `echodrive`, `echotone`, `echowidth`, `echolevel`.
- Do not add entries to `registry_` or to `DroseraCore::Param`.
- Work on the current branch, `claude/festive-mendel-815ec4`.
- Commit messages are one imperative sentence, as in `git log`, and end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Feedback percentages are engine values, as the readout shows them: knob position × 150%.
- The PONG ramp is 20 ms: `1.0f / 960.0f` per sample at 48 kHz, snapped to its target as `Engine` does today.

**Commands used throughout:**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

```bash
build/render_test
```

```bash
tools/hardware-comparison/check_reference.sh build/render_cli
```

## Review Focus

1. Tone automated while the loop runs away (Feedback 150%, Drive 1): moving band-pass coefficients in a resonant loop must stay finite and never trigger `Recover`. Test: `pongToneSweepWhileRunningAway`, Task 4.
2. `Recover`, or `Init`, with PONG on: `EchoDelay::Init` resets the center to 800 Hz, and the echo must come back at Tone's center. Test: `pongToneSurvivesRecover`, Task 4.
3. A project loaded with PONG on and Tone far from 800 Hz: the first block already sounds at Tone's center, with no 20 ms slide from 800 Hz. Test: `pongToneFromFirstBlock`, Task 4.
4. PONG switched during a runaway tail: the loop gain changes with the ramp, so the switch must stay bounded and click-free there too. Test: `pongSwitchDuringRunaway`, Task 4.
5. PONG switched off with Drive and Tone far from their defaults: the coefficients must still return bit for bit. Test: `pongBackToFirmware` uses Drive 0.9 and Tone 0.1, Task 4.

---

### Task 1: Split EchoDelay and run the firmware through StereoEcho

**Files:**
- Modify: `src/dsp/EchoDelay.h`
- Create: `src/dsp/StereoEcho.h`
- Modify: `src/dsp/FeedbackSynthEngine.h`, `src/dsp/FeedbackSynthEngine.cpp`
- Test: `tests/render_test.cpp`

**Interfaces:**
- Produces, in `EchoDelay<MaxLength>`:
  - `float Read()`: advances the time smoothing, reads, band-passes, soft-clips. The first half of today's `Process`.
  - `void Write(float fed_back, float in)`: stores `fed_back * feedback_ + in`.
  - `float Process(float in)`: `out = Read(); Write(out, in); return out;`
- Produces, in `src/dsp/StereoEcho.h`, `namespace infrasonic`, `template<size_t MaxLength> class StereoEcho`:
  - `void Init(float sample_rate)`: allocates the two `std::unique_ptr<EchoDelay<MaxLength>>` lines on its first call only, then calls `Init` on both.
  - `void SetDelayTime(float time_s, bool immediately = false)`, `void SetFeedback(float feedback)`, `void SetLagTime(float time_s)`: pass to both lines.
  - `void Process(float inL, float inR, float &wetL, float &wetR)`: in this task, only `wetL = lineL->Process(inL); wetR = lineR->Process(inR);`
- `Engine` keeps `pong_delay_` and its crossfade in this task. Only the classic wet comes from `StereoEcho`.

- [ ] **Step 1: Write the failing test**

```cpp
// The firmware's branch of StereoEcho is two EchoDelay lines, bit for bit.
static bool stereoEchoMatchesTwoLines() {
    using Line = infrasonic::EchoDelay<48000 * 5>;
    auto left = std::make_unique<Line>(), right = std::make_unique<Line>();
    auto stereo = std::make_unique<infrasonic::StereoEcho<48000 * 5>>();
    // Init(48000), SetDelayTime(0.3f, true), SetFeedback(1.2f), SetLagTime(0.5f) on all three.
    // 3 s of different noise on each side (std::minstd_rand, fixed seeds), amplitude 0.3.
    // pass: std::memcmp of left's output against stereo's wetL, and right's against wetR, is 0.
}
```

Call it from `main` next to `pongDelayTests()`.

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build`
Expected: compile error, `StereoEcho.h` not found.

- [ ] **Step 3: Split `EchoDelay::Process` and write `StereoEcho`**

`Engine` replaces `echo_delay_[2]` with `std::unique_ptr<StereoEcho<kMaxEchoDelaySamp>> stereo_echo_`, allocated where `echo_delay_` was. `Engine::Init` calls `stereo_echo_->Init`, then `SetDelayTime(5.0f, true)`, `SetFeedback(0.5f)` and `SetLagTime(0.5f)`, as it does today for each line. `SetEchoDelayTime` and `SetEchoDelayFeedback` go to `stereo_echo_`. In `ProcessEcho`:

```cpp
float wetL, wetR;
stereo_echo_->Process(sampL * echo_send_, sampR * echo_send_, wetL, wetR);
```

The `PongDelay` crossfade after it stays as it is.

- [ ] **Step 4: Run the tests and the reference check**

Run: `cmake --build build && build/render_test && tools/hardware-comparison/check_reference.sh build/render_cli`
Expected: `stereoEchoMatchesTwoLines: OK`, every other test as before, and the reference check passes with no new checksums. If the reference check fails, compare the generated code before changing anything else: the split must not change the order of floating-point operations.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/EchoDelay.h src/dsp/StereoEcho.h src/dsp/FeedbackSynthEngine.h src/dsp/FeedbackSynthEngine.cpp tests/render_test.cpp
git commit -m "Run the firmware's echo through StereoEcho, with EchoDelay split into Read and Write"
```

### Task 2: Shaped read and movable center in EchoDelay

**Files:**
- Modify: `src/dsp/EchoDelay.h`
- Test: `tests/render_test.cpp`

**Interfaces:**
- Consumes: `Read`, `Write`, `Process` from Task 1.
- Produces, in `EchoDelay<MaxLength>`:
  - `float ReadShaped(float drive_gain)`: as `Read`, but returns `daisysp::SoftClip(drive_gain * bpf_.Process(x))`.
  - `void SetCenter(float hz)`: when `hz` differs from the current center, calls `bpf_.SetParams(hz, 1.0f)`. `Init` sets the current center to 800.

- [ ] **Step 1: Write the failing tests**

```cpp
// Moving the center away and back gives the coefficients Init gives.
static bool echoCenterReturnsBitForBit();
// Two lines with Init(48000), SetDelayTime(0.1f, true), SetFeedback(0.9f). One gets SetCenter(300.0f) then
// SetCenter(800.0f) before any audio. 2 s of the same noise into both through Process.
// pass: memcmp of the two outputs is 0.

// ReadShaped(1) followed by Write is Process.
static bool echoShapedAtUnityIsProcess();
// Two identical lines, same noise for 2 s. One runs Process(in), the other out = ReadShaped(1.0f); Write(out, in).
// pass: memcmp of the two outputs is 0.
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmake --build build`
Expected: compile error, no member `SetCenter` or `ReadShaped`.

- [ ] **Step 3: Add `ReadShaped` and `SetCenter`**

`Read` and `ReadShaped` share the time smoothing and the read through one private helper, so the two paths cannot drift apart.

- [ ] **Step 4: Run the tests and the reference check**

Run: `cmake --build build && build/render_test && tools/hardware-comparison/check_reference.sh build/render_cli`
Expected: both new tests `OK`, everything else unchanged, the reference check passes.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/EchoDelay.h tests/render_test.cpp
git commit -m "Give EchoDelay a shaped read and a band-pass center that can move"
```

### Task 3: PONG mode in StereoEcho

**Files:**
- Modify: `src/dsp/StereoEcho.h`
- Test: `tests/render_test.cpp`

**Interfaces:**
- Consumes: `ReadShaped`, `Write`, `SetCenter`, `Process` from Tasks 1 and 2.
- Produces, in `StereoEcho<MaxLength>`:
  - `void SetPong(bool on, bool immediate = false)`: sets the target. With `immediate`, the ramp jumps there.
  - `void SetDrive(float drive)`: 0..1.
  - `void SetTone(float hz)`: the center PONG moves toward.
  - `void SetWidth(float width)`: 0..1.
  - `void SetLevel(float gain)`: linear.
  - `float PongMix() const`: the ramp, `x`.
  - `Init` sets `x` from the current target, so a project saved with PONG on starts on it, and forgets the center it last applied, since `EchoDelay::Init` puts it back to 800 Hz.
- Defaults: drive 0, tone 800 Hz, width 1, level 1, PONG off.

`Process` moves `x` by the ramp step toward its target first, then:

- `x == 0`: the explicit firmware branch from Task 1. When `x` has just reached 0, both lines get `SetCenter(800.0f)` before it runs.
- `x > 0`: the table in the spec's "x > 0" section, in that order: Drive, Tone, Read, Input, Feedback, Width, Level. The center is `800 * (tone / 800)^x`, passed through `SetCenter`, so it is recomputed only when it changes. At `x == 1` that is every time Tone's smoothed value moves, not every sample.

- [ ] **Step 1: Write the failing tests**

These drive `StereoEcho` alone, with `SetPong(true, true)` unless stated. A helper `renderStereo(echo, n, input)` feeds the same `input(i)` to both sides and returns `{L, R}`, as `renderPong` does today.

| Test | Setup | Assertions |
|---|---|---|
| `stereoBounces` | Impulse 0.1, time 0.25 s, Feedback 0.7 | For k = 1..3, the peak within ±40 samples of kT is on the left for odd k and on the right for even k, above 0.01. The other side's peak in that window is below `1e-3` of it |
| `stereoTiming` | Same, at 0.25 s and at `DroseraCore::EchoTimeSeconds(0.5f, true)` | The peaks at k = 1, 2, 3 are within ±2 samples of the k-th peak of a single `EchoDelay` line (Feedback 0.7) fed the same impulse. The band-pass delays every repeat by the same few samples in both |
| `stereoSameColor` | Impulse 0.1, Feedback 0, Drive 0, Tone 800, PONG on against off | The first repeat's magnitude spectrum on the left, from `magnitudeSpectrum` over a window that starts 100 samples before T, is within 0.5 dB at 200 Hz, 800 Hz and 3.2 kHz |
| `stereoToneMovesCenter` | Impulse 0.1, Feedback 0, Tone 300 Hz, then 3 kHz | The first repeat's spectral peak is within a third of an octave of the center: `fabs(log2(peak / center)) <= 1.0 / 3.0` |
| `stereoRunaway` | 800 Hz burst of 2400 samples at 0.1, time 0.1 s, level after 20 s | Drive 1: RMS above 0.1 at Feedback 0.6, below `1e-3` at Feedback 0.4. Drive 0: below `1e-3` at Feedback 0.9 |
| `stereoFeedbackZeroIsOneRepeat` | Impulse 0.1, Feedback 0, Drive 1, time 0.1 s | The right side is exactly 0 throughout. The left's peak after 1.5T is below `3.2e-5` of the first repeat's |
| `stereoWidthZeroIsMono` | As `pongWidthZeroIsMono` | L equals R sample for sample, and the first repeat is half the full-width side's, within `1e-6` |
| `stereoWidthAndLevelLeaveTheLoop` | As `pongWidthAndLevelLeaveTheLoop`, with an 800 Hz burst | Same assertions: the shaped output is the plain one's mid/side with Width 0.3 and Level 0.5 within `1e-6`, still sustaining after 4 s |
| `stereoAliasing` | 3100 Hz at 0.5, Drive 1, Tone 3200 Hz, Feedback 0 and 0.9, through `inharmonicDb` | Measure first, then set `kAliasLimitDry` and `kAliasLimitLoop` 3 dB above the values measured, with a comment giving those values |
| `stereoRampSettles` | `SetPong(true)`, 960 samples, then `SetPong(false)`, 960 samples | `PongMix()` reads exactly 1.0f after the first 960 and exactly 0.0f after the second |

- [ ] **Step 2: Run them to see them fail**

Run: `cmake --build build`
Expected: compile error, no member `SetPong` in `StereoEcho`.

- [ ] **Step 3: Implement PONG mode in `StereoEcho::Process` and the setters**

- [ ] **Step 4: Run the tests and the reference check**

Run: `cmake --build build && build/render_test && tools/hardware-comparison/check_reference.sh build/render_cli`
Expected: every `stereo*` test `OK`, with the measured aliasing printed. The reference check passes, since `Engine` never turns PONG on in `StereoEcho` yet.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/StereoEcho.h tests/render_test.cpp
git commit -m "Add PONG to StereoEcho: the two lines feed each other, with Drive, Tone, Width and Level"
```

### Task 4: Switch Engine and DroseraCore to StereoEcho's PONG and remove PongDelay

**Files:**
- Modify: `src/dsp/FeedbackSynthEngine.h`, `src/dsp/FeedbackSynthEngine.cpp`
- Modify: `src/dsp/DroseraCore.h`
- Delete: `src/dsp/PongDelay.h`
- Test: `tests/render_test.cpp`

**Interfaces:**
- Consumes: `StereoEcho`'s setters and `PongMix` from Task 3.
- Produces: `Engine::SetPong(bool, bool)`, `SetPongDrive(float)`, `SetPongTone(float hz)`, `SetPongWidth(float)`, `SetPongLevel(float gain)` and `PongMix()` keep their signatures and forward to `stereo_echo_`.
- `DroseraCore`: `kPongToneMin = 200.0f`, `kPongToneMax = 3200.0f`. `echo_registry_`'s Drive defaults to `0.0f` and Tone to `800.0f`, LOG, smoothing unchanged. `echo_drive_norm_` defaults to `0.0f`.

`ProcessEcho` becomes the `StereoEcho` call followed by `sampL = 0.5f * (sampL + wetL)` and the same for the right. `pong_delay_`, `pong_on_`, `pong_mix_`, the crossfade, `#include "PongDelay.h"` and `src/dsp/PongDelay.h` go.

- [ ] **Step 1: Replace the PongDelay tests and adapt the engine tests**

- Delete `pongDelayTests` and every test it calls, `makePong`, `renderPong` and `#include "PongDelay.h"`. Their replacements are Task 3's `stereo*` tests.
- Keep, with their limits: `pongOnFromFirstBlock`, `pongSwitchClickFree`, `pongSwitchReverses`, `pongBouncesThroughCore`, `pongWidthAndLevelThroughCore`, `pongRecoversFromNan`, `pongSurvivesTimeSweep`, `everyPresetWithPongOn`, `pongIndependentOfBlockSize`, `emberPongSelfSustains`.
- `pongBoundsAtMaximum` also asserts that the output's mean over its last 30 s is within ±0.01 on each side. The dry drone has no DC, and the limiter after the echo makes subtracting a dry render meaningless. The DC limit was `pongBlocksDc`'s.
- Add:

| Test | Setup | Assertions |
|---|---|---|
| `pongMonoSum` | Preset "Default" through `DroseraCore` for 10 s, PONG on against off | RMS of `L + R` over the last 5 s with PONG on is at most 3 dB below PONG off |
| `pongBackToFirmware` | Two cores with preset "Default" and Echo Send 0. One switches PONG on with Drive 0.9 and Tone 0.1, renders 1 s, switches it off and renders 1 s. The other renders the same 2 s without PONG. Then both set Echo Send to 0.3 | The next 5 s match byte for byte |
| `pongToneSweepWhileRunningAway` | `kPongMax`, Drive 1, Tone switched between 0 and 1 every 0.5 s for 30 s | Finite, peak within 1.0001, `RecoveryCount() == 0` |
| `pongToneSurvivesRecover` | PONG on, Tone at 300 Hz, Feedback 0, a short burst, then `Recover()` and the same burst | The first repeat's spectral peak is within a third of an octave of 300 Hz before and after |
| `pongToneFromFirstBlock` | `Init` with PONG on and Tone at 300 Hz already set, one burst | The first repeat's spectral peak is within a third of an octave of 300 Hz |
| `pongSwitchDuringRunaway` | `kPongMax`, Drive 1, running away for 4 s, PONG off then on 2 s apart | `jumpsWithinLimit` on both sides at both switches, and finite |

- [ ] **Step 2: Run them to see them fail**

Run: `cmake --build build && build/render_test`
Expected: the build succeeds while `PongDelay` still exists. `pongToneSurvivesRecover` and `pongToneFromFirstBlock` fail, because PONG still plays `PongDelay`, and its Tone is a low-pass.

- [ ] **Step 3: Switch `Engine` and `DroseraCore`, and delete `PongDelay.h`**

- [ ] **Step 4: Run all the tests and the reference check**

Run: `cmake --build build && build/render_test && build/voice_allocator_test && tools/hardware-comparison/check_reference.sh build/render_cli`
Expected: every test `OK` and the reference check passes. `emberPongSelfSustains` may fail, because "Ember Pong" still holds values for `PongDelay`. That failure is Task 6's. Note it in the commit message, and leave the test as it is.

- [ ] **Step 5: Commit**

```bash
git add -A src/dsp tests/render_test.cpp
git commit -m "Make PONG bounce the firmware's echo and remove PongDelay"
```

### Task 5: Parameters, readouts and preset default

**Files:**
- Modify: `src/plugin/PluginProcessor.cpp`, around lines 408 to 425
- Modify: `src/plugin/Presets.h`
- Test: `tests/plugin_host_test.cpp`

**Interfaces:**
- `echodrive`: default `0.0f`. Its readout stays `"+" + v * 12 + " dB"`.
- `echotone`: readout `fmap(v, 200.0f, 3200.0f, LOG)`, rounded, `" Hz"`.
- `Preset::echoDrive` defaults to `0.0f`. Its comment on `echoPong` becomes: the echo's repeats bounce between left and right.

- [ ] **Step 1: Update the host test's expectations**

- The "older" check expects `Echo Drive` at `0.0f`. Projects from before PONG load at the new default. The fixture does not change.
- The readout cases: `{"Echo Tone", 0.5f, "800 Hz"}`, `{"Echo Tone", 0.0f, "200 Hz"}`, `{"Echo Tone", 1.0f, "3200 Hz"}`, and `{"Echo Drive", 0.5f, "+6.0 dB"}` stays.

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build && build/plugin_host_test_artefacts/Release/plugin_host_test build/Drosera_artefacts/Release/VST3/Drosera.vst3 tests/fixtures/state-baseline.bin`
Expected: FAIL on Echo Drive's default and on the Tone readouts.

- [ ] **Step 3: Change the defaults and the readout**

- [ ] **Step 4: Run the host test, the UI test and the render test**

Run: `cmake --build build && build/plugin_host_test_artefacts/Release/plugin_host_test build/Drosera_artefacts/Release/VST3/Drosera.vst3 tests/fixtures/state-baseline.bin && build/ui_test_artefacts/Release/ui_test && build/render_test`
Expected: all pass, apart from `emberPongSelfSustains` if Task 4 left it failing.

- [ ] **Step 5: Commit**

```bash
git add src/plugin/PluginProcessor.cpp src/plugin/Presets.h tests/plugin_host_test.cpp
git commit -m "Default Echo Drive to 0 and show Echo Tone as the band-pass center"
```

### Task 6: Ember Pong, manual and limits

**Files:**
- Modify: `src/plugin/Presets.h`, the "Ember Pong" row
- Modify: `docs/MANUAL.md`, the Pong tab and Known limits
- Test: `tests/render_test.cpp` (`emberPongSelfSustains`, `everyPresetWithPongOn`)

- [ ] **Step 1: Set the new values by measurement**

Start from the current knobs, with Drive 0.8, Tone `0.146f` (300 Hz: `log(300 / 200) / log(16)`), Width 1.0 and Level 0 dB. With Drive 0.8 each pass gains `2^0.8 ≈ 1.74`, so the loop runs away above about 57% Feedback. Start the Echo Feedback knob at 0.42 (63%). Raise it in steps of 0.02 until `emberPongSelfSustains` passes with RMS above `1e-3`. Stop as soon as `everyPresetWithPongOn` still holds a peak within 1.0001.

- [ ] **Step 2: Run the render test and the host test**

Run: `cmake --build build && build/render_test && build/plugin_host_test_artefacts/Release/plugin_host_test build/Drosera_artefacts/Release/VST3/Drosera.vst3 tests/fixtures/state-baseline.bin`
Expected: all `OK`.

- [ ] **Step 3: Update the manual**

- Pong tab table:
  - Echo Drive: 0 to +12 dB into the echo's soft clip, 0 dB by default.
  - Echo Tone: 200 Hz to 3.2 kHz, the center of the echo's band-pass, 800 Hz by default as in the firmware.
  - Echo Pong: makes the firmware's echo bounce between left and right.
- The paragraph under the table: PONG sums the input to mono and starts on the left. Echo Time is the time between bounces. The color is the firmware's until Drive and Tone move. Keep the note on Drive and Feedback (runaway above 100% at Drive 0, above 50% at Drive 1), and the note on Width and Level.
- Delete the sentence comparing PONG with the firmware's band-pass and the low end.
- Known limits: replace the aliasing figures with those `stereoAliasing` printed, for the 3.1 kHz tone at half scale and Tone at 3.2 kHz. Keep "Ember Pong" in the list of values chosen by measurement and not yet judged by ear.

- [ ] **Step 4: Commit**

```bash
git add src/plugin/Presets.h docs/MANUAL.md tests/render_test.cpp
git commit -m "Retune Ember Pong for the bouncing firmware echo and update the manual"
```

- [ ] **Step 5: Hand the listening checks to the user**

Build the VST3. List the seven checks from the spec's "Listening" section for the user to run in a DAW. "Ember Pong" and the Drive and Tone ranges are final only after that.
