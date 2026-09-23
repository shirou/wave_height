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

#ifndef WAVE_VIBRATION_FILTER_H
#define WAVE_VIBRATION_FILTER_H

#include <stdbool.h>

typedef enum {
  WAVE_FILTER_ORIGINAL = 0,
  WAVE_FILTER_MILD,
  WAVE_FILTER_MEDIUM,
  WAVE_FILTER_STRONG,
  WAVE_FILTER_COUNT
} wave_filter_mode;

typedef struct {
  float b0, b1, b2, a1, a2;
  float x1, x2, y1, y2;
} wave_biquad;

typedef struct {
  wave_filter_mode mode;
  wave_biquad stage[2];
  bool primed;
} wave_vibration_filter;

/* Fourth-order Butterworth low pass at 0.8, 0.5 or 0.3 Hz.
 * These trial modes deliberately do NOT undo attenuation in the wave band.
 * They cannot remove vibration already aliased into that band at acquisition. */
void wave_vibration_filter_init(wave_vibration_filter *f, wave_filter_mode mode,
                                float sample_rate);
float wave_vibration_filter_push(wave_vibration_filter *f, float x);
const char *wave_filter_name(wave_filter_mode mode);

#endif
