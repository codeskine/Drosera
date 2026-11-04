# Drosera – Sound design measurements

Date: 2026-11-03, on commit `ec6e150` (with the review's fixes). The engine runs at 48 kHz through `DroseraCore`
(`review/experiments/sd_measure.cpp`), and the VST3 is loaded in a JUCE host to compare sample rates
(`review/experiments/sr_probe.cpp`). Everything is automatic: nothing was listened to. The readings below say where
listening is worth the time.

## What the measurements say

1. **The presets are not level-matched.** From −9.2 dBFS (Cathedral Bloom) to −22.5 dBFS (Quiet Room): a 13 dB swing
   when changing preset. Glass Wind and Cathedral Bloom are the hottest and have the lowest crest factor (the limiter
   is working). Glass Wind swings by 11.5 dB between 2 s windows and takes 3.6 s to build up: it is the least stable
   preset. Razor Feedback and Chord Keys start at once and hold still (spread 0.3–0.4 dB).

2. **Note range under MIDI.** Dry, every note from 18 to 84 is within ±17 cents at every Body setting, with one
   exception (note 80, Body 0.7: −36 cents). Note 16 always sounds 11 semitones high (+1104 cents): it should be left
   out of the MIDI range, or documented as it is today. No note is mute at these settings (Lowpass 0.8,
   Highpass 0.2). **With Reverb Mix at 35 %** the tuning breaks below note 48: +46 at 18, −25 at 24, −27 at 36, −25 at
   43, and +105 at 52 with Body 0.9. Above 48 it stays within ±20. The reverb inside the loop detunes the low notes:
   this is the most serious limit for melodic use, and it calls for a warning in the manual ("below C3, MIDI note 48,
   keep the reverb low") or for phase compensation of the reverb.

3. **The same project does not always sound the same.** A host applies the state *after* `prepareToPlay`, so the
   parameters glide from their defaults to the preset through the smoothing (up to 1 s for Body). Simulating the
   state arriving after 0, 1, 10 or 100 blocks: 9 presets out of 14 settle on the same partial, but Doppler Void
   (704 or 198 Hz), Dark Tide (195, 124 or 315 Hz, with a 4 dB level difference), Echo Drift (1142 or 832 Hz), Ember
   Pong (832 or 766 Hz) and Sub Cellar (668 or 721 Hz) change "note" depending on the timing. For someone saving a
   song this is a real problem. A small remedy: when the state is restored, make the parameters jump to their value
   (as `first_` does after `Init`) instead of gliding; presets loaded from the window can keep gliding.

4. **Mono.** Every preset loses between 0.5 and 4.4 dB when summed to mono. The two channels are nearly independent
   (correlation ~0: Echo Drift, Razor, Sympathetic, Ember Pong, Keys) because of the 4-sample offset between the left
   and right delays: −3 dB is the sum of two uncorrelated signals. Chord Keys has negative correlation (−0.27) and
   loses 4.4 dB: in mono it hollows out. Glass Wind, Cathedral Bloom and Open Fifths are close to mono instead
   (correlation 0.6–0.8). Whether this is the intended width is still to be decided; a Width control on the voices,
   like the PONG's, would make it a choice rather than an accident.

5. **Duration.** No level drift, no DC and no recovery in 30 minutes in total at extreme values. One behaviour worth
   knowing: with Reverb Decay at 1.0 the centroid falls from 1130 to 160 Hz in ten minutes (already 300 Hz after two):
   the drone sinks into a low rumble and does not come back. It is the infinite reverb accumulating the low end that
   the DC blocker lets through. Audible and perhaps intended, but worth a long listen.

6. **Extreme combinations.** Highpass above Lowpass gives total silence (−171 dBFS), and the window does not flag
   it. The other combinations stay bounded (peak ≤ 0.58) and sound, the PONG runaway included. The Resonator with a
   sine at its input locks to the input (220 Hz), and with intermittent bursts it rings at the note (110 Hz) between
   one burst and the next.

7. **Sample rate.** In the VST3 at 44.1, 48 and 96 kHz, levels match to a tenth of a dB and the partials match in 7
   presets out of 9. The two exceptions (Echo Drift at 96 kHz, Chord Keys at 48 kHz) have identical RMS and land on
   partials of similar level (for Chord Keys, the E and the G of the chord): the analysis window has a fixed number
   of samples, so at 96 kHz it lasts half as long. Read as a measurement artefact; not checked by ear.

## What is left for the ear

Listening to the 16 presets with these tables at hand; MIDI phrasing (fixed 200 ms glide, velocity ignored, no
sustain); knob movement; real sources in the Resonator; judging the PONG's aliasing at maximum Drive and Tone.

## Tables

### A. Presets: level, dynamics and onset time (20 s from Init)

| Preset | RMS 5-20 s | Peak | Crest | -3 dB after | Spread 2 s | Dominant | Centroid |
|---|---|---|---|---|---|---|---|
| Default | -18.3 dBFS | 0.33 | 8.8 dB | 0.95 s | 3.8 dB | 500 Hz | 3398 Hz |
| Sub Cellar | -17.5 dBFS | 0.47 | 11.1 dB | 0.85 s | 3.5 dB | 668 Hz | 1051 Hz |
| Glass Wind | -11.7 dBFS | 0.62 | 7.6 dB | 3.60 s | 11.5 dB | 294 Hz | 2066 Hz |
| Cathedral Bloom | -9.2 dBFS | 0.76 | 6.8 dB | 3.20 s | 4.3 dB | 175 Hz | 1154 Hz |
| Echo Drift | -19.8 dBFS | 0.26 | 8.1 dB | 0.45 s | 7.6 dB | 1142 Hz | 2433 Hz |
| Doppler Void | -19.3 dBFS | 0.36 | 10.4 dB | 0.60 s | 2.6 dB | 704 Hz | 1730 Hz |
| Dark Tide | -13.1 dBFS | 0.68 | 9.7 dB | 1.35 s | 5.1 dB | 195 Hz | 397 Hz |
| Razor Feedback | -16.8 dBFS | 0.26 | 5.1 dB | 0.05 s | 0.3 dB | 512 Hz | 7329 Hz |
| Quiet Room | -22.5 dBFS | 0.27 | 11.1 dB | 2.05 s | 3.8 dB | 543 Hz | 1117 Hz |
| Keys (play notes) | -18.3 dBFS | 0.33 | 8.8 dB | 0.40 s | 2.5 dB | 133 Hz | 765 Hz |
| Resonator (needs input) | (needs input: see F) | | | | | | |
| Init (silent) | (needs input: see F) | | | | | | |
| Open Fifths | -14.9 dBFS | 0.48 | 8.5 dB | 0.65 s | 2.9 dB | 151 Hz | 1075 Hz |
| Sympathetic | -17.5 dBFS | 0.52 | 11.8 dB | 0.75 s | 1.5 dB | 597 Hz | 1980 Hz |
| Chord Keys | -19.2 dBFS | 0.28 | 8.3 dB | 0.15 s | 0.4 dB | 165 Hz | 1253 Hz |
| Ember Pong | -21.3 dBFS | 0.23 | 8.4 dB | 0.35 s | 1.4 dB | 832 Hz | 3492 Hz |

### B. Note range under MIDI Pitch (error in cents of the dominant partial; `mute` = below -40 dBFS; `*` = over 25 cents)

#### Reverb Mix 0.00

| Body \ note | 16 | 18 | 20 | 24 | 28 | 31 | 36 | 40 | 43 | 48 | 52 | 55 | 60 | 64 | 67 | 72 | 76 | 79 | 80 | 84 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0.1 | +1104* | +1 | +1 | +1 | +0 | +1 | +0 | +1 | -3 | +2 | +2 | +2 | +5 | +7 | +0 | +0 | +0 | +16 | +17 | +0 |
| 0.3 | +1104* | +1 | +1 | +1 | +0 | +1 | +0 | +1 | -3 | +2 | +2 | +2 | -0 | +0 | +6 | +0 | +0 | +0 | +0 | +0 |
| 0.5 | +1104* | +1 | +1 | +1 | +0 | +1 | +0 | +1 | +0 | +2 | +2 | +2 | +3 | -0 | +4 | +0 | +4 | +0 | +0 | +0 |
| 0.7 | +1104* | +1 | +1 | +1 | +0 | +1 | +0 | +1 | +1 | +3 | +1 | +1 | +1 | -0 | +1 | +3 | +3 | +0 | -36* | +0 |
| 0.9 | +1104* | +1 | +1 | +1 | -0 | +1 | +0 | +1 | +1 | +1 | +2 | +2 | +1 | +1 | +1 | +1 | +1 | +2 | +0 | +0 |

#### Reverb Mix 0.35

| Body \ note | 16 | 18 | 20 | 24 | 28 | 31 | 36 | 40 | 43 | 48 | 52 | 55 | 60 | 64 | 67 | 72 | 76 | 79 | 80 | 84 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0.1 | +1125* | +46* | -15 | -25* | +0 | +15 | -27* | -7 | -25* | +20 | +13 | +8 | -1 | -9 | +10 | +3 | +9 | -1 | -1 | +19 |
| 0.3 | +1125* | +46* | -15 | -25* | +0 | +15 | -27* | -7 | -25* | +20 | +13 | +8 | -8 | -15 | +10 | +4 | +7 | +2 | +11 | +10 |
| 0.5 | +1125* | +46* | -15 | -25* | +0 | +15 | -27* | -7 | -9 | +19 | +11 | +7 | +14 | +2 | +9 | -0 | +5 | +1 | +8 | -1 |
| 0.7 | +1125* | +46* | -15 | -25* | +0 | +15 | -24 | -5 | -8 | +10 | +9 | +7 | +6 | -1 | +7 | +3 | +4 | +3 | +0 | +32* |
| 0.9 | +1125* | +46* | -15 | -28* | -5 | +10 | +18 | -5 | -7 | +5 | +105* | +3 | -1 | -0 | +6 | -0 | +32* | +2 | -0 | +21 |

### C. Repeatability: the same preset reached straight from Init, or after 1 / 10 / 100 blocks at the default values (like a host that loads the state after prepareToPlay)

| Preset | Dominant at once | after 1 block | after 10 | after 100 | RMS at once / 1 / 10 / 100 |
|---|---|---|---|---|---|
| Default | 500 Hz | 500 Hz | 500 Hz | 501 Hz | -19 / -19 / -19 / -19 |
| Sub Cellar | 668 Hz | 668 Hz | 668 Hz | 721 Hz | -17 / -17 / -17 / -17 |
| Glass Wind | 294 Hz | 294 Hz | 294 Hz | 296 Hz | -12 / -12 / -12 / -12 |
| Cathedral Bloom | 175 Hz | 175 Hz | 175 Hz | 176 Hz | -10 / -10 / -9 / -10 |
| Echo Drift | 1142 Hz | 832 Hz | 1142 Hz | 832 Hz | -20 / -21 / -21 / -18 |
| Doppler Void | 704 Hz | 198 Hz | 703 Hz | 198 Hz | -20 / -18 / -20 / -18 |
| Dark Tide | 195 Hz | 124 Hz | 124 Hz | 315 Hz | -14 / -16 / -17 / -18 |
| Razor Feedback | 512 Hz | 512 Hz | 512 Hz | 512 Hz | -17 / -17 / -17 / -17 |
| Quiet Room | 543 Hz | 543 Hz | 543 Hz | 544 Hz | -23 / -23 / -23 / -22 |
| Keys (play notes) | 133 Hz | 133 Hz | 133 Hz | 133 Hz | -18 / -18 / -18 / -18 |
| Open Fifths | 151 Hz | 151 Hz | 151 Hz | 151 Hz | -15 / -15 / -15 / -15 |
| Sympathetic | 597 Hz | 597 Hz | 597 Hz | 597 Hz | -18 / -17 / -17 / -18 |
| Chord Keys | 165 Hz | 165 Hz | 165 Hz | 165 Hz | -19 / -19 / -19 / -19 |
| Ember Pong | 832 Hz | 766 Hz | 766 Hz | 766 Hz | -21 / -20 / -20 / -20 |

### D. Mono compatibility (5-20 s): mono sum against the channel average, side energy, L/R correlation

| Preset | Mono - stereo | Side - Mid | L/R corr. |
|---|---|---|---|
| Default | -1.9 dB | -2.6 dB | +0.29 |
| Sub Cellar | -2.7 dB | -0.6 dB | +0.07 |
| Glass Wind | -0.6 dB | -8.4 dB | +0.75 |
| Cathedral Bloom | -0.5 dB | -9.6 dB | +0.80 |
| Echo Drift | -3.2 dB | +0.4 dB | -0.04 |
| Doppler Void | -2.3 dB | -1.6 dB | +0.18 |
| Dark Tide | -2.0 dB | -2.3 dB | +0.26 |
| Razor Feedback | -3.2 dB | +0.4 dB | -0.04 |
| Quiet Room | -2.6 dB | -0.9 dB | +0.10 |
| Keys (play notes) | -3.5 dB | +0.9 dB | -0.11 |
| Open Fifths | -0.9 dB | -6.3 dB | +0.62 |
| Sympathetic | -3.1 dB | +0.1 dB | -0.01 |
| Chord Keys | -4.4 dB | +2.4 dB | -0.27 |
| Ember Pong | -3.1 dB | +0.2 dB | -0.03 |

### F. Extreme combinations and input (20 s)

| Case | RMS 5-20 s | Peak | Dominant | Centroid | Notes |
|---|---|---|---|---|---|
| Highpass 4 kHz above Lowpass 100 Hz | -171.0 dBFS | 0.00 | 2319 Hz | 3467 Hz | the filters cancel out |
| Sympathetic, LOCK off, Coupling 100 % | -16.2 dBFS | 0.58 | 597 Hz | 1888 Hz | free voices, coupled |
| Ember Pong, Echo Feedback 150 %, Drive max, Tone 3.2 kHz | -18.1 dBFS | 0.29 | 832 Hz | 6777 Hz | saturated runaway |
| Default, Feedback +12 dB, Body minimum | -20.3 dBFS | 0.27 | 1076 Hz | 6036 Hz | shortest loop at full gain |
| Resonator, 220 Hz sine at the input, MIDI note 57 | -16.9 dBFS | 0.40 | 220 Hz | 642 Hz | continuous input |
| Resonator, 110 Hz in 0.5 s bursts, note 45 | -25.5 dBFS | 0.19 | 110 Hz | 254 Hz | intermittent input |

### E. Ten minutes at extreme values: level and centroid per minute

| Case | RMS per minute (dBFS) | Centroid per minute (Hz) | Peak | DC | Recoveries |
|---|---|---|---|---|---|
| Reverb Decay 1.0, Feedback max | -11 -9 -9 -9 -10 -10 -11 -11 -11 -11 | 1130 300 226 199 177 167 165 159 157 159 | 0.74 | -0.00013 | 0 |
| Echo feedback 150 %, PONG drive max | -16 -16 -17 -16 -16 -17 -17 -16 -17 -17 | 2452 2721 2555 2526 2557 2563 2490 2578 2526 2775 | 0.45 | +0.00000 | 0 |
| Poly, Coupling max, Feedback max | -19 -19 -20 -19 -19 -19 -20 -19 -20 -19 | 4862 4999 4882 5237 5057 5259 5222 5005 4832 5114 | 0.41 | -0.00000 | 0 |

### G. VST3 at the three sample rates (15 s, RMS 5-15 s, spectrum from 8 s)

| Preset | 44.1 kHz: RMS / dominant / centroid | 48 kHz | 96 kHz |
|---|---|---|---|
| Default | -20.6 dBFS / 500 Hz / 3176 Hz | -20.6 dBFS / 501 Hz / 3156 Hz | -20.6 dBFS / 501 Hz / 2889 Hz |
| Sub Cellar | -19.4 dBFS / 721 Hz / 998 Hz | -19.4 dBFS / 721 Hz / 994 Hz | -19.4 dBFS / 721 Hz / 1011 Hz |
| Glass Wind | -12.4 dBFS / 294 Hz / 2081 Hz | -12.4 dBFS / 294 Hz / 1885 Hz | -12.4 dBFS / 292 Hz / 1521 Hz |
| Echo Drift | -23.2 dBFS / 832 Hz / 2354 Hz | -23.2 dBFS / 832 Hz / 2349 Hz | -23.2 dBFS / 1142 Hz / 2243 Hz |
| Razor Feedback | -20.0 dBFS / 511 Hz / 7083 Hz | -20.0 dBFS / 512 Hz / 7195 Hz | -20.0 dBFS / 511 Hz / 7061 Hz |
| Open Fifths | -15.4 dBFS / 151 Hz / 987 Hz | -15.4 dBFS / 151 Hz / 976 Hz | -15.4 dBFS / 151 Hz / 860 Hz |
| Sympathetic | -20.6 dBFS / 592 Hz / 2036 Hz | -20.6 dBFS / 592 Hz / 2035 Hz | -20.6 dBFS / 593 Hz / 2058 Hz |
| Ember Pong | -24.5 dBFS / 1981 Hz / 3575 Hz | -24.5 dBFS / 1981 Hz / 3566 Hz | -24.5 dBFS / 1981 Hz / 3452 Hz |
| Chord Keys (C E G) | -23.8 dBFS / 197 Hz / 1241 Hz | -23.5 dBFS / 165 Hz / 1235 Hz | -23.8 dBFS / 197 Hz / 1217 Hz |
