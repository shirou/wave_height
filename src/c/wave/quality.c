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

#include "quality.h"

#include <math.h>
#include <string.h>

#include "gravity.h" /* wave_vec3_angle_deg */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Corner frequency of the high-pass used to isolate body motion. Above the
 * 0.5 Hz top of the wave band, below the 2-3 Hz of hand movement. */
#define WAVE_Q_HP_FC_HZ 1.0f

void wave_quality_init(wave_quality *q, float sample_rate_hz, float full_scale_mg) {
  memset(q, 0, sizeof(*q));
  const float dt = (sample_rate_hz > 0.0f) ? (1.0f / sample_rate_hz) : 0.1f;
  const float tau = 1.0f / (2.0f * (float)M_PI * WAVE_Q_HP_FC_HZ);
  q->hp_alpha = tau / (tau + dt);
  q->clip_threshold_mg = full_scale_mg - WAVE_Q_CLIP_MARGIN_MG;

  q->live_hf_alpha = dt / (WAVE_Q_LIVE_HF_TAU_S + dt);
}

void wave_quality_begin_segment(wave_quality *q, wave_vec3 g_hat) {
  q->hp_primed = false;
  q->hp_prev_in = 0.0f;
  q->hp_prev_out = 0.0f;
  q->hf_sum_sq = 0.0f;
  q->hf_count = 0;

  q->band_sum = 0.0f;
  q->band_sum_sq = 0.0f;
  q->band_count = 0;

  q->g_start = g_hat;
  q->g_end = g_hat;

  q->vibrated = false;
  q->clipped = false;

  /* live_hf_power and ref_band_var survive on purpose: they are what make the
   * warning continuous across boundaries. */
}

void wave_quality_push_raw(wave_quality *q, wave_vec3 a_mg, float a_vert_mg,
                           bool did_vibrate) {
  if (did_vibrate) {
    q->vibrated = true;
  }

  const float t = q->clip_threshold_mg;
  if (fabsf(a_mg.x) >= t || fabsf(a_mg.y) >= t || fabsf(a_mg.z) >= t) {
    q->clipped = true;
  }

  /* First-order high pass: y = a*(y_prev + x - x_prev). */
  if (!q->hp_primed) {
    q->hp_prev_in = a_vert_mg;
    q->hp_prev_out = 0.0f;
    q->hp_primed = true;
    return;
  }
  const float y = q->hp_alpha * (q->hp_prev_out + a_vert_mg - q->hp_prev_in);
  q->hp_prev_in = a_vert_mg;
  q->hp_prev_out = y;

  q->hf_sum_sq += y * y;
  q->hf_count++;

  /* Live term, carried across segment boundaries. */
  q->live_hf_power += q->live_hf_alpha * (y * y - q->live_hf_power);
}

void wave_quality_push_decimated(wave_quality *q, float a_vert_mg) {
  q->band_sum += a_vert_mg;
  q->band_sum_sq += a_vert_mg * a_vert_mg;
  q->band_count++;
}

void wave_quality_update_gravity(wave_quality *q, wave_vec3 g_hat) {
  q->g_end = g_hat;
}

float wave_quality_hf_ratio(const wave_quality *q) {
  if (q->hf_count <= 0 || q->band_count < WAVE_Q_MIN_BAND_SAMPLES) {
    return 0.0f;
  }
  const float hf_power = q->hf_sum_sq / (float)q->hf_count;

  /* Variance, not mean square: the decimated stream still carries any residual
   * DC offset from the projection, and that is not wave energy. */
  const float mean = q->band_sum / (float)q->band_count;
  float band_power = q->band_sum_sq / (float)q->band_count - mean * mean;
  if (band_power <= 0.0f) {
    return 0.0f;
  }
  return hf_power / band_power;
}

float wave_quality_band_var(const wave_quality *q) {
  if (q->band_count < WAVE_Q_MIN_BAND_SAMPLES) {
    return 0.0f;
  }
  const float mean = q->band_sum / (float)q->band_count;
  const float var = q->band_sum_sq / (float)q->band_count - mean * mean;
  return (var > 0.0f) ? var : 0.0f;
}

void wave_quality_set_reference(wave_quality *q, float band_var) {
  q->ref_band_var = (band_var > 0.0f) ? band_var : 0.0f;
  q->has_reference = true;
}

float wave_quality_live_ratio(const wave_quality *q) {
  /* No reference yet means no segment has completed, so there is nothing stable
   * to compare against and we stay quiet rather than guess. */
  if (!q->has_reference) {
    return 0.0f;
  }
  const float denom = (q->ref_band_var > WAVE_Q_MIN_REF_VAR)
                          ? q->ref_band_var
                          : WAVE_Q_MIN_REF_VAR;
  return q->live_hf_power / denom;
}

wave_quality_verdict wave_quality_verdict_of(const wave_quality *q) {
  if (q->vibrated) {
    return WAVE_Q_FAIL_VIBRATE;
  }
  if (q->clipped) {
    return WAVE_Q_FAIL_CLIP;
  }
  if (wave_vec3_angle_deg(q->g_start, q->g_end) > WAVE_Q_DRIFT_DEG_MAX) {
    return WAVE_Q_FAIL_DRIFT;
  }
  if (wave_quality_hf_ratio(q) > WAVE_Q_HF_RATIO_MAX) {
    return WAVE_Q_FAIL_HF;
  }
  return WAVE_Q_OK;
}

bool wave_quality_segment_ok(const wave_quality *q) {
  return wave_quality_verdict_of(q) == WAVE_Q_OK;
}
