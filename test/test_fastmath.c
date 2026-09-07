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

#include <math.h> /* the host's libm, as the reference to compare against */
#include <stdio.h>

#include "../src/c/wave/fastmath.h"
#include "../src/c/wave/gravity.h"
#include "test_util.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * These replace libm because libm's sqrtf faults on the watch. That makes them
 * load-bearing for every number the app produces, so they are checked against
 * the host's libm across the ranges the app actually uses.
 */

void test_fastmath_sqrt(void) {
  /* The ranges that matter: spectral moments (1e-6..1e0), acceleration
   * magnitudes squared (1e4..1e7), and the boat-length expression (1..20). */
  static const double xs[] = {
      1e-9, 1e-6, 1e-4, 0.0625, 0.25, 1.0, 2.0, 12.8, 100.0,
      1024.0, 1.0e4, 3.0e6, 1.0e7, 1.6e-4, 9.81, 3.14159,
  };
  const int n = (int)(sizeof(xs) / sizeof(xs[0]));

  double worst = 0.0;
  double worst_at = 0.0;
  for (int i = 0; i < n; i++) {
    const float got = wave_sqrtf((float)xs[i]);
    const double want = sqrt(xs[i]);
    const double rel = fabs((double)got - want) / want;
    if (rel > worst) {
      worst = rel;
      worst_at = xs[i];
    }
  }
  printf("      sqrt: worst relative error %.2e (at x=%g)\n", worst, worst_at);
  CHECK_LT(worst, 1e-6, "sqrt relative error over the working range");

  /* Edge cases the callers rely on. */
  CHECK(wave_sqrtf(0.0f) == 0.0f, "sqrt(0) must be 0");
  CHECK(wave_sqrtf(-1.0f) == 0.0f, "sqrt of a negative must be 0, not NaN");
  CHECK(wave_sqrtf(1.0f) == 1.0f, "sqrt(1) must be exactly 1");
}

void test_fastmath_trig(void) {
  /* Full turn either way, since the twiddle factors sweep -2*pi..0 and the
   * droop curve and Hann window sweep 0..pi. */
  double worst_sin = 0.0;
  double worst_cos = 0.0;
  for (int i = -720; i <= 720; i++) {
    const double x = (double)i * M_PI / 180.0;
    const double ds = fabs((double)wave_sinf((float)x) - sin(x));
    const double dc = fabs((double)wave_cosf((float)x) - cos(x));
    if (ds > worst_sin) {
      worst_sin = ds;
    }
    if (dc > worst_cos) {
      worst_cos = dc;
    }
  }
  printf("      trig: worst absolute error sin %.2e, cos %.2e\n", worst_sin,
         worst_cos);
  CHECK_LT(worst_sin, 1e-6, "sin absolute error over two turns");
  CHECK_LT(worst_cos, 1e-6, "cos absolute error over two turns");

  /* Exact points, since the window and twiddles land on them. */
  CHECK_NEAR(wave_cosf(0.0f), 1.0f, 1e-6, "cos(0)");
  CHECK_LT(fabs((double)wave_sinf(0.0f)), 1e-6, "sin(0)");
  CHECK_LT(fabs((double)wave_cosf((float)(M_PI / 2.0))), 1e-6, "cos(pi/2)");
  CHECK_NEAR(wave_sinf((float)(M_PI / 2.0)), 1.0f, 1e-6, "sin(pi/2)");
  CHECK_NEAR(wave_cosf((float)M_PI), -1.0f, 1e-6, "cos(pi)");
}

void test_fastmath_floor(void) {
  static const double xs[] = {0.0,  0.5,   1.0,   1.4999, 1.5,  1.9999,
                              -0.5, -1.0,  -1.5,  2.5,    19.0, 0.05,
                              9.95, 100.4, -0.01};
  const int n = (int)(sizeof(xs) / sizeof(xs[0]));
  for (int i = 0; i < n; i++) {
    const float got = wave_floorf((float)xs[i]);
    const double want = floor(xs[i]);
    CHECK(fabs((double)got - want) < 1e-6,
          "floor(%g) = %g, expected %g", xs[i], (double)got, want);
  }
  printf("      floor: %d values match libm\n", n);
}

void test_fastmath_isfinite(void) {
  CHECK(wave_isfinite(0.0f), "0 is finite");
  CHECK(wave_isfinite(1.0f), "1 is finite");
  CHECK(wave_isfinite(-1.0e20f), "-1e20 is finite");
  CHECK(!wave_isfinite((float)INFINITY), "inf is not finite");
  CHECK(!wave_isfinite((float)-INFINITY), "-inf is not finite");
  CHECK(!wave_isfinite((float)NAN), "NaN is not finite");
}

/*
 * The angle test replaced an acosf call with a squared-cosine comparison. The
 * two must agree, including on the sign case that squaring could otherwise get
 * wrong: an obtuse angle has a negative dot product, and squaring loses that.
 */
void test_fastmath_angle(void) {
  for (int deg = 0; deg <= 180; deg += 1) {
    const double r = (double)deg * M_PI / 180.0;
    wave_vec3 a = {1000.0f, 0.0f, 0.0f};
    wave_vec3 b = {(float)(1000.0 * cos(r)), (float)(1000.0 * sin(r)), 0.0f};

    /* Against a 35 degree limit and a 2 degree one, the two thresholds in use. */
    const bool want35 = (deg > 35);
    const bool want2 = (deg > 2);
    const bool got35 = wave_vec3_angle_exceeds(a, b, 0.8191520f);
    const bool got2 = wave_vec3_angle_exceeds(a, b, 0.9993908f);

    /* Allow disagreement only within a degree of the boundary, where float
     * rounding decides it. */
    if (deg < 34 || deg > 36) {
      CHECK(got35 == want35, "%d deg vs 35 deg limit: got %d want %d", deg,
            (int)got35, (int)want35);
    }
    if (deg < 1 || deg > 3) {
      CHECK(got2 == want2, "%d deg vs 2 deg limit: got %d want %d", deg,
            (int)got2, (int)want2);
    }
  }

  /* Degenerate inputs must not report an angle. */
  wave_vec3 zero = {0.0f, 0.0f, 0.0f};
  wave_vec3 unit = {1.0f, 0.0f, 0.0f};
  CHECK(!wave_vec3_angle_exceeds(zero, unit, 0.5f), "zero vector: no angle");
  CHECK(!wave_vec3_angle_exceeds(unit, zero, 0.5f), "zero vector: no angle");

  printf("      angle: 181 orientations agree with the intended thresholds\n");
}
