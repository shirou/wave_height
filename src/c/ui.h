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

#ifndef WH_UI_H
#define WH_UI_H

#include <pebble.h>

#include "settings.h"
#include "wave/session.h"

/*
 * Drawing only. No computation happens here; everything displayed arrives
 * already decided in wave_display, including the rounding of Hs, so that the
 * rules about how much precision to show live with the maths rather than with
 * the layout.
 */

void wh_ui_create(Window *window, const wh_settings *cfg);
void wh_ui_destroy(void);
void wh_ui_set(const wave_display *d);

/* Whether raw logging is running. Shown on screen when it is not, because the
 * user is expected to be out of phone range and cannot find out any other way
 * until they are back ashore. */
void wh_ui_set_logging(bool ok);

/* Whether the accelerometer is delivering the rate everything downstream assumes.
 * When it is not, the display must stop showing a height: every derived quantity
 * is scaled by the wrong frequency axis, and a plausible-looking wrong number is
 * worse than no number. */
void wh_ui_set_rate_ok(bool ok);

#endif /* WH_UI_H */
