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

#include <stdio.h>
#include <string.h>

#include "test_util.h"

int g_test_failures = 0;
const char *g_current_test = NULL;

void test_fft(void);
void test_rate_check(void);
void test_fastmath_sqrt(void);
void test_fastmath_trig(void);
void test_fastmath_floor(void);
void test_fastmath_isfinite(void);
void test_fastmath_angle(void);
void test_detrend(void);
void test_spectrum(void);
void test_spectrum_longperiod(void);
void test_spectrum_ensemble(void);
void test_leakage_correction(void);
void test_droop(void);
void test_noise_floor_raw(void);
void test_noise_floor_calib(void);
void test_gravity_diff(void);
void test_gravity_roll(void);
void test_gravity_settle(void);
void test_session(void);
void test_session_first_reading(void);
void test_session_persist(void);
void test_session_ema(void);
void test_session_calm_display(void);
void test_calibration(void);
void test_calibration_rejects_motion(void);
void test_quality_detect(void);
void test_quality_clip(void);
void test_quality_vibrate(void);
void test_quality_drift(void);
void test_quality_falsepos(void);
void test_quality_live_after_calm(void);
void test_quality_small_motion(void);
void test_accumulator(void);
void test_accumulator_gaps(void);
void test_accumulator_calm(void);
void test_leverarm_pure(void);
void test_leverarm_mixed(void);

typedef struct {
  const char *name;
  void (*fn)(void);
} test_entry;

int main(int argc, char **argv) {
  static const test_entry tests[] = {
      {"fft", test_fft},
      {"rate_check", test_rate_check},
      {"fastmath_sqrt", test_fastmath_sqrt},
      {"fastmath_trig", test_fastmath_trig},
      {"fastmath_floor", test_fastmath_floor},
      {"fastmath_isfinite", test_fastmath_isfinite},
      {"fastmath_angle", test_fastmath_angle},
      {"detrend", test_detrend},
      {"spectrum", test_spectrum},
      {"spectrum_longperiod", test_spectrum_longperiod},
      {"spectrum_ensemble", test_spectrum_ensemble},
      {"leakage_correction", test_leakage_correction},
      {"droop", test_droop},
      {"noise_floor_raw", test_noise_floor_raw},
      {"noise_floor_calib", test_noise_floor_calib},
      {"gravity_diff", test_gravity_diff},
      {"gravity_roll", test_gravity_roll},
      {"gravity_settle", test_gravity_settle},
      {"leverarm_pure", test_leverarm_pure},
      {"leverarm_mixed", test_leverarm_mixed},
      {"session_first_reading", test_session_first_reading},
      {"session", test_session},
      {"session_persist", test_session_persist},
      {"session_ema", test_session_ema},
      {"session_calm_display", test_session_calm_display},
      {"calibration", test_calibration},
      {"calibration_rejects_motion", test_calibration_rejects_motion},
      {"quality_detect", test_quality_detect},
      {"quality_clip", test_quality_clip},
      {"quality_vibrate", test_quality_vibrate},
      {"quality_drift", test_quality_drift},
      {"quality_falsepos", test_quality_falsepos},
      {"quality_live_after_calm", test_quality_live_after_calm},
      {"quality_small_motion", test_quality_small_motion},
      {"accumulator", test_accumulator},
      {"accumulator_gaps", test_accumulator_gaps},
      {"accumulator_calm", test_accumulator_calm},
  };
  const int n = (int)(sizeof(tests) / sizeof(tests[0]));

  const char *filter = (argc > 1) ? argv[1] : NULL;
  int ran = 0;

  for (int i = 0; i < n; i++) {
    if (filter && strstr(tests[i].name, filter) == NULL) {
      continue;
    }
    const int before = g_test_failures;
    g_current_test = tests[i].name;
    printf("  %-22s ", tests[i].name);
    fflush(stdout);
    printf("\n");
    tests[i].fn();
    ran++;
    if (g_test_failures == before) {
      printf("    ok\n");
    }
  }

  printf("\n%d test group(s) run, %d failure(s)\n", ran, g_test_failures);
  return g_test_failures == 0 ? 0 : 1;
}
