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

#include "gravity.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float vec3_norm(wave_vec3 v) {
  return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static float vec3_dot(wave_vec3 a, wave_vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

float wave_vec3_angle_deg(wave_vec3 a, wave_vec3 b) {
  const float na = vec3_norm(a);
  const float nb = vec3_norm(b);
  if (na < 1e-6f || nb < 1e-6f) {
    return 0.0f;
  }
  float c = vec3_dot(a, b) / (na * nb);
  if (c > 1.0f) {
    c = 1.0f;
  } else if (c < -1.0f) {
    c = -1.0f;
  }
  return acosf(c) * 180.0f / (float)M_PI;
}

void wave_gravity_init(wave_gravity *g, float sample_rate_hz) {
  memset(g, 0, sizeof(*g));
  g->dt = (sample_rate_hz > 0.0f) ? (1.0f / sample_rate_hz) : 0.1f;
}

/* First-order low-pass coefficient for a given time constant. */
static float alpha_for(float tau_s, float dt) {
  return dt / (tau_s + dt);
}

static void restart_settling(wave_gravity *g) {
  g->converged = false;
  g->sum.x = 0.0f;
  g->sum.y = 0.0f;
  g->sum.z = 0.0f;
  g->sum_count = 0;
  g->t_settle = 0.0f;
  g->t_off_axis = 0.0f;
}

void wave_gravity_push(wave_gravity *g, wave_vec3 a_mg) {
  if (!g->initialized) {
    /* Seed from the first sample so that the estimate is usable immediately --
     * this stands in for the peek() the SDK will not give us while a data
     * subscription is open -- but keep averaging until the window closes. */
    g->g_hat = a_mg;
    restart_settling(g);
    g->initialized = true;
  }

  if (!g->converged) {
    g->sum.x += a_mg.x;
    g->sum.y += a_mg.y;
    g->sum.z += a_mg.z;
    g->sum_count++;
    g->t_settle += g->dt;

    /* Publish the running mean as we go, so a projection requested mid-settle
     * is at least sensible. */
    g->g_hat.x = g->sum.x / (float)g->sum_count;
    g->g_hat.y = g->sum.y / (float)g->sum_count;
    g->g_hat.z = g->sum.z / (float)g->sum_count;

    if (g->t_settle >= WAVE_G_SETTLE_S) {
      g->converged = true;
    }
    return;
  }

  /* Restart settling if the watch has clearly been repositioned. Roll and wave
   * motion swing the instantaneous vector around the estimate but come back; a
   * new posture does not. */
  if (wave_vec3_angle_deg(a_mg, g->g_hat) > WAVE_G_RESET_DEG) {
    g->t_off_axis += g->dt;
    if (g->t_off_axis >= WAVE_G_RESET_HOLD_S) {
      restart_settling(g);
      return;
    }
  } else {
    g->t_off_axis = 0.0f;
  }

  const float alpha = alpha_for(WAVE_G_TAU_SLOW_S, g->dt);
  g->g_hat.x += alpha * (a_mg.x - g->g_hat.x);
  g->g_hat.y += alpha * (a_mg.y - g->g_hat.y);
  g->g_hat.z += alpha * (a_mg.z - g->g_hat.z);
}

bool wave_gravity_converged(const wave_gravity *g) {
  return g->converged;
}

bool wave_gravity_project(const wave_gravity *g, wave_vec3 a_mg, float *out_mg) {
  if (!g->initialized) {
    return false;
  }
  const float mag = vec3_norm(g->g_hat);
  if (mag < WAVE_G_MIN_MAGNITUDE_MG) {
    return false;
  }
  *out_mg = vec3_dot(a_mg, g->g_hat) / mag - mag;
  return true;
}
