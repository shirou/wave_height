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

#include "../src/c/wave/calibration.h"
#include "synth.h"
#include "test_util.h"

#define BATCH 25

/* Sensor noise at the acquisition rate, as assumed throughout. */
#define CAL_SIGMA_MG 2.9f

static void feed(wave_calibration *c, synth_t *syn, double seconds) {
  const int total = (int)(seconds * WAVE_ACQ_RATE_HZ + 0.5);
  wave_accel_sample buf[BATCH];
  int held = 0;
  for (int i = 0; i < total; i++) {
    synth_next(syn, &buf[held++]);
    if (held == BATCH) {
      wave_calib_push(c, buf, held);
      held = 0;
    }
  }
  if (held > 0) {
    wave_calib_push(c, buf, held);
  }
}

/*
 * Calibration has to recover the noise power that is actually present, because
 * everything downstream subtracts it: get it wrong and either a flat calm keeps
 * reading a tenth of a metre, or real swell is clamped away as calm.
 *
 * The expected value is the one-sided PSD of the noise as it reaches the
 * processing rate. The 5-point boxcar divides the acquisition-rate sigma by
 * sqrt(5), and a one-sided density spreads that variance over fs/2.
 */
void test_calibration(void) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 0.0f; /* a still watch: no waves at all */
  cfg.tp = 6.0f;
  cfg.noise_sigma_mg = CAL_SIGMA_MG;
  cfg.quantize_1mg = true;
  cfg.seed = 24601u;

  synth_t syn;
  synth_init(&syn, &cfg, WAVE_ACQ_RATE_HZ);

  wave_calibration cal;
  wave_calib_start(&cal, WAVE_ACQ_RATE_HZ, 4000.0f);
  CHECK(wave_calib_active(&cal), "calibration should be active after start");
  CHECK(wave_calib_target(&cal) == WAVE_CALIB_SEGMENTS, "unexpected target");

  /* Settling plus the segments it asks for, with a little slack for the batch
   * remainder. */
  feed(&cal, &syn, 16.0 + 32.0 * WAVE_CALIB_SEGMENTS + 4.0);

  CHECK(wave_calib_done(&cal), "calibration did not finish: %d/%d segments",
        wave_calib_progress(&cal), wave_calib_target(&cal));
  CHECK(wave_calib_rejected(&cal) == 0,
        "a still watch should have no rejected segments, got %d",
        wave_calib_rejected(&cal));

  const float measured = wave_calib_result(&cal);

  const double sigma_proc =
      (double)CAL_SIGMA_MG / sqrt((double)WAVE_DECIM_FACTOR) *
      (double)WAVE_MG_TO_MS2;
  const double expected = sigma_proc * sigma_proc / (WAVE_PROC_RATE_HZ / 2.0);

  printf("      measured %.3e, expected %.3e (%.0f%%)\n", (double)measured,
         expected, (double)measured / expected * 100.0);

  /* A median over the in-band bins across three segments; a factor of two either
   * way would be a real error, small scatter is not. */
  CHECK_NEAR(measured, expected, 0.5, "calibrated noise floor");

  wave_calib_stop(&cal);
  CHECK(!wave_calib_active(&cal), "calibration should be inactive after stop");
}

/*
 * A watch being held rather than resting must not calibrate. If it did, the
 * user's hand tremor would be subtracted from every later measurement, biasing
 * real seas low -- the opposite of the problem calibration exists to fix.
 */
void test_calibration_rejects_motion(void) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 0.0f;
  cfg.tp = 6.0f;
  cfg.noise_sigma_mg = CAL_SIGMA_MG;
  cfg.quantize_1mg = true;
  cfg.seed = 1337u;
  /* Continuous hand movement from the moment segments start being collected. */
  cfg.burst_amp_mg = 400.0f;
  cfg.burst_freq_hz = 2.5f;
  cfg.burst_start_s = 16.0f;
  cfg.burst_end_s = 1e6f;

  synth_t syn;
  synth_init(&syn, &cfg, WAVE_ACQ_RATE_HZ);

  wave_calibration cal;
  wave_calib_start(&cal, WAVE_ACQ_RATE_HZ, 4000.0f);
  feed(&cal, &syn, 16.0 + 32.0 * 5.0);

  printf("      held watch: %d accepted, %d rejected\n",
         wave_calib_progress(&cal), wave_calib_rejected(&cal));
  CHECK(!wave_calib_done(&cal),
        "calibration should not complete while the watch is being moved");
  CHECK(wave_calib_rejected(&cal) > 0,
        "expected rejected segments so the screen can tell the user to put it "
        "down");
}
