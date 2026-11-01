# Rugiada Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the grid of knobs with the Rugiada interface, which shows the real spectrum as a sundew and readouts of what really sounds, then release 0.1.0.

**Architecture:** The audio thread feeds a lock-free tap only while an editor is open. The message thread drains it at 30 Hz, runs the FFT and draws the plant. Spectrum and text logic are JUCE-free functions, so they can be tested without a window; the editor only assembles components.

**Tech Stack:** JUCE 8.0.9 (`juce_gui_basics`, `juce_dsp`), C++17.

**Spec:** `docs/superpowers/specs/2026-11-01-rugiada-interface-design.md`

**Requires:** `2026-11-01-repository-and-release.md` complete, including its Task 7. If TAPE was removed for 0.1.0, leave out every TAPE, Wow and Age item below.

## Global Constraints

- Fixed window 760×500, drawn as vectors. Not resizable.
- Parameter names are in English everywhere. Parameter IDs do not change.
- Colours: background `#120a10`, panel `#1c0d15`, stalk `#a33244`, dew and accent `#ffc27a`, secondary knob `#c98a6a`, knob track `#3a1a24`, text `#e9d6c8`, secondary text `#a98f88`.
- 54 tentacles, log-spaced 30 Hz to 16 kHz. Range −72 to 0 dBFS. Attack 40 ms, release 300 ms. **No invented motion.**
- FIFO 4096 samples, FFT 2048 points, Hann window, 30 Hz timer.
- No DSP changes. With plugin-only features off, renders stay byte-identical to `tools/hardware-comparison/reference/SHA256SUMS`.
- No allocation, locks or file access on the audio thread.
- Work on branch `rugiada-ui`, cut from `main`. Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Pushing, merging and tagging need the owner's approval each time.

## Review Focus

1. **The window opens before the host has called `prepareToPlay`**, so the sample rate is 0. The plant must rest without dividing by zero. Task 1 tests the rate of 0.
2. **Frequency is moved while the MIDI tag shows.** The knob looks dimmed, yet moving it must still take pitch back once no key is held. Dimming must not disable it. Task 4 tests that the slider stays enabled.
3. **A project holds an invalid or missing open tab**, from an older version or a damaged file. The window must open on Filters. Task 4 tests both.
4. **The host switches sample rate, or closes and reopens the window, during playback.** Stale samples must not appear as a burst on the plant. Task 1 tests the flush, and Task 2 wires it to both events.
5. **The host changes preset or automates Wow while another tab is open or TAPE is off.** Hidden controls must show the right value when they reappear. Task 4 tests it.

---

### Task 1: Spectrum logic and the tap

**Files:**
- Create: `src/plugin/SpectrumTap.h`, `src/plugin/ui/SpectrumBands.h`, `tests/ui_test.cpp`
- Modify: `CMakeLists.txt`, `.github/workflows/ci.yml`

**Interfaces:**
- Produces, neither file including JUCE:

```cpp
namespace drosera {
class SpectrumTap {
public:
    static constexpr size_t kCapacity = 4096;
    void setActive(bool) noexcept;            // true also requests a flush
    bool isActive() const noexcept;
    void requestFlush() noexcept;             // any thread; the consumer applies it
    void push(const float* l, const float* r, size_t n) noexcept; // audio thread; mono sum; non-finite becomes 0; no-op when inactive; drops when full
    size_t pop(float* dst, size_t max) noexcept;                  // message thread
};
constexpr int kNumBands = 54;
constexpr float kFloorDb = -72.0f;
float bandCentreHz(int band);
// magnitudes: unnormalised |X[k]|, k = 0..fftSize/2. A full-scale sine gives 0 dBFS.
void computeBands(const float* magnitudes, int fftSize, double sampleRate, float* outDb);
float smoothLevel(float current, float target, float dtSeconds); // attack 40 ms, release 300 ms
float bandAmount(float db);                                      // kFloorDb..0 to 0..1, clamped
}
```

- [ ] **Step 1: Write the failing tests in `tests/ui_test.cpp`**

The tap uses a fixed `std::array` member, so it never allocates, which is stricter than the spec. Flushing is a request the consumer applies, because `prepareToPlay` and the editor timer can run on different threads, and only the consumer may move the read position.

Tap tests:
- `tapKeepsOrder`: a producer thread pushes 200000 increasing samples in blocks of 64 while the consumer pops. Each popped value is greater than the last.
- `tapDropsWhenFull`: active, push 5000 with no consumer. `pop` returns 4096 values, which are the first 4096.
- `tapInactiveIgnores`: push while inactive, then `pop` returns 0.
- `tapSanitises`: NaN and infinity come out as 0.
- `tapFlush`: push 100, `requestFlush`, `pop` returns 0.
- `tapMonoSum`: L = 1, R = 0 gives 0.5.

Band tests, with magnitudes from `fft` in `tests/pitch_estimator.h` and a Hann window:
- `bandEnds`: `bandCentreHz(0)` is 30, `bandCentreHz(53)` is 16000.
- `sineLightsItsBand`: at 48 kHz, 1 kHz peaks at band 30 and 5 kHz at band 43. Every band more than 1 away is at least 30 dB below the peak. At 96 kHz the same with 20 dB, because bins are twice as wide.
- `calibration`: a full-scale 1 kHz sine reads within 0.5 dB of 0, and at −24 dBFS within 1 dB of −24.
- `silenceRests`: zeros give exactly `kFloorDb` for all 54.
- `aboveNyquistRests`: at 22050 Hz, bands with centres above 11025 Hz are at the floor.
- `zeroRateRests`: `sampleRate` 0 gives the floor for all, with no crash.
- `lowBandsHaveNoHoles`: a 60 Hz sine at 48 kHz, where bands are narrower than a bin, lights the band nearest 60 Hz above −6 dB.
- `ballistics`: rising from −72 towards 0 with `dt` 0.040 covers 63 % of the distance within 1 %. Falling with `dt` 0.300 covers 63 %. Two steps of 0.020 equal one of 0.040 within 0.1 dB.
- `bandAmountRange`: −72 gives 0, 0 gives 1, −100 gives 0, +6 gives 1.

- [ ] **Step 2: Add `ui_test` to CMake and run it to verify it fails**

`juce_add_console_app(ui_test)` inside the plugin block, linked like `editor_snapshot`. Move the plugin sources into `set(DROSERA_PLUGIN_SOURCES ...)` so `Drosera`, `editor_snapshot` and `ui_test` share one list. Add `juce::juce_dsp` to all three.

Run: `cmake --build build --target ui_test`
Expected: FAIL, headers not found.

- [ ] **Step 3: Implement both headers**

`SpectrumTap`: monotonic `uint32_t` indices with acquire and release ordering, masked into the array. `computeBands`: band edges are geometric midpoints between centres, the value is the largest normalised magnitude (`4 / fftSize × |X|`) among bins inside the edges, and a band with no bin inside uses the nearest bin. `smoothLevel` is a one-pole with `1 − exp(−dt / tau)`, choosing tau by direction, so timer jitter does not change the response.

- [ ] **Step 4: Run and add to CI**

Expected: `ALL OK` from `build/ui_test_artefacts/Release/ui_test`. Add it to `ci.yml` after `render_test`.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "Add the spectrum tap and band mapping for the new interface"
```

---

### Task 2: Publish what really sounds, and connect the processor

**Files:**
- Create: `src/plugin/EditorBridge.h`, `src/plugin/ui/Readouts.h`
- Modify: `src/dsp/DroseraCore.h`, `src/plugin/PluginProcessor.cpp`
- Test: `tests/render_test.cpp`, `tests/ui_test.cpp`

**Interfaces:**
- Produces on `DroseraCore`: `float PublishedNote() const`, `float PublishedBodySeconds() const`, `float PublishedEchoSeconds() const`, all relaxed atomic loads.
- Produces:

```cpp
class EditorBridge {
public:
    virtual ~EditorBridge() = default;
    virtual bool midiPitchActive() const = 0;
    virtual float soundingNote() const = 0;
    virtual float bodySeconds() const = 0;
    virtual float echoSeconds() const = 0;
    virtual double hostSampleRate() const = 0;  // 0 until prepared
    virtual drosera::SpectrumTap& spectrumTap() = 0;
};

namespace drosera::readouts {
struct Text { std::string value, tag; };
constexpr float kTapeUnityKnob = 0.69f;
std::string noteName(float midiNote);                              // 60 is "C4", 69 is "A4"
Text frequency(bool midi, float note, const std::string& knobText);
Text body(bool midi, float seconds, const std::string& knobText);
std::string echoTime(bool warp, float seconds, const std::string& knobText);
std::string echoFeedback(bool tape, const std::string& knobText);
}
```

- [ ] **Step 1: Write the failing tests**

`render_test.cpp`, `publishedValues`:
- With the Body knob at 0.30 and no MIDI, after 2 seconds `PublishedBodySeconds()` is within 1 % of `daisysp::fmap(0.30f, 0.001f, 0.1f, daisysp::Mapping::EXP)`.
- With `SetMidiPitch(true, 60)` and the same knob, after 2 seconds `PublishedNote()` is within 0.01 of 60, and `PublishedBodySeconds()` is within half a period of 3 periods of 261.63 Hz. The published value includes the loop's phase compensation, which can reach half a period.
- With the knob at 0.5 and WARP on, after 1 second `PublishedEchoSeconds()` is within 1 % of `daisysp::fmap(0.25f, 0.05f, 5.0f, daisysp::Mapping::EXP)`.

`ui_test.cpp`, each row exactly:

| Call | Result |
|---|---|
| `frequency(true, 69, "x")` | `{"A4 440.0 Hz", "MIDI"}` |
| `frequency(true, 60.4, "x")` | value starts `"C4 "`, Hz computed from 60.4 |
| `frequency(false, 69, "82.4 Hz")` | `{"82.4 Hz", ""}` |
| `body(true, 0.0971, "x")` | `{"97.1 ms", "SNAP"}` |
| `body(false, 0.0971, "30.0 ms")` | `{"30.0 ms", ""}` |
| `echoTime(true, 0.359, "x")` | `"0.36 s"` |
| `echoTime(false, 0.359, "1.29 s")` | `"1.29 s"` |
| `echoFeedback(true, "60 %")` | `""` |
| `echoFeedback(false, "60 %")` | `"60 %"` |
| `noteName(40)` | `"E2"` |

- [ ] **Step 2: Run to verify they fail**

Expected: compile errors on the missing functions.

- [ ] **Step 3: Implement**

In `DroseraCore`: store in `applyPitchAndBody` and in the `EchoTime` registry callback, turning that `std::bind` into a lambda. Publish the delay actually sent to the engine: in knob mode `body_s_`, in MIDI mode the compensated value. These are `std::atomic<float>` with `memory_order_relaxed`, and audio is not read from them.

In `DroseraProcessor`: derive from `EditorBridge`. Add a `SpectrumTap` member, call `requestFlush` in `prepareToPlay` and store `hostRate`. Call `tap.push` at the end of `processBlock`, after the clamp, when active. Today's editor keeps its current constructor until Task 4 passes `*this` to the new one.

- [ ] **Step 4: Verify nothing audible changed**

`render_test`, `ui_test` and `plugin_host_test` print `ALL OK`, and `check_reference.sh build/render_cli` exits 0.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "Publish sounding values and feed the spectrum tap"
```

---

### Task 3: Theme, knobs and the plant

**Files:**
- Create: `src/plugin/ui/Theme.h`, `src/plugin/ui/DroseraLookAndFeel.h`, `.cpp`, `src/plugin/ui/PlantView.h`, `.cpp`
- Modify: `CMakeLists.txt`, `tests/editor_snapshot.cpp`, `tests/ui_test.cpp`

**Interfaces:**
- Consumes: Task 1's `SpectrumTap`, `computeBands`, `smoothLevel`, `bandAmount`.
- Produces: `class PlantView : public juce::Component`, with:
  - `juce::Slider& feedback()`, the child control
  - `void update(SpectrumTap&, double sampleRate, float dtSeconds)`, called by the editor's timer: drains the tap, runs the FFT, updates levels, repaints only if changed
  - `float level(int band) const`, for tests
- Produces: slider property `"unityMark"` (double, 0..1). When present, the look and feel draws a tick at that fraction of the travel.

- [ ] **Step 1: Write the failing test**

`plantRestsAndResponds` in `ui_test.cpp`, using a real `PlantView` and tap:
- With no audio, after 30 `update` calls with `dt` 1/30, every `level` equals `kFloorDb`.
- After pushing a −12 dBFS 1 kHz sine for 1 second, `level(30)` is above −20.
- With zeros pushed for 3 more seconds, `level(30)` is back within 1 dB of the floor.
- Repeat the sine, then push **nothing** for 3 seconds, as when a host stops calling `processBlock`. `level(30)` is back within 1 dB of the floor, not frozen on the last sound.
- With a sample rate of 0, it does not crash and stays at rest.

- [ ] **Step 2: Run to verify it fails**

Expected: `PlantView` not found.

- [ ] **Step 3: Implement `Theme.h` and the look and feel**

Exact colours from Global Constraints. Use JUCE's default sans font, with nothing embedded. Knobs: 270° sweep starting at 135° clockwise from the positive x axis, as in the mockup's `dial`. Draw the track, value arc in accent, pointer and optional `unityMark`. Move the toggle drawing over from the old editor.

- [ ] **Step 4: Implement `PlantView`**

Geometry from the mockup at window coordinates: centre (380, 192), inner radius 54, reach 88. Band `i` sits at angle `i / 54 × 2π − π / 2`, so the lowest band is at the top and frequency runs clockwise. The stalk runs from the inner radius to `inner + 8 + reach × bandAmount`. The dew's size and brightness follow the same amount. At rest it is short, dim and still.

`setInterceptsMouseClicks(false, true)`, so only the Feedback knob at the heart takes clicks. Hold a `juce::dsp::FFT` of order 11 and a 2048-sample history; each update applies the Hann window to the latest 2048 samples. An update that pops no samples clears the history, so a stopped host reads as silence. All members are preallocated in the constructor.

- [ ] **Step 5: Verify and commit**

`ui_test` prints `ALL OK`.

```bash
git add -A && git commit -m "Add the theme, knob drawing and the plant view"
```

---

### Task 4: Tabs and the new editor

**Files:**
- Create: `src/plugin/ui/TabPanel.h`, `.cpp`
- Rewrite: `src/plugin/PluginEditor.h`, `.cpp`
- Modify: `tests/editor_snapshot.cpp`, `tests/ui_test.cpp`, `src/plugin/PluginProcessor.cpp`

**Interfaces:**
- Consumes: `EditorBridge`, `readouts`, `PlantView`, `DroseraLookAndFeel`.
- Produces: `DroseraEditor(juce::AudioProcessor&, juce::AudioProcessorValueTreeState&, EditorBridge&)`, with public:
  - `void setTab(int)`, clamped to 0..4 and stored in `apvts.state` property `"uiTab"`
  - `int tab() const`
  - `void refreshDisplay(float dtSeconds)`, the timer's body, public so tests and snapshots can run it
  - `juce::Slider* slider(const juce::String& paramId)`
  - `juce::String readout(const juce::String& paramId) const`
  - `juce::String tag(const juce::String& paramId) const`
- Tabs in order 0 to 4: Filters, Reverb, Echo, Output, MIDI, with the controls in the spec's table. Captions are short (`Mix`, `Send`); host parameter names do not change.

- [ ] **Step 1: Write the failing tests in `ui_test.cpp`**

Use the real processor from `createPluginFilter()` and its editor:
- `tabPersists`: `setTab(3)`, save state, restore into a new processor. Its editor's `tab()` is 3.
- `tabSurvivesBadState`: restore state with `uiTab` set to 99, and state with no `uiTab`, as in a project from an earlier version. `tab()` is 0 both times, and a parameter saved in that state still loads. Build both states in this test by editing the XML from `getStateInformation`. Older projects as a whole are covered by the fixture in `plugin_host_test`.
- `tapeHidesControls`: `echotape` off, then `slider("tapewow")->isVisible()` is false on the Echo tab. On, true. With another tab open, false.
- `hiddenValuesFollow`: with TAPE off and the Filters tab open, set `tapewow` to 0.8 and apply a preset. On returning to Echo with TAPE on, the slider's value equals the parameter.
- `midiDimsButDoesNotDisable`: play note 60 through `processBlock` with `midipitch` on and call `refreshDisplay`. `tag("freq")` is `"MIDI"`, `tag("body")` is `"SNAP"`, `readout("freq")` starts `"C4"`, `slider("freq")->isEnabled()` is true, and its alpha is below 1. After note-off with the Frequency parameter moved, `tag("freq")` is `""`.
- `warpShowsRealTime`: with WARP on and audio rendered, `readout("echotime")` equals `readouts::echoTime(true, published, ...)`, and it differs from the WARP-off text.
- `tapeHidesPercent`: with TAPE on, `readout("echofb")` is empty and `slider("echofb")` has `unityMark` 0.69. With TAPE off, it has text and no mark.
- `closingStopsTap`: while an editor exists, `spectrumTap().isActive()` is true. After destruction, false.
- `reopenDoesNotBurst`: open, render 1 second, close, render 1 second, reopen, and call `refreshDisplay` once with `dt` 1/30 and no new audio. Every level is at most the floor plus 1 dB.

- [ ] **Step 2: Run to verify they fail**

Expected: compile errors on the new editor API.

- [ ] **Step 3: Implement `TabPanel` and rewrite the editor**

Layout, using the mockup's option 2 for proportions: fixed 760×500 with `setResizable(false, false)`. Header with "DROSERA" left and preset selector with arrows right. `PlantView` over the whole foreground, Feedback at (380, 192). Frequency and Body large on either side. Tab strip with five tabs 132 wide and a 3 px accent underline at y 336. The panel below it shows one tab.

Each knob has a separate `juce::Label` readout set from the timer, and the slider's own text box is off. Dim with `setAlpha`, never `setEnabled(false)`. The Echo tab lays out again when TAPE changes. The timer runs at 30 Hz, calls `refreshDisplay` with the real elapsed time, follows host program changes as today, and re-reads `uiTab`. Constructor: `tap.setActive(true)`. Destructor: `tap.setActive(false)`. Delete the old look and feel.

- [ ] **Step 4: Add snapshot options and look at every state**

`editor_snapshot` gains `tab=N` and `sound=1`. The second renders 1 second with the editor open and runs `refreshDisplay` 30 times, so the plant shows real sound. Generate into the scratchpad, and **read each image**: five tabs, TAPE on and off, MIDI held with tags, the silent plant, and preset `Default` with `sound=1`. Compare with `docs/design/rugiada-layout-mockup.html` option 2. Fix clipped text and overlaps.

- [ ] **Step 5: Verify**

`ui_test`, `render_test` and `plugin_host_test` print `ALL OK`. `check_reference.sh` exits 0. On the standard build, the VST3 passes `codesign --verify --deep --strict`.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "Replace the editor with the Rugiada interface"
```

---

### Task 5: Documents and CI on the branch

**Files:**
- Create: `docs/images/drosera.png`
- Modify: `README.md`, `docs/MANUAL.md`, `CHANGELOG.md`, `CLAUDE.md`

- [ ] **Step 1: Update the documents**

Add a snapshot of `Default` with `sound=1` as `docs/images/drosera.png`, and put it at the top of the README. In the manual, describe the plant, tabs and tagged readouts, and say plainly that low bands are coarser than high ones, because a 2048-point FFT at 48 kHz has 23 Hz bins. Add the new code map entries to `CLAUDE.md`, and the interface to `Unreleased` in `CHANGELOG.md`.

- [ ] **Step 2: Ask the owner to approve the push, then confirm CI**

```bash
git push -u origin rugiada-ui
```

Expected: `ci.yml` is green on both systems, including pluginval, which exercises the editor. `ui_test` opens real windows on the runners. If a runner cannot, record that and run only the JUCE-free tests there. Fix real failures in the code.

- [ ] **Step 3: Commit any fixes**

```bash
git add -A && git commit -m "Document the new interface"
```

---

### Task 6: Hand checks and release 0.1.0

The automated work ends here. These steps need the owner, and none can be claimed from CI.

**Files:**
- Modify: `CHANGELOG.md`, and `CMakeLists.txt` if the version changes

- [ ] **Step 1: Install the build and give the owner this list**

- Use every control with the mouse, in a real DAW, on each system available.
- Listen to all 14 presets, and play notes with PITCH on.
- Measure GUI CPU with the window open in Activity Monitor on their Mac. The target is below 5 % of one core.
- Judge whether the plant looks right with real sound. If ranges feel wrong, tune the mapping in `bandAmount` and the plant's reach, not the audio.

- [ ] **Step 2: Apply what comes back, and record any waiver**

Fix what the owner reports, repeating Task 4 Step 5. If no Windows machine was available, the release notes must say Windows was tested only by CI. Say too that the x86_64 slice was built but not run, unless someone ran it.

- [ ] **Step 3: Confirm the release criteria**

Each with evidence: CI green with pluginval on both systems; DAW and listening done; the name and licence decisions from the previous plan's Task 7 recorded; the copyright holder in `README.md`.

- [ ] **Step 4: Prepare the version**

Move `Unreleased` into `## [0.1.0] - <today's date>`, and add an empty `## [Unreleased]` above it. Run `python3 tools/release_check.py v0.1.0`. Expected: prints the section, exit 0.

- [ ] **Step 5: Ask the owner to approve the merge and tag**

```bash
git tag v0.1.0 && git push origin main v0.1.0
```

- [ ] **Step 6: Check the draft, then hand it over**

Download and verify the zips as in the previous plan's Task 6 Step 8. The owner reviews and publishes the draft: this plan never publishes it.
