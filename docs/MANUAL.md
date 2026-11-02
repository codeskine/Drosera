# Drosera manual

Drosera is a feedback drone synth. A noise floor excites two plucked-string resonators inside a saturating
feedback loop, so it makes sound on its own, with no notes and no input. With the plugin-only features off, the
DSP is the Audrey II firmware's own code at a fixed 48 kHz.

Readouts show what the engine really uses, not the knob's position. These curves are far from linear.

## The plant

The sundew in the middle of the window shows the output's real spectrum. Each of its 54 tentacles is a frequency band,
from 30 Hz at the top, clockwise to 16 kHz, and its length and drop of dew follow that band's level from −72 dBFS to 0
dBFS. Nothing moves unless the sound does: a silent plugin shows a still plant.

Low bands are coarser than high ones. Below about 150 Hz several tentacles share what one FFT bin measured.

Readouts can carry a tag. **MIDI** on Frequency means a note sets the pitch, and **SNAP** on Body means Body shows the time
snapped to that note. Those knobs look dimmed, because they are not what sounds, but they keep working.

## Main controls

Feedback sits at the plant's heart, with Frequency and Body on either side. Everything else is in the tabs below, and the
open tab is saved with your project.

| Control | Range | What it does |
|---|---|---|
| Frequency | 20.6 Hz to 523 Hz (notes 16 to 72) | Pitch of the string resonators |
| Feedback | −60 dB to +12 dB | Gain around the loop. Self-oscillation starts around unity |
| Body | 1 ms to 100 ms | Length of the outer feedback delay |

The drone settles where the resonances of the string and of the Body delay coincide, which is rarely the exact
pitch Frequency shows. Moving Body can jump the drone to another partial.

## Filters tab

| Control | Range | What it does |
|---|---|---|
| Lowpass | 100 Hz to 18 kHz | Lowpass inside the loop |
| Highpass | 10 Hz to 4 kHz | Highpass inside the loop |

Both are in the feedback path, so they shape which partials sustain, not only the tone.

## Reverb tab

| Control | What it does |
|---|---|
| Reverb Mix | Dry to wet, inside the loop |
| Reverb Decay | Reverb feedback, 20 % to 100 %, on a curved law |

## Echo tab

| Control | What it does |
|---|---|
| Echo Send | Level into the echo |
| Echo Time | 50 ms to 5 s |
| Echo Feedback | 100 % is unity, and up to 150 % it runs away, bounded by a soft clip |
| Echo Warp (WARP) | The firmware's delay switch. Echo Time's readout shows the shortened time |

**WARP** halves the Echo Time knob's position before its square-law curve. Above about a quarter of the knob's
travel the echo gets 2.8 to 3.9 times shorter, not twice as short, so it acts as a doppler warp and not as a tempo
halving. Lower down the difference shrinks, because Echo Time cannot go below 50 ms.

## Output tab

| Control | What it does |
|---|---|
| Volume | Output level |
| Input Level | Gain of the audio input, 0 dB at centre |

The output has a limiter and a final hard clip at full scale, because the loop can get loud.

The audio input is optional, and excites the strings. It stays off until the host connects the plugin's input
bus. Some presets need it to make sound.

## MIDI tab

Both switches are off by default, and Drosera then ignores notes.

**MIDI Pitch (PITCH)** puts the drone on the played note, with pitch bend of ±2 semitones. Notes 16 to 84 are
accepted. While a note is in control:

- Body is snapped to a whole number of the note's periods, so the Body knob selects a multiple.
- A band-pass on the note keeps the loop on the fundamental.

Moving Frequency returns control to the knob once no key is held. A knob moved while a key is down does not take
pitch back after release: move it again.

**MIDI Gate (GATE)** closes the feedback unless a key is down, so the drone swells on note-on and dies away on
note-off.

## STRINGS tab

Three-string polyphony lets Drosera sound up to three notes at once, as three feedback loops. It is off by default,
and with it off Drosera sounds exactly as before.

| Control | Range | What it does |
|---|---|---|
| POLY | on/off | Runs three loops instead of one |
| Coupling | 0 to 100 % | How much each loop hears the other two |
| Interval 2 | −24 to +24 semitones | Pitch of the second string relative to Frequency. Default +7 |
| Interval 3 | −24 to +24 semitones | Pitch of the third string relative to Frequency. Default +12 |

**Coupling.** At 0 the three loops do not affect each other. Raising it feeds each loop a share of the other two,
so they ring on harmonically related intervals, like sympathetic strings. The knob is on a square law. Under MIDI its
effect sits mostly at the top of the knob, while from the knobs alone it is strong from about 40 %. A voice whose
gate is closed hears no Coupling, at any setting.

**Intervals.** Without MIDI the strings sit at Frequency, Frequency + Interval 2 and Frequency + Interval 3, limited
to notes 16 to 84. Body is not snapped here, so the loop settles where string and Body resonances coincide and the
intervals are exact only under MIDI. Under MIDI the intervals dim and show each voice's real note, tagged **MIDI**.
Frequency and Body show the first voice.

**Voice assignment** (MIDI Pitch and POLY on). Each new note takes a voice that has never played, then the voice
released longest ago, then the voice held longest. A taken voice glides to the new note without being cleared. When a
voice is released while a held note has no voice, it takes that note, most recent first. Pitch bend moves all voices.
Turning POLY on with keys down assigns the last three held notes. A voice that has never had a note stays closed,
so one note sounds one string.

**MIDI Gate per voice.** With MIDI Pitch on, each voice is open only while its own note is held. With MIDI Gate off, a
released note keeps sounding until its voice is taken. With MIDI Pitch off, the gate opens all three voices together.

## Presets

15 factory presets, also exposed to the host as programs. "Resonator" needs an audio input. "Keys" expects notes.
"Init" is silent by design. The last three use three strings: "Open Fifths" (knobs only, Coupling 0), "Sympathetic"
(knobs only, Coupling at 40 %) and "Chord Keys" (MIDI Pitch and MIDI Gate on, Coupling at 80 %). Choosing any of the
other 12 turns POLY off.

## Known limits

- With MIDI Pitch on, notes 16 to 18 and note 80 may stay silent in some settings, because the loop does not have
  the gain to oscillate there. Note 16 can also sound on the wrong partial, about 11 semitones sharp.
- With POLY on and Coupling above 0, the notes can move each other's pitch. From the knobs the voices can leave their
  intervals. Under MIDI, with no reverb, the chords tested with roots 24 to 60 stayed within 5 cents of their notes at
  100 %, which is not promised for every chord or setting.
- Reverb Mix detunes MIDI notes, because the reverb sits in the loop and its phase is not compensated. With POLY on,
  chords can go sour: with "Chord Keys" otherwise as it is, at Reverb Mix 35 % a C major chord read +20, -28 and -8 cents.
- The values of "Open Fifths", "Sympathetic" and "Chord Keys" were chosen by measurement and have not been judged by
  ear.
- The engine always runs at 48 kHz. At other host rates, windowed-sinc resampling adds latency, which is declared
  to the host only when the input bus is connected.
- The drone is chaotic. A small change to a knob, or to when it moves, can lead the loop somewhere else, so automation
  may not repeat a performance exactly.
