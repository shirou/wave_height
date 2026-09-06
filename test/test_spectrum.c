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

#include "../src/c/wave/decimate.h"
#include "../src/c/wave/spectrum.h"
#include "test_pipeline.h"
#include "test_util.h"

/* ---- detrend ----------------------------------------------------------- */

void test_detrend(void) {
  /* A pure ramp must be annihilated. */
  {
    float x[WAVE_SEG_SAMPLES];
    for (int i = 0; i < WAVE_SEG_SAMPLES; i++) {
      x[i] = 3.0f + 0.25f * (float)i;
    }
    wave_detrend(x, WAVE_SEG_SAMPLES);
    double worst = 0.0;
    for (int i = 0; i < WAVE_SEG_SAMPLES; i++) {
      if (fabs((double)x[i]) > worst) {
        worst = fabs((double)x[i]);
      }
    }
    CHECK_LT(worst, 1e-4, "residual of a pure ramp");
  }

  /* Detrending is linear, so adding a ramp to a signal and then detrending must
   * land on exactly the same place as detrending the signal alone. That is the
   * property we actually depend on.
   *
   * Note what is NOT true: detrending does not leave a sinusoid untouched, even
   * one with a whole number of cycles in the window. The least-squares slope of
   * sin(2*pi*k*i/N) over i = 0..N-1 is not zero, so a small ramp is always
   * subtracted. Asserting that the peak amplitude survives unchanged would be
   * asserting something false. */
  {
    float with_ramp[WAVE_SEG_SAMPLES];
    float without[WAVE_SEG_SAMPLES];
    const double amp = 2.0;
    for (int i = 0; i < WAVE_SEG_SAMPLES; i++) {
      const double v = amp * sin(2.0 * 3.14159265358979 * 4.0 * i /
                                 (double)WAVE_SEG_SAMPLES);
      without[i] = (float)v;
      with_ramp[i] = (float)(v + 10.0 + 0.5 * i);
    }
    wave_detrend(with_ramp, WAVE_SEG_SAMPLES);
    wave_detrend(without, WAVE_SEG_SAMPLES);
    double worst = 0.0;
    for (int i = 0; i < WAVE_SEG_SAMPLES; i++) {
      const double d = fabs((double)with_ramp[i] - (double)without[i]);
      if (d > worst) {
        worst = d;
      }
    }
    CHECK_LT(worst, 1e-3, "detrend linearity (ramp must not change the result)");
  }

  /* The reason detrending is mandatory: a drift of 20 mG across one segment
   * fabricates about 0.157 m of Hs if it is left in place. Verify both that the
   * drift really does that, and that detrending removes it. */
  {
    synth_config cfg;
    synth_default_config(&cfg);
    cfg.hs = 0.0f; /* no waves at all */
    cfg.tp = 6.0f;
    cfg.trend_mg_per_segment = 20.0f;

    wave_result r;
    const bool ok = pipeline_direct(&cfg, 1, 0.0f, &r);
    /* wave_spectrum_segment always detrends, so what comes out here is the
     * residual after removal. */
    CHECK(!ok || r.hs < 0.03f,
          "drift of 20 mG/segment left %.4f m of Hs after detrending "
          "(want < 0.03)",
          ok ? r.hs : 0.0f);
  }
}

/* ---- spectrum ---------------------------------------------------------- */

static void check_sea(float hs, float tp, float tol, const char *label) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = hs;
  cfg.tp = tp;
  cfg.seed = 4242u;

  synth_t probe;
  synth_init(&probe, &cfg, WAVE_PROC_RATE_HZ);
  const float truth = synth_true_hs(&probe);

  wave_result r;
  const bool ok = pipeline_direct(&cfg, 4, 0.0f, &r);
  CHECK(ok, "%s: no result", label);
  if (!ok) {
    return;
  }
  CHECK_NEAR(r.hs, truth, tol, label);

  /* Tm-1,0 lands near Tp for a JONSWAP sea, running a little low by
   * construction since it is an energy-weighted mean rather than a peak. Near
   * the low edge of the band it runs lower still: the spectrum below bin 2 is
   * simply not there to weight, so the mean is dragged toward higher
   * frequencies. */
  const double lo = (tp >= 9.0f) ? 0.6 : 0.8;
  CHECK_IN_RANGE(r.period, tp * lo, tp * 1.15, "Tm-1,0 near Tp");
}

void test_spectrum(void) {
  /* Periods well inside the integration band: the estimator should be nearly
   * exact, because synthesis and integration use the same bins. */
  check_sea(0.5f, 4.0f, 0.10f, "Hs=0.5 Tp=4");
  check_sea(1.0f, 4.0f, 0.10f, "Hs=1.0 Tp=4");
  check_sea(2.0f, 4.0f, 0.10f, "Hs=2.0 Tp=4");
  check_sea(0.5f, 6.0f, 0.10f, "Hs=0.5 Tp=6");
  check_sea(1.0f, 6.0f, 0.10f, "Hs=1.0 Tp=6");
  check_sea(2.0f, 6.0f, 0.10f, "Hs=2.0 Tp=6");
}

void test_spectrum_longperiod(void) {
  /* At Tp >= 10 s the peak sits near bin 3 and Hann leakage spills into bin 2,
   * whose 1/(2*pi*f)^4 weight is 3.2x larger, so Hs reads high. This is a known
   * systematic error, documented in the plan; the test pins it down rather than
   * pretending it is absent. A plain +/-10% band here would reject a correct
   * implementation. */
  check_sea(1.0f, 10.0f, 0.20f, "Hs=1.0 Tp=10 (known +8% leakage bias)");
  check_sea(1.0f, 12.0f, 0.20f, "Hs=1.0 Tp=12 (known +8% leakage bias)");
}

void test_spectrum_ensemble(void) {
  /* Rayleigh amplitudes are the physically realistic case, and with them a
   * single segment is genuinely noisy. The criterion has to be on the ensemble:
   * unbiased in the mean, with a spread that matches theory. */
  const int trials = 50;
  double sum = 0.0;
  double sum_sq = 0.0;
  double truth_sum = 0.0;
  int got = 0;

  for (int i = 0; i < trials; i++) {
    synth_config cfg;
    synth_default_config(&cfg);
    cfg.hs = 1.0f;
    cfg.tp = 6.0f;
    cfg.rayleigh_amp = true;
    cfg.seed = 1000u + (unsigned)i * 77u;

    /* With Rayleigh amplitudes the realised Hs differs from the requested one,
     * so the comparison has to be against what was actually generated. Using the
     * nominal 1.0 m instead would charge the estimator for the generator's own
     * spread, which is about -7% in the mean. */
    synth_t probe;
    synth_init(&probe, &cfg, WAVE_PROC_RATE_HZ);
    truth_sum += synth_true_hs(&probe);

    wave_result r;
    if (!pipeline_direct(&cfg, 1, 0.0f, &r)) {
      continue;
    }
    sum += r.hs;
    sum_sq += (double)r.hs * r.hs;
    got++;
  }
  const double hs_true = truth_sum / trials;

  CHECK(got >= trials - 2, "only %d/%d realisations produced a result", got,
        trials);
  if (got < 5) {
    return;
  }

  const double mean = sum / got;
  const double var = sum_sq / got - mean * mean;
  const double sd = (var > 0.0) ? sqrt(var) : 0.0;

  printf("      ensemble: true %.3f m, mean %.3f m (%+.1f%%), sd %.1f%% "
         "over %d realisations\n",
         hs_true, mean, (mean / hs_true - 1.0) * 100.0, sd / mean * 100.0, got);

  /* A single 32 s segment is genuinely noisy, and 4*sqrt(m0) is concave, so a
   * few percent of low bias is expected rather than a defect. What would be a
   * defect is a large bias or a spread far from theory. */
  CHECK_NEAR(mean, hs_true, 0.10, "ensemble mean Hs");
  CHECK_IN_RANGE(sd / mean, 0.15, 0.40, "ensemble relative sd");
}

/* ---- noise floor ------------------------------------------------------- */

/* Sensor noise as it appears AT THE PROCESSING RATE.
 *
 * The plan's assumed figure is 2.9 mG at the 10 Hz acquisition rate; the 5-point
 * boxcar that decimates to 2 Hz divides that by sqrt(5). Since this test injects
 * its signal directly at 2 Hz, bypassing decimation, it has to use the reduced
 * figure -- feeding 2.9 mG here would model a sensor sqrt(5) times noisier than
 * the one we assumed and make the floor look far worse than it is. */
#define TEST_NOISE_SIGMA_MG (2.9f / 2.2360679f)

/* Run the still-watch case through the very same chain to obtain the floor
 * constant, exactly as the on-device calibration step is specified to do. */
static float calibrate_noise_floor(void) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 0.0f;
  cfg.tp = 6.0f;
  cfg.noise_sigma_mg = TEST_NOISE_SIGMA_MG;
  cfg.quantize_1mg = true;
  cfg.seed = 999u;

  synth_t s;
  synth_init(&s, &cfg, WAVE_PROC_RATE_HZ);

  /* Average many segments so the constant itself is well determined. */
  const int nseg = 32;
  double acc[WAVE_NBINS];
  for (int k = 0; k < WAVE_NBINS; k++) {
    acc[k] = 0.0;
  }

  float seg[WAVE_SEG_SAMPLES];
  float psd[WAVE_NBINS];
  for (int n = 0; n < nseg; n++) {
    for (int i = 0; i < WAVE_SEG_SAMPLES; i++) {
      wave_accel_sample smp;
      synth_next(&s, &smp);
      seg[i] = ((float)smp.z - 1000.0f) * WAVE_MG_TO_MS2;
    }
    wave_spectrum_segment_ex(seg, psd, false);
    for (int k = 0; k < WAVE_NBINS; k++) {
      acc[k] += psd[k];
    }
  }

  /* Median of the in-band values, as specified. */
  double band[WAVE_BIN_HI - WAVE_BIN_LO + 1];
  int m = 0;
  for (int k = WAVE_BIN_LO; k <= WAVE_BIN_HI; k++) {
    band[m++] = acc[k] / nseg;
  }
  for (int i = 1; i < m; i++) {
    const double v = band[i];
    int j = i - 1;
    while (j >= 0 && band[j] > v) {
      band[j + 1] = band[j];
      j--;
    }
    band[j + 1] = v;
  }
  return (float)band[m / 2];
}

/* Mean apparent Hs of a still watch over several noise realisations. A single
 * draw scatters by tens of percent, which would make the acceptance bands below
 * either meaningless or flaky. */
static double mean_noise_hs(float noise_floor, int trials, bool *all_finite) {
  double sum = 0.0;
  *all_finite = true;
  for (int i = 0; i < trials; i++) {
    synth_config cfg;
    synth_default_config(&cfg);
    cfg.hs = 0.0f;
    cfg.tp = 6.0f;
    cfg.noise_sigma_mg = TEST_NOISE_SIGMA_MG;
    cfg.quantize_1mg = true;
    cfg.seed = 31337u + (unsigned)i * 101u; /* distinct from the calibration draw */

    wave_result r;
    const bool ok = pipeline_direct(&cfg, 4, noise_floor, &r);
    const double hs = ok ? (double)r.hs : 0.0;
    if (!isfinite(hs)) {
      *all_finite = false;
      continue;
    }
    sum += hs;
  }
  return sum / trials;
}

/* Theoretical apparent Hs from white noise of this sigma, integrated over the
 * band with the plain 1/(2*pi*f)^4 weights: about 0.069 m. */
#define TEST_NOISE_HS_THEORY 0.069

void test_noise_floor_raw(void) {
  /* Without subtraction the still watch must show a definite, non-zero apparent
   * Hs, close to what the noise power predicts. If this came out near zero the
   * whole exercise would be vacuous and the calibrated case below would prove
   * nothing whatsoever. */
  bool finite = true;
  const double hs = mean_noise_hs(0.0f, 20, &finite);
  printf("      mean apparent Hs with N_floor=0: %.4f m (theory %.3f)\n", hs,
         TEST_NOISE_HS_THEORY);
  CHECK(finite, "uncalibrated noise case produced a non-finite Hs");
  CHECK_IN_RANGE(hs, TEST_NOISE_HS_THEORY * 0.5, TEST_NOISE_HS_THEORY * 2.0,
                 "apparent Hs from sensor noise alone");
}

void test_noise_floor_calib(void) {
  const float nf = calibrate_noise_floor();

  bool finite_raw = true;
  bool finite_cal = true;
  const double raw = mean_noise_hs(0.0f, 20, &finite_raw);
  const double cal = mean_noise_hs(nf, 20, &finite_cal);

  printf("      N_floor = %.3e (m/s^2)^2/Hz; Hs %.4f -> %.4f m (%.0f%% of raw)\n",
         nf, raw, cal, cal / raw * 100.0);

  /* The calibration constant itself must match the noise power. Getting the
   * units wrong -- storing a sigma in mG rather than a spectral density -- would
   * be off by orders of magnitude and is caught here. */
  const double expected_nf =
      pow((double)TEST_NOISE_SIGMA_MG * (double)WAVE_MG_TO_MS2, 2.0) /
      (WAVE_PROC_RATE_HZ / 2.0);
  CHECK_NEAR(nf, expected_nf, 0.30, "N_floor matches the noise PSD");

  /* The subtraction has to actually do something. Forgetting to apply N_floor
   * would leave this equal to the raw case, which is the failure this test
   * exists to catch. It cannot reach zero, though: the per-bin clamp keeps
   * positive excursions of the noise while discarding negative ones, so a
   * sizeable fraction survives by construction. */
  CHECK_LT(cal, raw * 0.8, "subtraction reduces the apparent Hs");
  CHECK_LT(cal, 0.10, "residual Hs after floor subtraction");
  CHECK(finite_raw && finite_cal,
        "Hs stayed finite (NaN would mean m0 went negative)");
}

/* ---- leakage-corrected weights ----------------------------------------- */

/* Hs from the same averaged spectrum, but with the plain 1/(2*pi*f)^4 weights
 * the correction replaces. Lets a test compare the two on identical data. */
static double hs_with_plain_weights(const wave_accumulator *a) {
  double m0 = 0.0;
  for (int k = WAVE_BIN_LO; k <= WAVE_BIN_HI; k++) {
    const double f = (double)k * WAVE_DF;
    const double w = 2.0 * 3.14159265358979 * f;
    double resid = (double)a->s_avg[k] - (double)a->noise_floor;
    if (resid < 0.0) {
      resid = 0.0;
    }
    m0 += resid / (w * w * w * w) * WAVE_DF;
  }
  return (m0 > 0.0) ? 4.0 * sqrt(m0) : 0.0;
}

/*
 * The deconvolved integration weights have to earn their keep.
 *
 * Absolute accuracy bands cannot show this: both weightings land inside a +/-10%
 * band for short periods, so a test built on one would pass with the correction
 * deleted. The discriminating case is long periods, where the Hann leakage falls
 * into bin 2 and gets multiplied by a weight 5x larger than its neighbour's, and
 * the discriminating form is a difference test on identical spectra.
 *
 * Rayleigh amplitudes and several segments are used because that is the regime
 * the correction is derived for: it assumes the leakage decorrelates between
 * segments, which is true of a real sea and not of a single deterministic one.
 */
static void check_leakage_correction(float tp, int nseg, int trials) {
  double err_corrected = 0.0;
  double err_plain = 0.0;
  int got = 0;

  for (int i = 0; i < trials; i++) {
    synth_config cfg;
    synth_default_config(&cfg);
    cfg.hs = 1.0f;
    cfg.tp = tp;
    cfg.rayleigh_amp = true;
    cfg.freq_jitter_bins = 0.8f;
    cfg.seed = 11u + (unsigned)i * 5077u;

    synth_t s;
    synth_init(&s, &cfg, WAVE_PROC_RATE_HZ);
    const double truth = synth_true_hs(&s);

    wave_accumulator acc;
    wave_accum_init(&acc, 0.0f);
    float seg[WAVE_SEG_SAMPLES];
    float psd[WAVE_NBINS];
    for (int n = 0; n < nseg; n++) {
      for (int j = 0; j < WAVE_SEG_SAMPLES; j++) {
        wave_accel_sample smp;
        synth_next(&s, &smp);
        seg[j] = ((float)smp.z - 1000.0f) * WAVE_MG_TO_MS2;
      }
      wave_spectrum_segment_ex(seg, psd, false);
      wave_accum_add(&acc, psd);
    }

    wave_result r;
    if (!wave_accum_result(&acc, &r)) {
      continue;
    }
    err_corrected += (double)r.hs / truth - 1.0;
    err_plain += hs_with_plain_weights(&acc) / truth - 1.0;
    got++;
  }

  CHECK(got > trials / 2, "Tp=%.0f: too few realisations produced a result", tp);
  if (got == 0) {
    return;
  }
  const double bias_c = fabs(err_corrected / got);
  const double bias_p = fabs(err_plain / got);

  printf("      Tp=%4.1f: corrected %+.1f%%, plain %+.1f%%\n", tp,
         err_corrected / got * 100.0, err_plain / got * 100.0);

  CHECK(bias_c * 2.0 <= bias_p,
        "Tp=%.0f: corrected bias %.1f%% is not at least 2x smaller than plain "
        "%.1f%% -- the correction is not earning its complexity",
        tp, bias_c * 100.0, bias_p * 100.0);
  CHECK_LT(bias_c, 0.10, "corrected absolute bias");
}

void test_leakage_correction(void) {
  check_leakage_correction(10.0f, 4, 24);
  check_leakage_correction(12.0f, 4, 24);
}

/* ---- decimation droop -------------------------------------------------- */

/*
 * The boxcar that decimates to 2 Hz droops in band, and spectrum.c divides it
 * back out. Verified two ways, because either alone would let the compensation
 * be deleted unnoticed: the gain itself against hand-computed values, and the
 * ratio between compensated and uncompensated spectra of the same segment.
 */
void test_droop(void) {
  /* sin(m*d)/(m*sin(d)) with d = pi*f/f_acq, squared. */
  CHECK_NEAR(wave_decim_droop_gain2(0.0625f), 0.99693, 0.001, "droop at 0.0625 Hz");
  CHECK_NEAR(wave_decim_droop_gain2(0.25f), 0.95158, 0.001, "droop at 0.25 Hz");
  CHECK_NEAR(wave_decim_droop_gain2(0.5f), 0.81729, 0.001, "droop at 0.5 Hz");

  /* Never returns something a caller could divide by to infinity. */
  CHECK(wave_decim_droop_gain2(0.0f) > 0.0f, "gain at DC must be positive");
  for (int k = 1; k < WAVE_NBINS; k++) {
    CHECK(wave_decim_droop_gain2((float)k * WAVE_DF) > 1e-5f,
          "gain at bin %d must not be near zero", k);
  }

  /* And the compensation is actually applied. */
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 1.0f;
  cfg.tp = 3.0f; /* energy up near the top of the band, where droop bites */
  cfg.seed = 606u;

  synth_t s;
  synth_init(&s, &cfg, WAVE_PROC_RATE_HZ);
  float seg[WAVE_SEG_SAMPLES];
  for (int i = 0; i < WAVE_SEG_SAMPLES; i++) {
    wave_accel_sample smp;
    synth_next(&s, &smp);
    seg[i] = ((float)smp.z - 1000.0f) * WAVE_MG_TO_MS2;
  }

  float compensated[WAVE_NBINS];
  float raw[WAVE_NBINS];
  wave_spectrum_segment(seg, compensated);
  wave_spectrum_segment_ex(seg, raw, false);

  for (int k = WAVE_BIN_LO; k <= WAVE_BIN_HI; k++) {
    const float g2 = wave_decim_droop_gain2((float)k * WAVE_DF);
    CHECK_NEAR(compensated[k], raw[k] / g2, 1e-3,
               "compensated spectrum equals raw divided by the droop gain");
  }

  /* At the top of the band the correction is worth over 20%, so deleting it
   * would be a visible change rather than a rounding difference. */
  const float top = wave_decim_droop_gain2(WAVE_BIN_HI * WAVE_DF);
  CHECK(1.0f / top > 1.2f, "droop correction at the band edge should exceed 20%%");
}
