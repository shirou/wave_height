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

#include "fastmath.h"

/* Literals rather than M_PI: pulling in math.h is what this file exists to
 * avoid, and these are exact to float precision. */
#define FM_PI 3.14159265358979323846f
#define FM_TWO_OVER_PI 0.63661977236758134308f

/* pi/2 split into a part that is exact in float and the remainder.
 *
 * Reducing with a single float pi/2 loses low bits once the quadrant index
 * grows, because k*pi/2 and x become comparable in magnitude. Subtracting the
 * two halves separately (Cody-Waite) keeps them: the error went from 1.1e-6 to
 * below float resolution across two full turns. */
#define FM_HALF_PI_HI 1.5707963705062866f
#define FM_HALF_PI_LO -4.3711388286738e-08f

typedef union {
  float f;
  uint32_t u;
} fm_bits;

bool wave_isfinite(float x) {
  fm_bits v;
  v.f = x;
  return ((v.u >> 23) & 0xffu) != 0xffu;
}

float wave_fabsf(float x) {
  fm_bits v;
  v.f = x;
  v.u &= 0x7fffffffu;
  return v.f;
}

float wave_sqrtf(float x) {
  if (!(x > 0.0f)) {
    return 0.0f;
  }
  if (!wave_isfinite(x)) {
    return x;
  }

  /* Halving the exponent gives a guess within a few percent. The constant is
   * the usual one for this trick: it re-biases the exponent after the shift. */
  fm_bits v;
  v.f = x;
  v.u = 0x1fbd1df5u + (v.u >> 1);
  float y = v.f;

  /* Newton-Raphson on y^2 = x. Each step roughly doubles the correct digits,
   * so three take a 3% guess past float resolution. */
  y = 0.5f * (y + x / y);
  y = 0.5f * (y + x / y);
  y = 0.5f * (y + x / y);
  return y;
}

float wave_floorf(float x) {
  if (!wave_isfinite(x)) {
    return x;
  }
  /* Outside int32 every float is already an integer, so there is nothing to
   * round -- and the cast below would be undefined. */
  if (x >= 2147483520.0f || x <= -2147483520.0f) {
    return x;
  }
  const float t = (float)(int32_t)x; /* truncates toward zero */
  return (x < 0.0f && t != x) ? (t - 1.0f) : t;
}

/* sin(r) and cos(r) for |r| <= pi/4. Taylor to the term where the next one
 * falls below float resolution over that interval. */
static float fm_sin_kernel(float r) {
  const float r2 = r * r;
  return r * (1.0f +
              r2 * (-1.0f / 6.0f +
                    r2 * (1.0f / 120.0f +
                          r2 * (-1.0f / 5040.0f + r2 * (1.0f / 362880.0f)))));
}

static float fm_cos_kernel(float r) {
  const float r2 = r * r;
  return 1.0f +
         r2 * (-0.5f +
               r2 * (1.0f / 24.0f +
                     r2 * (-1.0f / 720.0f +
                           r2 * (1.0f / 40320.0f - r2 * (1.0f / 3628800.0f)))));
}

/* Reduce x to a quadrant index and a remainder in [-pi/4, pi/4], then pick the
 * kernel and sign for that quadrant. */
static void fm_reduce(float x, float *r_out, int *quad_out) {
  const float scaled = x * FM_TWO_OVER_PI;
  /* Nearest integer, without libm. */
  const int k = (int)(scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
  const float kf = (float)k;
  *r_out = (x - kf * FM_HALF_PI_HI) - kf * FM_HALF_PI_LO;
  *quad_out = (int)((unsigned int)(k & 3));
}

float wave_sinf(float x) {
  if (!wave_isfinite(x)) {
    return 0.0f;
  }
  float r;
  int q;
  fm_reduce(x, &r, &q);
  switch (q) {
    case 0:
      return fm_sin_kernel(r);
    case 1:
      return fm_cos_kernel(r);
    case 2:
      return -fm_sin_kernel(r);
    default:
      return -fm_cos_kernel(r);
  }
}

float wave_cosf(float x) {
  if (!wave_isfinite(x)) {
    return 0.0f;
  }
  float r;
  int q;
  fm_reduce(x, &r, &q);
  switch (q) {
    case 0:
      return fm_cos_kernel(r);
    case 1:
      return -fm_sin_kernel(r);
    case 2:
      return -fm_cos_kernel(r);
    default:
      return fm_sin_kernel(r);
  }
}
