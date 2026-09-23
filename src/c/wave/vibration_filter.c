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

#include "vibration_filter.h"

#include <string.h>
#include "fastmath.h"

const char *wave_filter_name(wave_filter_mode mode) {
  switch (mode) {
    case WAVE_FILTER_MILD: return "Mild";
    case WAVE_FILTER_MEDIUM: return "Medium";
    case WAVE_FILTER_STRONG: return "Strong";
    default: return "Original";
  }
}

void wave_vibration_filter_init(wave_vibration_filter *f, wave_filter_mode mode,
                                float sample_rate) {
  memset(f, 0, sizeof(*f));
  if (mode <= WAVE_FILTER_ORIGINAL || mode >= WAVE_FILTER_COUNT) {
    return;
  }
  f->mode = mode;
  const float fc = mode == WAVE_FILTER_MILD ? 0.8f :
                   mode == WAVE_FILTER_MEDIUM ? 0.5f : 0.3f;
  const float fs = sample_rate > 2.0f * fc ? sample_rate : 10.0f;
  const float w = 2.0f * 3.14159265358979323846f * fc / fs;
  const float c = wave_cosf(w);
  const float sn = wave_sinf(w);
  const float q[2] = {0.5411961f, 1.3065630f};
  for (int i = 0; i < 2; i++) {
    wave_biquad *b = &f->stage[i];
    const float alpha = sn / (2.0f * q[i]);
    const float norm = 1.0f / (1.0f + alpha);
    b->b0 = 0.5f * (1.0f - c) * norm;
    b->b1 = (1.0f - c) * norm;
    b->b2 = b->b0;
    b->a1 = -2.0f * c * norm;
    b->a2 = (1.0f - alpha) * norm;
  }
}

float wave_vibration_filter_push(wave_vibration_filter *f, float x) {
  if (f->mode == WAVE_FILTER_ORIGINAL) {
    return x;
  }
  if (!f->primed) {
    /* Initialise to a constant input to avoid inventing a startup impulse. */
    for (int i = 0; i < 2; i++) {
      f->stage[i].x1 = f->stage[i].x2 = x;
      f->stage[i].y1 = f->stage[i].y2 = x;
    }
    f->primed = true;
  }
  for (int i = 0; i < 2; i++) {
    wave_biquad *b = &f->stage[i];
    const float y = b->b0*x + b->b1*b->x1 + b->b2*b->x2
                    - b->a1*b->y1 - b->a2*b->y2;
    b->x2 = b->x1;
    b->x1 = x;
    b->y2 = b->y1;
    b->y1 = y;
    x = y;
  }
  return x;
}
