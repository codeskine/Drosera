# Three-String Polyphony Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let Drosera sound up to three notes at once, as three feedback loops joined by an adjustable Coupling, off by default.

**Architecture:** The loop's whole chain moves into a new `FeedbackVoice`. `Engine` owns three voices and one echo, and `DroseraCore` drives voices 1 and 2 through a second `ParameterRegistry`. A JUCE-free `VoiceAllocator` assigns MIDI notes, and a sixth tab holds the controls.

**Tech Stack:** C++17, JUCE 8.0.9, DaisySP `7f67768` (vendored), CMake with Ninja.

**Spec:** `docs/superpowers/specs/2026-11-02-three-string-polyphony-design.md`

## Global Constraints

- With polyphony off, renders stay byte-identical to `tools/hardware-comparison/reference/SHA256SUMS`. No new checksums.
- No allocation, locks or file access on the audio thread.
- MIDI tuning is judged by the dominant partial, never by a fit to the note's harmonic grid.
- `third_party/` does not change. `tests/fixtures/state-baseline.bin` is not regenerated.
- Existing parameter IDs do not change. Parameter names stay in English everywhere.
- New parameters go at the end of the list: `poly`, `coupling`, `interval2`, `interval3`.
- Do not add entries to the existing `registry_` or to `DroseraCore::Param`. The registry iterates an `unordered_map`, so more entries could reorder its handlers.
- Commit messages are one imperative sentence, as in `git log`, and end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

**Commands used throughout:**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
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

A threshold in this plan is a claim about the design. If a test fails on one, find out why, and report it before relaxing the number.

## Review Focus

- MIDI Pitch and MIDI Gate on with no keys, or a last note latched with MIDI Gate off, then POLY turned on: the latched note keeps sounding instead of everything going silent. Test in Task 5.
- POLY automated quickly, so a ramp reverses midway: no click, NaN or allocation. Tests in Tasks 3 and 4.
- Odd MIDI: a repeated note-on, a note-off with no note-on, more than 16 keys held. Nothing breaks and no voice stays stuck. Tests in Task 1.
- A host not at 48 kHz, with polyphony on: the chord still sounds and stays finite. Test in Task 5.
- A project saved with POLY on: on reload all three voices sound at once, with no ramp from a stale state, and intervals at their extremes stay within notes 16 to 84. Tests in Tasks 4 and 5.

---

### Task 1: VoiceAllocator

**Files:**
- Create: `src/dsp/VoiceAllocator.h`
- Modify: `CMakeLists.txt` (add `tests/voice_allocator_test.cpp` as target `voice_allocator_test`, linked to nothing, include path `src/dsp`)
- Test: `tests/voice_allocator_test.cpp`

**Interfaces:**
- Produces, in namespace `infrasonic`:

```cpp
class VoiceAllocator {
public:
    static constexpr int kVoices = 3, kNoNote = -1, kMaxWaiting = 16;
    void Reset();                                  // every voice never used, nothing waiting
    void NoteOn(int note);
    void NoteOff(int note);
    void AllNotesOff();                            // releases the voices, keeps their notes, drops waiting notes
    void Assign(const int* held, int count);       // Reset, then NoteOn each, oldest first
    int  Note(int voice) const;                    // kNoNote if never used
    bool Held(int voice) const;
};
```

- [ ] **Step 1: Write the failing test**

Plain `main` returning non-zero on failure, printing `OK`/`FAIL` per case like `render_test`. Cases, where `{a,b,c}` means `Note(0..2)`:

```cpp
// firstNotes:      on 60            -> {60,-1,-1}, Held(0)
// freeVoiceFirst:  on 60,64,67      -> {60,64,67}
// stealsOldestHeld: + on 71         -> {71,64,67}
// releasedBeforeHeld: on 60,64,67, off 64, on 71 -> {60,71,67}
// longestReleased: on 60,64,67, off 67, off 60, on 71 -> {60,64,71}
// waitingResumes:  on 60,64,67,71 (60 waits), off 67 -> {71,64,60}, Held(2)
// mostRecentWaitingFirst: on 60,62,64,65,67 (60,62 wait), off 65 -> voice of 65 takes 62
// repeatedNoteKeepsVoice: on 60,64, on 60 -> {60,64,-1}
// releasedNoteReturns: on 60,64, off 60, on 60 -> {60,64,-1}, Held(0)
// offWithoutOn:    off 60 on an empty allocator -> {-1,-1,-1}
// offOfWaiting:    on 60,64,67,71, off 60 -> {71,64,67}; then off 71 leaves voice 0 released, {71,64,67}
// allNotesOff:     on 60,64, AllNotesOff -> {60,64,-1}, !Held(0), !Held(1)
// overflow:        on 20 distinct notes, off all of them -> no voice Held, no crash
// assign:          Assign({60,62,64,65},4) -> {65,62,64}, all Held, and off 65 -> voice 0 takes the waiting 60
```

`Assign` calls `NoteOn` in press order, so the fourth note takes the oldest note's voice. That keeps the last three held notes, as the spec requires.

- [ ] **Step 2: Build and verify it fails**

Run: `cmake --build build --target voice_allocator_test`
Expected: compile error, `VoiceAllocator.h` not found.

- [ ] **Step 3: Implement `VoiceAllocator` in `src/dsp/VoiceAllocator.h`**

Fixed arrays only, no allocation. Per voice: note, held, and a monotonically increasing stamp set on note-on (held age) and on release (released age). Selection order: lowest-index never-used voice, then the released voice with the smallest stamp, then the held voice with the smallest stamp, whose note joins the waiting list. A full waiting list drops its oldest entry.

- [ ] **Step 4: Run the test and verify it passes**

Run: `cmake --build build --target voice_allocator_test && build/voice_allocator_test`
Expected: every case `OK`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/VoiceAllocator.h tests/voice_allocator_test.cpp CMakeLists.txt
git commit -m "Add the voice allocator"
```

---

### Task 2: Extract FeedbackVoice

A pure refactoring: `Engine` runs one `FeedbackVoice` and renders must not change.

**Files:**
- Create: `src/dsp/FeedbackVoice.h`, `src/dsp/FeedbackVoice.cpp`
- Modify: `src/dsp/FeedbackSynthEngine.h`, `src/dsp/FeedbackSynthEngine.cpp`, `src/dsp/DroseraCore.h:124-183`, `CMakeLists.txt:20-23`

**Interfaces:**
- Produces, in namespace `infrasonic::FeedbackSynth`:

```cpp
class FeedbackVoice {
public:
    void Init(float sample_rate);   // only the first call allocates; also clears the state on the audio thread
    void SetStringPitch(float nn);
    void SetFeedbackGain(float gain_dbfs);
    void SetFeedbackDelay(float delay_s);
    void SetFeedbackDelayLag(float time_s);
    void SetFeedbackLPFCutoff(float hz);
    void SetFeedbackHPFCutoff(float hz);
    void SetFeedbackTracking(bool enabled, float center_hz, float makeup_gain = 1.0f);
    void SetReverbMix(float mix);
    void SetReverbFeedback(float time);
    // samp is the loop's signal after filters and reverb, before the feedback gain and band-pass.
    void Process(float noise, float in, float couplingL, float couplingR, float& sampL, float& sampR);
};
```

- `Engine`'s per-voice setters gain a leading `size_t voice`: `SetStringPitch`, `SetFeedbackGain`, `SetFeedbackDelay`, `SetFeedbackDelayLag`, `SetFeedbackTracking`. In this task `Engine` has `kNumVoices = 1`. `SetFeedbackLPFCutoff`, `SetFeedbackHPFCutoff`, `SetReverbMix`, `SetReverbFeedback` go to all voices.

- [ ] **Step 1: Confirm the starting point**

Run the build, `render_test` and `check_reference.sh`.
Expected: `ALL OK`, and every checksum line `OK`.

- [ ] **Step 2: Move the loop into `FeedbackVoice` and leave the echo and noise in `Engine`**

Three things keep this byte-identical, and each has broken a port like this before:
- Keep each moved statement's shape. Clang contracts `a * b + c` into FMA within a statement on arm64, so splitting or merging a statement can change results.
- Compute the loop input as `Read(...) + noise + in + couplingL` in that order, never `Read(...) + excitation` with a precomputed sum, because float addition is not associative.
- `Init` must not reset `fb_gain_`, `fb_delay_samp_` or `fb_delay_samp_target_`. Today's `Engine::Init` leaves them alone, and `Recover` relies on it.

`Engine::Process` passes `0.0f` for both coupling arguments and keeps its echo, sum and output statements verbatim. Update `DroseraCore` calls to pass voice `0`, replacing the `std::bind` for `SetFeedbackGain` with a lambda.

- [ ] **Step 3: Verify nothing changed**

Run the build, `render_test` and `check_reference.sh`.
Expected: `ALL OK`, and every checksum line `OK`. A mismatch means a statement changed shape, so fix the code and never the checksums.

- [ ] **Step 4: Commit**

```bash
git add src/dsp CMakeLists.txt
git commit -m "Move the feedback loop into FeedbackVoice"
```

---

### Task 3: Three voices in the Engine

**Files:**
- Modify: `src/dsp/FeedbackSynthEngine.h`, `src/dsp/FeedbackSynthEngine.cpp`
- Test: `tests/render_test.cpp` (new function `engineVoiceTests()`, called from `main`)

**Interfaces:**
- Consumes: `FeedbackVoice` from Task 2.
- Produces on `Engine`:

```cpp
static constexpr size_t kNumVoices = 3;
void SetPolyphony(bool on, bool immediate = false);
void SetCoupling(float gain);      // linear gain, read by Task 4
bool PolyRunning() const;          // true from switch-on until the ramp ends on the way off
float VoiceTap(size_t voice, size_t channel) const;   // last sample's samp, before the ramp
```

- [ ] **Step 1: Write the failing tests**

Each drives `Engine` directly, heap-allocated, with `SetFeedbackGain(v, 0.0f)` (0 dB), with `SetPolyphony(true, true)` before the first sample unless noted. Reverb and echo at 0.

```cpp
// engineIndependentAtZeroCoupling: two Engines, Coupling 0, voice 0 at note 48 in both.
//   Voices 1,2 at 55,64 in the first and 40,70 in the second. 2 s, block by sample.
//   VoiceTap(0, 0) and VoiceTap(0, 1) are exactly equal (==) at every sample.
// engineUnisonNotIdentical: all voices at note 48, Coupling 0, 2 s.
//   VoiceTap(1,0) != VoiceTap(0,0) at some sample after 0.5 s, and same for 2 vs 1.
// engineCouplingRingsAClosedVoice: voice 0 at 48 open, voice 1 at 48 with gain -60 dB. 3 s.
//   RMS of VoiceTap(1,0) over the last second is at least 6 dB higher at SetCoupling(0.5f) than at 0.
// engineRampClickFree: voices at 48,55,64. Off->on at 2 s and on->off at 4 s.
//   Largest |out[n]-out[n-1]| in each ramp's 960 samples <= 1.5 * largest in the preceding second.
// engineRampEnds: PolyRunning() is true during the on state, still true 959 samples after
//   SetPolyphony(false), false after 961.
// engineRestartsFromSilence: on, 1 s, off, wait for the ramp's end, then on with the tap read at once:
//   VoiceTap(1,0) peak over the next 10 samples < 0.01.
// engineRampReverses: toggle every 240 samples (5 ms, inside the ramp) for 1 s: every sample finite,
//   and the largest jump <= 1.5 * the largest in the steady second before.
```

- [ ] **Step 2: Build and verify it fails**

Run: `cmake --build build --target render_test`
Expected: compile error, `SetPolyphony` is not a member of `Engine`.

- [ ] **Step 3: Implement**

- `Engine` holds `FeedbackVoice voices_[kNumVoices]` and `daisysp::WhiteNoise noise_[kNumVoices]`. In `Init`, voice 1's generator advances 20011 steps and voice 2's 40009 steps. `noise_[0]` stays as it is today.
- `Init` also jumps the ramp to its end for the current `SetPolyphony` state, which is what `Recover` needs.
- `ramp_` moves linearly by `1.0f / 960.0f` per sample toward its target.

```cpp
// Process, polyphonic branch only. The non-polyphonic branch stays exactly as in Task 2.
// tap_ holds the previous sample's output of each voice, which is the coupling's one-sample delay.
for each voice v:
    cL = coupling_ * ramp-weighted sum of tap_[others][0];   // voices 1 and 2 count as tap * ramp_
    cR = likewise for channel 1;
    voices_[v].Process(noise_[v].Process(), in, cL, cR, nextTap[v][0], nextTap[v][1]);
sumL = (nextTap[0][0] + ramp_ * (nextTap[1][0] + nextTap[2][0])) * (1.0f + ramp_ * (kInvSqrt3 - 1.0f));
```

Then the echo, `0.5f` mix and output level as in the mono branch. While ramping, voice 0's tap counts in full, so the fading voices keep hearing it, and voice 0 hears them only as they fade.

When the ramp reaches 0 going off, call `voices_[1].Init` and `voices_[2].Init` (no allocation, since the first call made it), and set `PolyRunning()` false. The non-polyphonic branch runs while `PolyRunning()` is false, and is what `check_reference.sh` exercises.

- [ ] **Step 4: Verify the new tests pass and renders have not changed**

Run the build, `render_test` and `check_reference.sh`.
Expected: `ALL OK`, and every checksum line `OK`.

- [ ] **Step 5: Commit**

```bash
git add src/dsp tests/render_test.cpp
git commit -m "Add three voices and coupling to the engine"
```

---

### Task 4: Per-voice control in DroseraCore

**Files:**
- Modify: `src/dsp/DroseraCore.h`
- Test: `tests/render_test.cpp` (new function `polyphonyTests()`, called from `main`; `noAllocationOnRender` extended)

**Interfaces:**
- Consumes: `Engine` API from Tasks 2 and 3.
- Produces on `DroseraCore`:

```cpp
void  SetPolyphony(bool on);
void  SetCoupling(float norm);                    // 0..1, mapped linearly to 0..kMaxCoupling
void  SetIntervals(int interval2, int interval3); // semitones
void  SetVoiceNote(size_t voice, float note);     // voices 1, 2: the note while SetMidiPitch is active
void  SetVoiceGate(size_t voice, bool enabled, bool open);  // SetGate(e, o) stays and means SetVoiceGate(0, e, o)
float PublishedNote(size_t voice = 0) const;
float PublishedBodySeconds(size_t voice = 0) const;
float VoiceTap(size_t voice, size_t channel) const;  // for tests
int   RecoveryCount() const;                      // times Render's NaN check fired, for tests
```

- [ ] **Step 1: Write the failing tests**

Base parameters are `featureTests`'s dry `base` array. Tap-based tests render with block size 1 and collect `VoiceTap(v, 0)`.

```cpp
// chordTuning (rule 3): SetPolyphony(true), SetMidiPitch(true, a), SetVoiceNote(1, b), SetVoiceNote(2, c),
//   all gates enabled and open, Coupling 0, for chords {36,43,52} {45,52,57} {48,55,64} {60,64,67} {64,69,76}.
//   5 s; for each voice, 1200*log2(dominantHz(tap, 48000*2) / nominal) within 25 cents, with dominantHz from
//   pitch_estimator.h. Print each value.
// knobIntervals: Frequency 0.43, intervals +7 and +12, no MIDI: PublishedNote(1) - PublishedNote(0) within 0.01
//   of 7 and PublishedNote(2) - PublishedNote(0) within 0.01 of 12, after 2 s.
// intervalsClamp: Frequency 1.0 with +24, then 0.0 with -24: PublishedNote stays in 16..84 and the output is finite.
// neverAssignedStaysClosed: one note on voice 0, voices 1,2 gate enabled and closed: after 3 s voice 1's tap RMS
//   is at least 40 dB below voice 0's.
// perVoiceGate: three notes, close voice 1 at 3 s: at 6 s its tap has dropped > 20 dB, voice 0's by < 3 dB.
// chordIsAudibleImmediately: polyphony on before the first Render (as after loading a project): all three voices'
//   tap RMS over 2..3 s > 1e-3, and PolyRunning has not ramped (voice 1's tap is non-zero within the first second).
// stabilityAtMaximum: Coupling 1, Feedback 1, Reverb Mix 0 and 1, LPF and HPF each at 0 and 1, chords {16,50,84}
//   and {48,48,48}, 60 s per chord in total across the settings. All finite, peak <= 1.0001, RecoveryCount() == 0.
// recoverRestoresAllVoices: Recover() with polyphony on: all three voices' tap RMS > 1e-3 within 3 s.
// cpuReport: print seconds of CPU per second of audio, polyphony on and off. No assertion.
```

Extend `noAllocationOnRender`: with `SetPolyphony` toggled every 100 ms, and voice notes and gates changed in the same loop.

- [ ] **Step 2: Build and verify it fails**

Run: `cmake --build build --target render_test`
Expected: compile error, `SetPolyphony` is not a member of `DroseraCore`.

- [ ] **Step 3: Implement**

- New private `enum VoiceParam : int { Voice1Pitch, Voice2Pitch, Voice1Gain, Voice2Gain, Coupling };` and `ParameterRegistry<VoiceParam> voice_registry_`. Pitch: range `kNoteMin..kNoteMax`, smoothing `0.2f`. Gain: `-60..12` dB, default smoothing, same as `FeedbackGain`. Coupling: `0..kMaxCoupling`, default smoothing.
- `static constexpr float kMaxCoupling = 0.5f;`, adjusted only by `stabilityAtMaximum`: halve until it passes, and record the outcome in the commit message.
- `nn_`, `tracking_`, the published atomics, `midi_pitch_`, `gate_enabled_` and `gate_open_` become arrays of 3. `applyPitchAndBody(size_t voice)` is today's function indexed by voice. Voice 0's statements keep their shape.
- `Render`: after `registry_.Process()` add `if (engine_->PolyRunning()) voice_registry_.Process();`. The handlers of voices 1 and 2 call `applyPitchAndBody(voice)`.
- `pushControls`: voice 0 as today, with its gate from `gate_*_[0]`. Then `e.SetPolyphony(poly_, first_)`, and only if `engine_->PolyRunning()`, update `voice_registry_`, using `first_` or "just started running" as `immediate`, so a pitch does not glide from stale values. The target pitch is `midi_pitch_on_ ? midi_pitch_[v] : knobNote + interval[v]`, clamped by the registry's `Update`, where `knobNote` is the same note the knob gives voice 0.

- [ ] **Step 4: Verify the new tests pass and renders have not changed**

Run the build, `render_test` and `check_reference.sh`.
Expected: `ALL OK`, and every checksum line `OK`.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/DroseraCore.h tests/render_test.cpp
git commit -m "Control three voices from DroseraCore"
```

---

### Task 5: Parameters, MIDI and presets

**Files:**
- Modify: `src/plugin/PluginProcessor.cpp`, `src/plugin/Presets.h`, `src/plugin/EditorBridge.h`
- Test: `tests/plugin_host_test.cpp`, `tests/ui_test.cpp`, `tests/render_test.cpp` (`presetTests`, `applyPreset`)

**Interfaces:**
- Consumes: `VoiceAllocator` (Task 1), `DroseraCore` API (Task 4).
- Produces:
  - `Preset` gains, after `needsInput`, with defaults so existing rows stay unedited: `bool poly = false; float coupling = 0.0f; int interval2 = 7; int interval3 = 12;`
  - `EditorBridge` gains `virtual bool polyMidiActive() const = 0;` (polyphony on and MIDI Pitch in control) and `virtual float voiceNote(int voice) const = 0;` (negative when the voice has no note).

- [ ] **Step 1: Write the failing tests**

In `plugin_host_test.cpp`, append to `kParamIdentity`: `{"poly","Polyphony"}, {"coupling","Coupling"}, {"interval2","Interval 2"}, {"interval3","Interval 3"}`. In `checkSavedState`, also assert the baseline leaves Polyphony at 0 and Coupling at 0.

In `ui_test.cpp`, new block in `editorTests`, all driving `processBlock` with notes. `render`'s MIDI argument covers one buffer.

```cpp
// defaultsOff: new processor has poly 0, coupling 0, interval2 == +7, interval3 == +12 (read as text "+7 st", "+12 st").
// polyStateRoundTrip: set poly 1, coupling 0.4, interval2 -5; state out and into a new instance; values match.
// oldPresetsTurnPolyOff: setCurrentProgram(13) (a polyphonic preset) -> poly 1; setCurrentProgram(2) -> poly 0.
// presetCountAndOrder: getNumPrograms() == 15; programs 0-11 have today's names; 12-14 are Open Fifths, Sympathetic, Chord Keys.
// threeNotesThreeVoices: midipitch, poly on; note-on 48,55,64; voiceNote(0..2) == 48,55,64; polyMidiActive().
// singleNoteLeavesOthersClosed: one note; voiceNote(1) < 0.
// fourthNoteSteals: 48,55,64,71 -> voiceNote(0) == 71; note-off 71 -> voiceNote(0) == 48.
// releaseKeepsDroneWithoutGate: gate off: note-off 48 keeps voiceNote(0) == 48.
// knobTakesAllVoicesBack: all notes off, freq moved: polyMidiActive() false; a new note then lands on voice 0.
// polyOnWithLatchedNote: midipitch on, poly off; note-on 48, note-off 48; poly on; render 3 s:
//   voiceNote(0) == 48 and output RMS over the last second > 1e-3.
// polyOnWithHeldKeys: poly off, hold 48,55,64,71; poly on -> voiceNote(0..2) == 71,55,64 per Assign.
// polyOffAfterChord: with a chord held, poly off; render: polyMidiActive() false, single-note behaviour resumes, last note 64.
// chordAt44100: prepareToPlay(44100.0, 512), poly on, three notes, 3 s: finite, RMS > 1e-3.
// polyAudioPathDoesNotAllocate: as audioPathDoesNotAllocate, with poly on and notes arriving past the three voices.
```

In `render_test.cpp`, `presetTests` and `applyPreset` apply `poly`, `coupling` and intervals. For a preset with `midiGate && poly`, also `SetVoiceNote(1, 52.0f)`, `SetVoiceNote(2, 55.0f)` and open gates on all voices. The existing pass condition then covers the three new presets.

- [ ] **Step 2: Build and verify it fails**

Run the build, then `build/plugin_host_test_artefacts/Release/plugin_host_test ...` and `ui_test`.
Expected: `parameter identity: FAIL`, and `ui_test` reports missing parameter `poly`.

- [ ] **Step 3: Add the parameters and presets**

In `createLayout`, after `midigate`: `AudioParameterBool "poly"`, `AudioParameterFloat "coupling"` (percent readout), and `AudioParameterInt "interval2"`/`"interval3"` (-24..24, defaults 7 and 12, text like `+7 st`, `0 st`, `-5 st`). `setCurrentProgram` sets them all, with the intervals through `prm->convertTo0to1`.

Start the new rows from these values and adjust by listening, keeping `render_test`'s preset check green:

| Name | knob | Others |
|---|---|---|
| Open Fifths | 0.40, 0.75, 0.30, 0.85, 0.15, 0.35, 0.60, 0.20, 0.50, 0.35, 0.70 | poly, coupling 0, +7, +12 |
| Sympathetic | 0.30, 0.78, 0.40, 0.70, 0.12, 0.45, 0.70, 0.15, 0.60, 0.30, 0.70 | poly, coupling 0.4, +7, +16 |
| Chord Keys | 0.40, 0.75, 0.30, 0.80, 0.20, 0.35, 0.60, 0.20, 0.50, 0.30, 0.70 | midiPitch, midiGate, poly, coupling 0.15 |

- [ ] **Step 4: Add the MIDI logic to the processor**

- Add `infrasonic::VoiceAllocator allocator`, `bool lastPoly`, `std::atomic<float> voiceNotes_[3]`, `std::atomic<bool> polyMidiActive_`.
- `handleMidi`: keep the mono `held` list and `lastNote` updated as today, and also forward note-on, note-off and All Notes Off to `allocator`.
- `updateParams`: `lastFreqKnob` branch, when `heldCount == 0`, also `allocator.Reset()`. On a change of `poly`: `allocator.Assign(held, heldCount)`, and when `heldCount == 0 && haveNote`, `allocator.NoteOn(lastNote); allocator.NoteOff(lastNote);` so a latched drone keeps sounding.
- `applyMidiState`, when polyphony and `pitchOn` are both on:

```cpp
for each voice v:
    note = allocator.Note(v)
    assigned = note != kNoNote
    v == 0 ? core->SetMidiPitch(true, assigned ? note + bendSemis : lastNote + bendSemis)
           : core->SetVoiceNote(v, assigned ? note + bendSemis : note-of-voice-0)   // a closed voice's pitch does not matter
    core->SetVoiceGate(v, true, gateParam ? (assigned && allocator.Held(v)) : assigned)
```

Otherwise, today's calls for voice 0, and `SetVoiceGate(v, gateParam, heldCount > 0)` for voices 1 and 2. `core->SetPolyphony`, `SetCoupling` and `SetIntervals` go in `updateParams`. Publish `voiceNotes_` (negative when unassigned) and `polyMidiActive_` for the bridge.

- [ ] **Step 5: Run everything and verify it passes**

Run the build, `render_test`, `ui_test`, `plugin_host_test` and `check_reference.sh`.
Expected: `ALL OK` from each test program, `parameter identity: OK`, `saved state from the baseline build: OK`, every checksum line `OK`.

- [ ] **Step 6: Commit**

```bash
git add src/plugin tests
git commit -m "Add the polyphony parameters, MIDI handling and presets"
```

---

### Task 6: The STRINGS tab

**Files:**
- Modify: `src/plugin/ui/TabPanel.h:33` (`kNumTabs = 6`), `src/plugin/ui/TabPanel.cpp:10,23-29,63-67`, `src/plugin/ui/Readouts.h`, `src/plugin/PluginEditor.cpp:77-80`
- Test: `tests/ui_test.cpp`

**Interfaces:**
- Consumes: `EditorBridge::polyMidiActive`, `voiceNote` from Task 5.
- Produces: `drosera::readouts::interval(bool polyMidi, float note, const std::string& knobText) -> Text`.

- [ ] **Step 1: Write the failing tests**

```cpp
// readoutTests:
//   interval(false, 60.0f, "+7 st")  -> {"+7 st", ""}
//   interval(true,  60.0f, "+7 st")  -> {"C4", "MIDI"}
//   interval(true,  -1.0f, "+7 st")  -> {"-", "MIDI"}
// editorTests:
//   stringsTabShowsItsControls: setTab(5): coupling, interval2, interval3 shown; lpf hidden.
//   sixthTabPersists: setTab(5), state round trip, tab() == 5.
//   tabsFitTheWindow: the last tab button's right edge <= 760 - 26.
//   intervalReadoutFollowsMidi: no MIDI: readout("interval2") == "+7 st", tag empty.
//     poly and midipitch on, notes 48,55: readout("interval2") == "G3", tag "MIDI", slider dimmed but enabled.
//     readout("interval3") == "-", tag "MIDI".
```

`tabsFitTheWindow` needs the button bounds: expose `juce::Rectangle<int> tabBounds(int) const` on `TabPanel` if `findChildWithID` is not simpler. Pick one and use it.

- [ ] **Step 2: Build and verify it fails**

Run: `cmake --build build --target ui_test`
Expected: compile error, `interval` is not in `drosera::readouts`.

- [ ] **Step 3: Implement**

- `kTabStep = 118`, `kTabWidth = 108`.
- Page appended after MIDI: `{"STRINGS", {"coupling", "interval2", "interval3"}, {"poly"}}`.
- Captions: `{"coupling","Coupling"}, {"interval2","Interval 2"}, {"interval3","Interval 3"}, {"poly","POLY"}`.
- In `refreshDisplay`'s knob loop, `interval2` and `interval3` show `readouts::interval(bridge_.polyMidiActive(), bridge_.voiceNote(1 or 2), text)`, value and tag. Other knobs are unchanged.

- [ ] **Step 4: Run the tests and check the window**

Run: `build/ui_test_artefacts/Release/ui_test`
Expected: `ALL OK`.

Then draw it and look at the tab strip's labels and the page, including that no label is cut off:

```bash
build/editor_snapshot_artefacts/Release/editor_snapshot /tmp/drosera-strings.png tab=5
```

- [ ] **Step 5: Commit**

```bash
git add src/plugin tests
git commit -m "Add the STRINGS tab"
```

---

### Task 7: Documentation and final check

**Files:**
- Modify: `docs/MANUAL.md`, `README.md:9`, `CHANGELOG.md` (`[Unreleased]`), `CLAUDE.md` (code map)

- [ ] **Step 1: Update the documents**

- `docs/MANUAL.md`: a `## STRINGS tab` section with POLY, Coupling (including that a closed voice can ring faintly when it is high), the intervals and their limit without MIDI, voice assignment, and MIDI Gate per voice. Presets: 15, and name the new three. Known limits: at Coupling above 0 the notes can move each other's pitch.
- `README.md`: 15 presets.
- `CHANGELOG.md`, under `[Unreleased]`, `### Added`: three-string polyphony, the STRINGS tab, three presets.
- `CLAUDE.md` code map: `src/dsp/` mentions `FeedbackVoice` and `VoiceAllocator`, and `tests/` mentions `voice_allocator_test`.

- [ ] **Step 2: Clean build and run every check**

Run the build from a clean `build` directory, then `build/voice_allocator_test`, `render_test`, `ui_test`, `plugin_host_test`, `check_reference.sh`.
Expected: all pass, with no checksum changed and `git status` showing `tests/fixtures/state-baseline.bin` and `tools/hardware-comparison/reference/SHA256SUMS` unmodified.

Record `render_test`'s `cpuReport` figures and the final `kMaxCoupling` for the hand-over.

- [ ] **Step 3: Commit**

```bash
git add docs/MANUAL.md README.md CHANGELOG.md CLAUDE.md
git commit -m "Document three-string polyphony"
```
