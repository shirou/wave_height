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

#ifndef SYNTH_H
#define SYNTH_H

#include "../src/c/wave/wave_types.h"

/*
 * Synthetic sea-state generator: the only input whose true Hs is known, and
 * therefore the foundation of every test below level 4.
 *
 * Amplitudes are DETERMINISTIC, set to sqrt(2*S(f_k)*df), with randomness only
 * in the phases, and components placed exactly on FFT bin centres. This matters:
 * with Rayleigh-distributed amplitudes (the physically realistic choice) a
 * single 32 s segment estimates Hs with a relative sd of about 22%, so a +/-10%
 * acceptance band would reject a perfectly correct implementation roughly two
 * times in three. With deterministic amplitudes Parseval makes m0 exact and the
 * band is easy to meet. Set `rayleigh_amp` for the ensemble test, where the
 * acceptance criterion is on the mean and sd over many realisations instead.
 */

typedef struct {
  /* Sea state. */
  float hs;    /* target significant wave height, m */
  float tp;    /* peak period, s */
  bool rayleigh_amp; /* false (default): deterministic amplitudes */
  unsigned seed;

  /* Restrict synthesis to the estimator's integration band. Default true, so
   * that the true Hs and the estimated Hs refer to the same band and any
   * mismatch is the estimator's fault. Set false to exercise band truncation. */
  bool band_limited;

  /* Scatter component frequencies away from exact bin centres, in bins.
   *
   * With components pinned to bin centres every 32 s segment sees the same
   * phases, so window leakage adds coherently and never averages out. A real
   * sea does not do that: the components are at arbitrary frequencies, so their
   * phase relative to the segment boundary drifts and the leakage decorrelates.
   * Default 0.8 (+/-0.4 bin). Set to 0 to reproduce the pathological coherent
   * case on purpose. */
  float freq_jitter_bins;

  /* Roll. Attitude rotation is applied with the full phi(t); the lever-arm
   * displacement is modelled as a pure sinusoid of amplitude r*sin(phi0) so that
   * the expected Hs contribution is exactly 2*sqrt(2)*r*sin(phi0) and the test
   * has a closed-form answer. */
  float roll_amp_deg;
  float roll_period_s;
  float roll_phase_rad;
  float lever_arm_m; /* distance from the roll axis, metres */

  /* Horizontal acceleration, to probe how much leaks through the projection. */
  float horiz_rms_mg;
  float horiz_freq_hz;
  float horiz_phase_rad;

  /* Linear drift, expressed over one 32 s segment. */
  float trend_mg_per_segment;

  /* Sensor imperfections. */
  float noise_sigma_mg;
  bool quantize_1mg;
  float clip_mg; /* 0 disables clipping */

  /* Body-motion burst: a band of high-frequency energy switched on between
   * these times, used to check that the quality gate fires. */
  float burst_start_s;
  float burst_end_s;
  float burst_amp_mg;
  float burst_freq_hz;
} synth_config;

#define SYNTH_MAX_COMPONENTS WAVE_NBINS

typedef struct {
  synth_config cfg;
  float sample_rate;
  double t;
  unsigned rng;

  int n_comp;
  float freq[SYNTH_MAX_COMPONENTS];  /* Hz */
  float amp[SYNTH_MAX_COMPONENTS];   /* displacement amplitude, m */
  float phase[SYNTH_MAX_COMPONENTS]; /* rad */

  float true_m0; /* exact, from Parseval over the synthesised components */
} synth_t;

void synth_default_config(synth_config *cfg);

void synth_init(synth_t *s, const synth_config *cfg, float sample_rate_hz);

/* True Hs of what is actually being generated (wave components only, excluding
 * roll lever arm, drift and noise). */
float synth_true_hs(const synth_t *s);

/* Produce the next three-axis accelerometer reading in milli-g, as the SDK
 * would report it. */
void synth_next(synth_t *s, wave_accel_sample *out);

#endif /* SYNTH_H */
