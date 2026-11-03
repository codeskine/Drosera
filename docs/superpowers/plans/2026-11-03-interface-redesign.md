# Interface Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the tabbed Rugiada window with the one-page, symmetric interface of the spec: plant at the centre, Frequency and Body beside it, one row of FILTER · REVERB | PONG | ECHO, a setup panel, and a window that scales from 75 % to 150 %.

**Architecture:** The editor draws a 900×560 content component and scales it with a transform. New focused components replace `TabPanel`: `ControlRow`, `SetupPanel`, `HeaderBar`, and a rewritten `PlantView`. Logic with no JUCE (`ResponseArcs.h`, `EchoPulse.h`, `Readouts.h`, `SpectrumBands.h`) is tested alone. The audio thread and the engine do not change.

**Tech Stack:** C++17, JUCE 8.0.9 (`juce_gui_basics`, `juce_dsp`), DaisySP's `fmap` for parameter mappings, CMake with Ninja, `juce_add_binary_data` for the fonts.

**Spec:** `docs/superpowers/specs/2026-11-03-interface-redesign-design.md`. The visual reference is `docs/design/drosera-redesign-mockup.html`: every coordinate below is in its 900×560 frame.

## Global Constraints

- No allocation, locks or file access on the audio thread. `render_test` counts allocations (CLAUDE.md rule 1).
- With the plugin-only features off, renders stay byte-identical to `tools/hardware-comparison/reference/SHA256SUMS` (rule 2). Nothing here touches the engine.
- Parameter IDs and names never change, and stay in English (rule 5).
- Do not regenerate `tests/fixtures/state-baseline.bin`. `plugin_host_test` must pass unchanged.
- Colours, verbatim from the spec: background `#1e1416` to `#0f0a0b`; loop `#d0465c`, tentacle bases `#a33244`; echo `#ffc27a`; knob track `#2a1f21`; text `#eadacd`; secondary text `#9d8a83`; hairlines `#33272a` to `#3d2f31`.
- Fonts: Cormorant Garamond for the name, Inter for everything else, embedded as binary data, with JUCE's default sans-serif as fallback.
- There is no invented motion: a silent plugin shows a still, closed plant.
- Levels rise with a 40 ms attack and fall with a 300 ms release, as today.
- Comments and names follow the surrounding code: short comments that say why, English everywhere.

## Review Focus

1. **A host restoring state off the message thread while the window reads or writes the scale** must not crash or tear: `uiScale` is an atomic, read and written like `uiTab` today. Test in Task 7.
2. **A window resized by the host to a size outside 675×420 to 1350×840, or with another aspect**, must keep the content's proportions and stay usable. Test in Task 7.
3. **Changes made while the setup panel is closed** (a preset, an automation lane) must show when it opens, as hidden tabs did. Test in Task 4.
4. **Clicking a dimmed PONG knob in CLASSIC** must still change the parameter, so it can be set before switching. Test in Task 4.
5. **POLY with fewer than three sounding notes** must name only the voices that sound, and never print a note for a silent voice. Test in Task 3.

---

### Task 1: Theme, fonts and knob drawing

**Files:**
- Create: `assets/fonts/CormorantGaramond-Medium.ttf`, `assets/fonts/Inter-Regular.ttf`, `assets/fonts/Inter-Medium.ttf`, `assets/fonts/OFL.txt`
- Create: `src/plugin/ui/LabeledKnob.h`, `src/plugin/ui/LabeledKnob.cpp` (moved out of `TabPanel.*`)
- Modify: `src/plugin/ui/Theme.h`, `src/plugin/ui/DroseraLookAndFeel.h/.cpp`, `src/plugin/ui/TabPanel.h/.cpp`, `src/plugin/PluginEditor.cpp`, `src/plugin/ui/PlantView.cpp`, `CMakeLists.txt`
- Test: `tests/ui_test.cpp`

**Interfaces:**
- Produces, in `namespace drosera::theme`: `loop()`, `loopDeep()`, `echo()`, `track()`, `text()`, `textDim()`, `hairline()` (`#33272a`), `cartouche()` (`#3d2f31`), `backgroundCentre()`, `backgroundEdge()`, all `juce::Colour`; `juce::Font titleFont()` (Cormorant Garamond 26, kerning for 9 px spacing), `labelFont()` (Inter Medium 11), `largeLabelFont()` (Inter Medium 14), `valueFont()` (Inter 10), `largeValueFont()` (Inter 12), `captionFont()` (Inter Medium 9.5, kerning for 3 px spacing); `bool fontsEmbedded()`.
- Produces, in `ui/LabeledKnob.h`: `enum class KnobSize { large, small, mini };` `enum class KnobGroup { loop, echo };` `LabeledKnob(juce::RangedAudioParameter&, const juce::String& caption, KnobSize, KnobGroup)`, keeping `slider()`, `parameter()`, `show(value, tag)`, `readout()`, `tag()`, and adding `void setDimmed(bool)` (alpha 0.35, still enabled) and `static int radius(KnobSize)` (48, 20, 18).
- Produces: `DroseraLookAndFeel::kGroup` slider property (`"loop"` or `"echo"`) next to `kMainControl`.

- [ ] **Step 1: Ask the owner to approve downloading the three font files and the OFL licence from Google Fonts** (the `google/fonts` repository, `ofl/cormorantgaramond` and `ofl/inter`). Downloading needs their yes. If Inter only ships as a variable font, ask them which static weights to use, or instance Regular and Medium from it.
- [ ] **Step 2: Write the failing test `fontsAreEmbedded`** in a new `themeTests()` called from `main`: `check(theme::fontsEmbedded(), "fontsAreEmbedded")`, and `check(theme::titleFont().getTypefaceName() == "Cormorant Garamond", "titleUsesCormorant")`.
- [ ] **Step 3: Run it to see it fail.** `cmake --build build --target ui_test && build/ui_test_artefacts/Release/ui_test`. Expected: does not compile, `fontsEmbedded` undeclared.
- [ ] **Step 4: Add `juce_add_binary_data(DroseraFonts SOURCES assets/fonts/*.ttf)`** and link it to `Drosera`, `ui_test` and `editor_snapshot`. Implement the theme with typefaces created once from `BinaryData` (`juce::Typeface::createSystemTypefaceFor`), falling back to `juce::FontOptions` defaults when a typeface is null. Replace the old colour names (`background`, `panel`, `stalk`, `dew`, `knob`, `heart`) at every use.
- [ ] **Step 5: Move `LabeledKnob` to its own files with the new constructor**, and update `TabPanel` and the editor to pass `KnobSize` and `KnobGroup` (Frequency and Body: `large, loop`; tab knobs: `small`, `loop` for filters and reverb, `echo` for the rest). Captions below the knob in `labelFont`/`largeLabelFont`, values below that in `valueFont`/`largeValueFont`, as in the mockup's `drawKnob`.
- [ ] **Step 6: Redraw the rotary slider** in `DroseraLookAndFeel::drawRotarySlider`: a 270° track in `track()`, the value arc in the group's colour (`loop()` or `echo()`), a round cap with a radial gradient `#2f2426` to `#151012`, a pointer line in `text()`. Large knobs get a 21-tick scale outside the arc, every fifth tick longer, ticks below the value in the group colour. Stroke widths: 4 large, 2.6 small and mini. Use a soft second stroke at low alpha for the glow, not a blur.
- [ ] **Step 7: Run `ui_test`.** Expected: every check OK, the two new ones included.
- [ ] **Step 8: Commit.** `git add assets CMakeLists.txt src/plugin tests/ui_test.cpp && git commit -m "Embed the interface fonts and draw knobs in their group's colour"`

### Task 2: Response arcs and the echo pulse

**Files:**
- Create: `src/plugin/ui/ResponseArcs.h`, `src/plugin/ui/EchoPulse.h` (no JUCE)
- Test: `tests/ui_test.cpp`

**Interfaces:**
- Produces, in `namespace drosera::arcs`: `constexpr int kBandTicks = 26, kToneTicks = 11;` `constexpr float kBandStub = 3.0f, kBandSpan = 14.0f, kToneStub = 2.0f, kToneSpan = 9.0f;` `float filterGain(float hz, float highpassHz, float lowpassHz)` as `1/sqrt(1+(fc/f)^4) * 1/sqrt(1+(f/fc)^4)` (the loop's two 12 dB/octave filters); `float bandTickHz(int i)` log-spaced 10 Hz to 18 kHz; `void bandArc(float highpassHz, float lowpassHz, float* lengths)`, each `kBandStub + kBandSpan * gain`; `float toneTickHz(int i)` log-spaced 100 Hz to 6.4 kHz; `void toneArc(float centreHz, float* lengths)`, each `kToneStub + kToneSpan * exp(-0.9 * d * d)` with `d = log2(f / centre)`.
- Produces, in `namespace drosera::pulse`: `constexpr double kFlashSeconds = 0.08;` `enum class Lit { none, single, left, right };` `Lit echoPulse(double seconds, double periodSeconds, bool pong, bool sending)`: `none` when not sending; otherwise lit only for the first `kFlashSeconds` of each period (period floored at 0.05 s), `single` in CLASSIC, and in PONG `left` on even repeats and `right` on odd ones.

- [ ] **Step 1: Write the failing tests** in a new `arcTests()`:
  - `bandArcFollowsFilters`: with `bandArc(1000, 18000, len)`, every tick with `bandTickHz(i) < 300` has `len[i] < kBandStub + 0.1f * kBandSpan`, the tick nearest 5 kHz has `len > kBandStub + 0.9f * kBandSpan`.
  - `bandArcNeverVanishes`: with `bandArc(4000, 100, len)`, every `len[i] >= kBandStub`.
  - `toneArcPeaksAtTone`: with `toneArc(800, len)`, the longest tick's frequency is within one tick step of 800 Hz, and `toneArc(3200, len)` peaks above 2 kHz.
  - `pulseFollowsEchoTime`: `echoPulse(0.01, 0.5, false, true) == Lit::single`, `echoPulse(0.2, 0.5, false, true) == Lit::none`, `echoPulse(0.51, 0.5, true, true) == Lit::right`, `echoPulse(1.01, 0.5, true, true) == Lit::left`, `echoPulse(0.01, 0.5, false, false) == Lit::none`.
- [ ] **Step 2: Run them to see them fail** (headers missing).
- [ ] **Step 3: Implement both headers** from the Interfaces block.
- [ ] **Step 4: Run `ui_test`.** Expected: all OK.
- [ ] **Step 5: Commit.** `git commit -m "Add the filter and Tone arcs and the echo pulse, with no JUCE"`

### Task 3: Readouts that say what sounds

**Files:**
- Modify: `src/plugin/ui/Readouts.h`, `src/plugin/ui/SpectrumBands.h`, `src/plugin/EditorBridge.h`, `src/plugin/PluginProcessor.cpp`, `src/plugin/PluginEditor.cpp`
- Test: `tests/ui_test.cpp`

**Interfaces:**
- Produces, in `drosera::readouts`: `Text playedInterval(bool polyRunning, float rootNote, float voiceNote, int knobSemitones, const std::string& knobText)` returns `{knobText, ""}` unless POLY runs, the voice has a note (`>= 0`) and `lround(voiceNote - rootNote) != knobSemitones`; then `{format("%+.0f st", actual), "LIMIT"}`. `std::string chord(const float* notes, int count)` joins the `noteName` of each note `>= 0` with `" \xc2\xb7 "` (a middle dot).
- Produces, in `SpectrumBands.h`: `int bandOfNote(float midiNote)`, the band whose centre is nearest the note's frequency, or `-1` below 30 Hz or above 16 kHz.
- Produces, in `EditorBridge`: `virtual float publishedNote(int voice) const = 0;` (the processor returns `core->PublishedNote(voice)`) and `virtual bool polyRunning() const = 0;` (POLY's parameter is on).
- Consumes: the existing `interval()`, which keeps showing the note name under polyphonic MIDI.

- [ ] **Step 1: Write the failing tests.** In `readoutTests()`:
  - `is(playedInterval(true, 72, 84, 24, "+24 st"), "+12 st", "LIMIT")`
  - `is(playedInterval(true, 60, 67, 7, "+7 st"), "+7 st", "")`
  - `is(playedInterval(false, 72, 84, 24, "+24 st"), "+24 st", "")`
  - `is(playedInterval(true, 72, -1, 24, "+24 st"), "+24 st", "")`
  - `chord(notes{48, 55, 60}, 3) == "C3 · G3 · C4"` and `chord(notes{48, -1, 60}, 3) == "C3 · C4"`
  - `bandOfNote(69) == ` the band nearest 440 Hz by `bandCentreHz`, `bandOfNote(16) == -1`.

  In `editorTests()`, `intervalShowsWhatSounds`: Frequency at 1.0 (note 72), `poly` on, `interval2` at 1.0 (+24), render 0.2 s, `refreshDisplay`; then `readout("interval2") == "+12 st"` and `tag("interval2") == "LIMIT"`. And `frequencyNamesTheChord`: with `poly` on and Frequency, `interval2`, `interval3` at their defaults, after 0.2 s `readout("freq")` contains `" · "`.
- [ ] **Step 2: Run them to see them fail.**
- [ ] **Step 3: Implement the three helpers and the two bridge methods.** In `refreshDisplay`, the interval knobs use `interval()` under polyphonic MIDI and `playedInterval()` otherwise, with `publishedNote(0)` as the root. With POLY running, Frequency shows `chord()` of the three published notes, tagged MIDI only under MIDI pitch.
- [ ] **Step 4: Run `ui_test`.** Expected: all OK, the existing interval tests included.
- [ ] **Step 5: Commit.** `git commit -m "Show the interval each voice really plays, and the chord under POLY"`

### Task 4: One page: the control row and the setup panel replace the tabs

**Files:**
- Create: `src/plugin/ui/ControlRow.h/.cpp`, `src/plugin/ui/SetupPanel.h/.cpp`, `src/plugin/ui/Pill.h` (a pill-shaped toggle and the CLASSIC/PONG switch)
- Delete: `src/plugin/ui/TabPanel.h/.cpp`
- Modify: `src/plugin/PluginEditor.h/.cpp`, `src/plugin/EditorBridge.h`, `src/plugin/PluginProcessor.cpp` (drop `uiTab`), `src/plugin/ui/Readouts.h` (drop `echoShaping`), `CMakeLists.txt`, `tests/editor_snapshot.cpp` (drop `tab=`)
- Test: `tests/ui_test.cpp`

**Interfaces:**
- Consumes: `LabeledKnob` (Task 1), `arcs::*` and `pulse::*` (Task 2).
- Produces `ControlRow(juce::AudioProcessorValueTreeState&)`: `const std::map<juce::String, std::unique_ptr<LabeledKnob>>& knobs() const`; `juce::Button* toggle(const juce::String& paramId)` for `echopong` and `echohalve`; `void refresh(float highpassHz, float lowpassHz, float toneHz, bool pong, pulse::Lit)`, which redraws the arcs, dims PONG's knobs in CLASSIC with `setDimmed`, and lights the pulse.
- Produces `SetupPanel(juce::AudioProcessorValueTreeState&)`: `knobs()`, `toggle(paramId)` for `poly`, `polylock`, `midipitch`, `midigate`; `void refresh(bool polyOn)`, which dims Coupling and the intervals with POLY off.
- Produces on `DroseraEditor`: `void setSetupOpen(bool)`, `bool setupOpen() const`, `juce::Button* toggle(const juce::String& paramId)`; `slider(id)` searches the main knobs, the row and the panel. `setTab`, `tab`, `tabBounds`, `toggleBounds` are removed.
- Produces on `EditorBridge`: `openTab` and `setOpenTab` are removed, and the processor no longer saves `uiTab`.

Layout, in content coordinates, from the mockup:

| Item | Where |
|---|---|
| Frequency, Body (large) | centres (150, 236) and (750, 236) |
| Row knobs, centre y 466 | Highpass 66, Lowpass 126, Mix 206, Decay 266 (small); Drive 360, Tone 420, Width 480, Level 540 (mini); Send 634, Time 694, Feedback 754 (small) |
| CLASSIC/PONG switch | centre (834, 458), 92×22 |
| WARP pill | centre (834, 490), 50×18 |
| Band arc | ticks from x 46 to 146, base y 430 |
| Tone arc | ticks from x 404 to 436, base y 432 |
| Echo pulse | dots of radius 2.5 at (720, 452) in CLASSIC, (716, 452) and (726, 452) in PONG |
| Cartouches, y 534 | FILTER 44–148, REVERB 184–288, PONG 340–560 (alpha 0.4 in CLASSIC), ECHO 612–866 |
| Setup panel | from y 368 to the bottom, over a scrim on the rest. Cartouches at y 392: STRINGS 100–320, OUTPUT 375–525, MIDI 660–830. POLY (175, 418), LOCK (245, 418), Coupling (140, 476), Interval 2 (210, 476), Interval 3 (280, 476), Volume (415, 476), Input (485, 476), PITCH (705, 456), GATE (785, 456) |

A cartouche is a hairline pill in `cartouche()` around the name in `captionFont`, with fine rules in `hairline()` out to the group's edges, 6 px from the pill.

- [ ] **Step 1: Replace the tab tests.** Remove `tabPersists`, `tabSurvivesBadState`, `tabSurvivesMissingState`, `tabsShowTheirControls`, `stringsTabShowsItsControls`, `tabsFitTheWindow`, `pongTabSitsAfterEcho`, `otherTabsKeepTheirPlaces`, `echoTabShowsItsControls`, `pongTabShowsItsControls`, `pongControlsFit`, `shapingTagWithPongOff`, `shapingTagWithPongOn`, `pongTabPersists`, and the `echoShaping` readout checks. Add:
  - `windowSize`: `getWidth() == 900 && getHeight() == 560`.
  - `everyControlHasAWidget`: each of the 25 IDs (`freq body fbgain hpf lpf revmix revdecay echodrive echotone echowidth echolevel echosend echotime echofb echopong echohalve poly polylock coupling interval2 interval3 volume inputgain midipitch midigate`) has `slider(id)` or `toggle(id)`.
  - `playControlsAlwaysShow`: with the editor visible and the setup closed, Frequency, Body, Feedback and the eleven row knobs are `isShowing()`, and PONG's four are showing in both modes.
  - `setupPanelOpensAndCloses`: `setSetupOpen(true)` shows `coupling`, `volume` and `midipitch`; `setSetupOpen(false)` hides them.
  - `changesReachTheClosedPanel`: with the panel closed, set `coupling` to 0.7 and then `setCurrentProgram(10)`; on opening, the Coupling slider equals the parameter.
  - `pongControlsDimInClassic`: `echopong` 0, `refreshDisplay(0)`: `echodrive`'s knob has alpha below 1 and is showing; setting its slider to 0.8 sets the parameter to 0.8. `echopong` 1: alpha 1.
  - `stringsDimWithPolyOff`: with `poly` 0 and the panel open, `coupling`'s knob has alpha below 1.
  - `rowControlsFit`: the row's knob cells, the switch and the WARP pill lie within x 0 to 900 and do not intersect.
  - `savedTabIsIgnored`: a state with `uiTab="3"` loads with its parameters, and the editor opens at 900×560.
- [ ] **Step 2: Run `ui_test` to see the new tests fail.**
- [ ] **Step 3: Implement `Pill.h`, `ControlRow` and `SetupPanel`** with the layout above. The mode switch is a `juce::Button` attached to `echopong` with a `ButtonParameterAttachment`, drawn as the mockup's two-part pill, titled "Echo Pong" for screen readers. The pulse dots are painted by `ControlRow`, in `echo()`, lit or at alpha 0.2.
- [ ] **Step 4: Rebuild the editor at 900×560**: Frequency and Body as large knobs, the row, the panel and its scrim (a click on the scrim or Escape closes the panel). Compute the arcs' Hz with `daisysp::fmap(norm, 10, 4000, LOG)` for Highpass, `(norm, 100, 18000, LOG)` for Lowpass and `(norm, 200, 3200, LOG)` for Tone, as the engine does. Advance a clock on each tick for the pulse and pass `echoPulse(clock, bridge.echoSeconds(), pong, echoSend > 0)`. Leave the preset combo box and the plant where they are: Tasks 5 and 6 move them. Delete `TabPanel`, `uiTab`, `openTab`, `setOpenTab` and `echoShaping`. Drop `tab=` from `editor_snapshot`.
- [ ] **Step 5: Run `ui_test`, `render_test` and `plugin_host_test`.** Expected: all OK.
- [ ] **Step 6: Commit.** `git commit -m "Replace the tabs with one row of controls and a setup panel"`

### Task 5: The header

**Files:**
- Create: `src/plugin/ui/HeaderBar.h/.cpp`
- Modify: `src/plugin/PluginEditor.h/.cpp`, `src/plugin/ui/DroseraLookAndFeel.cpp` (popup menu colours), `CMakeLists.txt`
- Test: `tests/ui_test.cpp`

**Interfaces:**
- Produces `HeaderBar(juce::AudioProcessor&)`: `std::function<void(int)> onProgramChosen; std::function<void()> onSetupClicked;` `void setProgram(int)`, `void setSetupOpen(bool)`; `juce::Button& previousButton()`, `nextButton()`, `presetButton()`, `setupButton()`; `juce::PopupMenu presetMenu() const`, one ticked item per program.
- Consumes: `DroseraEditor::selectProgram(int)` and `setSetupOpen(bool)` (Task 4).

Layout: bounds (0, 0, 900, 70). PRESET field at (40, 30, 210, 30), radius 7, with ‹ and › cells 30 px wide divided by hairlines, the name centred and ▾ before the › cell. SETUP field at (650, 30, 210, 30) with "Strings · Output · MIDI" left-aligned and ▾, ▴ when open, in `echo()` when open. Captions "PRESET" and "SETUP" at y 20 in `captionFont`. "DROSERA" centred at y 46 in `titleFont`.

- [ ] **Step 1: Write the failing tests.** `presetArrowsCycle`: from program 0, `previousButton().triggerClick()` (with the message loop run, or `onClick()` called) reaches the last program and `nextButton()` comes back to 0. `presetMenuListsEveryPreset`: `presetMenu()` has `getNumPrograms()` items and the current one is ticked. `setupFieldOpensThePanel`: `setupButton()` toggles `setupOpen()`. Keep `presetNotifiesHost`.
- [ ] **Step 2: Run them to see them fail.**
- [ ] **Step 3: Implement `HeaderBar`** and put it in the editor in place of the combo box and its arrows. The preset button shows `presetMenu()` with `showMenuAsync` and calls `onProgramChosen`. The editor keeps following the host's program changes in `refreshDisplay`.
- [ ] **Step 4: Run `ui_test`.** Expected: all OK.
- [ ] **Step 5: Commit.** `git commit -m "Add the header with the preset and setup fields"`

### Task 6: The sundew

**Files:**
- Modify: `src/plugin/ui/PlantView.h/.cpp`, `src/plugin/PluginEditor.cpp`
- Test: `tests/ui_test.cpp`

**Interfaces:**
- Produces on `PlantView`: `static constexpr int tentacleBand(int tentacle) { return (tentacle * 23) % kNumBands; }`; `void setVoiceBands(const std::array<int, 3>&)` (`-1` for none) and `bool isMarked(int band) const`. Keeps `feedback()`, `setFeedbackText()`, `update()`, `level()`.
- Consumes: `bandOfNote` (Task 3) and `publishedNote` / `polyRunning` from the bridge, for the editor to call `setVoiceBands` on each tick.

Geometry, in the component's coordinates; the editor gives it bounds (190, 66, 520, 340), so its centre is (450, 236) in the content:

- Leaf: radius `50 * (1 + 0.05 sin(3a + 1.1) + 0.035 sin(5a + 0.4) + 0.02 sin(8a))`, filled with a radial gradient from `rgb(110 + 40·mean, 30, 48)` through `#4d1222` at 0.6 to transparent at the edge, no outline, nine faint veins.
- Tentacle `j`, angle `2πj/54 − π/2` plus up to ±0.045 rad of jitter. Per tentacle, from a fixed-seed generator so the plant is the same every time: base 62–96, reach 70–115, rest curl ±(0.45–0.95) rad, sway ±0.125 rad, base width 2.1–3.0, drop size factor 0.7–1.3.
- With `amount` from the existing `bandAmount(level)`: length `(base + reach·amount)·(1 − 0.22·|sin a|)`, curl `restCurl·(1 − 0.8·amount)`, drawn as ten segments bending with `curl·u²`, width from `baseWidth + 0.8·amount` to 0.7, colour from `#6e1a2a` to `#d64a62`.
- Drop: radius `(1.5 + 4.3·amount)·factor`, a radial gradient with a highlight, and a glow of two translucent rings rather than a blur. A marked band's drop gets a 1 px ring in `text()` at radius + 3.
- Feedback: the slider fills a 64×64 square at the centre. A ring of radius 32 in `loop()` with a glow that follows the mean amount, and the value text inside.

- [ ] **Step 1: Write the failing tests.** `tentaclesCoverEveryBand`: `tentacleBand(0..53)` is a permutation of 0..53. `plantMarksVoiceBands`: `setVoiceBands({10, -1, 30})` gives `isMarked(10) && isMarked(30) && !isMarked(20)`. The existing plant tests stay as they are.
- [ ] **Step 2: Run them to see them fail.**
- [ ] **Step 3: Rewrite `PlantView::paint` and `resized`** with the geometry above, and place the plant in the editor. Keep the repaint threshold: the plant repaints only when a drawn amount moves by more than `kRepaintChange`.
- [ ] **Step 4: Run `ui_test`.** Expected: all OK, `plantRestsWithNoAudio` and `plantStillWithLongBlocks` included.
- [ ] **Step 5: Commit.** `git commit -m "Draw the plant as a sundew whose tentacles unfurl with the sound"`

### Task 7: A window that scales

**Files:**
- Modify: `src/plugin/PluginEditor.h/.cpp`, `src/plugin/EditorBridge.h`, `src/plugin/PluginProcessor.cpp`
- Test: `tests/ui_test.cpp`

**Interfaces:**
- Produces on `EditorBridge`: `virtual float uiScale() const = 0; virtual void setUiScale(float) = 0;`. The processor holds `std::atomic<float> uiScale { 1.0f }`, saves it as the state property `uiScale`, and on load stores the saved value if it lies in [0.75, 1.5], otherwise 1.0.
- Produces on `DroseraEditor`: `float scale() const`.

- [ ] **Step 1: Write the failing tests.**
  - `scalePersists`: `editor->setSize(1350, 840)`, save the state, load it in a second processor: its editor is 1350×840.
  - `scaleSurvivesBadState` and `scaleSurvivesMissingState`: with `uiScale="9"`, and with the property removed, the editor opens at 900×560 and the parameters are intact.
  - `windowKeepsItsProportions`: through the constrainer, a request for 1000×1000 gives a height of `width * 560 / 900` within a pixel, and a request for 200×100 gives 675×420.
  - `scaleSafeOffThread`: a thread restoring the state 2000 times while the message thread resizes the editor between 675 and 1350 wide and calls `refreshDisplay`, with no crash.
- [ ] **Step 2: Run them to see them fail.**
- [ ] **Step 3: Implement scaling.** Put every child in a 900×560 content component. `setResizable(true, true)`, a fixed aspect of 900/560, size limits 675×420 to 1350×840, and in `resized()` a `juce::AffineTransform::scale(getWidth() / 900.0f)` on the content, then `bridge_.setUiScale(scale)`. Open at `900 * uiScale()` wide. Raise the timer to 60 Hz.
- [ ] **Step 4: Run `ui_test`, `render_test` and `plugin_host_test`.** Expected: all OK.
- [ ] **Step 5: Commit.** `git commit -m "Scale the window from 75 % to 150 % and remember the scale"`

### Task 8: Snapshots, documents and the checks by hand

**Files:**
- Modify: `tests/editor_snapshot.cpp`, `docs/MANUAL.md`, `CHANGELOG.md`, `CLAUDE.md`, `docs/superpowers/specs/2026-11-01-rugiada-interface-design.md` (a note that it is replaced)

- [ ] **Step 1: Give `editor_snapshot` `setup=1` and `scale=N`**, which call `setSetupOpen(true)` and `setSize(900 * N, 560 * N)`, and update its usage line.
- [ ] **Step 2: Render and review the images** against the mockup: `build/editor_snapshot_artefacts/Release/editor_snapshot out/classic.png echopong=0 sound=1`, `out/pong.png echopong=1 sound=1`, `out/setup.png setup=1`, `out/small.png scale=0.75`, `out/large.png scale=1.5`, and `out/silent.png` with no sound. Expected: the layout of the mockup, the silent plant closed and still, nothing clipped at either scale. Fix and re-render until they match.
- [ ] **Step 3: Update the documents.** In `MANUAL.md`, replace "Filters tab" to "STRINGS tab" with sections named after the groups (FILTER, REVERB, ECHO with its PONG controls, SETUP with Strings, Output and MIDI) and a short "The window" section on the layout, the colours' meaning, the arcs, the pulse and the scale; keep every control's table. Add an `[Unreleased]` entry to `CHANGELOG.md`. In `CLAUDE.md`'s code map, replace "tabs" with the new components and name `ResponseArcs.h` and `EchoPulse.h` among the files with no JUCE. Add a line at the top of the Rugiada spec saying this design replaces it.
- [ ] **Step 4: Run the whole suite** from `CLAUDE.md`'s commands: build, `voice_allocator_test`, `render_test`, `ui_test`, `plugin_host_test` with the baseline fixture, and `tools/hardware-comparison/check_reference.sh build/render_cli`. Expected: all pass, checksums unchanged.
- [ ] **Step 5: List for the owner the checks that need them**: the mouse and the preset menu in a DAW, as AU and VST3; GUI CPU below 5 % of one core with the window open at 60 Hz, falling back to 30 Hz above that; and the plant judged by eye with real sound.
- [ ] **Step 6: Commit.** `git commit -m "Document the new window and snapshot it in each state"`
