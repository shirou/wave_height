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

#include "fft.h"

#include <math.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Twiddle factors for the largest supported transform, built on first use.
 * WAVE_FFT_MAX_N/2 entries are enough: smaller transforms stride through the
 * same table. */
static float s_tw_re[WAVE_FFT_MAX_N / 2];
static float s_tw_im[WAVE_FFT_MAX_N / 2];
static bool s_tw_ready = false;

static void build_twiddles(void) {
  for (int i = 0; i < WAVE_FFT_MAX_N / 2; i++) {
    const double ang = -2.0 * M_PI * (double)i / (double)WAVE_FFT_MAX_N;
    s_tw_re[i] = (float)cos(ang);
    s_tw_im[i] = (float)sin(ang);
  }
  s_tw_ready = true;
}

void wave_fft(float *re, float *im, int n) {
  if (n < 2 || n > WAVE_FFT_MAX_N) {
    return;
  }
  if (!s_tw_ready) {
    build_twiddles();
  }

  /* Bit-reversal permutation. */
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      float t;
      t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }

  /* Cooley-Tukey butterflies. The twiddle table is indexed at stride
   * WAVE_FFT_MAX_N/len so that one table serves every transform size. */
  for (int len = 2; len <= n; len <<= 1) {
    const int half = len >> 1;
    const int stride = WAVE_FFT_MAX_N / len;
    for (int i = 0; i < n; i += len) {
      int tw = 0;
      for (int k = 0; k < half; k++, tw += stride) {
        const float wr = s_tw_re[tw];
        const float wi = s_tw_im[tw];
        const int a = i + k;
        const int b = a + half;
        const float xr = re[b] * wr - im[b] * wi;
        const float xi = re[b] * wi + im[b] * wr;
        re[b] = re[a] - xr;
        im[b] = im[a] - xi;
        re[a] += xr;
        im[a] += xi;
      }
    }
  }
}
