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

#ifndef TEST_PIPELINE_H
#define TEST_PIPELINE_H

#include "../src/c/wave/accumulator.h"
#include "../src/c/wave/wave_types.h"
#include "synth.h"

/*
 * Drive the spectral chain directly at the 2 Hz processing rate, taking the
 * vertical acceleration straight from the generator and skipping gravity
 * estimation and decimation. Droop compensation is skipped too, since the
 * signal never passed through the boxcar.
 *
 * This isolates spectrum.c and accumulator.c: if a test using this fails, the
 * fault is in the spectral maths, not in attitude tracking or resampling.
 */
bool pipeline_direct(const synth_config *cfg, int n_segments, float noise_floor,
                     wave_result *out);

/* How to reduce three axes to one vertical channel. Having both lets a test
 * compare them on identical input, which is the only way to show that the
 * chosen method is actually better -- an absolute threshold is passed
 * comfortably by both, so it demonstrates nothing. */
typedef enum {
  VERT_PROJECTION, /* dot(a, g_hat)/|g_hat| - |g_hat|  (the shipped method) */
  VERT_MAGNITUDE   /* |a| - |g|                        (the rejected method) */
} vert_method;

/*
 * The full chain at the acquisition rate: gravity estimation, vertical
 * extraction, decimation with droop compensation, segmentation, spectra and
 * averaging. The gravity estimator is run to convergence first, so tests start
 * from the same steady state the app reaches before it collects anything.
 */
bool pipeline_full(const synth_config *cfg, int n_segments, float noise_floor,
                   vert_method method, wave_result *out);

/* Number of raw samples per segment at the acquisition rate. */
#define PIPELINE_RAW_PER_SEG (WAVE_SEG_SAMPLES * WAVE_DECIM_FACTOR)

#endif /* TEST_PIPELINE_H */
