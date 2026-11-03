# Interface redesign

Replaces the Rugiada interface (`2026-11-01-rugiada-interface-design.md`). It builds on the bouncing echo of
`2026-11-03-echo-pong-revision-design.md`.

## Goal

The tabs hide too much: while the drone plays, most of the instrument is out of sight. The plant is decorative more
than informative, and each new feature has added a tab. Drosera is played live first and shaped by sound design
second, so the new window puts everything that is touched while playing on one page, keeps the plant at its centre, and
leaves room for what comes next.

Success: the core, the filters and the effects can be played without changing page, and what the sound is doing can be
read at a glance.

## Direction

One page, no tabs, symmetric about the plant. No boxes and no signal lines: groups are separated by space, named by
light cartouches, and coloured by where they sit. **Drosera red** marks what is inside the feedback loop, which changes
what the drone sustains. **Amber** marks the echo, which colours what comes out.

The approved mockup is `docs/design/drosera-redesign-mockup.html`. Its sound is simulated from the knob values: this
document decides where the two differ.

## Layout

The window is 900×560 at 100 %, drawn as vectors. It scales from 75 % to 150 % with its proportions locked. The scale is
saved in the plugin state.

- **Header.** "DROSERA" centred, in Cormorant Garamond. On the left a **PRESET** field: previous arrow, the preset's
  name with a menu of all presets, next arrow. On the right a twin **SETUP** field, "Strings · Output · MIDI", which
  opens the setup panel. Each field has its caption above it.
- **Middle band**, on one line: **Frequency** large on the left, the **plant** in the centre with **Feedback** at its
  heart, **Body** large on the right. The three share a vertical centre, and nothing in this band ever moves position.
- **Bottom row**, one row in three blocks of four places: FILTER and REVERB on the left, PONG in the centre, ECHO on the
  right. Each group is named by a cartouche:

| Group | Colour | Controls |
|---|---|---|
| FILTER | red | Highpass, Lowpass, with the band arc above them |
| REVERB | red | Mix, Decay |
| PONG | amber | Drive, Tone (with its band-pass arc), Width, Level |
| ECHO | amber | Send, Time (with the echo pulse), Feedback, and the CLASSIC / PONG switch with WARP below it |

- **PONG's four controls are always visible.** With CLASSIC they are dimmed, because they have no effect there, and stay
  usable so they can be set before switching. Nothing appears or disappears when the mode changes.
- **Setup panel.** Rises over the bottom of the window and dims the rest. It holds STRINGS (POLY, LOCK, Coupling,
  Interval 2, Interval 3; the knobs are dimmed while POLY is off), OUTPUT (Volume, Input) and MIDI (PITCH, GATE). It
  closes from the SETUP field, by clicking above it, or with Escape.

All 25 controls of today's interface are on this page or in the setup panel. Parameter IDs and names do not change.

Colours:

| Use | Colour |
|---|---|
| Background, centre to edge | `#1e1416` to `#0f0a0b` |
| Loop controls and tentacles | `#d0465c`, tentacle bases `#a33244` |
| Echo controls, drops and readouts in use | `#ffc27a` |
| Knob track | `#2a1f21` |
| Text | `#eadacd` |
| Secondary text | `#9d8a83` |
| Hairlines and cartouches | `#33272a` to `#3d2f31` |

Fonts: Cormorant Garamond for the name, Inter for everything else. Both are under the SIL Open Font License and are
embedded in the plugin as binary data.

## The plant

A sundew leaf seen from above: a round, slightly irregular blade with faint veins and no outline, fading into the
background. 54 tentacles grow from its rim, one per band, log-spaced from 30 Hz to 16 kHz as today. Neighbouring bands
are spread around the leaf (band `(j * 23) mod 54` on tentacle `j`), so one partial lights tentacles on every side.

Each tentacle tapers from its base to its tip and ends in a drop of dew. Its band's level, from −72 dBFS to 0 dBFS as
today, drives it as a real sundew moves:

- **At rest** the tentacle is short and curled, and its drop is small and dim.
- **With sound** it lengthens and unfurls, and its drop swells and glows.
- The plant is wider than tall: tentacles reach about 22 % less upward and downward than sideways, so their tips stay
  clear of the header and the bottom row.

Levels rise with a 40 ms attack and fall with a 300 ms release. The leaf's colour and the glow of Feedback's ring follow
the mean of the bands. Feedback is drawn on the leaf as a ring with its value inside.

**There is no invented motion.** A silent plugin shows a still plant, closed and at rest.

**Three strings.** With POLY on, the drop of each tentacle whose band holds a sounding voice's note gets a fine ring, and
Frequency's readout shows the three notes, such as "C3 · G3 · C4". The spectrum is the mixed output, so the plant does
not pretend to separate the strings beyond this.

## Arcs

Two small arcs of ticks draw a filter's real response, and move only when their knobs turn:

- **Band arc**, above Highpass and Lowpass, from 10 Hz to 18 kHz. Each tick's length follows the gain of both filters
  at its frequency, with their slopes, so the arc draws the band that survives in the loop. Ticks in a cut band shrink
  to a short stub and never vanish, so the whole scale stays readable.
- **Tone arc**, above Tone, the same way around the echo's band-pass centre.

The arcs do not show the spectrum: the plant does.

## Echo pulse

Beside Time, a small amber dot blinks at the real time between repeats, from `echoSeconds()`, so WARP's shortening shows
too. With PONG it becomes two dots blinking in turn, left and right. With Echo Send at zero the pulse rests, because
no repeats sound.

## Honest readouts

Knobs show what sounds, as today:

| Situation | Display |
|---|---|
| MIDI pitch in control | Frequency shows the note and Hz, tagged MIDI, and the knob is dimmed |
| MIDI pitch in control | Body shows the real snapped time in ms, tagged SNAP, and the knob is dimmed |
| WARP on | Time shows the real, shortened time |
| An interval beyond the note range | Interval shows the interval the voice really plays, so +24 that sounds as +12 reads "+12 st" |
| POLY on | Frequency shows the three sounding notes |

## Components

`src/plugin/ui/`, each with one job:

| File | Job |
|---|---|
| `Theme.h` | Colours and the embedded fonts |
| `DroseraLookAndFeel.h/.cpp` | Large knobs with a scale, small knobs, pills, fields and menus, in the group's colour |
| `PlantView.h/.cpp` | Rewritten: leaf, tentacles, drops, Feedback's ring and the three strings' rings. Still runs the FFT and holds Feedback |
| `ResponseArcs.h` | New, no JUCE: tick lengths of the band arc and the Tone arc, from the parameters |
| `ControlRow.h/.cpp` | New, replaces `TabPanel`: the 4 \| 4 \| 4 row, cartouches, arcs, mode switch, WARP and the echo pulse |
| `HeaderBar.h/.cpp` | New: the PRESET and SETUP fields and the name |
| `SetupPanel.h/.cpp` | New: Strings, Output and MIDI |
| `Readouts.h` | Gains the real interval and the three notes |
| `SpectrumBands.h` | Unchanged |

`src/plugin/PluginEditor.*` assembles these on a 900×560 content component, scales it with a transform, and runs the
timer at 60 Hz. `TabPanel` is deleted.

`EditorBridge` loses `openTab` and `setOpenTab` and gains `uiScale` and `setUiScale`. The processor saves `uiScale` in its
state in place of `uiTab`, as an atomic, because hosts may restore state off the message thread. A saved `uiTab` is
ignored. The audio thread and the engine do not change.

## Failure and edge cases

- **A missing or out-of-range scale**, from an older project or a damaged one: the window opens at 100 %.
- **A scale the screen cannot fit:** the host's window limits apply, and the content keeps its proportions.
- **Fonts that fail to load:** JUCE's default sans-serif is used, and the layout stays the same.
- **POLY with fewer than three notes:** only the sounding voices get a ring and a name in the readout.
- **Sample rate changes, bands above Nyquist, non-finite samples, the editor closed while audio runs, several
  instances:** as in the Rugiada design, since the tap and the bands do not change.

## Testing

Automated, in `tests/ui_test.cpp`, run by CI:

- The band arc follows the filters: with Highpass at 1 kHz the ticks below 300 Hz are short, and no tick is ever below
  the stub. The Tone arc peaks at Tone's centre.
- Silence puts every band at rest, and a sine lights the expected tentacle and its neighbours only. These tests stay.
- Each readout in the table above produces the expected text.
- The scale survives closing and reopening the editor. A missing, damaged or out-of-range scale opens at 100 %. A host
  may restore state off the message thread while the window reads the scale.
- Every one of the 25 controls has its knob or switch, and PONG's four are visible in both modes.
- The tab tests are removed, since there are no tabs.

Unchanged, and they must still pass: `render_test`, including its allocation count, `voice_allocator_test`,
`plugin_host_test` with `tests/fixtures/state-baseline.bin` (it checks only parameter values), and the reference checksums.

`editor_snapshot` drops `tab=N` and gains `setup=1` and `scale=N`. Its images in CLASSIC, in PONG, with the setup panel
open, and at 75 % and 150 % are reviewed against the mockup.

By hand, because they cannot be automated here:

- The mouse and the preset menu in a DAW, as AU and VST3.
- GUI CPU use below 5 % of one core with the window open at 60 Hz, on the owner's Mac. Above that, the timer goes back
  to 30 Hz.
- Whether the plant looks right with real sound. Lengths, curls and timings may need tuning by eye.

## Out of scope

- Any change to the sound or the engine.
- Themes, a light mode, and translated labels.
- User presets.
- Touch input.
