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

#include "datalog.h"

#include <pebble.h>

/* Arbitrary but stable; tools/decode_log.py keys off it. */
#define WH_DATALOG_TAG 0x57480001

/* One record is a single sample: x, y, z as int16 little-endian. */
typedef struct __attribute__((__packed__)) {
  int16_t x;
  int16_t y;
  int16_t z;
} wh_log_record;

/* Consecutive transient failures tolerated before giving up. BUSY means someone
 * else holds the session and FULL means no space right now; both can clear, so
 * latching off on the first one would throw away the rest of a sea trial for a
 * momentary condition. Bounded so a permanently busy session does not warn
 * forever. */
#define WH_DATALOG_MAX_TRANSIENT 50

static DataLoggingSessionRef s_session;
static bool s_available;
static int s_transient_failures;

void wh_datalog_init(void) {
  s_transient_failures = 0;
  s_session = data_logging_create(WH_DATALOG_TAG, DATA_LOGGING_BYTE_ARRAY,
                                  sizeof(wh_log_record), true);
  s_available = (s_session != NULL);
  if (!s_available) {
    APP_LOG(APP_LOG_LEVEL_WARNING,
            "DataLogging unavailable; raw logging disabled");
  }
}

void wh_datalog_deinit(void) {
  if (s_session != NULL) {
    data_logging_finish(s_session);
    s_session = NULL;
  }
  s_available = false;
}

bool wh_datalog_available(void) {
  return s_available;
}

void wh_datalog_push(const wave_accel_sample *samples, int n) {
  if (!s_available || n <= 0) {
    return;
  }

  wh_log_record recs[25];
  const int max = (int)(sizeof(recs) / sizeof(recs[0]));
  int written = 0;
  while (written < n) {
    const int chunk = ((n - written) > max) ? max : (n - written);
    for (int i = 0; i < chunk; i++) {
      recs[i].x = samples[written + i].x;
      recs[i].y = samples[written + i].y;
      recs[i].z = samples[written + i].z;
    }
    const DataLoggingResult r =
        data_logging_log(s_session, recs, (uint32_t)chunk);

    if (r == DATA_LOGGING_SUCCESS) {
      s_transient_failures = 0;
      written += chunk;
      continue;
    }

    /* Losing log records must never take the measurement down with it. */
    APP_LOG(APP_LOG_LEVEL_WARNING, "data_logging_log failed: %d", (int)r);

    if (r == DATA_LOGGING_BUSY || r == DATA_LOGGING_FULL) {
      /* Transient: drop this batch and try again on the next one. */
      if (++s_transient_failures >= WH_DATALOG_MAX_TRANSIENT) {
        APP_LOG(APP_LOG_LEVEL_ERROR,
                "raw logging disabled after %d consecutive transient failures",
                s_transient_failures);
        s_available = false;
      }
      return;
    }

    /* NOT_FOUND, CLOSED, INVALID_PARAMS, INTERNAL_ERR: the session is gone and
     * retrying cannot help. */
    s_available = false;
    return;
  }
}
