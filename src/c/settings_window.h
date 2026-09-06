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
#include "wave/wave_types.h"

/*
 * On-watch settings, including noise-floor calibration.
 *
 * Deliberately not a phone-side (Clay) configuration page. Calibration means
 * "put the watch down and let it measure its own noise for a minute and a half",
 * and the moment someone will want to do that is aboard, out of phone range,
 * having just noticed a flat calm reading 0.1 m. A settings screen that needs a
 * phone would be unreachable exactly when it is needed.
 *
 * Changes are written to the caller's struct and persisted when the window is
 * popped.
 */
void wh_settings_window_push(wh_settings *cfg);

/*
 * Offer an accelerometer batch to the settings screen.
 *
 * Returns true if calibration consumed it, in which case the caller must not
 * also feed it to the measurement session -- the two would otherwise both
 * accumulate from the same samples.
 */
bool wh_settings_feed_accel(const wave_accel_sample *samples, int n);

#endif /* WH_SETTINGS_WINDOW_H */
