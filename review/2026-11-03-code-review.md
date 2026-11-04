# Drosera – Technical review (DSP, real time, VST3/AU, build)

Date: 2026-11-03. Commit reviewed: `9447f73` (branch `claude/audio-plugin-vst3-au-review-9b137d`).
The review ran on an isolated copy of the project; the original repository was not modified.
The experiments and trial patches are in `review/experiments/`. The build and test logs cited here were kept out of
the repository.

---

## 1. Executive summary

Drosera is a feedback drone synth (a port of the Audrey II firmware) in JUCE 8.0.9, in VST3, AU and Standalone
formats. The project is in good shape: the DSP engine (without JUCE) is cleanly separated from the plugin wrapper,
the audio thread neither allocates nor blocks, the test suite is broad (471 offline checks, a test of the VST3
loaded in a host, interface tests, reference checksums of the engine) and everything passed in the sandbox. The
manual documents the sound's limits honestly.

No Critical or High issues. The most relevant points:

| ID | Severity | Summary |
|---|---|---|
| D-01 | Medium | Signed integer overflow (undefined behaviour) in the DaisySP noise generator, run on every sample on the audio thread. Found by UBSan. One-line fix, bit-identical. |
| D-02 | Medium | The echo starts at 5 s and glides to the knob's value in ~3 s after every `prepareToPlay` (and after every NaN recovery): the first repeats are late and out of tune. Inherited from the firmware. |
| D-03 | Medium | The order of the parameter handlers depends on `std::unordered_map` iteration: the reference renders (checksums) depend on the standard library, and the MIDI phase compensation reads current-tick or previous-tick values depending on that order. |
| D-04 | Low | Latency reported to the host but no `processBlockBypassed`: in bypass the audio passes with no delay (the host's latency compensation is misaligned). |
| D-05 | Low | The Audio Unit is built but never validated (`auval`) or released; CI validates only the VST3. |
| D-06 | Low | `prepareToPlay` with a sample rate of 0 produces an infinite `ratio` converted to `size_t`. |

Measured CPU: from 0.5 % (mono) to 2.2 % (three MIDI voices) of one Apple silicon core. Memory: about 3.5 MB of DSP
state per instance.

Overall assessment: high quality for a project of this size; the suggested changes are small and low risk. See §12
for the plan.

---

## 2. Architecture reviewed

### 2.1 Components

| Area | File | Role |
|---|---|---|
| Engine (no JUCE) | `src/dsp/FeedbackSynthEngine.*` | Three `FeedbackVoice`s, white noise per voice, stereo echo, coupling between voices, polyphony ramp |
| | `src/dsp/FeedbackVoice.*` | One feedback loop: stereo delay line ("Body"), two Karplus-Strong strings, overdrive, biquad LPF/HPF, `ReverbSc` reverb, optional tracking band-pass |
| | `src/dsp/KarplusString.*`, `BiquadFilters.*`, `EchoDelay.h`, `StereoEcho.h` | DSP blocks |
| | `src/dsp/DroseraCore.h` | Front end fixed at 48 kHz: maps normalised parameters, smoothing at 12 kHz (4-sample block), MIDI pitch/gate, polyphony, PONG, limiter, NaN recovery |
| | `src/dsp/ParameterRegistry.h`, `SmoothedValue.h` | Parameter registry with one-pole smoothing and `std::function` handlers |
| | `src/dsp/SincResampler.h` | Windowed-sinc converter (96 taps, Kaiser β=9, 512 phases) |
| | `src/dsp/VoiceAllocator.h` | Assigns MIDI notes to three voices, fixed arrays |
| Plugin | `src/plugin/PluginProcessor.cpp` | `AudioProcessor`: APVTS (25 parameters), sample-accurate MIDI, sample-rate bridge, state, programs |
| | `src/plugin/PluginEditor.*`, `src/plugin/ui/*` | 760×500 window, plant/spectrum, knobs, tabs |
| | `src/plugin/SpectrumTap.h` | Lock-free SPSC FIFO, audio → UI |
| | `src/plugin/EditorBridge.h` | Editor → processor interface beyond the parameters |
| | `src/plugin/Presets.h` | 16 factory presets |
| Third party | `third_party/DaisySP` (commit 7f67768, MIT) | Delay line, ReverbSc, Limiter, Overdrive, WhiteNoise, Tone, DcBlock, CrossFade |
| Tests | `tests/render_test.cpp` (1930 lines), `plugin_host_test.cpp`, `ui_test.cpp`, `voice_allocator_test.cpp`, `render_cli.cpp`, `editor_snapshot.cpp` | |

### 2.2 Signal flow

```
host in (L+R)/2 · inputgain ──► [SincResampler host→48k] ──┐
                                                           ▼
   per voice v: noise_v + in + coupling_v ──► DelayLine(Body, R = L−4) ──► KarplusString ×2 ──► Overdrive
                ──► LPF12 ──► HPF12 ──► ReverbSc (mix) ──► samp_v ──► ×fb_gain ──► [BPF24 tracking] ──► DelayLine (write)
   voice mix (×ramp, ×1/√3) ──► StereoEcho (send; PONG: crossed lines, drive, tone, width, level) ──► 0.5·(dry+wet)
   ──► ×volume ──► Limiter (pre-gain 0.7) ──► NaN/inf check (Recover) ──► [SincResampler 48k→host]
   ──► hard clip ±1 ──► SpectrumTap (UI) ──► host out
```

Control runs at 12 kHz: every 4 samples `ParameterRegistry::Process` applies the smoothing and calls the handlers,
which write into the engine. In MIDI/LOCK mode `applyPitchAndBody` computes the phase response of the loop's filters
at the note (complex numbers) and corrects the delays so that the loop oscillates at the fundamental.

### 2.3 Threading

- Audio thread: `processBlock` → `updateParams` (reads the APVTS atomics) → `renderChunk` → `DroseraCore::Render`.
- Message thread: editor with a 30 Hz timer; reads only atomics (`published_note_`, `midiPitchActive_`, `uiTab`…) and the `SpectrumTap` FIFO.
- `prepareToPlay` allocates (vectors, resampler tables, `Engine::Init` on the first call); never from the audio thread.
- State: XML through `copyXmlToBinary`; `replaceState` is thread-safe (documented in JUCE).

---

## 3. Analysis environment

Sandbox: `…/scratchpad/sandbox/` with `src/` (a copy from `git archive HEAD`), `tools/venv/` (tools), `build/release`, `build/asan`, `experiments/`. Nothing installed globally on the host.

| Tool | Version | Source |
|---|---|---|
| macOS | Darwin 27.2.0, arm64 | host |
| Compiler | AppleClang 21.0.0.21000401 (Command Line Tools) | host, already present |
| CMake | 4.4.4 | sandbox venv (pip) – **missing on the host** |
| Ninja | 1.13.2 | sandbox venv (pip) – **missing on the host** |
| JUCE | 8.0.9 | downloaded by CMake `FetchContent` into the sandbox build dir |
| clang-tidy | LLVM 23.1.4 | sandbox venv (pip) |
| Python | 3.11 (uv venv) | for `tools/test_release_check.py` |

Not available and not installed: `pluginval` (needs an external binary to be downloaded), `auval` on a component not
installed in `~/Library/Audio/Plug-Ins/Components` (outside the sandbox), an Intel toolchain (the host is arm64; CI
builds the universal binary).

Experimental changes, **in the sandbox copy only**:
1. `CMakeLists.txt`: added the `host_probe` target (see `review/experiments/CMakeLists-host_probe.txt`).
2. `tests/host_probe.cpp`: new (copy in `review/experiments/`).
3. `third_party/DaisySP/Source/Noise/whitenoise.h`: patch for D-01 (`review/experiments/whitenoise-unsigned.patch`), applied **after** the first sanitizer run, to verify the fix.
4. Reconfigured with `CMAKE_EXPORT_COMPILE_COMMANDS=ON` for clang-tidy.

---

## 4. Configuration and build

| Build | Result |
|---|---|
| Release, Ninja, all targets (VST3, AU, Standalone, tests) | OK, 199 steps, 53 warnings |
| Debug + `-fsanitize=address,undefined` (render_test, voice_allocator_test, render_cli, ui_test) | OK |
| Ad hoc bundle signatures (`codesign --verify --deep --strict`) | OK for VST3, AU, Standalone |
| AU `Info.plist` | `aumu` / `Dros` / `Cdsk`, version 257 (0.1.1), bundle `com.codeskine.Drosera` |
| Binary | arm64, minOS 11.0; VST3 and AU ≈ 10 MB each (JUCE linked statically) |

Of the 53 warnings, 44 come from DaisySP (compiled with `-w`, but its headers are included by the plugin with JUCE's
flags) and 9 from the project's own code: `ParameterRegistry.h` (fields shadowed in the constructor, superfluous
`;`), `DroseraCore.h:52` (sign conversion), `PluginProcessor.cpp:243` (`!=` between floats, intentional here),
`PluginProcessor.cpp:50` (`switch-enum`). None points to a bug.

---

## 5. Test results

### 5.1 Passed

| Test | Result |
|---|---|
| `voice_allocator_test` | 14/14 OK |
| `render_test` (Release) | 471 checks OK, 0 FAIL; allocations during rendering (NaN recovery included): 0 |
| `ui_test` (Release) | ALL OK; allocations in the polyphonic audio path: 0 (`malloc_logger` hook) |
| `plugin_host_test` on the built VST3, with the `state-baseline.bin` fixture | ALL OK: identity of the 25 parameters and their VST3 IDs, state saved by the baseline build, state round trip, levels at 44.1/48/96 kHz × blocks of 64/512/1000, audio input, MIDI gate, reported latency = measured (±3 samples), MIDI note 60 within +0.2 cents at every sample rate, presets |
| `tools/hardware-comparison/check_reference.sh` | 10/10 WAVs with SHA-256 identical to the references |
| `tools/test_release_check.py` | 8 tests OK |
| `voice_allocator_test` under ASan+UBSan | OK, no reports |
| `render_test` and `ui_test` under ASan+UBSan | First run: **stopped at the first sample** by the overflow in `whitenoise.h:34` (D-01). With the D-01 patch applied to the sandbox copy: `ui_test` ALL OK and `render_test` 471/471 OK, **zero** ASan and UBSan **reports** in both. |
| `host_probe` (experiment, VST3 in the JUCE host) | ALL OK: 22.05/44.1/88.2/176.4/192 kHz × blocks of 1/7/2048/8192 with input; reported latency = measured at 22.05/88.2/192 kHz (±5); 60 s at 44.1 kHz with mixed blocks (4096, 64, 1000, 3, 2048, 512) without gaps; empty block; bypass; corrupt and truncated state; note-ons with velocity 0 |
| DSP experiments (`exp1_cpu`, `exp2_edges`, `exp3_order`, `exp4_size`) | See §7, §8, §9 |

### 5.2 Failed

No project test failed. The only negative result is the UBSan report for D-01, which stops the sanitizers from
being used as things stand.

### 5.3 Not runnable because of the environment

- `pluginval` (CI runs it at strictness 5 on the VST3): needs an external executable to be downloaded.
- `auval` on the AU: needs the component installed in `~/Library`, outside the sandbox.
- Intel x86_64 build and tests: the host is arm64. The reference checksums are declared valid on arm64 only.
- ThreadSanitizer: not run (it would need a third build; the audio↔UI path was checked by reading, §8).

### 5.4 Not covered by the project

- AU validation in CI (D-05).
- Tests at sample rates other than 44.1/48/96 kHz and with blocks over 1000 samples (covered here by `host_probe`, all OK).
- A sanitizer build in CI.

---

## 6. Issues found (by severity)

Convention: files and lines refer to the commit reviewed.

### D-01 · Medium · Real time / Bug
**File:** `third_party/DaisySP/Source/Noise/whitenoise.h:34` (`randseed_ *= 16807;`, `int32_t`)
**Description:** a signed integer multiplication that overflows on almost every call: undefined behaviour in C++.
It runs on every sample in three generators (`Engine::Process`) and 60,020 times in `Engine::Init` (seed
pre-roll).
**Impact:** today's compilers produce the expected wrap-around, so the sound is correct; but the code has no
guarantees, a future compiler or flag (`-ftrapv`, optimisations that exploit UB) may change the result, and **today
it makes ASan/UBSan unusable** on the whole suite (they stop at the first sample).
**Evidence:** `runtime error: signed integer overflow: 282475249 * 16807 cannot be represented in type 'int'`,
stack `WhiteNoise::Process ← Engine::Init ← DroseraCore::Init ← prepareToPlay`.
**Fix:** `randseed_ = (int32_t) ((uint32_t) randseed_ * 16807u);` – same bits, same sound.
Verified in the sandbox: with the patch, `check_reference.sh` gives 10/10 identical checksums and `render_test`
passes (471/471), so the sound does not change by a single bit. Record the change in
`third_party/DaisySP/PATCHES.txt` (rule 4).
**Priority:** 1 (trivial, unblocks the sanitizers).

### D-02 · Medium · DSP
**File:** `src/dsp/FeedbackSynthEngine.cpp:34-36` (`SetDelayTime(5.0f, true)`, `SetLagTime(0.5f)`), `src/dsp/EchoDelay.h:114-118`
**Description:** `Engine::Init` sets the echo time to 5 s "immediately"; the real value comes from the registry on
the next tick, but as a *target*, and `EchoDelay` chases it with a 0.5 s one-pole. So the delay time glides from 5 s
to the knob's value in the first seconds after **every** `prepareToPlay` (loading, a change of sample rate or buffer
in many hosts) and after every `Recover()`.
**Impact:** the first repeats are very late and, since the delay changes while it is being read, transposed down (a
tape effect). After about 3 s everything is in place. This is the firmware's behaviour, but there `Init` happens only
at power-on.
**Evidence:** `exp2_edges`, section A, echo time 0.248 s: impulse at t=0 → first repeat after 0.954 s; at t=2 s →
0.296 s; at t=6 s → 0.248 s.
**Fix:** on the first push (`first_`) and after `Recover`, make the echo's internal state jump as well:
`Engine::SetEchoDelayTime(float, bool immediate)` → `stereo_echo_->SetDelayTime(t, immediate)`, called from
`DroseraCore::pushControls` with `imm`. This changes the reference renders that use the echo: it must be declared and
the checksums regenerated (rule 2).
**Note found while applying the fix** (`review/experiments/exp5_stall.cpp`): `EchoDelay`'s `float` one-pole never
reaches its target: it stops when the step falls below half an ulp, that is 8.6 samples short of 0.248 s, 68.7 short
of 1.29 s, 137 short of 2.5 s (up to 2.9 ms). That is why the three renders with echo change for their whole length,
not just the first seconds. After the fix, the same gap remains on every movement of the Echo Time knob: inaudible
(0.1 %), but for exactness it is enough to snap the value to the target once the distance is under one sample
(optional; it would change the checksums again).
**Priority:** 3.

### D-03 · Medium · Architecture (portability, determinism)
**File:** `src/dsp/ParameterRegistry.h:67-71` and `:99`, `src/dsp/DroseraCore.h:141` (a comment admitting the problem), `:206-227`
**Description:** the handlers are called in the iteration order of `std::unordered_map<int, …>`, which is a detail
of the standard library. With libc++ it is the reverse of the registration order (`exp3_order`:
`OutputVolume … FeedbackBody FeedbackGain Frequency`). The `Frequency` handler calls `applyPitchAndBody(0)`, which
uses `body_s_`, `lpf_hz_`, `hpf_hz_` written by the Body/LPF/HPF handlers: with the current order it sees the current
tick's values, with another order the previous tick's. Moreover, every tick does a hash lookup and a `std::function`
call per parameter.
**Impact:** (a) the reference checksums hold only for this standard library (as well as for arm64 only); (b) a build
with libstdc++ (Linux/Windows, mentioned in `BUILDING.md` as a possible future step) would render differently with
nothing to flag it; (c) an avoidable cost per tick.
**Evidence:** `review/experiments/exp3_order.cpp`; `DroseraCore.h:141` ("more entries could reorder its handlers").
**Fix:** replace the map with a `std::array<ParamState, N>` indexed by the enum and iterate in an explicitly chosen
order. Keeping the current reverse order leaves the renders identical; otherwise declare the change. It removes
`kParamSmoothingThresh` (unused) and the TODO. ~40 lines, less code.
**Priority:** 2.

### D-04 · Low · Compatibility
**File:** `src/plugin/PluginProcessor.cpp:168-170` (reported latency), no override of `processBlockBypassed`
**Description:** with a host rate other than 48 kHz and the input bus connected, the plugin reports a latency (327
samples at 44.1 kHz, 1264 at 192 kHz); JUCE's default bypass copies the input to the output with no delay and, in
Debug, `jassert(getLatencySamples() == 0)` fires (`juce_AudioProcessor.cpp:599`).
**Impact:** during bypass, the audio passing through is shifted ahead by that latency relative to the host's
compensation; in Debug the assert interrupts. It only matters when Drosera is used as an effect on an input.
**Fix:** override `processBlockBypassed` with a delay line of `getLatencySamples()` samples sized in `prepareToPlay`
(`juce::dsp::DelayLine` or a ring on an already allocated `std::vector`). ~25 lines.
**Priority:** 4.

### D-05 · Low · Compatibility (AU)
**File:** `CMakeLists.txt:67` (`FORMATS VST3 AU Standalone`), `.github/workflows/ci.yml` (target `Drosera_VST3`, `pluginval` on the VST3 only)
**Description:** every local build builds the AU, but CI does not compile it, does not run `auval` or `pluginval` on
it, and the release publishes only the VST3.
**Impact:** the AU is an unverified artefact; whoever installs it from a local build has no guarantees
(Logic/GarageBand reject components that fail `auval`).
**Fix:** either remove `AU` from the formats until it is supported, or add to CI: build `Drosera_AU`, copy it to
`~/Library/Audio/Plug-Ins/Components`, `killall -9 AudioComponentRegistrar`, `auval -v aumu Dros Cdsk`, `pluginval`
on the `.component`.
**Priority:** 5.

### D-06 · Low · Bug (robustness)
**File:** `src/plugin/PluginProcessor.cpp:142-150`
**Description:** `ratio = 48000 / sampleRate`; with a `sampleRate` of 0 (some hosts query or prepare with
provisional values) `ratio` is infinite, `std::ceil(inf)` converted to `size_t` is undefined behaviour, and
`outL.assign(...)` may ask for an enormous amount of memory.
**Impact:** a crash or a `bad_alloc` exception on opening in a host that passes 0.
**Fix:** at the start of `prepareToPlay`: `if (sampleRate <= 0.0) sampleRate = DroseraCore::kRate;`. 1 line.
**Priority:** 6.

### D-07 · Low · Bug (UX) / Compatibility
**File:** `src/dsp/DroseraCore.h:192` (clamp to `kNoteMin..kNoteMax` through `Update`), `src/plugin/PluginProcessor.cpp:403-404` (`Interval` −24..+24)
**Description:** a voice's note is limited to 16..84; with the Frequency knob high (note 72) an interval of +24 st
plays note 84, the same as +12. The same happens at the bottom.
**Evidence:** `exp2_edges`, section D: voice 1 (+24) and voice 2 (+12) both publish 84.0.
**Impact:** the knob shows "+24 st" but the voice does not play it; no artefacts.
**Fix:** show the effective interval in the interval readout (published note − voice 0's note), or document it in
the manual. Do not change the parameter's range (it would break automation).
**Priority:** 9.

### D-08 · Low · Real time
**File:** `src/dsp/DroseraCore.h:125-129` (`Recover`), `FeedbackSynthEngine.cpp:13-37`
**Description:** NaN recovery re-initialises the whole engine on the audio thread: it clears 3 `ReverbSc`s (386 KB
each), the echo's two lines (940 KB each), the strings and delays (≈ 160 KB per voice): ≈ 3.5 MB written in one go,
and it also puts the echo back to 5 s (D-02).
**Impact:** a load spike of a few hundred µs in the block where it fires; with 64-sample buffers at 96 kHz (0.67 ms)
it can cause a dropout. It is an emergency path: in 3 minutes of rendering at maximum values (`exp2_edges` B) it
never fired, and the `recoversFromNan` test checks its outcome.
**Fix:** acceptable as is; if desired, clear only what is needed (delay lines, filters, reverbs) without recomputing
the noise seeds, and jump the echo straight to the current value.
**Priority:** 10 (optional).

### D-09 · Low · Real time / Compatibility (threads)
**File:** `src/plugin/PluginProcessor.cpp:103-130`
**Description:** `setCurrentProgram` writes `apvts.state.setProperty(...)` (a `ValueTree`, not thread-safe) and calls
`setValueNotifyingHost` with gestures. From the editor this happens on the message thread; an AU host restoring a
preset from another thread would race with the APVTS listeners.
**Fix:** run the `ValueTree` part on the message thread (`MessageManager::callAsync`), or at least
`jassert(MessageManager::existsAndIsCurrentThread())`. ~5 lines.
**Priority:** 11.

### D-10 · Low · Security (supply chain) / Build
**File:** `CMakeLists.txt:48-51`
**Description:** JUCE is downloaded with `GIT_TAG 8.0.9`. A git tag is mutable.
**Fix:** use the hash of the tag's commit (with `GIT_SHALLOW` this needs a server that supports fetching by SHA,
which GitHub does) or verify a hash of the archive. 1 line.
**Priority:** 7.

### D-11 · Low · Bug (MIDI)
**File:** `src/plugin/PluginProcessor.cpp:158-159`
**Description:** `prepareToPlay` clears `heldCount`, `haveNote` and the allocator: a note held while the host changes
buffer or sample rate is lost (with MIDI Gate the sound closes until another note is played). The sustain pedal
(CC 64) and a configurable bend range (RPN), today fixed at ±2 st, are also missing.
**Impact:** rare in normal use; the latter two are feature limits, not bugs.
**Fix:** keep `held[]` and reapply `allocator.Assign(held, heldCount)` after `Init`; consider CC 64 and RPN 0 as
features.
**Priority:** 12.

### D-12 · Low · Code quality
Warnings in the project's code (§4) and clang-tidy findings with no impact: `performance-unnecessary-value-param`
for `Handler` in `ParameterRegistry.h:39,87` (a `std::function` copy at registration, only in `Init`),
`bugprone-implicit-widening-of-multiplication-result` in `PluginProcessor.cpp:147,150` and `FeedbackSynthEngine.h:77`
(small values, harmless), `cert-err33-c` on `snprintf` in `Readouts.h:14`. Fix them when the file is touched.
**Priority:** 13.

---

## 7. DSP and audio quality analysis

**Engine fixed at 48 kHz.** The right choice to keep the firmware's behaviour (delay line sizes, `ReverbSc` tuned for
48 kHz) and the reproducibility of the checksums. The cost is the resampler and its latency, reported only with the
input connected (consistent: with no input there is nothing to align).

**Windowed-sinc resampler.** Measurements (`exp2_edges` C, 48 k → 44.1 k): gain 0.00 dB at 1, 10 and 18 kHz;
−16.5 dB at 21 kHz (transition band, below the new Nyquist of 22.05 kHz); −94 dB at 23 kHz. No audible aliasing;
the table is normalised to unity gain at DC. The input-driven/output-driven bridge with a 256-sample prime held up
through 60 s of mixed blocks from 3 to 4096 samples without gaps; the latency measured after that run is 329 samples
against 327 reported: no FIFO drift.

**Numerical stability.** The loop is bounded by `fclamp(±20)` in the string, `SoftClip` in the overdrive and in the
echo, and the `Limiter` at the output; `juce::ScopedNoDenormals` in `processBlock` covers denormals on the audio
thread (the offline tests do not enable it, but showed no slowdowns). With everything at maximum (reverb decay 1.0,
feedback +12 dB, echo feedback 150 %, PONG drive +12 dB, coupling 10 %) 3 minutes of rendering stay finite, peak
≤ 0.82, zero recoveries (`exp2_edges` B). The per-block NaN/inf check with `Recover` is a reasonable safety net for
a chaotic system.

**Headroom and clipping.** The DaisySP `Limiter` uses `SoftLimit`, which is not bounded to ±1 on transients; the final
`jlimit(−1, 1)` in `processBlock` is therefore justified. In the 3 minutes at maximum, 1.0 was never exceeded after
the limiter. The echo's `0.5·(dry+wet)` always halves the direct signal: that is the firmware's, not a mistake.

**Filters.** Single-precision TDF-II biquads with standard bilinear coefficients (`BiquadFilters.cpp`), Q 0.9 for
LPF/HPF, cutoff limited to `[1, fs/2]`. The tracking band-pass (BPF24, Q 1.5) and the complex phase compensation in
`applyPitchAndBody` are correct: note 60 measures +0.1/+0.2 cents through the VST3 at 44.1/48/96 kHz. The note's
smoothing (t60 0.2 s) produces a portamento on every MIDI note change: it fits the firmware's character, but it is
worth knowing that it is not configurable.

**Aliasing.** The PONG's saturation is not oversampled; the manual says so (−54 dB of inharmonic power at maximum
Drive). For a "tape" echo this is an acceptable choice; 2× oversampling on `ReadShaped`'s `SoftClip` alone would
cost little (medium complexity) if it ever needs cleaning up.

**Continuity on parameter changes.** Everything goes through the registry with smoothing at 12 kHz (no zipper
noise); the polyphony and PONG ramps (20 ms) and the tuning blend (50 ms) are verified click-free by the tests.
Exception: D-02 at start-up.

**Sound design.** The creative choices (Body "snapping" to the note's periods, Coupling with make-up gain, WARP as a
doppler) are deliberate and documented in the design specs in `docs/superpowers/`. I do not class them as issues.
The manual lists the limits honestly (notes 16–18 and 80 mute, the reverb detuning MIDI notes, chord drift with high
Body).

---

## 8. Real-time issues

Check of the `processBlock` → `renderChunk` → `DroseraCore::Render` → `Engine::Process` path:

| Aspect | Result |
|---|---|
| Allocations | None: vectors and tables allocated in `prepareToPlay`; `Engine::Init` allocates only on the first call; `MidiMessage` built only for messages ≤ 3 bytes (sysex would allocate, and is avoided). Confirmed by `render_test` (operator new counted: 0) and `ui_test` (`malloc_logger`: 0). |
| Locks | None. `SpectrumTap` is a lock-free SPSC with correct acquire/release; the UI reads only atomics. |
| I/O, logging | None on the audio thread. |
| Determinism | No variable-time calls; `std::pow/tan/atan2/log2` are constant-time. |
| Denormals | `ScopedNoDenormals`. |
| UB | D-01 (overflow in the noise). |
| Cost in rare cases | `Recover()` (D-08). |
| UI ↔ audio communication | Parameters through APVTS atomics read once per block; `published_note_`/`published_body_s_` relaxed atomics; `uiTab`, `currentProgram`, `hostRate` atomics; `setStateInformation` uses `replaceState` (thread-safe, not real-time, never from the audio thread). `setCurrentProgram`: see D-09. |
| `prepared` race | A non-atomic `bool` written in `prepareToPlay`/`releaseResources` and read in `processBlock`: acceptable because hosts serialise these calls, but a `std::atomic<bool>` costs nothing. |

---

## 9. Performance optimisations

Measurements (`exp1_cpu`, 20 s of audio at 48 kHz, 512-sample blocks, one M-series core, Release `-O3`):

| Mode | CPU |
|---|---|
| Default (mono, knobs) | 0.53 % |
| Keys (mono, MIDI tracking) | 0.91 % |
| Open Fifths (poly, LOCK) | 2.13 % |
| Sympathetic (poly, free) | 1.35 % |
| Chord Keys (poly MIDI) | 2.19 % |
| Ember Pong (PONG) | 0.59 % |

`render_test` reports 9 ms per second of mono audio and 21 ms poly. On top of this comes the resampler at host rates
other than 48 kHz (96 taps × 3 channels ≈ 300 MACs per sample, negligible). Memory per instance: `Engine` 475 KB
inline + 3 × `ReverbSc` 386 KB + echo 1.9 MB + resampler tables 0.6 MB ≈ 3.5 MB.

**Conclusion:** no fix needed. *Optional* optimisations, in order of payoff:

1. **Do not recompute coefficients on every tick** (`DroseraCore.h:212-215, 243-248`, `FeedbackVoice::SetFeedbackTracking`).
   Today every 4 samples every handler writes into the engine even when the value is still: `tanf` for LPF/HPF ×3
   voices, 4 `tanf` for each voice's tracking BPF24s, `expf` for the PONG level, plus `pow`, `polar`, `atan2`, `log2`
   in `applyPitchAndBody`. Remedy: have `SmoothedValue::get` also return "changed" (a bitwise comparison; once
   converged the one-pole is exactly stable) and skip the handler. Estimated benefit: −30/50 % on the control part
   (which is a fraction of the 2 %). Risk: the ordering and dependency between handlers (D-03) must be resolved first,
   because `applyPitchAndBody` depends on several parameters. Medium complexity.
2. **`applyPitchAndBody(0)` called twice per tick** (Frequency and Body handlers, `DroseraCore.h:207, 211`): one call
   after both is enough. Small benefit, no risk if the order is explicit (D-03).
3. **Registry without hashing and `std::function`** (part of D-03): an array indexed by the enum, handlers as
   member-function pointers or a `switch`. Small benefit; it matters for determinism more than for CPU.
4. **SIMD**: not recommended. The loop is recursive sample by sample (fractional-delay feedback) and cannot be
   vectorised; the three voices are independent only within a sample. The resampler is already a dot product that
   the compiler vectorises on its own.

---

## 10. Architecture and maintainability issues

**Strengths.** A clean DSP/JUCE boundary with tests that need no JUCE; `EditorBridge` keeps the editor away from the
processor; `Presets.h`, `Readouts.h`, `SpectrumBands.h` testable on their own; clear ownership (`unique_ptr` for large
objects, `Engine` non-copyable); no naked `new`; parameter IDs with `versionHint` for AU ordering; written project
rules (`CLAUDE.md`, `CONTRIBUTING.md`) and a design spec for every feature; CI with pluginval.

**To improve.**
- `ParameterRegistry` (D-03): the generality of the template and the map is unused (three registries with fixed
  enums); an array replaces it with less code and stronger guarantees.
- `DroseraCore.h` is a 368-line header with non-trivial maths (`applyPitchAndBody`, `blendedStringNote`) and three
  registries: it stays readable thanks to its comments, but the frequency-response functions (`stringLoopResponse`,
  `biquadResponse`) replicate by hand the coefficients of `BiquadFilters.cpp` and of `Tone`/`DcBlock`: if one
  changes, the other must be updated by hand. A cross-reference comment or a shared function returning the
  coefficients would prevent drift.
- `PluginProcessor.cpp` holds the parameter layout, the MIDI state (`held[]`, allocator, bend), the sample-rate
  bridge and the programs together: 475 lines, still manageable, but the MIDI state (`handleMidi`, `applyMidiState`,
  `removeNote`) is a natural candidate for a small class testable without JUCE, like `VoiceAllocator`.
- A cache of `std::atomic<float>*` for 25 parameters: it works, but an array indexed by an enum of the plugin
  parameters would cut the 15 repeated `getRawParameterValue` lines.
- `kSpecs` in `PluginProcessor.cpp` duplicates the ranges and curves of `DroseraCore::registerParams` ("Ranges/curves
  mirror…"): a shared table in the DSP would avoid the duplication.
- The comment in `FeedbackVoice.h:21-22` ("Init leaves fb_delay_samp_…") and the one in `Engine::Init` are essential,
  because `Init` also serves as a reset on the audio thread: a delicate convention, but documented.

---

## 11. VST3 / AU compatibility

| Aspect | Status |
|---|---|
| Parameters | 25 parameters 0..1 with versioned `ParameterID`s (1 and 2), English names, text formatting consistent with the engine; an ID/name identity test that blocks regressions. In the VST3 the host sees 2107 parameters: the 25 + Bypass + 2080 for MIDI CC/aftertouch/pitch-bend emulation (`JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS`, JUCE's default for synths). It is needed to receive pitch bend in VST3; it is normal for JUCE synths, but it clutters hosts' automation lists. No action. |
| Automation | Read once per block, then smoothed internally at 12 kHz: no zipper noise, no sample-accurate automation (JUCE standard). |
| State | XML in binary, tag checked, missing parameters → defaults (old projects load, tested with the baseline fixture), `uiTab` and `program` saved. Corrupt or truncated state ignored without a crash (`host_probe`). |
| Programs | 16 presets as programs; `setCurrentProgram` sets the parameters while notifying the host; index persisted. See D-09. |
| Bypass | JUCE default; D-04 when there is latency. |
| Buses | Stereo output only; input disabled/mono/stereo. A host asking for a mono output does not instantiate: acceptable for a synth. A mono input is read from channel 0 only (correct: channel 1 of the buffer may hold garbage). |
| MIDI | Note on/off (velocity 0 = off), all notes/sound off, pitch wheel ±2 st, omni. Sysex ignored without allocating. Sample-accurate. D-11. |
| Sample rate and buffer | Verified at 22.05–192 kHz, blocks of 1–8192 and 0, with correct reported latency. |
| Double precision | Not supported (declared correctly). |
| Tail | 10 s reported; the drone self-oscillates, so the value is nominal. |
| AU | Correct bundle (`aumu`/`Dros`/`Cdsk`, version 257); never validated (D-05). |
| Standalone | Built and signed; not run (needs an audio device). |
| macOS | minOS 11.0; ad hoc signature redone after the build (correct, because JUCE adds `moduleinfo.json` after signing); CI signs and notarizes if the secrets exist. |

---

## 12. Prioritised plan

| # | Change | ID | Kind | Complexity | Effect on checksums |
|---|---|---|---|---|---|
| 1 | Unsigned multiplication in `whitenoise.h` + note in `PATCHES.txt`; add a CI job with ASan/UBSan on `render_test` and `ui_test` | D-01 | Required fix | Trivial | None (verified) |
| 2 | `ParameterRegistry` on an array with explicit order | D-03 | Recommended fix | Low (~40 lines, less code) | None if the current order is kept |
| 3 | Echo jumping straight to the knob's value at start-up and after `Recover` | D-02 | Recommended fix (audible) | Low | Yes: regenerate with a stated reason |
| 4 | `processBlockBypassed` with a delay equal to the latency | D-04 | Recommended fix | Low | None |
| 5 | Validate the AU in CI (`auval`, `pluginval`) or remove `AU` from the formats | D-05 | Recommended | Low (CI) | None |
| 6 | Clamp the sample rate in `prepareToPlay` | D-06 | Fix (1 line) | Trivial | None |
| 7 | Pin JUCE to the commit hash | D-10 | Recommended | Trivial | None |
| 8 | `std::atomic<bool> prepared` | §8 | Optional | Trivial | None |
| 9 | Effective-interval readout, or a note in the manual | D-07 | Optional | Trivial | None |
| 10 | Lighter NaN recovery | D-08 | Optional | Medium | None (only after a NaN) |
| 11 | `setCurrentProgram` on the message thread | D-09 | Optional | Low | None |
| 12 | Held notes across `prepareToPlay`; sustain pedal; RPN bend range | D-11 | Feature | Low/Medium | None |
| 13 | Skip handlers whose value is still; one `applyPitchAndBody` per tick | §9 | Optional optimisation | Medium | None if done bitwise |
| 14 | Clean up warnings and clang-tidy findings | D-12 | Optional | Trivial | None |

---

## 13. Changes applied (after the review)

On request, changes 1–7 of the plan were applied in the worktree (not committed). Diff: 13 files, +144/−56 lines.

| # | ID | What changed | Verification |
|---|---|---|---|
| 1 | D-01 | `whitenoise.h`: product in `uint32_t` converted back to `int32_t`; note in `PATCHES.txt` | 7 renders without echo bit-identical; `ui_test` and `voice_allocator_test` pass under ASan+UBSan with no reports; `render_test` under ASan+UBSan: 472/472 OK, zero reports |
| 2 | D-03 | `ParameterRegistry` on `std::array<std::optional<ParamState>, N>`, `Process` runs from the highest id to the lowest (the order libc++ gave); the enums have `NumVoiceParams`/`NumEchoParams`; −9 lines net | The 7 renders without echo stay bit-identical; the 3 with echo change only because of D-02 |
| 3 | D-02 | `Engine::SetEchoDelayTime(t, immediate)`; `DroseraCore` makes the echo jump on the first tick after `Init` and after `Recover` (`echo_jump_`); new test `echoStartsAtKnobTime` | First repeat at 0.248 s with the knob at 0.248 s (before: 0.954 s). `SHA256SUMS` regenerated: p05, p06, p10 change, the other 7 do not. Reason in the CHANGELOG |
| 4 | D-04 | `processBlockBypassed` with a ring per channel `getLatencySamples()` long, allocated in `prepareToPlay`; new tests `bypassKeepsLatency`/`bypassPassesThrough` | Bypass at 44.1 kHz: 327 reported, 327 measured; at 48 kHz it passes unchanged |
| 5 | D-05 | CI: build of `Drosera_AU`, install into the runner's Components folder, `auval -v aumu Dros Cdsk`, `pluginval` on the AU | Not runnable here (see §3); to be watched on the first CI run |
| 6 | D-06 | `prepareToPlay`: sample rate ≤ 0 → 48,000 | Compiles; behaviour at valid rates unchanged (`plugin_host_test` ALL OK) |
| 7 | D-10 | JUCE's `GIT_TAG` = hash of the 8.0.9 commit | The worktree's configure downloaded JUCE by hash with `GIT_SHALLOW ON` |

Full suites on the worktree after the changes: `voice_allocator_test` OK, `render_test` 472 checks OK, `ui_test` ALL
OK, `plugin_host_test` ALL OK (parameter identity, baseline fixture, latency), `check_reference.sh` 10/10 with the new
checksums. `tests/fixtures/state-baseline.bin` was not touched.

Still open: optional items 8–14 of the plan and the note on the residual gap of the echo's one-pole (in D-02).

---

## 14. Sound design measurements

The automatic measurements of preset levels, MIDI note range, load repeatability, mono compatibility, duration,
extreme combinations and sample rates are in `review/2026-11-03-sound-design-measurements.md`. The two points to
carry into the plan: the same project can settle on a different partial depending on when the host delivers the
state (remedy: make the parameters jump on restore), and the reverb inside the loop detunes MIDI notes below 48 by
up to ±46 cents at 35 % Mix.

---

## Overall assessment and roadmap

The project is solid: the audio thread is clean, the DSP is correct and measured (MIDI tuning within 0.2 cents,
resampler at −94 dB in the stopband, stability for minutes at extreme values), persistence is protected by a
fixture, and the sound choices are documented along with their limits. The defects found are peripheral: a UB
inherited from a vendored library (which today rules out the sanitizers), a start-up transient inherited from the
firmware, an evaluation order left to the standard library, and an AU built but not verified.

Suggested roadmap:
- **Now (one session):** 1, 6, 7, 8 — trivial fixes with no effect on the renders — plus the CI job with sanitizers.
- **Next release:** 2 and 3 (with checksum regeneration explained in the changelog), 4, 5.
- **When the area is touched:** 9–14.

Not checked: pluginval and auval (sandbox constraints), the Intel build, ThreadSanitizer, subjective listening to the
presets.
