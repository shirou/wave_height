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

#include "../src/c/wave/gravity.h"
#include "test_pipeline.h"
#include "test_util.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- projection versus magnitude --------------------------------------- */

/* Mean fractional bias of one method over several seeds. Single realisations
 * scatter by more than ten percent, which is larger than the effect being
 * measured, so a one-seed comparison would be meaningless. */
static double mean_bias(const synth_config *base, vert_method method,
                        int trials) {
  double sum = 0.0;
  int got = 0;
  for (int i = 0; i < trials; i++) {
    synth_config cfg = *base;
    cfg.seed = 1u + (unsigned)i * 7919u;

    synth_t probe;
    synth_init(&probe, &cfg, WAVE_ACQ_RATE_HZ);
    const double truth = synth_true_hs(&probe);

    wave_result r;
    if (!pipeline_full(&cfg, 4, 0.0f, method, &r)) {
      continue;
    }
    sum += (double)r.hs / truth - 1.0;
    got++;
  }
  return (got > 0) ? (sum / got) : 0.0;
}

/*
 * Every input is pinned so the expected answer is a number rather than a range:
 * sea state, roll, horizontal amplitude, frequency, phase, segment count, and
 * the seed sequence. Leave any of them free and "the error ratio is below 1/5"
 * stops being something anyone can check.
 *
 * Horizontal sway at the bottom of the integration band is the discriminating
 * input. Under pure rotation |a| is exactly g, so the rejected magnitude method
 * looks flawless and the comparison proves nothing; it is the a_h^2/(2g) term
 * that exposes it. 0.0625 Hz is chosen because that is where the 1/(2*pi*f)^4
 * weighting is harshest.
 */
void test_gravity_diff(void) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 1.0f;
  cfg.tp = 6.0f;
  cfg.rayleigh_amp = false;
  cfg.freq_jitter_bins = 0.8f;

  cfg.roll_amp_deg = 0.0f;  /* rotation is covered by the roll test below */
  cfg.lever_arm_m = 0.0f;   /* and the lever arm by its own tests */

  cfg.horiz_rms_mg = 200.0f;
  cfg.horiz_freq_hz = 0.0625f;
  cfg.horiz_phase_rad = 0.0f;

  const double bias_p = mean_bias(&cfg, VERT_PROJECTION, 24);
  const double bias_m = mean_bias(&cfg, VERT_MAGNITUDE, 24);
  const double err_p = fabs(bias_p);
  const double err_m = fabs(bias_m);

  printf("      200 mG sway @ 0.0625 Hz: projection %+.2f%%, magnitude %+.2f%%\n",
         bias_p * 100.0, bias_m * 100.0);

  /* The shipped method has to be substantially better, not merely different. */
  CHECK(err_p * 5.0 <= err_m,
        "projection bias %.2f%% is not at least 5x smaller than magnitude bias "
        "%.2f%%",
        err_p * 100.0, err_m * 100.0);

  /* A ratio alone would also be satisfied by two methods that are both wrong,
   * so require absolute accuracy as well. */
  CHECK_LT(err_p, 0.10, "projection absolute bias");
}

/*
 * Roll is the one input where the magnitude method has an edge, since |a| does
 * not care which way the watch points while the projection picks up a cos
 * error. The point here is that the penalty stays small, not that projection
 * wins: 25 degrees of roll is already more than a small boat sees outside
 * resonance.
 */
void test_gravity_roll(void) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 1.0f;
  cfg.tp = 6.0f;
  cfg.freq_jitter_bins = 0.8f;
  cfg.roll_amp_deg = 25.0f;
  cfg.roll_period_s = 4.0f;
  cfg.roll_phase_rad = 0.0f;
  cfg.lever_arm_m = 0.0f;

  const double bias = mean_bias(&cfg, VERT_PROJECTION, 24);
  printf("      25 deg roll: projection %+.2f%%\n", bias * 100.0);
  CHECK_LT(fabs(bias), 0.12, "projection bias under heavy roll");
}

/*
 * Regression guard on the settle behaviour, which turned out to matter more than
 * the choice of method. Seeding the estimator with a fast IIR leaves it holding
 * the sway it absorbed while settling, and horizontal motion then leaks
 * directly into the projection -- that bug read +100% on this very input. The
 * averaging window nulls the offending frequency instead.
 */
void test_gravity_settle(void) {
  wave_gravity g;
  wave_gravity_init(&g, WAVE_ACQ_RATE_HZ);

  /* Level watch plus pure horizontal sway at the bottom of the band. */
  const double f = 0.0625;
  const double amp = 200.0 * sqrt(2.0);
  const int n = (int)(WAVE_ACQ_RATE_HZ * WAVE_G_SETTLE_S);

  for (int i = 0; i < n; i++) {
    const double t = (double)i / WAVE_ACQ_RATE_HZ;
    wave_vec3 a;
    a.x = (float)(amp * cos(2.0 * M_PI * f * t));
    a.y = 0.0f;
    a.z = 1000.0f;
    wave_gravity_push(&g, a);
  }

  CHECK(wave_gravity_converged(&g), "estimator did not settle within the window");

  /* The horizontal component the estimate carries away is what leaks. The sinc
   * null at 0.0625 Hz should keep it tiny; the old fast-IIR seeding left over
   * 100 mG here. */
  printf("      residual horizontal component after settle: %.1f mG "
         "(sway amplitude %.0f mG)\n",
         (double)fabsf(g.g_hat.x), amp);
  CHECK_LT(fabsf(g.g_hat.x), 15.0f, "residual horizontal component");

  /* Settling must not eat the vertical reference either. */
  CHECK_NEAR(g.g_hat.z, 1000.0f, 0.02, "vertical component after settle");
}

/* ---- lever arm --------------------------------------------------------- */

/* Expected Hs from roll alone: the measurement point moves r*sin(phi0)
 * sinusoidally, so m0 = (r*sin(phi0))^2/2 and Hs = 2*sqrt(2)*r*sin(phi0). */
static double leverarm_hs(double r_m, double phi_deg) {
  return 2.0 * sqrt(2.0) * r_m * sin(phi_deg * M_PI / 180.0);
}

/*
 * With no waves at all, whatever comes out is entirely the lever-arm artefact,
 * and it has a closed-form expected value. This is the test that pins the
 * artefact down; there is no version of the algorithm that removes it, because
 * the watch really is moving up and down.
 */
void test_leverarm_pure(void) {
  const double r = 1.5;    /* half-beam of a 3 m boat */
  const double phi = 15.0; /* degrees, single amplitude */

  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 0.0f; /* no waves */
  cfg.tp = 6.0f;
  cfg.seed = 555u;
  cfg.roll_amp_deg = (float)phi;
  cfg.roll_period_s = 4.0f;
  cfg.roll_phase_rad = 0.0f;
  cfg.lever_arm_m = (float)r;

  wave_result res;
  const bool ok = pipeline_full(&cfg, 4, 0.0f, VERT_PROJECTION, &res);
  CHECK(ok, "lever arm alone produced no result");
  if (!ok) {
    return;
  }

  const double expected = leverarm_hs(r, phi);
  printf("      roll-only Hs = %.4f m (expected %.4f)\n", res.hs, expected);
  CHECK_NEAR(res.hs, expected, 0.10, "lever-arm-only Hs");
}

/*
 * Waves and roll together. The two are statistically independent, so their
 * variances add, and the overpredicted Hs follows. At Hs = 0.5 m with a 1.5 m
 * lever arm and 15 degrees of roll the reading is 2.41x the truth -- the number
 * that drove the decision to move the measurement position off the gunwale.
 */
void test_leverarm_mixed(void) {
  const double r = 1.5;
  const double phi = 15.0;
  const double hs_wave = 0.5;

  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = (float)hs_wave;
  cfg.tp = 6.0f;
  cfg.seed = 777u;
  cfg.roll_amp_deg = (float)phi;
  cfg.roll_period_s = 4.0f;
  cfg.roll_phase_rad = 0.0f;
  cfg.lever_arm_m = (float)r;

  wave_result res;
  const bool ok = pipeline_full(&cfg, 4, 0.0f, VERT_PROJECTION, &res);
  CHECK(ok, "mixed lever arm case produced no result");
  if (!ok) {
    return;
  }

  const double m0_wave = (hs_wave / 4.0) * (hs_wave / 4.0);
  const double a_lever = r * sin(phi * M_PI / 180.0);
  const double m0_total = m0_wave + a_lever * a_lever / 2.0;
  const double expected = 4.0 * sqrt(m0_total);

  printf("      wave %.2f m + roll -> %.4f m (expected %.4f, %.2fx truth)\n",
         hs_wave, res.hs, expected, res.hs / hs_wave);

  CHECK_NEAR(res.hs, expected, 0.15, "wave plus lever-arm Hs");

  /* Guard the documented figure itself: if this ever stops being roughly 2.4x,
   * the limitation section in the plan and README needs revisiting. */
  CHECK_IN_RANGE(res.hs / hs_wave, 2.0, 2.9, "overprediction factor");
}
