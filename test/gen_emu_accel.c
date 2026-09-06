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

/*
 * Produce a synthetic sea as comma-separated x,y,z milli-g, for
 *
 *   pebble emu-accel custom --file <output>
 *
 * The 1 g static component IS included. Without it the gravity estimate has
 * near-zero magnitude, projection refuses to run, and the app sits in
 * "Levelling" forever -- an easy way to spend an afternoon debugging the app
 * when the fault is in the stimulus.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "synth.h"

static void usage(const char *prog) {
  fprintf(stderr,
          "usage: %s [options] > accel.csv\n"
          "  --hs <m>        significant wave height (default 1.0)\n"
          "  --tp <s>        peak period (default 6.0)\n"
          "  --seconds <s>   duration (default 120)\n"
          "  --roll <deg>    roll amplitude (default 0)\n"
          "  --lever <m>     lever arm from the roll axis (default 0)\n"
          "  --sway <mG>     horizontal acceleration, rms (default 0)\n"
          "  --noise <mG>    sensor noise sigma (default 0)\n"
          "  --motion        add a hand-motion burst midway through\n"
          "  --seed <n>      RNG seed (default 1)\n",
          prog);
}

int main(int argc, char **argv) {
  synth_config cfg;
  synth_default_config(&cfg);
  cfg.hs = 1.0f;
  cfg.tp = 6.0f;
  cfg.quantize_1mg = true;
  cfg.seed = 1u;

  double seconds = 120.0;
  bool motion = false;

  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    const bool has_val = (i + 1 < argc);
    if (!strcmp(a, "--hs") && has_val) {
      cfg.hs = (float)atof(argv[++i]);
    } else if (!strcmp(a, "--tp") && has_val) {
      cfg.tp = (float)atof(argv[++i]);
    } else if (!strcmp(a, "--seconds") && has_val) {
      seconds = atof(argv[++i]);
    } else if (!strcmp(a, "--roll") && has_val) {
      cfg.roll_amp_deg = (float)atof(argv[++i]);
    } else if (!strcmp(a, "--lever") && has_val) {
      cfg.lever_arm_m = (float)atof(argv[++i]);
    } else if (!strcmp(a, "--sway") && has_val) {
      cfg.horiz_rms_mg = (float)atof(argv[++i]);
      cfg.horiz_freq_hz = 0.0625f;
    } else if (!strcmp(a, "--noise") && has_val) {
      cfg.noise_sigma_mg = (float)atof(argv[++i]);
    } else if (!strcmp(a, "--seed") && has_val) {
      cfg.seed = (unsigned)atoi(argv[++i]);
    } else if (!strcmp(a, "--motion")) {
      motion = true;
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  if (motion) {
    cfg.burst_amp_mg = 400.0f;
    cfg.burst_freq_hz = 2.5f;
    cfg.burst_start_s = (float)(seconds * 0.5);
    cfg.burst_end_s = (float)(seconds * 0.5 + 30.0);
  }

  synth_t s;
  synth_init(&s, &cfg, WAVE_ACQ_RATE_HZ);

  fprintf(stderr, "true Hs = %.3f m, Tp = %.1f s, %.0f s at %.0f Hz\n",
          synth_true_hs(&s), cfg.tp, seconds, WAVE_ACQ_RATE_HZ);

  const long n = (long)(seconds * WAVE_ACQ_RATE_HZ);
  for (long i = 0; i < n; i++) {
    wave_accel_sample smp;
    synth_next(&s, &smp);
    printf("%d,%d,%d\n", smp.x, smp.y, smp.z);
  }
  return 0;
}
