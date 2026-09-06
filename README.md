# Wave Height

Estimate the height of the waves you are in, from the wrist, on a Pebble Time 2.

The watch's accelerometer measures the vertical motion of the boat. Integrating
that twice in the frequency domain gives the displacement spectrum, and from it
the significant wave height — the same quantity a wave buoy reports and the same
one a marine forecast means by "wave height", so the two can be compared
directly.

## What it actually measures, and where that breaks down

**It measures the vertical motion of the spot the watch is sitting on. That is
not the same as the height of the waves.** Two systematic errors pull in
opposite directions, and both are large enough to matter.

### It reads low when the waves are short

A hull only follows waves roughly twice its own length or longer. Shorter ones
it rides over. In deep water that puts the limit at `T_min = sqrt(4*pi*L/g)`:

| Boat length | Waves shorter than | are underestimated |
|-------------|--------------------|--------------------|
| 5 m | 2.5 s | |
| 8 m | 3.2 s | |
| 10 m | 3.6 s | |
| 12 m | 3.9 s | |

Set your boat length in the settings. It is used **only** to warn you when the
measured period falls below this limit — never to scale the answer. Correcting
for it properly would need the hull form, loading and heading, and inventing a
correction from length alone would be false precision.

This is why the app targets boats up to about 12 m. On anything larger the
underestimate stops being a caveat and becomes the dominant term.

### It reads high if the watch is away from the centre of the boat

A boat is rigid, so any point away from the axis it rotates about moves up and
down by `distance x rotation angle` on top of the actual heave. Put the watch on
the gunwale and you are at the point of maximum leverage for roll.

Measured, with a 1.5 m half-beam and 15 degrees of roll: a real 0.5 m sea reads
as **1.21 m, 2.4 times the truth**.

No amount of signal processing removes this. The watch genuinely is moving up
and down that much. A gyroscope would let the rotation be measured and
subtracted, but the public Pebble SDK does not expose one.

**So: put your hand near the middle of the boat, not on the gunwale.** On the
centreline removes the roll lever arm; halfway along the boat as well removes
the pitch one. As a guide, stay within about half a metre of the centreline:

| Real sea | Roll amplitude | Stay within |
|----------|----------------|-------------|
| 1.0 m | 10 deg | 1.35 m |
| 1.0 m | 15 deg | 0.91 m |
| 0.5 m | 10 deg | 0.68 m |
| 0.5 m | 15 deg | **0.45 m** |

Small seas with big roll are the unforgiving case. If the only thing to hold is
the gunwale, the reading will be high — by how much depends on how the boat is
rolling.

### Other conditions

- **Measure while lying to or drifting, not under way.** Making way shifts the
  encounter period: at 10 knots in a real 6 s sea, the period reads 3.9 s head-on
  and 13.3 s following. Hs survives; the period does not.
- **Turn on Quiet Time.** A notification buzz invalidates the segment it lands
  in, and on a boat Bluetooth reconnects can fire them repeatedly.

## Using it

Rest your hand somewhere solid near the middle of the boat and keep it still.

1. **Levelling** — about 16 seconds while the app works out which way is down.
2. **Measuring** — a reading appears after one 32-second segment, so roughly 50
   seconds after launch.
3. It keeps going. Each further segment sharpens the estimate.

You do not have to hold still the whole time. Segments are scored independently
and the average is built from the good ones, so you can rest your arm and pick it
up again. If the app sees you move it says **Hold still** and drops that segment;
what you have already banked is kept, and the warning clears within a few seconds
of you settling down again.

The warning is deliberately less trigger-happy than the rejection: a warning that
fires on ordinary wave-to-wave variation just teaches you to ignore it, whereas
dropping a segment only costs half a minute.

The dots at the top right are how much to trust the number:

| Dots | Segments | Accumulated | Spread of Hs | Shown to |
|------|----------|-------------|--------------|----------|
| ● | 1–2 | 32–64 s | about 25% | 0.5 m |
| ●● | 3–6 | 1.6–3.2 min | about 15% | 0.1 m |
| ●●● | 7+ | 3.7 min+ | about 10% | 0.1 m |

The displayed precision follows the confidence deliberately: a value good to
±31% has no business being shown to the nearest 0.1 m.

Alongside the height: **Period** (Tm-1,0, whole seconds) and **1/10**, the mean
of the highest tenth of the waves — 1.27 x Hs. Unlike "maximum wave height",
that does not creep upward the longer you measure.

Hold **Back** to exit. A short press does nothing, so the accumulation survives
the watch being pressed against a fitting.

## Accuracy

Against synthetic seas of known height, the estimator sits within a few percent
across periods from 4 to 12 seconds. That is the algorithm's own error, and it is
the smallest term in the total:

- statistical spread, one segment: **31%**, falling to 10% by seven segments
- boat following the waves: nothing below `T_min`, **-8%** at Tp = 10 s from
  window leakage
- lever arm: up to **+140%** if the watch is on the gunwale in a rolling boat

Expect ±40% against a reference buoy in ordinary conditions, and treat the number
as "about 1.5 m", not "1.47 m".

In a flat calm it reads **calm** rather than a number — but only once the noise
floor has been calibrated on a still watch, because sensor noise alone accounts
for about 0.07 m of apparent height. **That calibration cannot be entered yet:
there is no settings screen, so the noise floor is currently fixed at zero and a
still watch will show roughly 0.1 m rather than "calm".** Boat length and the
choice of feet are stuck at their defaults for the same reason. See "Status".

## Building

```sh
uv tool install pebble-tool --python 3.13
pebble sdk install latest

pebble build
pebble install --emulator emery
```

## Testing

The signal processing under `src/c/wave/` does not include a single Pebble
header. That is deliberate: it means the whole estimator compiles on a host,
where it can be driven with a synthetic sea whose true wave height is known —
which is the only place correctness can actually be established, since the real
sea does not come with an answer key.

```sh
cd test && make check
```

26 groups covering the FFT, spectral normalisation, detrending, the noise floor,
the leakage-corrected integration weights, decimation droop, gravity projection
against the rejected alternative, the lever-arm artefact, quality gating
(including that it does *not* reject rough seas), Welch accumulation, and the
state machine including persistence and its rejection paths.

Several are difference tests rather than absolute bands, because an absolute band
often passes with the feature deleted. Removing the leakage correction fails 6
groups; removing droop compensation fails 8; substituting the rejected
`|a|`-based vertical extraction fails the gravity comparison.

To drive the emulator with a synthetic sea:

```sh
cd test && make gen_emu_accel
./gen_emu_accel --hs 1.5 --tp 6 --seconds 180 --noise 2.9 > /tmp/sea.csv
split -l 255 -d /tmp/sea.csv /tmp/chunk_          # 255 samples per call is the limit
for f in /tmp/chunk_*; do pebble emu-accel custom "$f"; done
```

Note that this is good for checking the display and the state machine, not the
numbers: the 255-sample limit forces the stream to be injected in pieces, and the
discontinuity at each join lands in the lowest frequency bin, where the 1/f^4
weighting amplifies it most. Trust `make check` for the arithmetic.

## Status

Working and tested on the host and in the emulator. Not yet validated at sea —
the numbers above for the algorithm come from synthetic seas, and the real
comparison against buoy data has not been done yet.

Known gaps:

- **No settings screen.** `boat_length_m`, `use_feet`, `diagnostic_mode` and the
  calibrated `noise_floor` can be read from persistent storage but never written,
  so all four sit at their defaults. The consequences are that a still watch
  reads about 0.1 m instead of "calm", the short-wave warning always assumes a
  10 m boat, feet are unreachable, and the bench test that needs the quality gate
  bypassed cannot be run on the watch.
- **Raw logging is unverified on hardware.** Whether the DataLogging API works on
  this firmware is the first thing the on-device spike checks. If it is not
  running, the screen shows **no raw log** so the failure is visible before
  leaving harbour rather than after.

See `docs/plans/2026-09-06-pebble-wave-height.md` for the full design, the
alternatives that were rejected and why, and what remains open.

## Prior art

The approach follows established wave-buoy practice:

- [OpenMetBuoy-v2021](https://www.mdpi.com/2076-3263/12/3/110) — accelerometer to
  wave spectrum on a Cortex-M
- [SFY](https://arxiv.org/html/2401.02286) — low-frequency cutoff selection, and
  why sampling rate matters for impulsive events

## License

Apache License 2.0. See [LICENSE](LICENSE).
