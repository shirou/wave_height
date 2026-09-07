/*
 * Copyright 2026 The wave_height Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "../src/c/wave/session.h"
#include "synth.h"
#include "test_util.h"

#define BATCH 25

/* Run a sea for a while and report what fraction of segments were thrown out. */
static double rejection_rate(const synth_config *cfg, double seconds,
                             int *out_total) {
  synth_t syn;
  synth_init(&syn, cfg, WAVE_ACQ_RATE_HZ);

  wave_session s;
  wave_session_init(&s, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);

  const int total_samples = (int)(seconds * WAVE_ACQ_RATE_HZ + 0.5);
  wave_accel_sample buf[BATCH];
  int held = 0;
  for (int i = 0; i < total_samples; i++) {
    synth_next(&syn, &buf[held++]);
    if (held == BATCH) {
      wave_session_push(&s, buf, held);
      held = 0;
    }
  }
  if (held > 0) {
    wave_session_push(&s, buf, held);
  }

  const int accepted = s.acc.n_seg;
  const int total = accepted + s.rejected_total;
  if (out_total) {
    *out_total = total;
  }
  return (total > 0) ? ((double)s.rejected_total / total) : 0.0;
}

/* ---- true positives ---------------------------------------------------- */

void test_quality_detect(void) {
  /* Continuous hand movement in the 2-3 Hz range: every segment should go. */
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 1.0f;
  cfg.tp = 6.0f;
  cfg.seed = 111u;
  cfg.burst_amp_mg = 400.0f;
  cfg.burst_freq_hz = 2.5f;
  cfg.burst_start_s = 0.0f;
  cfg.burst_end_s = 1e6f;

  int total = 0;
  const double rate = rejection_rate(&cfg, 16.0 + 32.0 * 6.0, &total);
  printf("      sustained hand motion: %.0f%% of %d segments rejected\n",
         rate * 100.0, total);
  CHECK(total >= 4, "expected several segments, got %d", total);
  CHECK(rate > 0.9, "sustained body motion should reject nearly everything, "
                    "got %.0f%%", rate * 100.0);
}

void test_quality_clip(void) {
  /* Slamming that saturates the sensor produces broadband harmonics; those
   * segments have to go regardless of what the frequency ratio says. */
  wave_quality q;
  wave_quality_init(&q, WAVE_ACQ_RATE_HZ, 4000.0f);
  wave_vec3 g = {0.0f, 0.0f, 1000.0f};
  wave_quality_begin_segment(&q, g);

  wave_vec3 normal = {0.0f, 0.0f, 1000.0f};
  for (int i = 0; i < 100; i++) {
    wave_quality_push_raw(&q, normal, 0.0f, false);
  }
  CHECK(wave_quality_segment_ok(&q), "a quiet segment should pass");

  wave_vec3 clipped = {0.0f, 0.0f, 3950.0f}; /* within 100 mG of full scale */
  wave_quality_push_raw(&q, clipped, 0.0f, false);
  CHECK(!wave_quality_segment_ok(&q), "a clipped sample should fail the segment");
  CHECK(wave_quality_verdict_of(&q) == WAVE_Q_FAIL_CLIP,
        "expected the clip verdict");
}

void test_quality_vibrate(void) {
  /* Notification buzzes corrupt the data outright, and on a boat Bluetooth
   * reconnects can fire them repeatedly -- hence the advice to enable Quiet
   * Time while measuring. */
  wave_quality q;
  wave_quality_init(&q, WAVE_ACQ_RATE_HZ, 4000.0f);
  wave_vec3 g = {0.0f, 0.0f, 1000.0f};
  wave_quality_begin_segment(&q, g);
  for (int i = 0; i < 100; i++) {
    wave_quality_push_raw(&q, g, 0.0f, i == 50);
  }
  CHECK(wave_quality_verdict_of(&q) == WAVE_Q_FAIL_VIBRATE,
        "a vibrating sample should fail the segment");
}

void test_quality_drift(void) {
  /* Slow attitude drift is the dangerous case: it is invisible to a
   * high-frequency check yet 20 mG of it fabricates 0.157 m of Hs. */
  wave_quality q;
  wave_quality_init(&q, WAVE_ACQ_RATE_HZ, 4000.0f);
  wave_vec3 start = {0.0f, 0.0f, 1000.0f};
  wave_quality_begin_segment(&q, start);
  for (int i = 0; i < 100; i++) {
    wave_quality_push_raw(&q, start, 0.0f, false);
    wave_quality_push_decimated(&q, 0.0f);
  }

  /* Tilted by 3 degrees over the segment: past the 2 degree limit. */
  wave_vec3 drifted;
  drifted.x = (float)(1000.0 * sin(3.0 * 3.14159265358979 / 180.0));
  drifted.y = 0.0f;
  drifted.z = (float)(1000.0 * cos(3.0 * 3.14159265358979 / 180.0));
  wave_quality_update_gravity(&q, drifted);

  CHECK(wave_quality_verdict_of(&q) == WAVE_Q_FAIL_DRIFT,
        "3 degrees of drift should fail the segment");

  /* One degree must not. */
  wave_quality_begin_segment(&q, start);
  for (int i = 0; i < 100; i++) {
    wave_quality_push_raw(&q, start, 0.0f, false);
    wave_quality_push_decimated(&q, 0.0f);
  }
  wave_vec3 slight;
  slight.x = (float)(1000.0 * sin(1.0 * 3.14159265358979 / 180.0));
  slight.y = 0.0f;
  slight.z = (float)(1000.0 * cos(1.0 * 3.14159265358979 / 180.0));
  wave_quality_update_gravity(&q, slight);
  CHECK(wave_quality_segment_ok(&q), "1 degree of drift should be tolerated");
}

/* ---- false positives --------------------------------------------------- */

/*
 * The gate must not reject rough seas.
 *
 * This is the failure mode that matters most, and it is invisible to a
 * detection-only test. Rough conditions have more high-frequency energy and more
 * roll, so a gate calibrated on a gentle swell throws out the rough segments
 * preferentially -- and because only the calm ones survive, Hs reads
 * systematically low exactly when it matters. It is also why the high-frequency
 * check is a ratio rather than an absolute level.
 */
void test_quality_falsepos(void) {
  struct {
    float hs;
    float tp;
    float roll;
    const char *label;
  } cases[] = {
      {1.5f, 6.0f, 15.0f, "rough swell with 15 deg roll"},
      {0.5f, 2.5f, 10.0f, "short steep chop"},
      {2.0f, 8.0f, 20.0f, "big sea with 20 deg roll"},
      {0.2f, 8.0f, 2.0f, "near calm"},
  };

  for (int i = 0; i < 4; i++) {
    synth_config cfg;
    synth_default_config(&cfg);
    cfg.hs = cases[i].hs;
    cfg.tp = cases[i].tp;
    cfg.seed = 2000u + (unsigned)i * 31u;
    cfg.roll_amp_deg = cases[i].roll;
    cfg.roll_period_s = 4.0f;
    cfg.noise_sigma_mg = 2.9f;
    cfg.quantize_1mg = true;

    int total = 0;
    const double rate = rejection_rate(&cfg, 16.0 + 32.0 * 8.0, &total);
    printf("      %-30s %.0f%% of %d segments rejected\n", cases[i].label,
           rate * 100.0, total);
    CHECK(total >= 6, "%s: expected several segments, got %d", cases[i].label,
          total);
    CHECK_LT(rate, 0.10, cases[i].label);
  }
}

/*
 * A perfectly still first segment leaves the reference variance at zero. If that
 * silences the live indicator, hand motion afterwards goes unremarked until the
 * segment is finally rejected up to 32 s later -- which defeats the whole point
 * of having a live indicator. The floor on the denominator is what prevents it.
 *
 * Reachable in practice: a watch resting on a table, or a boat lying still in a
 * glassy calm.
 */
void test_quality_live_after_calm(void) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 0.0f; /* dead flat: z is a constant 1 g */
  cfg.tp = 6.0f;
  cfg.seed = 4321u;
  /* Motion starts after the first segment has been accepted. */
  cfg.burst_amp_mg = 400.0f;
  cfg.burst_freq_hz = 2.5f;
  /* The first segment closes at 16 + 32 = 48 s. Start the motion comfortably
   * after that so the still segment is definitely banked first. */
  cfg.burst_start_s = 55.0f;
  cfg.burst_end_s = 300.0f;

  synth_t syn;
  synth_init(&syn, &cfg, WAVE_ACQ_RATE_HZ);

  wave_session s;
  wave_session_init(&s, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);

  /* Through the first segment: still, accepted, reference variance zero. */
  wave_accel_sample buf[BATCH];
  int held = 0;
  /* Push whole batches only, as the SDK does, so allow for the remainder: at
   * 25 samples a batch, stopping at exactly 49 s would leave the last partial
   * batch unpushed and the segment one sample short of closing. */
  const int until_burst = (int)(52.0 * WAVE_ACQ_RATE_HZ);
  for (int i = 0; i < until_burst; i++) {
    synth_next(&syn, &buf[held++]);
    if (held == BATCH) {
      wave_session_push(&s, buf, held);
      held = 0;
    }
  }
  CHECK(s.acc.n_seg >= 1, "the still segment should have been accepted, n_seg=%d",
        s.acc.n_seg);

  wave_display before;
  wave_session_get_display(&s, &before);
  CHECK(!before.warn_hold_still, "no warning before the motion starts");

  /* The reference variance really is zero here, so the ratio is only defined
   * because of the floor on the denominator. Without it this is 0/0, which
   * happens to compare as "warn" for inf and as "no warn" for NaN -- passing or
   * failing the warning checks below for entirely the wrong reason. Pin the
   * arithmetic down directly. */
  CHECK(s.qual.ref_band_var == 0.0f,
        "expected a zero reference variance from a dead-still segment, got %g",
        (double)s.qual.ref_band_var);
  CHECK(isfinite(wave_quality_live_ratio(&s.qual)),
        "the live ratio must stay finite when the reference variance is zero");

  /* Now shake, and look for the warning well before the segment would close. */
  double t_warn = -1.0;
  const int extra = (int)(10.0 * WAVE_ACQ_RATE_HZ);
  for (int i = 0; i < extra && t_warn < 0.0; i++) {
    synth_next(&syn, &buf[held++]);
    if (held < BATCH) {
      continue;
    }
    wave_session_push(&s, buf, held);
    held = 0;
    wave_display d;
    wave_session_get_display(&s, &d);
    if (d.warn_hold_still) {
      t_warn = (double)s.elapsed_s - (double)cfg.burst_start_s;
    }
  }

  printf("      warning appeared %.1f s after the motion started\n", t_warn);
  CHECK(t_warn >= 0.0,
        "no hold-still warning within 10 s of motion, even though the first "
        "segment was accepted (zero reference variance must not silence it)");
  CHECK(t_warn < 8.0, "warning took %.1f s; should be a few seconds", t_warn);
}

/*
 * The absolute floor that lets a still watch through must not be set so high
 * that real movement slips under it.
 *
 * Measured on the host: hf_power is about 4.5 for a still watch, 440 for a
 * 40 mG shake and 44000 for a 400 mG one. The floor has to sit between the
 * first two, and this pins the upper end -- raising it by 10x would let this
 * case through, while test_calibration pins the lower end by requiring a still
 * watch to be accepted. Between them the constant cannot drift far enough to
 * change behaviour.
 */
void test_quality_small_motion(void) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 0.0f; /* no waves, so the ratio is decided by the movement alone */
  cfg.tp = 6.0f;
  cfg.seed = 8080u;
  cfg.noise_sigma_mg = 2.9f;
  cfg.quantize_1mg = true;
  cfg.burst_amp_mg = 40.0f;
  cfg.burst_freq_hz = 2.5f;
  cfg.burst_start_s = 0.0f;
  cfg.burst_end_s = 1e6f;

  int total = 0;
  const double rate = rejection_rate(&cfg, 16.0 + 32.0 * 5.0, &total);
  printf("      40 mG shake: %.0f%% of %d segments rejected\n", rate * 100.0,
         total);
  CHECK(total >= 3, "expected several segments, got %d", total);
  CHECK(rate > 0.9,
        "a 40 mG shake should still be rejected, got %.0f%% -- the absolute "
        "floor may be set too high",
        rate * 100.0);
}

/*
 * Machinery vibration: detected, not corrected.
 *
 * The noise floor is calibrated ashore, so it covers the sensor and nothing
 * else. Engine vibration that reaches the integration band is added to the
 * height with nothing to offset it, and it cannot be subtracted -- in-band
 * vibration is indistinguishable from wave energy, and estimating it from the
 * high-frequency end is the same mistake as estimating the noise floor that
 * way. So the user is told instead.
 *
 * The first implementation compared the absolute high-frequency level against
 * the still-watch level from calibration. That is what these tests exist to
 * stop coming back: a clean Hs 2 m sea, with no vibration anywhere in it, fired
 * the warning on most batches while the app reported the height correctly.
 * Because the 1 Hz high pass passes a good fraction of the wave band, the
 * absolute level tracks the sea state, so the comparison has to be against the
 * sea state -- which is what the live ratio already does.
 */

typedef struct {
  float onset_s;            /* when the warning first appeared, -1 if never */
  bool coincided_with_move; /* was a movement warning up at the same time --
                             * which is what makes the display ordering in
                             * status_text load-bearing */
  bool cleared_after_onset; /* did it drop again once it had appeared */
  bool dipped_after_onset;  /* did the ratio itself fall back under the
                             * threshold -- without this the dip test would
                             * quietly stop exercising the latch */
  float max_ratio;
} vib_trace;

static vib_trace run_vib(const synth_config *cfg, double seconds) {
  synth_t syn;
  synth_init(&syn, cfg, WAVE_ACQ_RATE_HZ);

  wave_session s;
  wave_session_init(&s, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);

  vib_trace tr;
  tr.onset_s = -1.0f;
  tr.coincided_with_move = false;
  tr.cleared_after_onset = false;
  tr.dipped_after_onset = false;
  tr.max_ratio = 0.0f;

  const int total = (int)(seconds * WAVE_ACQ_RATE_HZ + 0.5);
  wave_accel_sample buf[BATCH];
  int held = 0;
  for (int i = 0; i < total; i++) {
    synth_next(&syn, &buf[held++]);
    if (held < BATCH) {
      continue;
    }
    wave_session_push(&s, buf, held);
    held = 0;

    if (s.live_hf_ratio > tr.max_ratio) {
      tr.max_ratio = s.live_hf_ratio;
    }
    if (tr.onset_s >= 0.0f && s.live_hf_ratio <= WAVE_Q_LIVE_RATIO_WARN) {
      tr.dipped_after_onset = true;
    }
    wave_display d;
    wave_session_get_display(&s, &d);
    if (d.warn_engine_vib) {
      if (tr.onset_s < 0.0f) {
        tr.onset_s = s.elapsed_s;
      }
      if (d.warn_hold_still || d.warn_reposition) {
        tr.coincided_with_move = true;
      }
    } else if (tr.onset_s >= 0.0f) {
      tr.cleared_after_onset = true;
    }
  }
  return tr;
}

/* A sea with an engine running in it from the given time onwards. */
static void vib_sea(synth_config *c, float hs, float tp, unsigned seed) {
  synth_default_config(c);
  c->hs = hs;
  c->tp = tp;
  c->noise_sigma_mg = 7.6f; /* the level hardware actually shows */
  c->quantize_1mg = true;
  c->seed = seed;
  c->burst_freq_hz = 3.0f;
}

void test_quality_vibration(void) {
  /* ---- the false positive that the absolute test produced ----
   *
   * No vibration at all, across the sea states where the old rule misfired.
   * Tp is kept short on purpose: a choppy sea is the worst case, because it
   * puts the most energy near the high-pass corner. */
  const float hs_cases[] = {0.5f, 1.0f, 2.0f};
  const float tp_cases[] = {4.0f, 5.0f, 6.0f};
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      synth_config clean;
      vib_sea(&clean, hs_cases[i], tp_cases[j], 4100u + (unsigned)(i * 3 + j));
      clean.burst_amp_mg = 0.0f;
      const vib_trace tr = run_vib(&clean, 150.0);
      printf("      clean Hs=%.1f Tp=%.0f: max ratio %.2f, warned %s\n",
             (double)hs_cases[i], (double)tp_cases[j], (double)tr.max_ratio,
             tr.onset_s >= 0.0f ? "YES" : "no");
      CHECK(tr.onset_s < 0.0f,
            "a clean sea must never be reported as engine vibration");
    }
  }

  /* ---- an engine that starts once the measurement is under way ----
   *
   * After the first segment, not before: the ratio's denominator is the wave
   * band variance of the last ACCEPTED segment, so nothing can be judged until
   * one has been accepted. See the limitation pinned at the end of this test. */
  synth_config eng;
  vib_sea(&eng, 0.5f, 6.0f, 4200u);
  eng.burst_amp_mg = 60.0f; /* modest but relentless, as machinery is */
  eng.burst_start_s = 60.0f;
  eng.burst_end_s = 1.0e6f;
  const vib_trace on = run_vib(&eng, 200.0);
  printf("      engine from %.0fs: onset %.0fs (max ratio %.2f)\n",
         (double)eng.burst_start_s, (double)on.onset_s, (double)on.max_ratio);
  CHECK(on.onset_s >= 0.0f, "sustained vibration should raise the warning");
  /* Pins WAVE_Q_VIB_HOLD_S from below, with no slack: the warning must not
   * appear until the contamination has genuinely lasted long enough to rule out
   * a movement. Slack here was hiding a batch-boundary error that credited up
   * to one whole batch of not-yet-elevated time. */
  CHECK(on.onset_s >= eng.burst_start_s + WAVE_Q_VIB_HOLD_S,
        "the warning appeared sooner than the hold time allows");
  /* And from above: it has to arrive before a contaminated segment completes,
   * which is the whole reason the hold time is under 32 s. */
  CHECK(on.onset_s <= eng.burst_start_s + 32.0f,
        "the warning arrived too late to precede a contaminated segment");
  /* The display ranks "Vibration" above "Hold still" and "Reposition". That
   * ordering only matters because the conditions overlap, and it is the overlap
   * that a host test can pin -- status_text itself needs the SDK. If this ever
   * stops being true, re-check the ranking in ui.c rather than deleting it. */
  CHECK(on.coincided_with_move,
        "vibration no longer coincides with a movement warning, so the display "
        "ordering in status_text needs revisiting");

  /* ---- the engine's amplitude wandering must not clear the warning ----
   *
   * Requiring every batch to be over the threshold meant a 1.5 s dip cleared a
   * latched warning and demanded another 25 s to earn it back. */
  synth_config dip = eng;
  dip.seed = 4201u;
  dip.burst_gap_start_s = 130.0f;
  dip.burst_gap_end_s = 142.0f; /* throttled back to idle, then opened up */
  const vib_trace held = run_vib(&dip, 200.0);
  CHECK(held.onset_s >= 0.0f, "vibration with a dip should still warn");
  /* Without this the next check passes for the wrong reason: if the ratio never
   * actually crosses back under the threshold, the latch is never asked to hold
   * anything. */
  CHECK(held.dipped_after_onset,
        "the dip case is no longer exercising the latch -- lengthen the gap");
  CHECK(!held.cleared_after_onset,
        "a twelve-second dip must not clear a latched vibration warning");

  /* ---- and it does clear, once the engine actually stops ----
   *
   * Pins the release from the other side. A warning that cannot be cleared is
   * as bad as one that never appears: the user stops the engine, waits, and has
   * no way to tell whether the reading is trustworthy again. */
  synth_config stops = eng;
  stops.seed = 4202u;
  stops.burst_end_s = 140.0f;
  const vib_trace off = run_vib(&stops, 220.0);
  CHECK(off.onset_s >= 0.0f, "vibration should warn before the engine stops");
  CHECK(off.cleared_after_onset,
        "the warning must clear once the engine has been off long enough");

  /* ---- movements, which do stop ---- */
  synth_config reach;
  vib_sea(&reach, 0.5f, 6.0f, 4300u);
  reach.burst_amp_mg = 400.0f; /* far larger than the engine case */
  reach.burst_start_s = 60.0f;
  reach.burst_end_s = 70.0f; /* ten seconds: reaching for the throttle */
  const vib_trace mv = run_vib(&reach, 180.0);
  printf("      10s movement: max ratio %.2f, warned %s\n",
         (double)mv.max_ratio, mv.onset_s >= 0.0f ? "YES" : "no");
  CHECK(mv.onset_s < 0.0f,
        "a ten-second movement must not be reported as vibration, however "
        "large");

  /* Repeated movements with real quiet between them must not add up into a
   * warning either -- what is being established is continuity. */
  synth_config fidget;
  vib_sea(&fidget, 0.5f, 6.0f, 4301u);
  fidget.burst_amp_mg = 400.0f;
  fidget.burst_start_s = 60.0f;
  fidget.burst_end_s = 130.0f;
  fidget.burst_gap_start_s = 68.0f;
  fidget.burst_gap_end_s = 122.0f; /* one 8 s movement, then a long quiet */
  const vib_trace fid = run_vib(&fidget, 180.0);
  CHECK(fid.onset_s < 0.0f,
        "movements separated by quiet must not accumulate into a warning");

  /* ---- what the detector cannot see at all: machinery below ~1 Hz ----
   *
   * The evidence is a 1 Hz high-pass output, so a forced oscillation with no
   * content above that is invisible to it while still landing squarely in the
   * 0.063-0.5 Hz integration band. A slow, regular hull motion driven by
   * machinery is therefore added to the height with no warning at all. This is
   * the same wall the whole design runs into -- in-band contamination is
   * indistinguishable from swell -- and it is pinned here so that "Vibration"
   * is never mistaken for a general contamination detector. */
  {
    synth_config slow;
    vib_sea(&slow, 0.3f, 8.0f, 4500u);
    slow.burst_freq_hz = 0.25f; /* 4 s period, inside the wave band */
    slow.burst_amp_mg = 20.0f;
    slow.burst_start_s = 0.0f;
    slow.burst_end_s = 1.0e6f;

    synth_t syn;
    synth_init(&syn, &slow, WAVE_ACQ_RATE_HZ);
    wave_session s3;
    wave_session_init(&s3, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
    wave_accel_sample buf[BATCH];
    int held3 = 0;
    bool warned = false;
    const int total = (int)(200.0 * WAVE_ACQ_RATE_HZ);
    for (int i = 0; i < total; i++) {
      synth_next(&syn, &buf[held3++]);
      if (held3 == BATCH) {
        wave_session_push(&s3, buf, held3);
        held3 = 0;
        wave_display dd;
        wave_session_get_display(&s3, &dd);
        if (dd.warn_engine_vib) {
          warned = true;
        }
      }
    }
    wave_display d;
    wave_session_get_display(&s3, &d);
    printf("      0.25 Hz machinery: Hs %.2f m (true 0.30), warned %s\n",
           (double)d.result.hs, warned ? "YES" : "no");
    CHECK(d.result.valid, "the 0.25 Hz case should still produce a height");
    CHECK(d.result.hs > 0.4f,
          "the 0.25 Hz tone should be inflating the height -- if it no longer "
          "does, this limitation test has stopped testing anything");
    CHECK(!warned,
          "documented limitation changed: sub-1 Hz machinery now warns, which "
          "is an improvement -- update this test and the comment above it");
  }

  /* ---- the known limitation, pinned so nobody assumes it is covered ----
   *
   * With the engine already running when the measurement starts, no segment is
   * ever clean enough to be accepted, so the ratio has no denominator and the
   * warning cannot fire. What the user gets instead is a measurement that never
   * completes and a "Reposition" prompt -- unhelpful wording, but no wrong
   * number, which is the property that matters. Fixing it would need a sea
   * state estimate from contaminated data, which is the thing the whole design
   * says cannot be had. */
  synth_config from_start;
  vib_sea(&from_start, 0.5f, 6.0f, 4400u);
  from_start.burst_amp_mg = 60.0f;
  from_start.burst_start_s = 0.0f;
  from_start.burst_end_s = 1.0e6f;
  const vib_trace fs = run_vib(&from_start, 200.0);
  CHECK(fs.onset_s < 0.0f,
        "documented limitation changed: vibration from the start now warns, "
        "which is an improvement -- update this test and the comment above it");
  {
    /* And confirm the compensating property: nothing is reported as measured. */
    synth_t syn;
    synth_init(&syn, &from_start, WAVE_ACQ_RATE_HZ);
    wave_session s2;
    wave_session_init(&s2, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
    wave_accel_sample buf[BATCH];
    int held2 = 0;
    const int total = (int)(200.0 * WAVE_ACQ_RATE_HZ);
    for (int i = 0; i < total; i++) {
      synth_next(&syn, &buf[held2++]);
      if (held2 == BATCH) {
        wave_session_push(&s2, buf, held2);
        held2 = 0;
      }
    }
    wave_display d;
    wave_session_get_display(&s2, &d);
    printf("      engine from 0s: valid=%d rej=%d warn_reposition=%d\n",
           (int)d.result.valid, s2.rejected_total, (int)d.warn_reposition);
    CHECK(!d.result.valid,
          "vibration from the start must not produce a height at all");
    CHECK(d.warn_reposition,
          "vibration from the start must at least tell the user something is "
          "wrong");
  }
}
