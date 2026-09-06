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

#include "accumulator.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Longuet-Higgins: the mean of the highest tenth of the waves is 1.27*Hs.
 * Unlike the expected maximum of N waves, this does not depend on how long the
 * user has been measuring, so the number stops creeping upward in a steady sea. */
#define WAVE_H_ONE_TENTH_RATIO 1.27f

/* ---- Leakage-corrected integration weights -----------------------------
 *
 * Naively, m0 = sum over the band of S_a(k)/(2*pi*f_k)^4 * df. That is biased
 * high, and badly so at long periods, because the Hann window spreads each
 * component into its neighbours while the 1/f^4 weight varies steeply from bin
 * to bin. Energy leaking downward is multiplied by a much larger weight than
 * energy leaking upward -- the ratio w(2)/w(3) alone is 5.06 -- so the two do
 * not cancel. Measured bias with plain weights: +4% at Tp = 4 s rising to +19%
 * at Tp = 10 s.
 *
 * The fix works on the weights rather than on the spectrum. With incoherent
 * phases the observed spectrum is the true one convolved with the window's
 * power kernel, S_obs = K S_true with K = tridiag(1/6, 2/3, 1/6). Then
 *
 *     sum_k w_k S_obs(k) = sum_j (K^T w)_j S_true(j)
 *
 * so integrating S_obs against w'' = K^-1 w recovers sum_j w_j S_true(j), which
 * is what we wanted. K is symmetric, tridiagonal and constant, so this is one
 * Thomas solve at startup.
 *
 * A small Tikhonov term damps the alternating-sign ringing that an exact
 * inverse produces. It was chosen empirically: with lambda = 0.1 the residual
 * bias stays within +/-2.5% for Tp from 4 s to 12 s, and the run-to-run spread
 * is slightly smaller than with plain weights (17.8% versus 19.0% on a single
 * segment at Hs = 1 m), so the correction costs nothing in robustness.
 *
 * Note this assumes the leakage decorrelates between segments, which is true of
 * a real sea, where components sit at arbitrary frequencies rather than exactly
 * on bin centres. */
#define WAVE_LEAK_TIKHONOV 0.1f

#define WAVE_BAND_BINS (WAVE_BIN_HI - WAVE_BIN_LO + 1)

static float s_w_m0[WAVE_BAND_BINS];      /* corrected weights for m0   */
static float s_w_mm1[WAVE_BAND_BINS];     /* corrected weights for m_-1 */
static bool s_weights_ready = false;

/* Scratch for the weight solve, at file scope rather than on the stack.
 *
 * Six double[15] arrays plus the solver's two is about 1.1 kB, and a Pebble app
 * stack is roughly 2 kB. build_weights is called from wave_accum_init, which
 * runs at startup, but the lazy fallback in wave_accum_result could otherwise
 * put that 1.1 kB at the deepest point of the accelerometer callback -- on top
 * of the FFT path -- where nothing on the host's 8 MB stack would ever notice.
 * Single-threaded, called once, so sharing these is safe. */
static double s_sub[WAVE_BAND_BINS];
static double s_diag[WAVE_BAND_BINS];
static double s_sup[WAVE_BAND_BINS];
static double s_rhs0[WAVE_BAND_BINS];
static double s_rhs1[WAVE_BAND_BINS];
static double s_out[WAVE_BAND_BINS];
static double s_cp[WAVE_BAND_BINS];
static double s_dp[WAVE_BAND_BINS];

/* Thomas algorithm for a symmetric tridiagonal system. */
static void solve_tridiag(const double *sub, const double *diag,
                          const double *sup, const double *rhs, double *x,
                          int n) {
  double *cp = s_cp;
  double *dp = s_dp;

  cp[0] = sup[0] / diag[0];
  dp[0] = rhs[0] / diag[0];
  for (int i = 1; i < n; i++) {
    const double m = diag[i] - sub[i] * cp[i - 1];
    cp[i] = sup[i] / m;
    dp[i] = (rhs[i] - sub[i] * dp[i - 1]) / m;
  }
  x[n - 1] = dp[n - 1];
  for (int i = n - 2; i >= 0; i--) {
    x[i] = dp[i] - cp[i] * x[i + 1];
  }
}

static void build_weights(void) {
  const int n = WAVE_BAND_BINS;
  double *sub = s_sub;
  double *diag = s_diag;
  double *sup = s_sup;
  double *rhs0 = s_rhs0;
  double *rhs1 = s_rhs1;
  double *out = s_out;

  for (int i = 0; i < n; i++) {
    const double f = (double)(WAVE_BIN_LO + i) * WAVE_DF;
    const double om = 2.0 * M_PI * f;
    const double w = 1.0 / (om * om * om * om);
    rhs0[i] = w;
    rhs1[i] = w / f;
    sub[i] = (i == 0) ? 0.0 : (1.0 / 6.0);
    sup[i] = (i == n - 1) ? 0.0 : (1.0 / 6.0);
    diag[i] = (2.0 / 3.0) + (double)WAVE_LEAK_TIKHONOV;
  }

  solve_tridiag(sub, diag, sup, rhs0, out, n);
  for (int i = 0; i < n; i++) {
    s_w_m0[i] = (float)out[i];
  }

  solve_tridiag(sub, diag, sup, rhs1, out, n);
  for (int i = 0; i < n; i++) {
    s_w_mm1[i] = (float)out[i];
  }

  s_weights_ready = true;
}

void wave_accum_init(wave_accumulator *a, float noise_floor) {
  memset(a, 0, sizeof(*a));
  a->noise_floor = noise_floor;

  /* Build the weight table here, at startup, rather than lazily on first use.
   * The lazy path would otherwise run a double-precision tridiagonal solve
   * inside the accelerometer callback, at the deepest point of the call chain. */
  if (!s_weights_ready) {
    build_weights();
  }
}

void wave_accum_reset(wave_accumulator *a) {
  memset(a->s_avg, 0, sizeof(a->s_avg));
  a->n_seg = 0;
}

void wave_accum_add(wave_accumulator *a, const float *psd) {
  const int n = a->n_seg + 1;
  const int denom = (n < WAVE_EMA_MAX_SEG) ? n : WAVE_EMA_MAX_SEG;
  const float alpha = 1.0f / (float)denom;

  for (int k = 0; k < WAVE_NBINS; k++) {
    a->s_avg[k] += alpha * (psd[k] - a->s_avg[k]);
  }

  /* Keep counting past the EMA horizon so confidence still rises, but stop
   * before anything can overflow on a very long session. */
  if (a->n_seg < 100000) {
    a->n_seg = n;
  }
}

static wave_confidence confidence_for(int n_seg) {
  if (n_seg <= 0) {
    return WAVE_CONF_NONE;
  }
  if (n_seg <= 2) {
    return WAVE_CONF_LOW;
  }
  if (n_seg <= 6) {
    return WAVE_CONF_MID;
  }
  return WAVE_CONF_HIGH;
}

bool wave_accum_result(const wave_accumulator *a, wave_result *out) {
  memset(out, 0, sizeof(*out));
  out->n_seg = a->n_seg;
  out->conf = confidence_for(a->n_seg);

  if (a->n_seg <= 0) {
    return false;
  }

  if (!s_weights_ready) {
    build_weights();
  }

  float m0 = 0.0f;
  float m_minus1 = 0.0f;

  for (int k = WAVE_BIN_LO; k <= WAVE_BIN_HI; k++) {
    const int i = k - WAVE_BIN_LO;

    /* Clamp the noise-floor residual per bin, before any weighting. Letting a
     * negative residual through would cancel genuine energy elsewhere and can
     * drag m0 below zero -- on a flat calm that happens more than half the
     * time, and 4*sqrt(negative) is NaN. */
    float resid = a->s_avg[k] - a->noise_floor;
    if (resid < 0.0f) {
      resid = 0.0f;
    }

    m0 += s_w_m0[i] * resid * WAVE_DF;
    m_minus1 += s_w_mm1[i] * resid * WAVE_DF;
  }

  if (!(m0 > 0.0f) || !isfinite(m0)) {
    /* Flat calm, or everything clamped away. Not an error. */
    return false;
  }

  out->hs = 4.0f * sqrtf(m0);
  out->h_one_tenth = WAVE_H_ONE_TENTH_RATIO * out->hs;

  /* Tm-1,0 rather than a peak-picked Tp: argmax hops between bins because each
   * bin is chi-square with 2 dof, giving sd 1.00 s at one segment, whereas this
   * energy-weighted mean lands in the same range with sd 0.57 s. */
  out->period = (isfinite(m_minus1) && m_minus1 > 0.0f) ? (m_minus1 / m0) : 0.0f;

  if (!isfinite(out->hs) || !isfinite(out->h_one_tenth)) {
    return false;
  }

  out->valid = true;
  return true;
}
