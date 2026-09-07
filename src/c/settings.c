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

#include "settings.h"

#include <pebble.h>

#include "wave/fastmath.h"

#define WH_PERSIST_KEY_SETTINGS 1

void wh_settings_load(wh_settings *s) {
  s->schema_version = WH_SETTINGS_SCHEMA;
  s->boat_length_m = WH_DEFAULT_BOAT_LENGTH_M;
  s->noise_floor = 0.0f;
  s->full_scale_mg = WH_DEFAULT_FULL_SCALE_MG;
  s->use_feet = false;
  s->diagnostic_mode = false;
  s->intro_seen = false;

  if (persist_exists(WH_PERSIST_KEY_SETTINGS)) {
    wh_settings stored;
    const int read = persist_read_data(WH_PERSIST_KEY_SETTINGS, &stored,
                                       sizeof(stored));
    if (read == (int)sizeof(stored) &&
        stored.schema_version == WH_SETTINGS_SCHEMA) {
      *s = stored;
    } else if (read > 0) {
      APP_LOG(APP_LOG_LEVEL_WARNING,
              "stored settings ignored (%d bytes, schema %u); using defaults",
              read, (unsigned)stored.schema_version);
    }
  }

  /* Guard against a stored value that would break the maths. */
  if (!(s->boat_length_m > 0.5f) || s->boat_length_m > 50.0f) {
    s->boat_length_m = WH_DEFAULT_BOAT_LENGTH_M;
  }
  if (!(s->full_scale_mg > 100.0f)) {
    s->full_scale_mg = WH_DEFAULT_FULL_SCALE_MG;
  }
  if (!(s->noise_floor >= 0.0f) || s->noise_floor > WH_NOISE_FLOOR_MAX) {
    s->noise_floor = 0.0f;
  }
}

void wh_settings_save(const wh_settings *s) {
  persist_write_data(WH_PERSIST_KEY_SETTINGS, s, sizeof(*s));
}

float wh_follow_limit_period(const wh_settings *s) {
  /* T_min = sqrt(4*pi*L/g) */
  const float g = 9.80665f;
  return wave_sqrtf(4.0f * 3.14159265f * s->boat_length_m / g);
}

float wh_display_height(const wh_settings *s, float metres) {
  return s->use_feet ? (metres * 3.28084f) : metres;
}

const char *wh_height_unit(const wh_settings *s) {
  return s->use_feet ? "ft" : "m";
}
