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

#include "test_pipeline.h"

#include <math.h>

#include "../src/c/wave/decimate.h"
#include "../src/c/wave/gravity.h"
#include "../src/c/wave/spectrum.h"

#define SYNTH_G_MG 1000.0f

bool pipeline_direct(const synth_config *cfg, int n_segments, float noise_floor,
                     wave_result *out) {
  synth_t s;
  synth_init(&s, cfg, WAVE_PROC_RATE_HZ);

  wave_accumulator acc;
  wave_accum_init(&acc, noise_floor);

  float seg[WAVE_SEG_SAMPLES];
  float psd[WAVE_NBINS];

  for (int n = 0; n < n_segments; n++) {
    for (int i = 0; i < WAVE_SEG_SAMPLES; i++) {
      wave_accel_sample smp;
      synth_next(&s, &smp);
      /* With no roll the watch stays level, so the vertical specific force is
       * simply z minus the 1 g static component. */
      seg[i] = ((float)smp.z - SYNTH_G_MG) * WAVE_MG_TO_MS2;
    }
    wave_spectrum_segment_ex(seg, psd, false);
    wave_accum_add(&acc, psd);
  }

  return wave_accum_result(&acc, out);
}

static wave_vec3 sample_to_vec3(const wave_accel_sample *s) {
  wave_vec3 v;
  v.x = (float)s->x;
  v.y = (float)s->y;
  v.z = (float)s->z;
  return v;
}

bool pipeline_full(const synth_config *cfg, int n_segments, float noise_floor,
                   vert_method method, wave_result *out) {
  synth_t syn;
  synth_init(&syn, cfg, WAVE_ACQ_RATE_HZ);

  wave_gravity grav;
  wave_gravity_init(&grav, WAVE_ACQ_RATE_HZ);

  wave_decimator dec;
  wave_decim_init(&dec);

  wave_accumulator acc;
  wave_accum_init(&acc, noise_floor);

  /* Run the attitude estimator to convergence before collecting anything, which
   * is what the app does too: it delays the start of the first segment rather
   * than discarding a finished one. Cap the wait so a pathological input cannot
   * spin here forever. */
  const int warmup_limit = (int)(WAVE_ACQ_RATE_HZ * 120.0f);
  for (int i = 0; i < warmup_limit && !wave_gravity_converged(&grav); i++) {
    wave_accel_sample smp;
    synth_next(&syn, &smp);
    wave_gravity_push(&grav, sample_to_vec3(&smp));
  }

  float seg[WAVE_SEG_SAMPLES];
  float psd[WAVE_NBINS];

  for (int n = 0; n < n_segments; n++) {
    int filled = 0;
    /* Bounded for the same reason the warmup loop above is: if projection fails
     * on every sample -- a config with no static gravity component, say -- the
     * continue below would never let filled advance and `make check` would hang
     * with no diagnostic rather than fail. */
    int guard = PIPELINE_RAW_PER_SEG * 10;
    while (filled < WAVE_SEG_SAMPLES) {
      if (--guard < 0) {
        return false;
      }
      wave_accel_sample smp;
      synth_next(&syn, &smp);
      const wave_vec3 a = sample_to_vec3(&smp);
      wave_gravity_push(&grav, a);

      float vert_mg = 0.0f;
      if (method == VERT_PROJECTION) {
        if (!wave_gravity_project(&grav, a, &vert_mg)) {
          continue;
        }
      } else {
        vert_mg = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z) - SYNTH_G_MG;
      }

      float dec_out;
      if (wave_decim_push(&dec, vert_mg, &dec_out)) {
        seg[filled++] = dec_out * WAVE_MG_TO_MS2;
      }
    }
    wave_spectrum_segment(seg, psd);
    wave_accum_add(&acc, psd);
  }

  return wave_accum_result(&acc, out);
}
