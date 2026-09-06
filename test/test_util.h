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

#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <math.h>
#include <stdio.h>

extern int g_test_failures;
extern const char *g_current_test;

#define CHECK(cond, ...)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      printf("    FAIL %s:%d: ", __FILE__, __LINE__);             \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
      g_test_failures++;                                          \
    }                                                             \
  } while (0)

#define CHECK_NEAR(actual, expected, tol_frac, label)                     \
  do {                                                                    \
    const double a_ = (double)(actual);                                   \
    const double e_ = (double)(expected);                                 \
    const double lim_ = fabs(e_) * (double)(tol_frac);                    \
    if (!(fabs(a_ - e_) <= lim_)) {                                       \
      printf("    FAIL %s:%d: %s = %.6g, expected %.6g +/- %.1f%% "       \
             "(off by %.1f%%)\n",                                         \
             __FILE__, __LINE__, (label), a_, e_,                         \
             (double)(tol_frac)*100.0,                                    \
             (e_ != 0.0) ? fabs(a_ - e_) / fabs(e_) * 100.0 : 0.0);       \
      g_test_failures++;                                                  \
    }                                                                     \
  } while (0)

#define CHECK_LT(actual, limit, label)                                 \
  do {                                                                 \
    const double a_ = (double)(actual);                                \
    const double l_ = (double)(limit);                                 \
    if (!(a_ < l_)) {                                                  \
      printf("    FAIL %s:%d: %s = %.6g, expected < %.6g\n",           \
             __FILE__, __LINE__, (label), a_, l_);                     \
      g_test_failures++;                                               \
    }                                                                  \
  } while (0)

#define CHECK_IN_RANGE(actual, lo, hi, label)                              \
  do {                                                                     \
    const double a_ = (double)(actual);                                    \
    if (!(a_ >= (double)(lo) && a_ <= (double)(hi))) {                     \
      printf("    FAIL %s:%d: %s = %.6g, expected in [%.6g, %.6g]\n",      \
             __FILE__, __LINE__, (label), a_, (double)(lo), (double)(hi)); \
      g_test_failures++;                                                   \
    }                                                                      \
  } while (0)

#endif /* TEST_UTIL_H */
