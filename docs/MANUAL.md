# Drosera manual

Drosera is a feedback drone synth. A noise floor excites two plucked-string resonators inside a saturating
feedback loop, so it makes sound on its own, with no notes and no input. With the plugin-only features off, the
DSP is the Audrey II firmware's own code at a fixed 48 kHz.

Readouts show what the engine really uses, not the knob's position. These curves are far from linear.

## Main controls

| Control | Range | What it does |
|---|---|---|
| Frequency | 20.6 Hz to 523 Hz (notes 16 to 72) | Pitch of the string resonators |
| Feedback | −60 dB to +12 dB | Gain around the loop. Self-oscillation starts around unity |
| Body | 1 ms to 100 ms | Length of the outer feedback delay |

The drone settles where the resonances of the string and of the Body delay coincide, which is rarely the exact
pitch Frequency shows. Moving Body can jump the drone to another partial.

## Filters

| Control | Range | What it does |
|---|---|---|
| Lowpass | 100 Hz to 18 kHz | Lowpass inside the loop |
| Highpass | 10 Hz to 4 kHz | Highpass inside the loop |

Both are in the feedback path, so they shape which partials sustain, not only the tone.

## Reverb

| Control | What it does |
|---|---|
| Reverb Mix | Dry to wet, inside the loop |
| Reverb Decay | Reverb feedback, 20 % to 100 %, on a curved law |

## Echo

| Control | What it does |
|---|---|
| Echo Send | Level into the echo |
| Echo Time | 50 ms to 5 s |
| Echo Feedback | 100 % is unity, and up to 150 % it runs away, bounded by a soft clip |
| Echo Warp (WARP) | The firmware's delay switch |
| Echo Tape (TAPE) | Replaces the echo with a tape echo |
| Tape Wow | Tape only: pitch wander |
| Tape Age | Tape only: wear of the tape |

**WARP** halves the Echo Time knob's position before its square-law curve. The echo gets 2.8 to 3.9 times
shorter, not twice as short, so it acts as a doppler warp and not as a tempo halving.

**TAPE** uses the octamad tape echo. Echo Feedback follows the tape's steeper law, so its percent readout is only
a guide: repeats die off quickly below about 60 % of the knob's travel, and self-oscillate above about 70 %.

## Output

| Control | What it does |
|---|---|
| Volume | Output level |
| Input Level | Gain of the audio input, 0 dB at centre |

The output has a limiter and a final hard clip at full scale, because the loop can get loud.

The audio input is optional, and excites the strings. It stays off until the host connects the plugin's input
bus. Some presets need it to make sound.

## MIDI

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

14 factory presets, also exposed to the host as programs. "Resonator" needs an audio input. "Keys" expects notes.
"Init" is silent by design.

## Known limits

- With MIDI Pitch on, notes 16 to 18 and note 80 may stay silent in some settings, because the loop does not have
  the gain to oscillate there.
- The engine always runs at 48 kHz. At other host rates, windowed-sinc resampling adds latency, which is declared
  to the host only when the input bus is connected.
- The drone is chaotic. Two runs of the same preset never match sample by sample.
