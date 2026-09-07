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

#include "decimate.h"

#include <string.h>

#include "fastmath.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void wave_decim_init(wave_decimator *d) {
  memset(d, 0, sizeof(*d));
}

bool wave_decim_push(wave_decimator *d, float x, float *out) {
  d->acc += x;
  d->count++;
  if (d->count < WAVE_DECIM_FACTOR) {
    return false;
  }
  *out = d->acc / (float)WAVE_DECIM_FACTOR;
  d->acc = 0.0f;
  d->count = 0;
  return true;
}

float wave_decim_droop_gain2(float f_hz) {
  const int m = WAVE_DECIM_FACTOR;
  const float fs_in = WAVE_ACQ_RATE_HZ;
  if (f_hz <= 0.0f) {
    return 1.0f;
  }
  const float d = (float)M_PI * f_hz / fs_in;
  const float sd = wave_sinf(d);
  if (wave_fabsf(sd) < 1e-9f) {
    return 1.0f;
  }
  const float h = wave_sinf((float)m * d) / ((float)m * sd);
  const float g2 = h * h;

  /* The sinc has nulls at multiples of fs_in/m, and callers divide by this. No
   * null falls inside the integration band for the current rate and factor, but
   * changing either could move one in, so refuse to hand back something that
   * would produce an infinity. */
  if (g2 < 1e-4f) {
    return 1e-4f;
  }
  return g2;
}
