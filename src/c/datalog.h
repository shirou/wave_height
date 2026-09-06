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

#ifndef WH_DATALOG_H
#define WH_DATALOG_H

#include <stdbool.h>

#include "wave/wave_types.h"

/*
 * Raw three-axis logging, for working out afterwards why a sea trial disagreed
 * with the reference buoy.
 *
 * What is logged is the RAW acquisition-rate stream, not the decimated or
 * projected signal. Post-decimation data cannot answer the questions that
 * actually come up -- whether something aliased down from above 5 Hz, whether
 * the gravity estimate was off, whether engine vibration lifted the noise
 * floor -- because the evidence has already been filtered out by then.
 *
 * Volume: at 10 Hz, three int16 axes is 60 B/s, so a 10 minute trial is about
 * 36 kB. That is well within what DataLogging buffers and forwards on the next
 * phone connection.
 *
 * Whether the DataLogging API is actually alive in the Core Devices SDK is
 * still unverified; it is the first thing the on-device spike checks. If it is
 * not, the fallback is app_message through src/pkjs, since persist cannot hold
 * this (one segment alone exceeds PERSIST_DATA_MAX_LENGTH many times over).
 * Until then this module degrades to a no-op rather than failing the app.
 */

void wh_datalog_init(void);
void wh_datalog_deinit(void);

/* True if a logging session was established. */
bool wh_datalog_available(void);

void wh_datalog_push(const wave_accel_sample *samples, int n);

#endif /* WH_DATALOG_H */
