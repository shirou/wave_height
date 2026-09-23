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
#include "../src/c/wave/session.h"
#include "synth.h"
#include "test_util.h"

static double gain(wave_filter_mode mode, double hz) {
  wave_vibration_filter f;
  wave_vibration_filter_init(&f, mode, WAVE_ACQ_RATE_HZ);
  double input = 0, output = 0;
  for (int i = 0; i < 4000; i++) {
    float x = (float)sin(2.0 * 3.141592653589793 * hz * i / WAVE_ACQ_RATE_HZ);
    float y = wave_vibration_filter_push(&f, x);
    if (i >= 1000) {
      input += x*x;
      output += y*y;
    }
  }
  return sqrt(output/input);
}

void test_vibration_filter(void) {
  const double cutoffs[] = {0, 0.8, 0.5, 0.3};
  for (int m = 1; m < WAVE_FILTER_COUNT; m++) {
    const double pass = gain((wave_filter_mode)m, 0.125);
    const double stop = gain((wave_filter_mode)m, 2.5);
    printf("      %s: 8s wave gain %.4f, 2.5Hz gain %.6f\n",
           wave_filter_name((wave_filter_mode)m), pass, stop);
    CHECK_NEAR(pass, 1.0, 0.01, "8-second swell must survive");
    CHECK_LT(stop, 0.005, "2.5 Hz engine tone must be suppressed");
    CHECK_NEAR(gain((wave_filter_mode)m, cutoffs[m]), sqrt(0.5), 0.002,
               "Butterworth cutoff must be -3 dB");
    wave_vibration_filter f;
    wave_vibration_filter_init(&f, (wave_filter_mode)m, WAVE_ACQ_RATE_HZ);
    for (int i = 0; i < 2000; i++) {
      CHECK_NEAR(wave_vibration_filter_push(&f, 200.0f), 200.0, 0.02,
                 "constant input must not cause a startup transient");
    }
  }
  CHECK_NEAR(gain(WAVE_FILTER_ORIGINAL, 2.5), 1.0, 1e-6,
             "Original must be an exact bypass");
}

static void run_sea(wave_session *s, float hz, float amplitude) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 0.5f;
  cfg.tp = 8.0f;
  cfg.seed = 4400u;
  cfg.burst_freq_hz = hz;
  cfg.burst_amp_mg = amplitude;
  cfg.burst_start_s = 0;
  cfg.burst_end_s = 1.0e6f;
  synth_t syn;
  synth_init(&syn, &cfg, WAVE_ACQ_RATE_HZ);
  wave_accel_sample buf[25];
  for (int b = 0; b < 100; b++) {
    for (int i = 0; i < 25; i++) synth_next(&syn, &buf[i]);
    wave_session_push(s, buf, 25);
  }
}

void test_filter_session(void) {
  /* Frequencies off the decimator nulls can fold into the wave band. Check
   * measured height as well as acceptance so merely disabling gating fails. */
  const float tones[] = {1.8f, 2.5f, 3.8f};
  for (int m = 0; m < WAVE_FILTER_COUNT; m++) {
    wave_session clean;
    wave_session_init(&clean, WAVE_ACQ_RATE_HZ, 0, 4000);
    wave_session_set_filter(&clean, (wave_filter_mode)m);
    run_sea(&clean, 2.5f, 0);
    wave_display reference;
    wave_session_get_display(&clean, &reference);
    CHECK(reference.result.valid, "clean sea must produce a result");
    CHECK_IN_RANGE(reference.result.hs, 0.35, 0.65, "clean swell height");
    for (int t = 0; t < 3; t++) {
      wave_session s;
      wave_session_init(&s, WAVE_ACQ_RATE_HZ, 0, 4000);
      wave_session_set_filter(&s, (wave_filter_mode)m);
      run_sea(&s, tones[t], 60);
      wave_display d;
      wave_session_get_display(&s, &d);
      printf("      %s %.1fHz: A=%d R=%d Hs=%.3f (clean %.3f)\n",
             wave_filter_name((wave_filter_mode)m), tones[t], d.result.n_seg,
             s.rejected_total, d.result.hs, reference.result.hs);
      if (m == 0) {
        CHECK(s.rejected_total > 0, "Original should reject the engine stimulus");
      } else {
        CHECK(d.result.n_seg >= 5, "trial must accumulate despite engine tone");
        CHECK_NEAR(d.result.hs, reference.result.hs, 0.06,
                   "filtered engine must not substantially inflate wave height");
      }
    }
  }

  wave_session s;
  wave_session_init(&s, WAVE_ACQ_RATE_HZ, 0.001f, 4000);
  run_sea(&s, 2.5f, 0);
  CHECK(s.acc.n_seg > 0, "setup must have accumulated data");
  wave_session_set_filter(&s, WAVE_FILTER_STRONG);
  CHECK(s.acc.n_seg == 0 && s.valid_s == 0 && s.rejected_total == 0 &&
        s.seg_filled == 0 && s.state == WAVE_STATE_SETTLING,
        "changing filter must clear the previous measurement");
  CHECK(s.acc.noise_floor == 0, "trial must not use unfiltered noise calibration");
  wave_session_set_noise_floor(&s, 0.002f);
  CHECK(s.acc.noise_floor == 0, "calibration updates must not alter trial floor");
  run_sea(&s, 2.5f, 60);
  CHECK(s.acc.n_seg > 0, "snapshot test must contain accepted trial data");
  const int accepted = s.acc.n_seg;
  wave_session_set_filter(&s, WAVE_FILTER_STRONG);
  CHECK(s.acc.n_seg == accepted, "same filter must preserve current measurement");
  wave_session_snapshot snap;
  wave_session_save(&s, &snap, 100);
  CHECK(!wave_session_restore(&s, &snap, 101, 600),
        "trial snapshots must not be restored");
  wave_session_set_filter(&s, WAVE_FILTER_ORIGINAL);
  CHECK_NEAR(s.acc.noise_floor, 0.002, 1e-7, "Original retains calibration");
  CHECK(!wave_session_restore(&s, &snap, 101, 600),
        "trial snapshot must not contaminate Original");

  /* Raw clipping and notification vibration must survive filtering. */
  for (int m = 1; m < WAVE_FILTER_COUNT; m++) {
    for (int buzz = 0; buzz <= 1; buzz++) {
      wave_session_init(&s, WAVE_ACQ_RATE_HZ, 0, 4000);
      wave_session_set_filter(&s, (wave_filter_mode)m);
      wave_accel_sample buf[25];
      for (int i = 0; i < 25; i++) {
        buf[i] = (wave_accel_sample){0, 0, buzz ? 1000 : 4000, buzz != 0};
      }
      for (int b = 0; b < 46; b++) wave_session_push(&s, buf, 25);
      CHECK(s.acc.n_seg == 0 && s.rejected_total >= 3,
            "trial must still reject raw clipping / notification buzzes");
      CHECK(s.last_verdict == (buzz ? WAVE_Q_FAIL_VIBRATE : WAVE_Q_FAIL_CLIP),
            "trial must preserve rejection reason");
    }
  }
}
