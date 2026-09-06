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

#ifndef WAVE_DECIMATE_H
#define WAVE_DECIMATE_H

#include "wave_types.h"

/*
 * Boxcar decimation from the acquisition rate to the 2 Hz processing rate.
 *
 * A 5-point boxcar at 10 Hz has exact nulls at 2, 4, 6 and 8 Hz, which is
 * precisely where the alias centres of a 5:1 decimation sit, so fold-down
 * suppression is good (measured contribution to Hs: +0.1% to +4.4%). Its cost
 * is in-band droop, which spectrum.c divides back out.
 */

typedef struct {
  float acc;
  int count;
} wave_decimator;

void wave_decim_init(wave_decimator *d);

/* Push one sample. Returns true and writes *out when a decimated sample is
 * ready, which happens every WAVE_DECIM_FACTOR inputs. */
bool wave_decim_push(wave_decimator *d, float x, float *out);

/* Power response |H(f)|^2 of the boxcar, so a caller can divide its in-band
 * droop back out. Flat to 0.978 at 0.167 Hz but down to 0.817 at 0.5 Hz, so
 * short-period seas read up to 9.6% low uncorrected.
 *
 * This lives with the filter rather than with the spectrum code on purpose:
 * it IS the filter's transfer function, and keeping the two apart invites
 * changing the filter shape without changing its correction. */
float wave_decim_droop_gain2(float f_hz);

#endif /* WAVE_DECIMATE_H */
