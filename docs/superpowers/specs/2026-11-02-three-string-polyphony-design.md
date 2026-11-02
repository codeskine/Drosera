# Three-string polyphony design

## Goal

Let Drosera sound up to three notes at once, as three feedback loops that can stay independent or push each other.
It is a plugin-only feature and off by default. With it off, Drosera behaves and renders exactly as it does today.

Success means:

- A three-note chord holds, with the dominant partial of each note on that note.
- At Coupling 0 the notes do not affect each other. Raising Coupling audibly changes the sound.
- The three strings work with MIDI and from the knobs alone.
- Nothing allocates, locks or touches files on the audio thread, and renders with the feature off match
  `tools/hardware-comparison/reference/SHA256SUMS` with no new checksums.

## Non-goals

Panning between voices, more than three voices, MPE, per-voice filter or Body knobs, and updating DaisySP.
DaisySP stays pinned at `7f67768`, and `third_party/` is not changed.

## Why three separate loops

Today's two "strings" are the left and right sides of one note: same pitch, same Body delay. MIDI holds that note with
a snapped Body, a band-pass on the note and phase compensation, all of which are per-note. The overdrive in the loop
also favours one mode, so three notes through one saturator compete until one takes over. Each note therefore gets its
own loop.

The reverb is in the loop too: `samp -= (samp - verb) * mix`. A shared reverb would be cheaper, but at Reverb Mix 100%
every loop would receive the same wet signal and the chord would collapse into one loop. Coupling would also not be
zero at 0 with any reverb open. Each voice therefore has its own reverb.

Three full `Engine` instances were rejected: they bring three echoes, which sound different from one echo on the
chord, and the coupling would have to open `Engine` anyway.

CPU allows this. The current engine renders 60 s of audio in 0.49 s, about 0.8% of a core, including writing the file.

## Engine

```text
Voice 0 ─┐ noise + in + coupling × (voices 1, 2) → string → overdrive → LPF/HPF → reverb ─┬→ Body delay → (back to input)
Voice 1 ─┤ same chain                                                                     │
Voice 2 ─┤ same chain                                                                     │
         └───────────────── sum × 1/√3 ────────────────→ one echo → volume → limiter ─────┘
```

**`FeedbackVoice`** is a new class in `src/dsp/`. It holds everything in the loop: the left and right strings, the Body
delay and its smoothing, the overdrives, LPF, HPF, the note's band-pass, its gain and a `ReverbSc`. Each voice stays
stereo, with today's 4-sample difference between left and right.

**`Engine`** owns three voices, the stereo echo and the output level. All voices are allocated in the first `Init`, so
turning polyphony on does not allocate. Filter and reverb settings go to all three voices. Pitch, Body delay, tracking
and feedback gain are per voice.

**With polyphony off**, `Engine::Process` runs voice 0 only, with the same operations in the same order as today, using
today's noise generator.

**Coupling.** The tap is each voice's `samp` after filters and reverb, before the feedback gain and the band-pass, so
the coupled amount does not depend on Feedback or on the make-up gain of up to 8×. Each voice adds
`coupling × (sum of the other two voices' taps)` to its loop input, channel by channel, with one sample of delay so
there is no algebraic loop. The signal then goes through the receiving voice's string, so it rings mainly on
harmonically related intervals, like a sympathetic string. Under MIDI it first passes through a second band-pass on the
receiving voice's note, with a make-up gain: without the band-pass, low chords at high Coupling jumped about four octaves to
a partial their strings share, by a path that misses each loop's own band-pass. A voice whose gate is closed receives no coupling, at any
Coupling, so MIDI Gate still closes it to at least 40 dB below the open voices.

**Noise.** DaisySP's `WhiteNoise` always seeds with 1 and has no way to change it, so three strings in unison would be
identical and coupling would do nothing. Voice 0 uses the existing generator. Voices 1 and 2 have their own, advanced
by a different number of steps in `Init`.

**Switching.** A linear ramp of 960 samples (20 ms) fades voices 1 and 2 in or out and moves the sum's scale between 1
and 1/√3. When the ramp finishes on the way off, voices 1 and 2 stop being processed and are cleared, so they restart
from silence. When polyphony is already on at the first render or after `Recover`, the ramp jumps to its end.

**Safety.** The limiter, the NaN check and `Recover` stay in `DroseraCore` and cover all voices. `Recover` clears every
voice.

## Control

**Parameters** are added at the end of the list, because tests read `getParameters()[0]` as Frequency. Existing IDs do
not change.

| ID | Name | Values | Default |
|---|---|---|---|
| `poly` | Polyphony | on/off | off |
| `coupling` | Coupling | 0 to 100% | 0% |
| `interval2` | Interval 2 | -24 to +24 semitones, whole steps | +7 |
| `interval3` | Interval 3 | -24 to +24 semitones, whole steps | +12 |
| `polylock` | String Lock | on/off | on |

Coupling maps through `daisysp::Mapping::EXP` (norm squared) to a gain from 0 to `kMaxCoupling`, which is 0.1. From the
knobs that is the whole gain, and the output's magnitude spectrum differs by 0.55 at default knobs at 100% against 0, with String Lock off, and by about 1.0 on average across nine settings. With String Lock on it is 0.19. Under MIDI the
band-pass and its make-up apply as well. Of the make-ups tried (1, 2, 2.5, 3, 4), 1 fails the measure on one test chord
(0.035) and 2 clears it thinly (0.115). 2.5 gives 0.17 and 0.45 on the two test chords. Across 296
chords with roots 24 to 60, default dry knobs, the dominant partial stays within 5 cents of the note at 100%. At a
make-up of 4 the worst is 12 cents; at 6, 16 of 168 voices leave their notes.

A long Body packs its resonances close around the note, and a fixed make-up pushed voices onto the next one: at 100%,
Body 0.9 and 1.0, 29 and 34 of 168 voices were off against 3 and 2 at Coupling 0. Lowering the fixed make-up did not cure
it (at 1, chords still moved). So the make-up is 2.5 × min(1, 0.025 s / Body)³: full up to the middle of the knob, where the
spectrum still differs by 0.29 at Body 0.5, then falling fast, to under 0.02 from Body 0.7. Across 56 chords, Body 0.5 to
1.0 and Coupling 60 to 100%, 17 voices of 3024 are off, against 18 for the same settings at Coupling 0. The cost is
that under MIDI Coupling does almost nothing at long Bodies. From the knobs nothing changes.
Saved projects find the new parameters at their
defaults, so they sound as before.

**`DroseraCore`.** Voices 1 and 2 use a second `ParameterRegistry`, for their pitch and feedback gain, and it also holds
Coupling. New entries must not go in the existing registry: it iterates an `unordered_map` in `Process()`, so more
entries could reorder the handlers and change renders with polyphony off. `DroseraCore::Param` also stays as it is,
because `kSpecs` follows its order.

That registry also holds what each voice receives through Coupling, smoothed: 1 when its gate is open or disabled, 0 when
it is enabled and closed.

`applyPitchAndBody` becomes per voice, so each note has its own snapped Body, band-pass and phase compensation, with
Frequency's 0.2 s glide. The Body knob stays single. The published note and Body delay become one value per voice.

**Without MIDI**, the pitches are Frequency, Frequency + Interval 2 and Frequency + Interval 3, clamped to notes 16 to
84. A recording from a real host showed why a choice is needed: with a shared Body, all voices can settle on one
resonance (175 Hz for 90 s of it). At default knobs, across 42 settings of Frequency and Body, two or more voices shared a
pitch in 13 to 16, depending on the window measured, and the intervals were right in 1. String Lock chooses:

- **On:** tuned as under MIDI, with the snapped Body, band-passes and phase compensation. No voices share a pitch, the
  intervals are right in 35 of 42, and with no reverb within 1 cent. Dark and full: 92% of the power is below 300 Hz.
- **Off:** each voice's Body is scaled by its interval, so a voice's resonances move with its string. Voices share a pitch
  in 6 of 42. Bright and moving, like the free drone.

Both are kept because they are different instruments. String Lock has no effect with polyphony off, so renders there do
not change, or while a MIDI note is in control, which is already tuned. Its cost is the one MIDI has: a note far outside
the filters cannot sustain, so with String Lock on, a string goes silent where the free drone finds another resonance.

**With MIDI Pitch on and polyphony on:**

- A new note takes a never-used voice, then the voice released longest ago, then the voice held longest. A note already
  sounding keeps its voice.
- A taken voice glides to the new note without being cleared, so the loop is not interrupted.
- When a voice is released while a held note has no voice, it takes that note, most recent first. This matches the mono
  fallback to the previous held note.
- A voice that has never had a note stays closed, so one note sounds one string.
- Pitch bend applies to all voices.
- Moving Frequency with no key held returns all voices to the knobs, as it does today.
- All Notes Off releases the voices and keeps their notes.
- Turning polyphony on with keys down assigns the last three held notes, in the order they were pressed.

**MIDI Gate:**

- With MIDI Pitch on, each voice is open only while its own note is held, closing to -60 dB as the gate does today.
- With MIDI Gate off, a released note keeps sounding until its voice is taken, like the mono drone.
- With MIDI Pitch off, the gate opens all three voices together.

**`VoiceAllocator`** is a new header in `src/dsp/`, with no JUCE, so tests can use it alone. `handleMidi` only calls it.
With polyphony off, the existing mono logic is what runs.

## Presets

`Preset` gains `poly`, `coupling`, `interval2`, `interval3` and `stringLock`. The 12 existing presets set polyphony off, so choosing
one turns it off. Three presets are added at the end, so saved program indexes stay valid:

| Name | Use |
|---|---|
| Open Fifths | Knobs only, String Lock on, Coupling 0 |
| Sympathetic | Knobs only, String Lock off, Coupling around 40% |
| Chord Keys | MIDI Pitch and MIDI Gate on, Coupling 80% |

Their exact knob values are set during implementation. `render_test` already renders every factory preset and checks
that it is finite, within full scale and audible, so its `applyPreset` learns the new fields and the three presets are
covered with polyphony on.

## Interface

A sixth tab, `STRINGS`, goes after `MIDI`, so saved tab indexes stay valid. It holds the `POLY` and `LOCK` switches and the Coupling,
Interval 2 and Interval 3 knobs.

The window stays 760×500, so the tab strip narrows from a step of 142 to 118 px, checked with `editor_snapshot`.

The intervals read like `+7 st`. While MIDI is in control they dim and show that voice's real note with the **MIDI**
tag, as Frequency does. Frequency and Body show voice 0. `EditorBridge` gains a reading per voice. The plant does not
change, since it already shows the real spectrum.

## Testing

Existing checks keep passing, including `check_reference.sh` with no new checksums. `state-baseline.bin` is not
regenerated.

`render_test` adds:

- **Tuning (rule 3):** at Coupling 0, three-note chords in several registers, judged by the dominant partial of each
  note, never by a fit to a harmonic grid. It avoids the notes `docs/MANUAL.md` lists as a known limit (16 to 18, 80).
- **Independence:** at Coupling 0, a `FeedbackVoice`'s output is identical, sample by sample, whatever the others do.
- **Coupling:** the output's magnitude spectrum at Coupling 100% differs from Coupling 0 by more than 10%, under MIDI and
  from the knobs. This test sets `kMaxCoupling` and the make-up. 56 chords across roots 24 to 60 hold their notes at
  Coupling 100%, by the dominant partial, within 25 cents. From Body 0.7 up, a voice that holds its note at Coupling 0
  still holds it at 80 and 100%. At 100% a closed voice stays at least 40 dB below the open voices.
- **Stability:** Coupling and Feedback at maximum, across extreme notes and filters, for 60 s, with no non-finite value
  and no `Recover`, under MIDI and from the knobs.
- **Clicks:** the largest sample-to-sample jump stays bounded through the ramp, both ways.
- **Allocations:** none on the audio thread with polyphony on, during the ramp, and while voices are being taken.
- **Allocator:** note sequences for a free voice, taking a voice, picking up a waiting note, and All Notes Off.

`plugin_host_test` adds: the baseline state loads with polyphony off, the new parameters survive a state round trip, and
the old presets turn polyphony off.

`ui_test` adds: the sixth tab works, and its readouts are right, with and without MIDI in control.

CPU with polyphony on is measured and reported. The estimate is about 2.5% of a core.

## Documentation

`docs/MANUAL.md` (the new tab and MIDI behaviour), `README.md` (15 presets), `CHANGELOG.md`, and the code map in
`CLAUDE.md`.
