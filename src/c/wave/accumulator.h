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

#ifndef WAVE_ACCUMULATOR_H
#define WAVE_ACCUMULATOR_H

#include "wave_types.h"

/*
 * Welch-style averaging of acceleration spectra, and the wave parameters
 * derived from the average.
 *
 * Averaging happens on S_a and the wave parameters are computed once, at the
 * end. Doing it the other way round -- Hs per segment, then averaging Hs --
 * biases low, because sqrt is concave.
 *
 * Segments need not be contiguous. Welch averaging stays valid across gaps as
 * long as the sea state is stationary, which is what lets the user rest their
 * arm between 32 s segments.
 */

typedef struct {
  float s_avg[WAVE_NBINS]; /* averaged one-sided PSD, (m/s^2)^2/Hz */
  int n_seg;               /* segments folded in so far */
  float noise_floor;       /* static-calibration constant, (m/s^2)^2/Hz */
} wave_accumulator;

/* noise_floor comes from the static calibration described in the plan: rest
 * the watch on a table, run the recording through this very pipeline, and take
 * the median of bins 2..16 of the resulting PSD. It is a spectral density, not
 * a standard deviation -- passing a sigma in mG here is a dimensional error. */
void wave_accum_init(wave_accumulator *a, float noise_floor);

void wave_accum_reset(wave_accumulator *a);

/* Fold one segment's PSD into the average.
 *
 * The weight is 1/min(n, WAVE_EMA_MAX_SEG), so the first WAVE_EMA_MAX_SEG
 * segments form a plain arithmetic mean and later ones decay old data with an
 * effective window of WAVE_EMA_MAX_SEG segments. This keeps a stale sea state
 * from being asserted with high confidence, and unlike a ring buffer it can be
 * restored from a persisted mean, since no "oldest element" has to be evicted. */
void wave_accum_add(wave_accumulator *a, const float *psd);

/* Derive Hs, Tm-1,0 and H1/10 from the averaged spectrum.
 *
 * Returns false when nothing usable is available (no segments, or the whole
 * band clamped to zero, which is what a flat calm looks like). In that case
 * out->valid is false and the caller shows "calm" rather than a number. */
bool wave_accum_result(const wave_accumulator *a, wave_result *out);

/*
 * Estimate the sensor noise floor from the averaged spectrum, for use as the
 * noise_floor constant on a later run.
 *
 * Only meaningful when the watch was still: what it returns is simply the
 * median of the in-band values, which is the noise floor precisely because
 * there is no wave energy to measure. The median rather than the mean so that
 * a stray knock during calibration does not drag the estimate up.
 *
 * Returns 0 if too few segments have been folded in to say anything.
 */
float wave_accum_noise_estimate(const wave_accumulator *a);

#endif /* WAVE_ACCUMULATOR_H */
