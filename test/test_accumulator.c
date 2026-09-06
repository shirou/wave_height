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

#include "../src/c/wave/accumulator.h"
#include "../src/c/wave/spectrum.h"
#include "synth.h"
#include "test_util.h"

/*
 * Averaging happens on the spectrum, and the wave parameters are derived once
 * at the end. Doing it the other way round -- Hs per segment, then averaging the
 * Hs values -- is biased low, because 4*sqrt(m0) is concave. This test pins the
 * order down by computing both and requiring the implementation to match the
 * spectral one.
 */
void test_accumulator(void) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 1.0f;
  cfg.tp = 6.0f;
  cfg.seed = 97531u;
  cfg.rayleigh_amp = true; /* spread the per-segment values so the two differ */

  synth_t syn;
  synth_init(&syn, &cfg, WAVE_PROC_RATE_HZ);

  wave_accumulator acc;
  wave_accum_init(&acc, 0.0f);

  const int nseg = 8;
  double hs_of_each_sum = 0.0;
  int counted = 0;

  for (int n = 0; n < nseg; n++) {
    float seg[WAVE_SEG_SAMPLES];
    float psd[WAVE_NBINS];
    for (int i = 0; i < WAVE_SEG_SAMPLES; i++) {
      wave_accel_sample smp;
      synth_next(&syn, &smp);
      seg[i] = ((float)smp.z - 1000.0f) * WAVE_MG_TO_MS2;
    }
    wave_spectrum_segment_ex(seg, psd, false);

    /* Hs of this segment on its own, for the comparison. */
    wave_accumulator one;
    wave_accum_init(&one, 0.0f);
    wave_accum_add(&one, psd);
    wave_result r_one;
    if (wave_accum_result(&one, &r_one)) {
      hs_of_each_sum += r_one.hs;
      counted++;
    }

    wave_accum_add(&acc, psd);
  }

  wave_result r;
  CHECK(wave_accum_result(&acc, &r), "no result from the accumulator");
  CHECK(counted == nseg, "expected %d per-segment results, got %d", nseg,
        counted);
  if (!r.valid || counted == 0) {
    return;
  }

  const double mean_of_hs = hs_of_each_sum / counted;
  printf("      spectral average %.4f m vs mean of per-segment Hs %.4f m "
         "(%.1f%% lower)\n",
         r.hs, mean_of_hs, (1.0 - mean_of_hs / r.hs) * 100.0);

  /* The averaging-Hs route must come out lower; if it did not, this test would
   * not be able to tell the two apart and would be worthless. */
  CHECK(mean_of_hs < r.hs,
        "averaging per-segment Hs (%.4f) should be below the spectral average "
        "(%.4f); the test cannot discriminate otherwise",
        mean_of_hs, r.hs);

  /* And the implementation must be on the spectral side of that gap. */
  const double midpoint = 0.5 * (mean_of_hs + (double)r.hs);
  CHECK((double)r.hs > midpoint,
        "the accumulator appears to be averaging Hs rather than spectra");
}

/*
 * Below the exponential horizon the average must be a plain arithmetic mean of
 * the segments that were actually folded in.
 *
 * This used to compare a "contiguous" run against a "gapped" one, but both
 * executed the identical call sequence -- the gaps existed only in comments --
 * and wave_accum_add is a pure function of its arguments, so the two could never
 * differ whatever the implementation did. It passed for any weighting at all.
 * Checking against an independently computed mean actually pins the weights
 * down: getting alpha wrong by one, say 1/(n+1) instead of 1/n, fails here.
 *
 * Rejected segments simply never reach the accumulator, so "surviving a gap"
 * amounts to the count being driven by folds rather than by elapsed time --
 * which is exactly what this asserts.
 */
void test_accumulator_gaps(void) {
  float psd_a[WAVE_NBINS];
  float psd_b[WAVE_NBINS];
  for (int k = 0; k < WAVE_NBINS; k++) {
    psd_a[k] = 1.0f + 0.01f * (float)k;
    psd_b[k] = 3.0f - 0.02f * (float)k;
  }

  wave_accumulator acc;
  wave_accum_init(&acc, 0.0f);
  wave_accum_add(&acc, psd_a);
  /* (a rejected segment here, which never reaches the accumulator) */
  wave_accum_add(&acc, psd_b);
  /* (two more rejected) */
  wave_accum_add(&acc, psd_a);

  for (int k = WAVE_BIN_LO; k <= WAVE_BIN_HI; k++) {
    const float expected = (psd_a[k] + psd_b[k] + psd_a[k]) / 3.0f;
    CHECK_NEAR(acc.s_avg[k], expected, 1e-5,
               "arithmetic mean of the three folded spectra");
  }
  CHECK(acc.n_seg == 3, "segment count should be 3, got %d", acc.n_seg);
}

/* A flat calm must return "no result" rather than a NaN. Subtracting the noise
 * floor drives most bins to zero, and 4*sqrt of a negative m0 would be NaN --
 * which is what the per-bin clamp exists to prevent. */
void test_accumulator_calm(void) {
  float psd[WAVE_NBINS];
  for (int k = 0; k < WAVE_NBINS; k++) {
    psd[k] = 1.0e-4f;
  }

  wave_accumulator acc;
  wave_accum_init(&acc, 1.0e-3f); /* floor well above the signal */
  for (int i = 0; i < 4; i++) {
    wave_accum_add(&acc, psd);
  }

  wave_result r;
  const bool ok = wave_accum_result(&acc, &r);
  CHECK(!ok, "an all-below-floor spectrum should report no result");
  CHECK(!r.valid, "result should not be marked valid");
  CHECK(isfinite(r.hs), "Hs must not be NaN even when everything clamps to zero");
  CHECK(r.n_seg == 4, "segment count should still be reported, got %d", r.n_seg);

  /* Exactly zero input is the degenerate version of the same thing. */
  float zero[WAVE_NBINS];
  for (int k = 0; k < WAVE_NBINS; k++) {
    zero[k] = 0.0f;
  }
  wave_accumulator z;
  wave_accum_init(&z, 0.0f);
  wave_accum_add(&z, zero);
  wave_result rz;
  CHECK(!wave_accum_result(&z, &rz), "zero spectrum should report no result");
  CHECK(isfinite(rz.hs) && isfinite(rz.period), "zero input must not yield NaN");
}
