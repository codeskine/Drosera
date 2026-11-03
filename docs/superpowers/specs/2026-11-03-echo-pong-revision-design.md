# Echo Pong revision design

Supersedes `2026-11-02-echo-pong-design.md` for how PONG sounds and where it sits in the signal path. That spec's
parameters, preset slot and PONG tab stay.

## Goal

Make PONG widen the firmware's echo instead of replacing it. With PONG on, the firmware's own repeats bounce between
left and right. Their color stays Audrey II's, and Drive, Tone, Width and Level shape them from there.

Today PONG crossfades the wet signal to `PongDelay`, a second echo with its own sound. The firmware's echo goes silent,
which is not what PONG was expected to do.

Success means:

- With PONG on and Drive and Tone at their defaults, the echo has the firmware's color, and each repeat sounds on the
  opposite side from the one before.
- Switching PONG makes no click, and a tail already playing starts or stops bouncing.
- The output summed to mono loses no more than 3 dB against PONG off.
- Renders with PONG off match `tools/hardware-comparison/reference/SHA256SUMS` with no new checksums.
- Nothing allocates, locks or touches files on the audio thread.
- `PongDelay` is gone, with its 1.9 MB and its CPU.

## Non-goals

A dense, saturated echo full of low end, as "Ember Pong" is today. On a drone that is a different effect, and it can
come later as its own feature. Also out: tempo sync, separate left and right times, a stereo input to the bounce, and
changes to parameter IDs, names or order.

## Why the low end goes

The 2026-11-02 spec built `PongDelay` to keep the drone's low end, which `EchoDelay`'s band-pass at 800 Hz removes. That
was the wrong target:

- The drone is continuous, and its fundamental is already in the dry signal. A full-range echo of it adds a comb filter
  to the dry signal, not audible repeats. With WARP, the comb moves and the fundamental wavers.
- Low frequencies that bounce between left and right give an unstable image and change when summed to mono.

The band-pass keeps the echo above the drone. What the firmware's echo lacked was movement, and that is what PONG adds.

## PONG is released nowhere

`v0.1.1` predates the first PONG commit, and PONG exists only on `main`. No saved project from a user holds its
parameters, so their meaning and defaults can change. Their IDs still do not, by rule 5.

## Signal path

```
PONG off (the firmware, bit for bit):
  L·send → line L → BP 800 → clip ─→ wet L      feedback L → L
  R·send → line R → BP 800 → clip ─→ wet R      feedback R → R

PONG on:
  ½(L+R)·send → line L → BP(Tone) → clip(Drive) → out L ─┐
                    ↑ feedback               ↓ feedback   ├→ Width → Level → wet L, wet R
                    └── out R ← clip ← BP ← line R ←─────┘

Either way: output = 0.5 · (dry + wet), as today
```

With Echo Time T, the first repeat is on the left at T, then right at 2T, left at 3T. The echo repeats every T, as the
firmware's does, and alternates sides. Send, Time, Feedback and WARP are the echo's own, since there is one engine.

## EchoDelay

`EchoDelay` is in the firmware's path, so its changes are only these:

- `Process(in)` becomes `out = Read(); Write(out, in); return out;`. `Write` stores `out * feedback_ + in`, the
  expression it stores today.
- `Read()` reads, band-passes and soft-clips, as `Process` does today.
- `ReadShaped(drive_gain)` returns `SoftClip(drive_gain * bpf(x))`.
- `SetCenter(hz)` recomputes the band-pass only when the value changes. Returning to the firmware calls
  `SetParams(800.0f, 1.0f)`, the call `Init` makes, so the coefficients come back bit for bit.

## StereoEcho

`src/dsp/StereoEcho.h` has no JUCE. It owns the two `EchoDelay` lines, the PONG ramp, Drive, Tone, Width and Level.

```cpp
void Process(float inL, float inR, float &wetL, float &wetR);
```

`x` is the PONG ramp, from 0 to 1 in 20 ms, moved by `kRampStep` and snapped to its target as `Engine` does today.

### x == 0

An explicit branch runs the firmware:

```cpp
wetL = lineL_.Process(inL);
wetR = lineR_.Process(inR);
```

A blend with `x = 0` would not do, because `+ 0 * y` changes the bits of `-0.0f` and lets a NaN through.

### x > 0

| Stage | Value |
|---|---|
| Drive | `d = x * drive`, `gain = 4^d`, `trim = 2^-d` |
| Tone | Center `800 * (tone_hz / 800)^x`, so it moves from 800 Hz to Tone with the ramp |
| Read | `outL = lineL_.ReadShaped(gain)`, `outR = lineR_.ReadShaped(gain)` |
| Input | Left gets `inL + x * (mono - inL)`, right gets `inR * (1 - x)`, where `mono = 0.5 * (inL + inR)` |
| Feedback | Left stores `trim * (outL + x * (outR - outL))`, right stores `trim * (outR + x * (outL - outR))`, through `Write` |
| Width | `w = 1 + x * (width - 1)`, then `mid = 0.5 * (outL + outR)` and `side = w * 0.5 * (outL - outR)` |
| Level | `g = 1 + x * (level - 1)`, then `wetL = g * (mid + side)` and `wetR = g * (mid - side)` |

Drive and Tone follow the ramp, so switching PONG changes neither color nor level in one step. At small signals each
pass has gain `Feedback × 2^drive`. The loop runs away from Feedback 100% at Drive 0 and from 50% at Drive 1, as with
`PongDelay`. Outputs never exceed ±1.

Width sits between the two sides, so it never makes a repeat louder than it was. At 0 every repeat plays in the middle,
at half the level it had on its side.

### Returning to the firmware

When `x` reaches exactly 0, both lines call `SetCenter(800)` through `SetParams(800.0f, 1.0f)`, and the next sample
takes the firmware branch.

While the ramp moves, the band-pass coefficients are recomputed on every sample, about 960 times per line. That happens
only when PONG is switched. With PONG on, moving Tone recomputes them at most at the 12 kHz control rate, and only
while the smoothed value changes.

### Init

`StereoEcho` holds the two lines through `unique_ptr` and allocates them only in its first `Init`, as `Engine` does
today; later calls clear them on the audio thread. `Engine` holds `StereoEcho` in place of `echo_delay_` and
`pong_delay_`. `Init` sets `x` from the current PONG state and applies the current Tone's center when `x` is 1,
since `EchoDelay::Init` resets it to 800 Hz. `SetPong(on, immediate)` skips the ramp on the first block, so a project
saved with PONG on starts on it.

## Engine

- `ProcessEcho` becomes `stereo_echo_->Process(sampL * echo_send_, sampR * echo_send_, wetL, wetR)`, then
  `sampL = 0.5f * (sampL + wetL)` and the same for the right. The inputs are the products the lines receive today.
- `SetEchoDelayTime` and `SetEchoDelayFeedback` go to `StereoEcho`, which passes them to both lines.
- `SetPongDrive`, `SetPongTone`, `SetPongWidth` and `SetPongLevel` go to `StereoEcho`. `PongMix()` reads its ramp.
- `pong_delay_`, `PongDelay.h` and its include are removed.
- `Recover` clears both lines through `Init`, as it does today.

## DroseraCore

`echo_registry_` keeps its four entries and their smoothing:

| Entry | Before | After |
|---|---|---|
| Drive | Default 0.5, 0..1 | **Default 0**, 0..1 |
| Tone | Default 3795 Hz, 800 to 18000 Hz, LOG | **Default 800 Hz, 200 to 3200 Hz**, LOG |

`kPongToneMin` and `kPongToneMax` become 200 and 3200. With the LOG mapping, the normalized value 0.5 is the geometric
mean, 800 Hz.

## Parameters

IDs, names and layout order do not change.

| ID | Change |
|---|---|
| `echodrive` | Default 0, shown as `+0.0 dB`. Still 0 to +12 dB, now into the echo's own soft clip |
| `echotone` | Shown as 200 to 3200 Hz, the band-pass center. 0.5 reads `800 Hz` |
| `echowidth`, `echolevel`, `echopong` | No change |

`Preset::echoDrive` defaults to 0. The 15 existing presets have PONG off, so their Drive and Tone do nothing and stay as
they are.

## Interface

The PONG tab (ID 6) keeps its four knobs, its toggle and the `PONG OFF` tag. Only Tone's readout changes.

For the interface redesign: there is one echo branch on the output, and PONG is a mode of it, with Drive, Tone, Width
and Level as its controls.

## Ember Pong

Same slot, 15, and same name. New values: high Drive, Tone near 300 Hz, Feedback near the point where the loop runs
away, Width 100%. It sustains itself, warm and dark, and moves across the field. The values are set by measurement
first, so that it sustains, then confirmed by ear in a DAW.

## Testing

Feedback percentages are engine values, as the readout shows them: 0 to 150%.

### Byte identity

`tools/hardware-comparison/check_reference.sh` passes with no new checksums at two points: right after `EchoDelay` is
split and `StereoEcho` runs only the firmware branch, and at the end. A failure is a bug, and the checksums are not
updated.

### render_test

The `PongDelay` tests are replaced by these. Bounce to Width and Level drive `StereoEcho` alone, where the wet signal is
isolated. The last three run through `DroseraCore`:

| Test | Setup | Pass |
|---|---|---|
| Bounce | Impulse into both inputs, Time T, Feedback 70%, PONG on | Left at T, right at 2T, left at 3T. The other side is below -60 dB relative to the peak |
| Timing | Same, with and without WARP | Peaks at T, 2T and 3T within ±2 samples of a single firmware line's, since the band-pass delays every repeat by a few samples |
| Same color | Noise burst, Drive 0, Tone 0.5, PONG on against off with the same mono input on both sides | First repeat's spectrum within 0.5 dB at 200 Hz, 800 Hz and 3.2 kHz |
| Tone | Noise burst, Tone at 300 Hz and 3 kHz | The first repeat's spectral peak within a third of an octave of the center |
| Runaway | Drive 1, level after 20 s | Sustains at Feedback 60%, dies at 40%. At Drive 0 it dies at 90% |
| Width and Level | Width 0, then Level changes | Width 0 gives L = R. Repeats after the first are the same whatever Width and Level are |
| Bounds and DC | Drive 1, Feedback 150%, Send 100%, minimum Time, all voices, 60 s | Always finite, `RecoveryCount() == 0`, output mean within ±0.01 |
| Mono sum | Drone, L + R with PONG on and off | RMS with PONG on at most 3 dB below PONG off |
| Back to the firmware | Send 0, PONG switched on and off before any signal reaches the echo | The render matches one where PONG was never switched, byte for byte |

These existing tests are adapted, keeping their limits: switching without clicks, switching back mid-ramp, PONG on from
the first block, no allocation with PONG on and while switching, independence from block size, every preset with PONG
on, "Ember Pong" sustains itself, recovery from a NaN, and a sweep of Time with PONG on.

### Other tests

- `plugin_host_test`: `state-baseline.bin` loads with Echo Drive at 0, the new default. The test's expectation changes,
  not the fixture. Tone at 0.5 reads `800 Hz`. The state round trip and the preset switch away from "Ember Pong" stay.
- `ui_test`: unchanged, apart from Tone's readout.
- `editor_snapshot`: the PONG tab, checked by eye.

### Listening

The tests check timing, level, color and stability, not whether the sound is right. These are confirmed in a DAW:

1. Default preset, PONG on and off with everything at its default: the same color, only the position changes.
2. Switching PONG during a long tail: no click.
3. Output summed to mono with PONG on: no hole and no clear drop.
4. Width from 100% to 0%: the bounce narrows to the middle with no jump in level.
5. Tone from 200 Hz to 3.2 kHz: from body that does not cloud the fundamental to a telephone-like echo.
6. Drive at maximum with Feedback high: dense and dirty, still controllable, running away earlier but bounded.
7. "Ember Pong": sustains itself, warm and dark, and moves across the field.

## Documentation

- `docs/MANUAL.md`, Pong tab: PONG makes the firmware's echo bounce. Tone is the band-pass center. Drive is the gain
  into the echo's soft clip. The note on how Drive and Feedback interact stays.
- "Known limits": the aliasing note is measured again with the new Drive, and the "Ember Pong" note stays.
- `2026-11-02-echo-pong-design.md` gets a first line pointing to this spec.
