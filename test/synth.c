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

#include "synth.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SYNTH_G_MG 1000.0

/* Deterministic LCG so that every run reproduces the same sea. */
static double rng_uniform(unsigned *state) {
  *state = (*state) * 1103515245u + 12345u;
  return (double)((*state >> 16) & 0x7fff) / 32768.0;
}

static double rng_gauss(unsigned *state) {
  double u1 = rng_uniform(state);
  const double u2 = rng_uniform(state);
  if (u1 < 1e-12) {
    u1 = 1e-12;
  }
  return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

/* Unnormalised JONSWAP shape; the caller rescales to hit the requested Hs. */
static double jonswap_shape(double f, double fp) {
  if (f <= 0.0) {
    return 0.0;
  }
  const double gamma = 3.3;
  const double sigma = (f <= fp) ? 0.07 : 0.09;
  const double d = f - fp;
  const double r = exp(-(d * d) / (2.0 * sigma * sigma * fp * fp));
  const double base = pow(f, -5.0) * exp(-1.25 * pow(fp / f, 4.0));
  return base * pow(gamma, r);
}

void synth_default_config(synth_config *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->hs = 1.0f;
  cfg->tp = 6.0f;
  cfg->seed = 12345u;
  cfg->band_limited = true;
  cfg->freq_jitter_bins = 0.8f;
  cfg->roll_period_s = 4.0f;
}

void synth_init(synth_t *s, const synth_config *cfg, float sample_rate_hz) {
  memset(s, 0, sizeof(*s));
  s->cfg = *cfg;
  s->sample_rate = sample_rate_hz;
  s->rng = cfg->seed ? cfg->seed : 1u;
  s->t = 0.0;

  const int k_lo = cfg->band_limited ? WAVE_BIN_LO : 1;
  const int k_hi = cfg->band_limited ? WAVE_BIN_HI : (WAVE_NBINS - 1);
  const double fp = (cfg->tp > 0.0f) ? (1.0 / cfg->tp) : 0.1667;

  /* Two passes: shape first, then scale so that the band integral gives
   * exactly the requested Hs. */
  double shape[SYNTH_MAX_COMPONENTS];
  double m0_raw = 0.0;
  for (int k = k_lo; k <= k_hi; k++) {
    const double f = (double)k * WAVE_DF;
    shape[k] = jonswap_shape(f, fp);
    m0_raw += shape[k] * WAVE_DF;
  }
  if (m0_raw <= 0.0) {
    return;
  }
  const double target_m0 = ((double)cfg->hs / 4.0) * ((double)cfg->hs / 4.0);
  const double scale = target_m0 / m0_raw;

  double m0_actual = 0.0;
  s->n_comp = 0;
  for (int k = k_lo; k <= k_hi; k++) {
    const double f = (double)k * WAVE_DF;
    const double s_k = shape[k] * scale;
    double amp = sqrt(2.0 * s_k * WAVE_DF);

    if (cfg->rayleigh_amp) {
      /* Rayleigh amplitude with unit mean square, i.e. the realistic case. */
      double u = rng_uniform(&s->rng);
      if (u < 1e-12) {
        u = 1e-12;
      }
      amp *= sqrt(-log(u));
    }

    double f_actual = f;
    if (cfg->freq_jitter_bins > 0.0f) {
      f_actual += ((rng_uniform(&s->rng) - 0.5) * (double)cfg->freq_jitter_bins) *
                  WAVE_DF;
    }
    s->freq[s->n_comp] = (float)f_actual;
    s->amp[s->n_comp] = (float)amp;
    s->phase[s->n_comp] = (float)(2.0 * M_PI * rng_uniform(&s->rng));
    m0_actual += 0.5 * amp * amp;
    s->n_comp++;
  }
  s->true_m0 = (float)m0_actual;
}

float synth_true_hs(const synth_t *s) {
  return 4.0f * sqrtf(s->true_m0);
}

void synth_next(synth_t *s, wave_accel_sample *out) {
  const double t = s->t;
  const synth_config *c = &s->cfg;

  /* Wave-induced vertical acceleration, m/s^2. Differentiating the displacement
   * sum analytically keeps this exact; numerical differentiation would inject
   * its own high-frequency noise. */
  double a_z_ms2 = 0.0;
  for (int i = 0; i < s->n_comp; i++) {
    const double w = 2.0 * M_PI * (double)s->freq[i];
    a_z_ms2 += -(double)s->amp[i] * w * w *
               cos(w * t + (double)s->phase[i]);
  }

  /* Roll attitude. */
  double phi = 0.0;
  if (c->roll_amp_deg != 0.0f && c->roll_period_s > 0.0f) {
    const double wr = 2.0 * M_PI / (double)c->roll_period_s;
    phi = (double)c->roll_amp_deg * M_PI / 180.0 *
          sin(wr * t + (double)c->roll_phase_rad);
  }

  /* Lever-arm heave. Modelled as a pure sinusoid of amplitude r*sin(phi0) so
   * that the expected contribution is exactly 2*sqrt(2)*r*sin(phi0) and the
   * test has a closed-form expectation. The attitude rotation above still uses
   * the full phi(t). */
  if (c->lever_arm_m != 0.0f && c->roll_amp_deg != 0.0f &&
      c->roll_period_s > 0.0f) {
    const double wr = 2.0 * M_PI / (double)c->roll_period_s;
    const double z_amp =
        (double)c->lever_arm_m * sin((double)c->roll_amp_deg * M_PI / 180.0);
    a_z_ms2 += -z_amp * wr * wr * sin(wr * t + (double)c->roll_phase_rad);
  }

  double a_z_mg = a_z_ms2 / (double)WAVE_MG_TO_MS2;

  /* Linear drift, specified per 32 s segment. */
  if (c->trend_mg_per_segment != 0.0f) {
    a_z_mg += (double)c->trend_mg_per_segment * (t / 32.0);
  }

  /* Body-motion burst. */
  if (c->burst_amp_mg != 0.0f && t >= (double)c->burst_start_s &&
      t < (double)c->burst_end_s) {
    a_z_mg += (double)c->burst_amp_mg *
              sin(2.0 * M_PI * (double)c->burst_freq_hz * t);
  }

  /* Horizontal acceleration; rms -> amplitude for a sinusoid. */
  double a_h_mg = 0.0;
  if (c->horiz_rms_mg != 0.0f) {
    a_h_mg = (double)c->horiz_rms_mg * sqrt(2.0) *
             cos(2.0 * M_PI * (double)c->horiz_freq_hz * t +
                 (double)c->horiz_phase_rad);
  }

  /* Specific force in the world frame, then rotated into the watch frame about
   * the roll axis. At rest and level this reads +1 g on Z, matching what an
   * accelerometer reports. */
  const double fz_world = a_z_mg + SYNTH_G_MG;
  double ax = a_h_mg;
  double ay = sin(phi) * fz_world;
  double az = cos(phi) * fz_world;

  if (c->noise_sigma_mg != 0.0f) {
    ax += (double)c->noise_sigma_mg * rng_gauss(&s->rng);
    ay += (double)c->noise_sigma_mg * rng_gauss(&s->rng);
    az += (double)c->noise_sigma_mg * rng_gauss(&s->rng);
  }

  if (c->clip_mg > 0.0f) {
    const double lim = (double)c->clip_mg;
    if (ax > lim) ax = lim;
    if (ax < -lim) ax = -lim;
    if (ay > lim) ay = lim;
    if (ay < -lim) ay = -lim;
    if (az > lim) az = lim;
    if (az < -lim) az = -lim;
  }

  /* The SDK reports whole milli-g, so rounding is part of the real signal chain.
   * It is off unless a test asks for it, which keeps the accuracy tests measuring
   * the estimator rather than the sensor; the noise-floor and false-positive
   * tests, where quantisation matters, switch it on explicitly. */
  if (c->quantize_1mg) {
    ax = floor(ax + 0.5);
    ay = floor(ay + 0.5);
    az = floor(az + 0.5);
  }

  out->x = (int16_t)ax;
  out->y = (int16_t)ay;
  out->z = (int16_t)az;
  out->did_vibrate = false;

  s->t += 1.0 / (double)s->sample_rate;
}
