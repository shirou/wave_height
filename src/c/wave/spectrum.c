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

#include "spectrum.h"

#include <math.h>
#include <stdbool.h>

#include "decimate.h"
#include "fft.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void wave_detrend(float *x, int n) {
  if (n < 2) {
    return;
  }
  const float x_mean = (float)(n - 1) * 0.5f;

  float y_sum = 0.0f;
  for (int i = 0; i < n; i++) {
    y_sum += x[i];
  }
  const float y_mean = y_sum / (float)n;

  /* sum((i - x_mean)^2) has the closed form n(n^2-1)/12 for i = 0..n-1. */
  const float sxx = (float)n * ((float)n * (float)n - 1.0f) / 12.0f;

  float sxy = 0.0f;
  for (int i = 0; i < n; i++) {
    sxy += ((float)i - x_mean) * (x[i] - y_mean);
  }
  const float slope = sxy / sxx;

  for (int i = 0; i < n; i++) {
    x[i] -= y_mean + slope * ((float)i - x_mean);
  }
}

/* Periodic Hann window and its power sum, built once. */
static float s_win[WAVE_SEG_SAMPLES];
static float s_win_pow_sum = 0.0f;
static bool s_win_ready = false;

static void build_window(void) {
  s_win_pow_sum = 0.0f;
  for (int i = 0; i < WAVE_SEG_SAMPLES; i++) {
    /* Periodic (divide by N, not N-1) so that sum(w^2) is exactly 3N/8. */
    s_win[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i /
                                   (float)WAVE_SEG_SAMPLES));
    s_win_pow_sum += s_win[i] * s_win[i];
  }
  s_win_ready = true;
}

void wave_spectrum_segment(const float *a_vert_ms2, float *psd_out) {
  wave_spectrum_segment_ex(a_vert_ms2, psd_out, true);
}

void wave_spectrum_segment_ex(const float *a_vert_ms2, float *psd_out,
                              bool compensate_droop) {
  if (!s_win_ready) {
    build_window();
  }

  float re[WAVE_SEG_SAMPLES];
  float im[WAVE_SEG_SAMPLES];

  for (int i = 0; i < WAVE_SEG_SAMPLES; i++) {
    re[i] = a_vert_ms2[i];
    im[i] = 0.0f;
  }

  wave_detrend(re, WAVE_SEG_SAMPLES);

  for (int i = 0; i < WAVE_SEG_SAMPLES; i++) {
    re[i] *= s_win[i];
  }

  wave_fft(re, im, WAVE_FFT_N);

  /* One-sided PSD. The 2x accounts for folding the negative frequencies onto
   * the positive ones; it does not apply at DC or Nyquist, neither of which we
   * use (the integration band is bins 2..16). */
  const float norm = 2.0f / (WAVE_PROC_RATE_HZ * s_win_pow_sum);

  psd_out[0] = 0.0f;
  for (int k = 1; k < WAVE_NBINS; k++) {
    const float mag2 = re[k] * re[k] + im[k] * im[k];
    const float f = (float)k * WAVE_DF;
    const float droop = compensate_droop ? wave_decim_droop_gain2(f) : 1.0f;
    psd_out[k] = norm * mag2 / droop;
  }
}
