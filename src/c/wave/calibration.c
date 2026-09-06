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

#include "calibration.h"

#include <string.h>

void wave_calib_start(wave_calibration *c, float acq_rate_hz,
                      float full_scale_mg) {
  memset(c, 0, sizeof(*c));

  /* Zero noise floor: we are measuring what that constant should be, so
   * subtracting a previous estimate would fold it into the new one. */
  wave_session_init(&c->session, acq_rate_hz, 0.0f, full_scale_mg);

  c->target = WAVE_CALIB_SEGMENTS;
  c->active = true;
}

void wave_calib_push(wave_calibration *c, const wave_accel_sample *samples,
                     int n) {
  if (!c->active) {
    return;
  }
  wave_session_push(&c->session, samples, n);
}

bool wave_calib_active(const wave_calibration *c) {
  return c->active;
}

int wave_calib_progress(const wave_calibration *c) {
  return c->session.acc.n_seg;
}

int wave_calib_target(const wave_calibration *c) {
  return c->target;
}

int wave_calib_rejected(const wave_calibration *c) {
  return c->session.rejected_total;
}

bool wave_calib_done(const wave_calibration *c) {
  return c->session.acc.n_seg >= c->target;
}

float wave_calib_result(const wave_calibration *c) {
  return wave_accum_noise_estimate(&c->session.acc);
}

void wave_calib_stop(wave_calibration *c) {
  c->active = false;
}
