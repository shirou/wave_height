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

#ifndef WAVE_SPECTRUM_H
#define WAVE_SPECTRUM_H

#include "wave_types.h"

/*
 * Per-segment spectral estimation.
 *
 * This module stops at the acceleration PSD. It deliberately does NOT compute
 * Hs: because Hs = 4*sqrt(m0) is concave, averaging per-segment Hs values
 * biases the result low (Jensen), and subtracting the noise floor once per
 * segment applies the clamp n times instead of once. Averaging happens in
 * accumulator.c, on S_a.
 */

/* Remove the least-squares linear trend in place.
 *
 * This is a mandatory step, not a refinement. A drift of only 10 mG across a
 * 32 s segment (a 0.6 degree tilt) produces a spurious Hs of 0.078 m, which
 * already exceeds the sensor noise floor, because the 1/f^4 displacement
 * conversion amplifies bin 2 by 42x. */
void wave_detrend(float *x, int n);

/*
 * Detrend -> periodic Hann window -> FFT -> one-sided PSD -> droop compensation.
 *
 * a_vert_ms2  input, WAVE_SEG_SAMPLES vertical accelerations in m/s^2
 * psd_out     output, WAVE_NBINS entries in (m/s^2)^2/Hz; index 0 is set to 0
 *             and only k = 1 .. WAVE_NBINS-1 carry meaning
 *
 * Normalisation is S(f_k) = 2|X_k|^2 / (fs * sum(w[n]^2)). Note that
 * sum(w^2) = 3N/8 for a periodic Hann window, so this expression already
 * contains the familiar 8/3 power correction. Do not apply 8/3 again --
 * doing so inflates Hs by a factor of 1.633.
 */
void wave_spectrum_segment(const float *a_vert_ms2, float *psd_out);

/* As above, but with the droop compensation switchable. Tests that feed a
 * signal sampled directly at the processing rate never went through the boxcar,
 * so compensating would over-correct them. Production code calls the plain
 * form. */
void wave_spectrum_segment_ex(const float *a_vert_ms2, float *psd_out,
                              bool compensate_droop);

#endif /* WAVE_SPECTRUM_H */
