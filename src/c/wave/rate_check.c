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

#include "rate_check.h"

wave_rate_verdict wave_check_rate(uint64_t span_ms, int n, float expected_hz,
                                  float *out_hz) {
  if (out_hz != NULL) {
    *out_hz = 0.0f;
  }
  if (n < 2 || span_ms == 0 || !(expected_hz > 0.0f)) {
    return WAVE_RATE_UNKNOWN;
  }

  const float measured = 1000.0f * (float)(n - 1) / (float)span_ms;
  if (out_hz != NULL) {
    *out_hz = measured;
  }

  const float ratio = measured / expected_hz;
  if (ratio < WAVE_RATE_TOL_LO || ratio > WAVE_RATE_TOL_HI) {
    return WAVE_RATE_WRONG;
  }
  return WAVE_RATE_OK;
}
