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

#ifndef WAVE_RATE_CHECK_H
#define WAVE_RATE_CHECK_H

#include <stddef.h>
#include <stdint.h>

/*
 * Judging whether the accelerometer is delivering the rate everything else
 * assumes.
 *
 * This matters more than it looks: the frequency axis, the decimation ratio and
 * the 1/(2*pi*f)^4 conversion are all built on the assumed rate, so a watch
 * running at its 25 Hz default while the code assumes 10 Hz produces heights
 * wrong by a factor of about 39 -- and entirely plausible-looking.
 *
 * Split out of the SDK adapter so it can be tested on a host. It exists because
 * the check is a safety mechanism, and an untested safety mechanism is how the
 * first version came to latch "checked" before it had actually checked
 * anything.
 */

typedef enum {
  /* No time information in this batch, so nothing can be concluded. NOT a pass:
   * the caller must keep looking at later batches. The emulator's accelerometer
   * injection delivers whole batches sharing one timestamp, so this is a real
   * case and not a theoretical one. */
  WAVE_RATE_UNKNOWN = 0,
  WAVE_RATE_OK,
  WAVE_RATE_WRONG
} wave_rate_verdict;

/* Tolerance either side of the expected rate. Wide, because the point is to
 * catch a wrong rate setting (the available rates differ by 2.5x or more), not
 * to police jitter. */
#define WAVE_RATE_TOL_LO 0.75f
#define WAVE_RATE_TOL_HI 1.33f

/*
 * span_ms   elapsed time across the batch, from the first sample to the last
 * n         samples in the batch
 * out_hz    optional; receives the measured rate when one could be computed
 */
wave_rate_verdict wave_check_rate(uint64_t span_ms, int n, float expected_hz,
                                  float *out_hz);

#endif /* WAVE_RATE_CHECK_H */
