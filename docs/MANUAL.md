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

**WARP** halves the Echo Time knob's position before its square-law curve. The echo gets 2.8 to 3.9 times
shorter, not twice as short, so it acts as a doppler warp and not as a tempo halving.

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

## Presets

12 factory presets, also exposed to the host as programs. "Resonator" needs an audio input. "Keys" expects notes.
"Init" is silent by design.

## Known limits

- With MIDI Pitch on, notes 16 to 18 and note 80 may stay silent in some settings, because the loop does not have
  the gain to oscillate there.
- The engine always runs at 48 kHz. At other host rates, windowed-sinc resampling adds latency, which is declared
  to the host only when the input bus is connected.
- The drone is chaotic. Two runs of the same preset never match sample by sample.
