# Comparing the plugin with hardware (Audrey II on Daisy Seed)

The instrument is a saturating feedback loop excited by noise, so two runs never match sample by sample, even on the
same hardware. The comparison therefore uses **statistics**: spectral balance, centroid, stereo width and level
stability. It does not compare waveforms.

## What you need

- An Audrey II with the **official v3 firmware** (`audrey-ii-v3.bin`), with its L/R output into an audio interface.
- A **stereo, 48 kHz, 24-bit** recording. Other formats work, because the script resamples. Use **no** processing, EQ
  or compression. Keep the input gain fixed for all takes, and never let it clip.

## Knob positions

`presets.txt` defines 10 presets with knobs at quarter-turn positions: **0, ¼, ½, ¾, 1**. To avoid doubts about the
direction of the potentiometers (the firmware inverts the ADC value), **0 is the control's minimum**: lowest frequency,
feedback at −60 dB, shortest body, send at zero. **1 is its maximum**. The direction you turn does not matter, the
effect does: check by ear that 0 really is the minimum.

| Preset | freq | fb gain | body | lpf | hpf | rev mix | rev decay | echo send | echo time | echo fb | volume | switch |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| p01_mid_drone | ½ | ¾ | ½ | ¾ | ¼ | 0 | ½ | 0 | ½ | 0 | ¾ | off |
| p02_low_dark | ¼ | ¾ | ¾ | ½ | ¼ | 0 | ½ | 0 | ½ | 0 | ¾ | off |
| p03_high_bright | ¾ | ¾ | ¼ | 1 | ¼ | 0 | ½ | 0 | ½ | 0 | ¾ | off |
| p04_reverb_wash | ½ | ¾ | ½ | ¾ | ¼ | ¾ | ¾ | 0 | ½ | 0 | ¾ | off |
| p05_echo_swell | ½ | ¾ | ½ | ¾ | ¼ | 0 | ½ | ¾ | ½ | ¾ | ¾ | off |
| p06_echo_halved | ½ | ¾ | ½ | ¾ | ¼ | 0 | ½ | ¾ | ½ | ¾ | ¾ | **on** (warp: time 3.6 times shorter) |
| p07_long_body | ½ | ¾ | 1 | ¾ | ¼ | 0 | ½ | 0 | ½ | 0 | ¾ | off |
| p08_max_feedback | ½ | 1 | ½ | ¾ | ¼ | 0 | ½ | 0 | ½ | 0 | ¾ | off |
| p09_dark_filtered | ½ | ¾ | ½ | ¼ | ½ | ¼ | ½ | 0 | ½ | 0 | ¾ | off |
| p10_everything_mid | ½ | ½ | ½ | ½ | ½ | ½ | ½ | ½ | ½ | ½ | ½ | off |

Reverb decay has a special curve in the firmware (tension −3): ½ is the knob's position, the effect is not linear, and
the plugin already handles this.

## Recording

For each preset: set the knobs, **wait about 5 s** for the drone to settle, then record **at least 20 s**, or better 25.
Use one file per preset, named as the preset plus any suffix: `p01_mid_drone_hw.wav`, `p02_low_dark_hw.wav`, and so on.
Put them in one folder, such as `recordings/`. Do not touch the knobs while recording. Mind the volume: p04 and p08 are
the loudest.

## Analysis

From `tools/hardware-comparison/`, with numpy, scipy and matplotlib installed (`pip install numpy scipy matplotlib`):

```bash
python3 compare.py --hw-dir recordings --ref-dir reference -o report
```

For each preset it prints level, peak, centroid, L/R correlation, level movement and the **difference in spectral
shape** (in dB, with levels matched), and saves one PNG per preset in `report/`.

The files in `reference/` are renders of the plugin: same engine, blocks of 4 samples as on the firmware, 20 s. They are
not in the repository. Regenerate them with `render_cli presets.txt reference 20`.

### Reading the verdict

- **MATCH:** the difference is within the recording's own variability (between its first and second half, plus 1.5 dB,
  always between 3 and 6 dB). It cannot be told apart from a second run of the hardware.
- **CLOSE:** up to twice the tolerance. Typical of presets that evolve, such as reverb wash and echo swell.
- **DIFFERENT:** to investigate. Look at the plot to see which bands diverge. A partial more or less suggests a difference
  in tuning or filters, and an overall tilt suggests filters or reverb.
- **Absolute level depends on your input gain,** so look at the spectrum first.
- In the plugin's renders the level is almost the same for almost all presets, because of the loop's saturation and the
  output limiter. It does not separate them: the spectrum does.

## Preliminary firmware check

`audrey-ii-v3.bin` (92,192 bytes, SHA-256 `184f3457fb0c752a2c2e78441cfbd84a1c3aa2777a7e1086509bea13d10723b4`) was
inspected **without running it**. It cannot run on a computer: it is a Cortex-M7 image for STM32H750, with initial SP
`0x20020000` and reset vector `0x08000959`.

The binary's float and integer literals include the source code's characteristic constants: 0.7 (limiter), 0.85 (reverb
feedback), 0.98 (string brightness), 12000 Hz (reverb LPF), 18000 Hz, 800 Hz (echo BPF), 8000 Hz (Tone), 0.1447597 (T60),
240000 samples (echo 5 s), 8192 (string delay). 1.5, 5.0, −3.0, an integer 12000 and −90 dB do not appear, which is
expected: ARM compilers encode many values as immediates or compute them at runtime.

This makes it very plausible that v3 derives from this source, but **does not prove it**. Only an audio comparison can.
