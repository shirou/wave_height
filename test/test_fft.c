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

#include <math.h>
#include <stdio.h>

#include "../src/c/wave/fft.h"
#include "../src/c/wave/wave_types.h"
#include "test_util.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* A bin-centred sinusoid must land entirely in its own bin. Anything leaking
 * elsewhere is an indexing or twiddle error. */
static void check_single_tone(int bin) {
  const int n = WAVE_FFT_N;
  float re[WAVE_FFT_N];
  float im[WAVE_FFT_N];

  for (int i = 0; i < n; i++) {
    re[i] = cosf(2.0f * (float)M_PI * (float)bin * (float)i / (float)n);
    im[i] = 0.0f;
  }

  wave_fft(re, im, n);

  double peak = 0.0;
  int peak_bin = -1;
  for (int k = 1; k < n / 2; k++) {
    const double mag = sqrt((double)re[k] * re[k] + (double)im[k] * im[k]);
    if (mag > peak) {
      peak = mag;
      peak_bin = k;
    }
  }
  CHECK(peak_bin == bin, "tone at bin %d peaked at bin %d", bin, peak_bin);

  /* Every other bin must be at least 40 dB down. */
  double worst_other = 0.0;
  int worst_bin = -1;
  for (int k = 1; k < n / 2; k++) {
    if (k == bin) {
      continue;
    }
    const double mag = sqrt((double)re[k] * re[k] + (double)im[k] * im[k]);
    if (mag > worst_other) {
      worst_other = mag;
      worst_bin = k;
    }
  }
  const double db = 20.0 * log10((worst_other + 1e-30) / (peak + 1e-30));
  CHECK(db < -40.0, "tone at bin %d: worst sidelobe bin %d at %.1f dB (want < -40)",
        bin, worst_bin, db);

  /* Amplitude: a unit cosine over n samples gives |X| = n/2 in its bin. */
  const double mag_peak = sqrt((double)re[bin] * re[bin] + (double)im[bin] * im[bin]);
  CHECK_NEAR(mag_peak, n / 2.0, 0.01, "peak magnitude");
}

/* Parseval: sum|x|^2 = (1/N) sum|X|^2. Catches a missing or doubled scale. */
static void check_parseval(void) {
  const int n = WAVE_FFT_N;
  float re[WAVE_FFT_N];
  float im[WAVE_FFT_N];
  unsigned rng = 7u;

  double time_energy = 0.0;
  for (int i = 0; i < n; i++) {
    rng = rng * 1103515245u + 12345u;
    const float v = (float)(((rng >> 16) & 0x7fff) / 32768.0 - 0.5);
    re[i] = v;
    im[i] = 0.0f;
    time_energy += (double)v * v;
  }

  wave_fft(re, im, n);

  double freq_energy = 0.0;
  for (int k = 0; k < n; k++) {
    freq_energy += (double)re[k] * re[k] + (double)im[k] * im[k];
  }
  freq_energy /= (double)n;

  CHECK_NEAR(freq_energy, time_energy, 1e-4, "Parseval energy");
}

void test_fft(void) {
  check_single_tone(1);
  check_single_tone(2);
  check_single_tone(8);
  check_single_tone(16);
  check_single_tone(31);
  check_parseval();
}
