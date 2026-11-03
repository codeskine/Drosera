# Echo Pong design

> Superseded by `2026-11-03-echo-pong-revision-design.md`: PONG now makes the firmware's echo bounce, and `PongDelay`
> is removed.

## Goal

Add a second echo algorithm whose repeats bounce between left and right and stay dense and saturated, even on low
drones. It is a plugin-only feature and off by default. With it off, Drosera behaves and renders exactly as it does
today.

Success means:

- With PONG on, each repeat sounds on the opposite channel from the one before.
- Repeats keep the low end of the drone, and Drive makes them denser and louder.
- Feedback stays useful at every Drive setting, and the signal stays bounded at every setting.
- Switching PONG makes no click.
- Nothing allocates, locks or touches files on the audio thread, and renders with PONG off match
  `tools/hardware-comparison/reference/SHA256SUMS` with no new checksums.
- Saved projects and the 15 existing presets sound as they do today.

## Non-goals

Tempo sync, separate left and right times, a Width control, a stereo input to the bounce, and changes to `EchoDelay`.
Oversampling is out unless the aliasing measurement below calls for it.

## Why the current echo sounds thin

`EchoDelay` passes both its output and its feedback through a fixed band-pass at 800 Hz, Q 1. Its loss per pass,
calculated from the coefficients:

| Frequency | Loss per pass |
|---|---|
| 55 Hz | 23 dB |
| 110 Hz | 17 dB |
| 220 Hz | 11 dB |
| 800 Hz | 0 dB |
| 3.2 kHz | 12 dB |

The Default preset's note is about 82 Hz, so its fundamental loses about 20 dB per pass. Echo Feedback defaults to 60%
(4.4 dB), which gives about 24 dB per repeat at that frequency. Raising Feedback cannot make up for this, and the two
channels never exchange signal.

This filter is part of the firmware's sound, so the fix is a new algorithm beside it.

## Why a separate class

Adding a mode to `EchoDelay` would save code and 1.9 MB, but it would put branches in the firmware's signal path, where
rule 2 requires byte identity. The modes would also share buffers, so turning PONG on would play a tail already filtered
at 800 Hz. `PongDelay` is therefore a separate class, and `EchoDelay` does not change.

## PongDelay

`src/dsp/PongDelay.h` has no JUCE and has the same shape as `EchoDelay`: a template on `MaxLength`, with `Init`,
`SetLagTime`, `SetDelayTime` and `SetFeedback`, plus `SetDrive(0..1)` and `SetTone(hz)`.

```cpp
fonepole(time_current_, time_target_, smooth_coef_);  // one time for both lines keeps the bounces evenly spaced
lineL_.SetDelay(time_current_ * sample_rate_);
lineR_.SetDelay(time_current_ * sample_rate_);

outL = SoftClip(drive_gain_ * lpL_.Process(hpL_.Process(lineL_.Read())));
outR = SoftClip(drive_gain_ * lpR_.Process(hpR_.Process(lineR_.Read())));

lineL_.Write(in + feedback_ * loop_trim_ * outR);
lineR_.Write(     feedback_ * loop_trim_ * outL);
```

| Value | Definition | Purpose |
|---|---|---|
| `drive_gain_` | `4^drive` | 0 to +12 dB, linear in dB |
| `loop_trim_` | `2^-drive`, which is 1/√gain | Small-signal gain per bounce is `Feedback × √gain` |
| High-pass | One pole, 20 Hz | Removes DC from the loop |
| Tone | One pole, 800 Hz to 18 kHz | Each bounce gets a little darker |

Echo Time is the time between bounces, so one channel repeats every 2T. WARP works as it does for the classic echo. The
input enters only the left line, so the first repeat is on the left.

Self-oscillation starts at Feedback 100% with Drive 0 and at Feedback 50% with Drive 1. Outputs never exceed ±1, so at
most 1.5 plus the input returns to the loop.

`SoftClip` is the limiter `EchoDelay` already uses. `SetDrive` and `SetTone` recompute their coefficients only when the
value changes, because their handlers run at 12 kHz.

## Engine

- The echo block duplicated in `Process` and `ProcessPoly` becomes one `ProcessEcho(sampL, sampR)`, with the order of
  operations unchanged.
- The input to `PongDelay` is `0.5 * (sampL + sampR) * echo_send_`.
- `SetEchoDelayTime` and `SetEchoDelayFeedback` also go to `PongDelay`. The classic echo receives the same values as
  before.
- Both algorithms always run, and PONG only crossfades the wet signal over 20 ms using `kRampStep`. This avoids clicks,
  old tails coming back and clearing buffers on the audio thread. The cost is 1.9 MB and a little CPU with PONG off.
- The crossfade is an explicit branch:

  ```cpp
  if (pong_mix_ != 0.0f) wet += pong_mix_ * (pong - wet);
  ```

  `+ 0 * x` would change the bits of `-0.0f` and would let a NaN through.
- The final mix stays `0.5 * (samp + wet)`.
- `PongDelay` allocates only in the first `Init`, as `echo_delay_` does, and `Recover` clears it too.
- `SetPong(on, immediate)` skips the crossfade on the first block, so a project saved with PONG on starts on it.

## DroseraCore

`enum Param` does not change, because `kSpecs` and the presets depend on its order. The new controls have their own
setters, as Coupling does: `SetEchoPong(bool)`, `SetEchoDrive(norm)` and `SetEchoTone(norm)`.

Drive and Tone go in a third registry, `echo_registry_`, processed on every control tick. `registry_` iterates an
`unordered_map`, so more entries there could reorder its handlers and change renders. Both are smoothed over 50 ms, and
Tone uses the LOG mapping.

## Parameters

All three are added at the end of the layout. Existing IDs do not change.

| ID | Name | Type and default | Shown as | Caption |
|---|---|---|---|---|
| `echopong` | Echo Pong | Bool, off | | PONG |
| `echodrive` | Echo Drive | 0..1, default 0.5 | `+6.0 dB` (0 to +12) | Drive |
| `echotone` | Echo Tone | 0..1, default 0.5 | `3795 Hz` (800 to 18000) | Tone |

Parameters missing from a saved state load at their defaults, so old projects have PONG off.
`tests/fixtures/state-baseline.bin` is not regenerated.

## Interface

The ECHO tab becomes `Send · Time · Feedback · Drive · Tone · WARP · PONG`.

At the current 112 px step, seven cells in the 760 px window push the first knob 2 px past the left edge.
`TabPanel::resized` therefore uses a step of `min(112, (width - 2 * margin - cell) / (cells - 1))`, which is about 102 px
for seven cells. Other tabs have at most five cells and stay where they are.

With PONG off, Drive and Tone are dimmed and tagged `PONG OFF` through `LabeledKnob::show(value, tag)`. They stay
editable, so they can be set before turning PONG on.

## Presets

`Preset` gets three fields at its end: `echoPong = false`, `echoDrive = 0.5f`, `echoTone = 0.5f`. The existing presets are
not edited.

`setCurrentProgram` always sets all three, or PONG would stay on when moving from a PONG preset to a classic one.

One preset, "Ember Pong", is added at the end of the list, so saved program indexes stay valid. It is dense and
self-sustaining. Its values are set by measurement and confirmed by ear in a DAW.

## Testing

Feedback percentages are engine values, as the readout shows them: 0 to 150%.

### Byte identity

`tools/hardware-comparison/check_reference.sh` passes with no new checksums. It runs right after the `ProcessEcho`
refactor, before anything else is added, so a difference has only one possible cause. A failure is a bug, and the
checksums are not updated.

### render_test

| Test | Setup | Pass |
|---|---|---|
| Bounce | Impulse, Time T, Feedback 70%, Drive 0 | Left at T, right at 2T, left at 3T. The other channel is below -60 dB relative to the peak |
| Timing | Same impulse, with and without WARP | Peaks at T, 2T and 3T within ±2 samples |
| Low end | 82 Hz burst, Feedback 70%, Drive 0, Tone at maximum | At most 3.6 dB lost per bounce |
| Tone | 3.2 kHz burst, Tone at minimum and at maximum | At least 6 dB more lost per bounce at minimum |
| Self-oscillation | Drive 1, level after 20 s | Sustains at Feedback 60%, dies at Feedback 40% |
| Bounds | Drive 1, Feedback 150%, Send 100%, minimum Time, all voices, 60 s | Always finite, `RecoveryCount() == 0`, peak below the output limit |
| DC | Same as Bounds | Wet mean within ±0.01 |
| Switching | PONG on and off during a drone | No sample step above the largest measured without switching, plus 20% |
| Allocation | Existing counter, with PONG on and while switching | Zero allocations |
| Aliasing | About 3 kHz, Drive 1, Feedback 90%, Tone at maximum | Measured first, then the limit is set and written into the test |

If the aliasing measurement is poor, 2x oversampling around the saturator is proposed as a change of scope, because it
raises the CPU cost.

### Other tests

- `ui_test`: the seven ECHO controls fit inside the margins without overlapping, other tabs keep their coordinates, and
  Drive and Tone show `PONG OFF` with PONG off.
- `plugin_host_test`: `state-baseline.bin` loads with PONG off and Drive and Tone at their defaults. The three parameters
  survive a save and reload. Choosing a classic preset after "Ember Pong" turns PONG off.
- `editor_snapshot`: a PNG of the ECHO tab, checked by eye.

The tests check timing, level and stability, not whether the character is right. Drive, Tone and "Ember Pong" are
confirmed by listening.

## Documentation

`docs/MANUAL.md` gets the three new controls in its Echo tab chapter, with a short note on how Drive and Feedback
interact.
