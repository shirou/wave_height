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
