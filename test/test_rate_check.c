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

#include "../src/c/wave/rate_check.h"
#include "../src/c/wave/wave_types.h"
#include "test_util.h"

/*
 * The distinction that matters here is UNKNOWN versus OK.
 *
 * A batch whose samples all share one timestamp carries no rate information,
 * and treating that as a pass is how the first version of the caller came to
 * disable its own check: it latched "already checked" on such a batch at
 * startup and never looked again, so a watch running at the wrong rate went
 * unnoticed for the whole session.
 */
void test_rate_check(void) {
  float hz = -1.0f;

  /* 25 samples at 10 Hz span 2400 ms. */
  CHECK(wave_check_rate(2400, 25, 10.0f, &hz) == WAVE_RATE_OK,
        "10 Hz batch should be OK");
  CHECK_NEAR(hz, 10.0f, 0.01, "measured rate");

  /* The same 25 samples at 25 Hz span 960 ms -- the default the watch falls back
   * to, and the case the check exists for. */
  CHECK(wave_check_rate(960, 25, 10.0f, &hz) == WAVE_RATE_WRONG,
        "25 Hz delivered against a 10 Hz assumption should be WRONG");
  CHECK_NEAR(hz, 25.0f, 0.01, "measured rate");

  /* No time spread: nothing can be concluded, and it must not read as a pass. */
  CHECK(wave_check_rate(0, 25, 10.0f, &hz) == WAVE_RATE_UNKNOWN,
        "a batch with no time spread must be UNKNOWN, not OK");
  CHECK(hz == 0.0f, "no rate should be reported when none could be measured");

  /* Too few samples to span anything. */
  CHECK(wave_check_rate(100, 1, 10.0f, &hz) == WAVE_RATE_UNKNOWN,
        "a single sample must be UNKNOWN");
  CHECK(wave_check_rate(100, 0, 10.0f, &hz) == WAVE_RATE_UNKNOWN,
        "an empty batch must be UNKNOWN");

  /* A nonsensical expectation must not divide by zero. */
  CHECK(wave_check_rate(2400, 25, 0.0f, &hz) == WAVE_RATE_UNKNOWN,
        "a zero expected rate must be UNKNOWN, not a division by zero");

  /* Tolerance is wide enough for jitter and narrow enough to catch the next
   * rate up. Just inside and just outside the limits: */
  CHECK(wave_check_rate((uint64_t)(1000.0 * 24.0 / (10.0f * 0.80f)), 25, 10.0f,
                        &hz) == WAVE_RATE_OK,
        "8 Hz (0.80x) should be within tolerance");
  CHECK(wave_check_rate((uint64_t)(1000.0 * 24.0 / (10.0f * 0.70f)), 25, 10.0f,
                        &hz) == WAVE_RATE_WRONG,
        "7 Hz (0.70x) should be outside tolerance");
  CHECK(wave_check_rate((uint64_t)(1000.0 * 24.0 / (10.0f * 1.25f)), 25, 10.0f,
                        &hz) == WAVE_RATE_OK,
        "12.5 Hz (1.25x) should be within tolerance");
  CHECK(wave_check_rate((uint64_t)(1000.0 * 24.0 / (10.0f * 1.50f)), 25, 10.0f,
                        &hz) == WAVE_RATE_WRONG,
        "15 Hz (1.50x) should be outside tolerance");

  /* NULL out_hz must be accepted. */
  CHECK(wave_check_rate(2400, 25, 10.0f, NULL) == WAVE_RATE_OK,
        "NULL out_hz should be accepted");

  printf("      verdicts for 10/25 Hz, no-span, short batches and tolerances "
         "all as expected\n");
}
