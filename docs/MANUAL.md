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

Echo Send, Echo Time and Echo Feedback also drive Echo Pong, in the Pong tab.

**WARP** halves the Echo Time knob's position before its square-law curve. Above about a quarter of the knob's
travel the echo gets 2.8 to 3.9 times shorter, not twice as short, so it acts as a doppler warp and not as a tempo
halving. Lower down the difference shrinks, because Echo Time cannot go below 50 ms.

## Pong tab

| Control | What it does |
|---|---|
| Echo Drive | 0 to +12 dB into the saturation of Echo Pong |
| Echo Tone | 800 Hz to 18 kHz, how fast Echo Pong's repeats get darker |
| Echo Width | 0 % puts every repeat in the middle, 100 % bounces them fully left and right. 100 % by default |
| Echo Level | -30 to +6 dB on Echo Pong's repeats. 0 dB by default |
| Echo Pong (PONG) | Replaces the firmware's echo with repeats that bounce between left and right. Off by default |

The four knobs are dimmed while PONG is off, and stay usable.

**PONG** sums the input to mono and starts on the left, and Echo Time is the time between bounces. Unlike the firmware's
echo, which passes everything through a band-pass at 800 Hz, it keeps the low end of the drone. Echo Drive also raises the
gain of each bounce: with Drive at zero, Echo Feedback runs away above 100 %, and with Drive at maximum, above 50 %.
Echo Width and Echo Level act on what comes out, not on the loop, so they change neither how long the repeats last nor
where Echo Feedback runs away. At 0 %, Width plays each repeat in the middle at half the level it had on its side.

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
| LOCK | on/off | From the knobs: on, the strings sound the notes they show; off, they roam. On by default |
| Coupling | 0 to 100 % | How much each loop hears the other two |
| Interval 2 | −24 to +24 semitones | Pitch of the second string relative to Frequency. Default +7 |
| Interval 3 | −24 to +24 semitones | Pitch of the third string relative to Frequency. Default +12 |

**Coupling.** At 0 the three loops do not affect each other. Raising it feeds each loop a share of the other two,
so they ring on harmonically related intervals, like sympathetic strings. The knob is on a square law. Under MIDI its
effect sits mostly at the top of the knob and fades as Body goes past the middle, to almost nothing from about 0.7,
which keeps long Bodies in tune. LOCK behaves the same way. From the knobs with LOCK off it is usually strong from
about 40 %, at any Body. A voice whose gate is closed hears no Coupling, at any setting.

**LOCK and the intervals.** Without MIDI the strings sit at Frequency, Frequency + Interval 2 and Frequency +
Interval 3, limited to notes 16 to 84.

- **LOCK on** tunes them as MIDI does. The chord is the one the knobs show, full and dark, and it follows Frequency
  down to notes too low for small speakers. Body picks a multiple of the note's period and reads **SNAP**. Reverb Mix
  detunes these notes, as it does under MIDI.
- **LOCK off** keeps the free drone. Each loop settles where string and Body resonances coincide, and each voice's Body
  follows its interval, so the voices rarely land on one pitch. The sound is bright and moves, and the intervals are
  only a starting point.

LOCK does nothing with POLY off or while a MIDI note is in control. Under MIDI the intervals dim and show each voice's real note,
tagged **MIDI**. Frequency and Body show the first voice.

**Voice assignment** (MIDI Pitch and POLY on). Each new note takes a voice that has never played, then the voice
released longest ago, then the voice held longest. A taken voice glides to the new note without being cleared. When a
voice is released while a held note has no voice, it takes that note, most recent first. Pitch bend moves all voices.
Turning POLY on with keys down assigns the last three held notes. A voice that has never had a note stays closed,
so one note sounds one string.

**MIDI Gate per voice.** With MIDI Pitch on, each voice is open only while its own note is held. With MIDI Gate off, a
released note keeps sounding until its voice is taken. With MIDI Pitch off, the gate opens all three voices together.

## Presets

16 factory presets, also exposed to the host as programs. "Resonator" needs an audio input. "Keys" expects notes.
"Init" is silent by design. Three use three strings: "Open Fifths" (knobs only, LOCK on, Coupling 0), "Sympathetic"
(knobs only, LOCK off, Coupling at 40 %) and "Chord Keys" (MIDI Pitch and MIDI Gate on, Coupling at 80 %). Choosing any of the
others turns POLY off. "Ember Pong" uses PONG and sustains itself, and choosing any other preset turns PONG off.

## Known limits

- With MIDI Pitch or LOCK on, notes 16 to 18 and note 80 may stay silent in some settings, because the loop does not have
  the gain to oscillate there. Note 16 can also sound on the wrong partial, about 11 semitones sharp.
- With POLY on and Coupling above 0, the notes can move each other's pitch. From the knobs with LOCK off the voices can
  leave their intervals. Under MIDI, with no reverb, the chords tested with roots 24 to 60 stayed within 5 cents of their notes at
  100 %, which is not promised for every chord or setting.
- With MIDI Pitch or LOCK on, a note far below Highpass or far above Lowpass cannot sustain, so that string goes quiet or
  silent. At Frequency 0.43 this starts with Highpass at about 0.6. Lower Highpass, raise Lowpass, or turn LOCK off:
  the free drone finds another resonance instead.
- With MIDI Pitch or LOCK on and Body at about 0.7 or more, a few chord notes sit up to about 60 cents off, at any Coupling,
  on a neighbouring Body resonance.
- Reverb Mix detunes MIDI notes, because the reverb sits in the loop and its phase is not compensated. With POLY on,
  chords can go sour: with "Chord Keys" otherwise as it is, at Reverb Mix 35 % a C major chord read +20, -28 and -8 cents.
- The values of "Open Fifths", "Sympathetic", "Chord Keys" and "Ember Pong", and the defaults of Echo Drive, Echo Tone and Echo Level,
  were chosen by measurement and have not been judged by ear.
- Echo Pong's saturation is not oversampled, so loud high partials alias at high Drive. At Drive 100 %, with no feedback, a
  3.1 kHz tone at half scale measured -54 dB of inharmonic power, and a 9.1 kHz tone at full scale -10.5 dB.
- The engine always runs at 48 kHz. At other host rates, windowed-sinc resampling adds latency, which is declared
  to the host only when the input bus is connected.
- The drone is chaotic. A small change to a knob, or to when it moves, can lead the loop somewhere else, so automation
  may not repeat a performance exactly.
