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

#ifndef WH_SETTINGS_WINDOW_H
#define WH_SETTINGS_WINDOW_H

#include <stdbool.h>

#include "settings.h"
#include "wave/calibration.h"
#include "wave/wave_types.h"

/*
 * On-watch settings, including noise-floor calibration.
 *
 * Deliberately not a phone-side (Clay) configuration page. Calibration means
 * "put the watch down and let it measure its own noise for about two minutes",
 * and the moment someone will want to do that is aboard, out of phone range,
 * having just noticed a flat calm reading 0.1 m. A settings screen that needs a
 * phone would be unreachable exactly when it is needed.
 *
 * Changes are written to the caller's struct and persisted when the window is
 * popped.
 *
 * The calibration state belongs to the caller, not to this module: it is a
 * measurement object that runs its own pipeline, and main.c owns those. This
 * module only drives and displays it.
 */
void wh_settings_window_push(wh_settings *cfg, wave_calibration *calib);

/* Tell the settings screen whether the accelerometer is delivering the expected
 * rate. Calibration is meaningless at the wrong rate -- the spectrum would be on
 * the wrong frequency axis and its median would be stored as the noise floor --
 * so the screen has to say so rather than appear stuck. */
void wh_settings_window_set_rate_ok(bool ok);

/* Release the windows. Call from the app's deinit.
 *
 * Windows are created once and reused rather than destroyed in their own unload
 * handler: window_destroy inside unload frees the window while the stack pop
 * that triggered it is still in progress, which is not the pattern the SDK's own
 * examples use. */
void wh_settings_window_deinit(void);

#endif /* WH_SETTINGS_WINDOW_H */
