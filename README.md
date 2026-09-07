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
- **Engine vibration is not subtracted.** The noise floor is calibrated ashore,
  so it accounts for the sensor and nothing else. Vibration that reaches the
  0.063–0.5 Hz band while the engine runs adds to the reading. If you see the
  height change when the engine starts, that is what you are looking at — there
  is no compensation for it yet.
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

## Settings

**Select** opens them. Everything is on the watch rather than on a phone,
because the moment you want to calibrate is aboard, out of range, having just
noticed a flat calm reading 0.1 m.

- **Boat length** — 5, 6, 8, 10 or 12 m. Only ever used to warn you when the
  measured period drops below what your hull follows; it never scales the answer.
- **Units** — metres or feet.
- **Noise floor** — measures the accelerometer's own noise so that a flat calm
  reads "calm". **Run it once, ashore, on a table** — see
  [Calibrating the noise floor](#calibrating-the-noise-floor), which explains
  where and why, because getting the location wrong makes real seas read low.
- **Diagnostic** — accepts every segment regardless of quality. Needed for the
  bench test where you shake the watch at a fixed period, which otherwise trips
  the motion gate on every segment. Leave it off for real measurements.

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
floor has been calibrated, because sensor noise alone accounts for a tenth of a
metre or more of apparent height. See
[Calibrating the noise floor](#calibrating-the-noise-floor); it has to be done
ashore, and until it is, a still watch shows a number rather than "calm".

## Building

```sh
uv tool install pebble-tool --python 3.13
pebble sdk install latest

pebble build                        # produces build/wave_height.pbw
pebble install --emulator emery     # run it in the emulator
```

Built against SDK 4.33.1, targeting `emery` only.

## Installing on a watch

The watch is reached through the Pebble app on your phone. The current
(Core Devices) app relays through the cloud, so the computer and the phone do
**not** have to be on the same network.

**One-time setup**

1. In the phone app: **Devices → ⋯ → Enable Dev Connect**, then sign in with
   GitHub.
2. On the computer, sign in with the *same* GitHub account:

   ```sh
   pebble login          # pebble login --status to check
   ```

**Install**

```sh
pebble build
pebble install --cloudpebble
```

**Watch the logs.** `APP_LOG` output comes back over the same connection, which
is how to see the calibration result, the segment-by-segment state, and the
warning if the accelerometer is not running at the expected rate:

```sh
pebble logs --cloudpebble        # or: pebble install --cloudpebble --logs
```

### Over local Wi-Fi instead

Still supported, and useful when offline, but fiddlier. **Two separate toggles
have to be on** — with only one, the tool gets a refused connection:

1. **Devices → ⋯ → Enable Dev Connect** (as above), and
2. **LAN developer** in the app's Settings.

Then use the **Server IP** the app shows:

```sh
pebble install --phone 192.168.1.42
export PEBBLE_PHONE=192.168.1.42   # to skip the flag next time
pebble logs --phone 192.168.1.42
```

### Other routes

```sh
pebble install --adb              # Android over USB
pebble install --serial /dev/ttyUSB0
```

`build/wave_height.pbw` can also be installed by hand — send the file to the
phone and open it with the Pebble app — which is the route for someone who is
not set up for development at all.

### Calibrating the noise floor

**Do this once, ashore, before the first trip. On a table, indoors, with the
watch off your wrist.**

```
Select → Noise floor → leave it alone for about two minutes
```

The app is measuring its own accelerometer's noise. That only works somewhere
with no wave energy to confuse it with, which is why it has to be ashore:

- **On a table indoors** — correct. Nothing is moving.
- **On your wrist, sitting still** — no. Your pulse and tremor get recorded as
  sensor noise and subtracted from every later reading.
- **Aboard, tied up in harbour** — no. Harbour slop is small but it is real wave
  energy, and subtracting it makes real seas read low.
- **Aboard, under way or in a swell** — definitely not. This is the case that
  ruins the calibration outright.

Quality gating stays on throughout, so a watch that is being held or knocked has
those segments discarded and the progress count stops advancing. If `0 / 3` sits
there not moving, something is disturbing it.

The value is stored and survives reinstalling, so once is enough. Redo it only
if the readings look wrong, or on a different watch.

**Why it matters:** measured on a Pebble Time 2 the floor comes out around
1.1e-3, roughly seven times the figure this app was designed around. Skip the
calibration and a watch sitting on a table reads about **0.17 m** instead of
**calm** — and 0.17 m is squarely inside the range a real small sea occupies, so
there is no way to tell the difference by looking.

### Also before the first trip

**Enable Quiet Time while measuring.** A notification buzz invalidates the
32-second segment it lands in, and Bluetooth reconnects aboard can fire them
repeatedly.

Also set **Settings → Boat length** to something close to your hull, so the
short-wave warning is calibrated to the right period.

## Testing

The signal processing under `src/c/wave/` does not include a single Pebble
header. That is deliberate: it means the whole estimator compiles on a host,
where it can be driven with a synthetic sea whose true wave height is known —
which is the only place correctness can actually be established, since the real
sea does not come with an answer key.

```sh
cd test && make check
```

31 groups covering the FFT, spectral normalisation, detrending, the noise floor,
the leakage-corrected integration weights, decimation droop, gravity projection
against the rejected alternative, the lever-arm artefact, quality gating
(including that it does *not* reject rough seas), Welch accumulation, the state
machine including persistence and its rejection paths, noise-floor calibration,
and the sample-rate sanity check.

Several are difference tests rather than absolute bands, because an absolute band
often passes with the feature deleted. Removing the leakage correction fails 6
groups; removing droop compensation fails 8; substituting the rejected
`|a|`-based vertical extraction fails the gravity comparison. Thresholds are
pinned from both sides where it matters — lowering the motion gate's absolute
floor makes a still watch fail to calibrate, raising it lets a 40 mG shake
through.

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

- **Raw logging is unverified on hardware.** Whether the DataLogging API works on
  this firmware is the first thing the on-device spike checks. If it is not
  running, the screen shows **no raw log** so the failure is visible before
  leaving harbour rather than after.
- **Calibration is verified on the host, not in the emulator.** The emulator's
  accelerometer injection caps at 255 samples per call and its packets stop
  decoding when they are fed back to back, so a two-minute calibration run
  cannot be driven through it. The logic is covered by host tests instead, including
  that a still watch calibrates to within a few percent of the expected noise
  power and that a held watch refuses to complete.

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
