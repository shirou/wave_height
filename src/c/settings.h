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

#ifndef WH_SETTINGS_H
#define WH_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "wave/wave_types.h"

/* Default boat length: the fishing-charter class this app is aimed at. */
#define WH_DEFAULT_BOAT_LENGTH_M 10.0f

/* Sensor full scale, per the SDK documentation. Replaced by the value measured
 * during the on-device spike once that has been run. */
#define WH_DEFAULT_FULL_SCALE_MG 4000.0f

/* Layout tag for the persisted blob.
 *
 * persist_read_data only reports a byte count, so without this a struct change
 * is silently mishandled in one of two ways. Adding a field changes sizeof, the
 * length check fails, and every setting -- including the calibrated noise floor
 * -- resets with no indication. Removing one can leave sizeof unchanged, and the
 * old bytes get reinterpreted; since diagnostic_mode is a bool, a stray non-zero
 * byte landing there would silently bypass all quality gating. Bump on any
 * layout change. */
#define WH_SETTINGS_SCHEMA 1u

/* Sanity ceiling for the calibrated noise floor. The expected value for the
 * assumed sensor is about 1.6e-4; this is roughly a hundred times that. A wildly
 * high floor makes real seas read as calm, because the per-bin clamp combined
 * with the (deliberately) negative deconvolution weight at bin 3 can drive m0
 * below zero. */
#define WH_NOISE_FLOOR_MAX 1.0e-2f

typedef struct {
  uint32_t schema_version;
  float boat_length_m; /* used only to warn about short waves, never to correct */
  float noise_floor;   /* one-sided PSD, (m/s^2)^2/Hz, from still-watch calibration */
  float full_scale_mg;
  bool use_feet;
  bool diagnostic_mode;
  bool intro_seen;
} wh_settings;

void wh_settings_load(wh_settings *s);
void wh_settings_save(const wh_settings *s);

/*
 * Shortest wave period the boat still follows, in seconds.
 *
 * A hull tracks waves whose length is roughly twice its own or more, and deep
 * water gives L = g*T^2/(2*pi), so T_min = sqrt(4*pi*L/g). Below this the boat
 * rides over the waves instead of following them and the reading is an
 * underestimate -- which is worth telling the user about, since it is invisible
 * in the number itself.
 *
 * This is deliberately NOT used to scale the answer. A response-amplitude
 * correction depends on hull form, loading, heading and wave direction; deriving
 * one from length alone would be inventing precision.
 */
float wh_follow_limit_period(const wh_settings *s);

/* Metres to whatever the user asked for. */
float wh_display_height(const wh_settings *s, float metres);
const char *wh_height_unit(const wh_settings *s);

#endif /* WH_SETTINGS_H */
