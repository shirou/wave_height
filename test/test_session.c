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
#include <stdio.h>
#include <float.h>
#include <string.h>

#include "../src/c/wave/calibration.h"
#include "../src/c/wave/session.h"
#include "synth.h"
#include "test_util.h"

/* Batch size the SDK actually delivers. */
#define BATCH 25

static void feed_seconds(wave_session *s, synth_t *syn, double seconds) {
  const int total = (int)(seconds * WAVE_ACQ_RATE_HZ + 0.5);
  wave_accel_sample buf[BATCH];
  int held = 0;
  for (int i = 0; i < total; i++) {
    synth_next(syn, &buf[held++]);
    if (held == BATCH) {
      wave_session_push(s, buf, held);
      held = 0;
    }
  }
  if (held > 0) {
    wave_session_push(s, buf, held);
  }
}

/* ---- time to first reading -------------------------------------------- */

/*
 * The whole design rests on the user only having to hold still for one segment,
 * so the wall-clock delay before a number appears is a functional requirement,
 * not a nicety. Settling deliberately delays the START of collection instead of
 * discarding a finished segment: discarding would push this to two segment
 * lengths, 64 s, which is past what the user said they can manage.
 */
void test_session_first_reading(void) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 1.0f;
  cfg.tp = 6.0f;
  cfg.seed = 24680u;

  synth_t syn;
  synth_init(&syn, &cfg, WAVE_ACQ_RATE_HZ);

  wave_session s;
  wave_session_init(&s, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);

  double t_first = -1.0;
  wave_accel_sample buf[BATCH];
  const int max_samples = (int)(WAVE_ACQ_RATE_HZ * 180.0f);
  int held = 0;
  for (int i = 0; i < max_samples && t_first < 0.0; i++) {
    synth_next(&syn, &buf[held++]);
    if (held < BATCH) {
      continue;
    }
    wave_session_push(&s, buf, held);
    held = 0;

    wave_display d;
    wave_session_get_display(&s, &d);
    if (d.result.valid) {
      t_first = (double)s.elapsed_s;
    }
  }

  printf("      first reading at %.1f s\n", t_first);
  CHECK(t_first > 0.0, "no reading appeared within 180 s");
  /* Expected: 16 s of settling plus one 32 s segment, so about 48 s. Allow a
   * batch of slack either side, and hard-fail past 60 s. */
  CHECK_IN_RANGE(t_first, 44.0, 60.0, "time to first reading");
}

/* ---- state machine ----------------------------------------------------- */

void test_session(void) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 1.0f;
  cfg.tp = 6.0f;
  cfg.seed = 13579u;

  /* Settling: no result, and the state says so. */
  {
    synth_t syn;
    synth_init(&syn, &cfg, WAVE_ACQ_RATE_HZ);
    wave_session s;
    wave_session_init(&s, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);

    feed_seconds(&s, &syn, 8.0);
    wave_display d;
    wave_session_get_display(&s, &d);
    CHECK(d.state == WAVE_STATE_SETTLING, "state should still be settling at 8 s");
    CHECK(!d.result.valid, "no result should exist while settling");
    CHECK(d.result.conf == WAVE_CONF_NONE, "confidence should be NONE");
  }

  /* Confidence climbs with accumulated segments, and the display step follows
   * it: coarse while the estimate is coarse. */
  {
    synth_t syn;
    synth_init(&syn, &cfg, WAVE_ACQ_RATE_HZ);
    wave_session s;
    wave_session_init(&s, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);

    feed_seconds(&s, &syn, 16.0 + 32.0 + 1.0);
    wave_display d1;
    wave_session_get_display(&s, &d1);
    CHECK(d1.result.valid, "expected a result after one segment");
    CHECK(d1.result.n_seg == 1, "expected exactly 1 segment, got %d",
          d1.result.n_seg);
    CHECK(d1.result.conf == WAVE_CONF_LOW, "one segment should be LOW confidence");
    /* 0.5 m steps at LOW confidence. */
    CHECK_LT(fabsf(d1.hs_display - roundf(d1.hs_display / 0.5f) * 0.5f), 1e-5,
             "Hs should be on a 0.5 m step at LOW confidence");

    feed_seconds(&s, &syn, 32.0 * 3.0);
    wave_display d2;
    wave_session_get_display(&s, &d2);
    CHECK(d2.result.n_seg >= 3, "expected at least 3 segments, got %d",
          d2.result.n_seg);
    CHECK(d2.result.conf >= WAVE_CONF_MID,
          "three or more segments should be at least MID confidence");

    feed_seconds(&s, &syn, 32.0 * 5.0);
    wave_display d3;
    wave_session_get_display(&s, &d3);
    CHECK(d3.result.conf == WAVE_CONF_HIGH,
          "seven or more segments should be HIGH confidence, got conf=%d n=%d",
          (int)d3.result.conf, d3.result.n_seg);
    printf("      segments %d -> %d -> %d, Hs %.2f m\n", d1.result.n_seg,
           d2.result.n_seg, d3.result.n_seg, d3.result.hs);
  }

  /* A burst of body motion must be discarded WITHOUT losing what came before.
   * Throwing away the accumulation on every fidget would make the design
   * unusable, since the user is expected to rest their arm between segments. */
  {
    synth_config bcfg = cfg;
    bcfg.burst_amp_mg = 400.0f;
    bcfg.burst_freq_hz = 2.5f;
    bcfg.burst_start_s = 16.0f + 32.0f + 2.0f; /* inside the second segment */
    bcfg.burst_end_s = bcfg.burst_start_s + 30.0f;

    synth_t syn;
    synth_init(&syn, &bcfg, WAVE_ACQ_RATE_HZ);
    wave_session s;
    wave_session_init(&s, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);

    feed_seconds(&s, &syn, 16.0 + 32.0 + 1.0);
    wave_display before;
    wave_session_get_display(&s, &before);
    CHECK(before.result.n_seg == 1, "expected 1 segment before the burst");
    CHECK(!before.warn_hold_still, "no warning should be showing before the burst");

    /* Mid-burst: the warning has to appear while the motion is happening, not
     * once the segment finally closes 30 s later. */
    feed_seconds(&s, &syn, 16.0);
    wave_display mid;
    wave_session_get_display(&s, &mid);
    CHECK(mid.warn_hold_still, "expected a hold-still warning DURING the motion");

    /* Segment closes at 80 s and must be thrown out, without costing the good
     * one already banked. */
    feed_seconds(&s, &syn, 16.0);
    wave_display during;
    wave_session_get_display(&s, &during);
    CHECK(during.result.n_seg == 1,
          "the contaminated segment should have been rejected, but n_seg went "
          "to %d",
          during.result.n_seg);
    CHECK(s.rejected_total >= 1, "expected at least one rejected segment");
    CHECK(during.result.valid,
          "the earlier good segment must survive a rejection");

    /* And the warning must CLEAR once the user stops moving. Clearing it only
     * on the next successful segment left it stuck for up to 64 s, most of that
     * while the user was already still, which made it useless as a prompt. */
    feed_seconds(&s, &syn, 10.0);
    wave_display cleared;
    wave_session_get_display(&s, &cleared);
    CHECK(!cleared.warn_hold_still,
          "the hold-still warning must clear within seconds of the motion "
          "stopping, not wait for the next segment");

    /* And accumulation resumes. */
    feed_seconds(&s, &syn, 32.0 * 3.0);
    wave_display after;
    wave_session_get_display(&s, &after);
    CHECK(after.result.n_seg > 1, "should resume accumulating after the burst");
    printf("      rejected %d segment(s), warning cleared, recovered to "
           "n_seg=%d\n",
           s.rejected_total, after.result.n_seg);
  }
}

/* ---- persistence ------------------------------------------------------- */

/*
 * Restoring is load-bearing, not a nicety: reaching three stars needs 96-192 s
 * of valid data while the user can hold still for under a minute, so the
 * accumulation has to survive across separate holds and app launches.
 */
void test_session_persist(void) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 1.2f;
  cfg.tp = 6.0f;
  cfg.seed = 8642u;

  synth_t syn;
  synth_init(&syn, &cfg, WAVE_ACQ_RATE_HZ);

  wave_session a;
  wave_session_init(&a, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
  feed_seconds(&a, &syn, 16.0 + 32.0 * 4.0 + 1.0);

  wave_display da;
  wave_session_get_display(&a, &da);
  CHECK(da.result.n_seg >= 3, "expected several segments before saving");

  wave_session_snapshot snap;
  wave_session_save(&a, &snap, 1000000u);

  /* Fresh session, restored. */
  wave_session b;
  wave_session_init(&b, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
  const bool ok = wave_session_restore(&b, &snap, 1000000u + 60u, 600u);
  CHECK(ok, "restore of a fresh snapshot failed");

  wave_display db;
  wave_session_get_display(&b, &db);
  CHECK(db.result.n_seg == da.result.n_seg, "segment count did not survive");
  CHECK_NEAR(db.result.hs, da.result.hs, 1e-4, "Hs did not survive the round trip");

  /* Stale snapshots must be refused; the sea will have moved on. */
  wave_session c;
  wave_session_init(&c, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
  CHECK(!wave_session_restore(&c, &snap, 1000000u + 3600u, 600u),
        "a one-hour-old snapshot should have been refused");

  /* An empty snapshot carries nothing worth restoring. */
  wave_session_snapshot empty = snap;
  empty.n_seg = 0;
  wave_session f0;
  wave_session_init(&f0, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
  CHECK(!wave_session_restore(&f0, &empty, 1000000u + 10u, 600u),
        "an empty snapshot should have been refused");

  /* A wrong magic means the layout changed under us. */
  wave_session_snapshot badmagic = snap;
  badmagic.magic ^= 0xffu;
  wave_session f1;
  wave_session_init(&f1, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
  CHECK(!wave_session_restore(&f1, &badmagic, 1000000u + 10u, 600u),
        "a snapshot with the wrong magic should have been refused");

  /* A single NaN would survive every later update and pin the app to "calm"
   * forever, with nothing the user could do about it. */
  wave_session_snapshot nan_snap = snap;
  nan_snap.s_avg[WAVE_BIN_LO] = NAN;
  wave_session f2;
  wave_session_init(&f2, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
  CHECK(!wave_session_restore(&f2, &nan_snap, 1000000u + 10u, 600u),
        "a snapshot containing NaN should have been refused");

  /* Finiteness is not enough. FLT_MAX restores cleanly, sends m0 to infinity and
   * shows as "calm", and the exponential average would need something like
   * twelve hours to wash it out. */
  wave_session_snapshot huge = snap;
  huge.s_avg[WAVE_BIN_LO] = 3.0e38f;
  wave_session f3;
  wave_session_init(&f3, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
  CHECK(!wave_session_restore(&f3, &huge, 1000000u + 10u, 600u),
        "a snapshot with an absurd but finite PSD should have been refused");

  /* So must one from the future, which is what a clock change looks like. */
  wave_session d;
  wave_session_init(&d, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
  CHECK(!wave_session_restore(&d, &snap, 999000u, 600u),
        "a snapshot from the future should have been refused");

  /* Continuing from a restored state must keep the weighting right. */
  wave_session e;
  wave_session_init(&e, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
  CHECK(wave_session_restore(&e, &snap, 1000000u + 10u, 600u), "restore failed");
  const int n_before = e.acc.n_seg;
  feed_seconds(&e, &syn, 16.0 + 32.0 + 1.0);
  CHECK(e.acc.n_seg == n_before + 1,
        "expected one more segment after restore, got %d -> %d", n_before,
        e.acc.n_seg);
  printf("      saved n_seg=%d, restored and continued to n_seg=%d\n",
         da.result.n_seg, e.acc.n_seg);
}

/* ---- exponential average takeover ------------------------------------- */

/*
 * Past WAVE_EMA_MAX_SEG the average must stop being arithmetic and start
 * decaying old data, otherwise a sea state from ten minutes ago is still being
 * asserted at three stars.
 */
void test_session_ema(void) {
  float psd_lo[WAVE_NBINS];
  float psd_hi[WAVE_NBINS];
  for (int k = 0; k < WAVE_NBINS; k++) {
    psd_lo[k] = 1.0f;
    psd_hi[k] = 2.0f;
  }

  wave_accumulator acc;
  wave_accum_init(&acc, 0.0f);

  /* Fill exactly to the horizon with the low value. */
  for (int i = 0; i < WAVE_EMA_MAX_SEG; i++) {
    wave_accum_add(&acc, psd_lo);
  }
  CHECK_NEAR(acc.s_avg[WAVE_BIN_LO], 1.0f, 1e-4, "arithmetic mean while filling");

  /* One high segment should now move the average by exactly 1/N. */
  wave_accum_add(&acc, psd_hi);
  const float expected = 1.0f + (2.0f - 1.0f) / (float)WAVE_EMA_MAX_SEG;
  CHECK_NEAR(acc.s_avg[WAVE_BIN_LO], expected, 1e-4,
             "EMA step after the horizon");

  /* Feeding the high value forever must converge on it, i.e. old data really
   * does age out. */
  for (int i = 0; i < 200; i++) {
    wave_accum_add(&acc, psd_hi);
  }
  CHECK_NEAR(acc.s_avg[WAVE_BIN_LO], 2.0f, 0.01, "EMA converges on new value");

  /* And confidence keeps counting past the horizon rather than sticking. */
  CHECK(acc.n_seg > WAVE_EMA_MAX_SEG, "segment count should keep rising");
}

/*
 * A still watch must read "calm", not a small number.
 *
 * Subtracting the noise floor cannot reach zero -- the residual scatters, and
 * the mean of 4*sqrt(m0) over the positive draws stays above it -- so the
 * display has a threshold instead. Hardware showed 0.10 to 0.16 m on a table
 * before this was in place, which is squarely inside the range a real small
 * sea occupies.
 */
void test_session_calm_display(void) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 0.0f; /* a still watch */
  cfg.tp = 6.0f;
  cfg.noise_sigma_mg = 2.9f;
  cfg.quantize_1mg = true;
  cfg.seed = 5150u;

  synth_t syn;
  synth_init(&syn, &cfg, WAVE_ACQ_RATE_HZ);

  wave_session s;
  wave_session_init(&s, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
  feed_seconds(&s, &syn, 16.0 + 32.0 * 6.0 + 2.0);

  wave_display d;
  wave_session_get_display(&s, &d);

  printf("      still watch after %d segments: Hs=%.4f m (calm threshold %.2f)\n",
         d.result.n_seg, (double)d.result.hs, (double)WAVE_CALM_BELOW_M);

  CHECK(d.result.n_seg >= 4, "expected several segments, got %d",
        d.result.n_seg);

  /* The residual must be small enough that the threshold catches it. If this
   * fails, either the floor subtraction regressed or the threshold is too low
   * for the noise the estimator actually leaves behind. */
  CHECK_LT(d.result.hs, WAVE_CALM_BELOW_M,
           "a still watch must fall below the calm threshold");

  /* And a real sea must not be swallowed by that threshold. */
  synth_config sea;
  synth_default_config(&sea);
  sea.hs = 0.3f;
  sea.tp = 6.0f;
  sea.noise_sigma_mg = 2.9f;
  sea.quantize_1mg = true;
  sea.seed = 5151u;

  synth_t syn2;
  synth_init(&syn2, &sea, WAVE_ACQ_RATE_HZ);
  wave_session s2;
  wave_session_init(&s2, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
  feed_seconds(&s2, &syn2, 16.0 + 32.0 * 6.0 + 2.0);

  wave_display d2;
  wave_session_get_display(&s2, &d2);
  printf("      Hs=0.3 m sea: measured %.3f m\n", (double)d2.result.hs);
  CHECK(d2.result.hs >= WAVE_CALM_BELOW_M,
        "a 0.3 m sea must read as a number, not as calm (got %.3f)",
        (double)d2.result.hs);

  /* Again at the noise level the hardware actually has.
   *
   * On-watch calibration returned 1.116e-3, about 6.9x the figure the design
   * assumed, which corresponds to sigma near 7.6 mG. That is the condition the
   * calm threshold has to hold under, not the optimistic one -- and it is what
   * made a still watch on a table read 0.1 to 0.16 m before the per-bin clamp
   * was removed. */
  synth_config hw;
  synth_default_config(&hw);
  hw.hs = 0.0f;
  hw.tp = 6.0f;
  hw.noise_sigma_mg = 7.6f;
  hw.quantize_1mg = true;
  hw.seed = 5152u;

  /* Calibrate first, as the user would, then measure with that floor. */
  synth_t cal_syn;
  synth_init(&cal_syn, &hw, WAVE_ACQ_RATE_HZ);
  wave_session cal;
  wave_session_init(&cal, WAVE_ACQ_RATE_HZ, 0.0f, 4000.0f);
  feed_seconds(&cal, &cal_syn, 16.0 + 32.0 * WAVE_CALIB_SEGMENTS + 2.0);
  const float floor_psd = wave_accum_noise_estimate(&cal.acc);

  hw.seed = 5153u;
  synth_t hw_syn;
  synth_init(&hw_syn, &hw, WAVE_ACQ_RATE_HZ);
  wave_session s3;
  wave_session_init(&s3, WAVE_ACQ_RATE_HZ, floor_psd, 4000.0f);
  feed_seconds(&s3, &hw_syn, 16.0 + 32.0 * 6.0 + 2.0);

  wave_display d3;
  wave_session_get_display(&s3, &d3);
  printf("      at hardware noise (7.6 mG): floor %.3e, still watch %.4f m\n",
         (double)floor_psd, (double)d3.result.hs);
  CHECK_LT(d3.result.hs, WAVE_CALM_BELOW_M,
           "a still watch at hardware noise must still read calm");
}
