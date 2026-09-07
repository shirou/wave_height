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

#ifndef WAVE_CALIBRATION_H
#define WAVE_CALIBRATION_H

#include "gravity.h"
#include "session.h"
#include "wave_types.h"

/*
 * Measuring the sensor noise floor, by running the ordinary pipeline on a watch
 * that is deliberately not moving.
 *
 * The constant this produces is what lets a flat calm read "calm" instead of a
 * few centimetres: sensor noise alone accounts for roughly 0.07 m of apparent
 * height once 1/(2*pi*f)^4 has amplified it, and there is no way to tell that
 * apart from real wave energy without measuring it first.
 *
 * It has to be measured on the watch rather than assumed, because the figure
 * depends on the specific accelerometer and the mode the firmware runs it in --
 * neither of which the app can query.
 *
 * Quality gating stays ON during calibration on purpose. If the watch is being
 * held rather than resting on something, segments are rejected and the progress
 * count simply stops advancing, which tells the user to put it down. Bypassing
 * the gate would instead calibrate the noise floor against their hand tremor and
 * then subtract that from every later measurement.
 */

/* Segments to collect. Three is about 96 s of data on top of the settling
 * window; enough for the median to be stable without asking the user to sit and
 * watch the watch for longer than they will tolerate. */
#define WAVE_CALIB_SEGMENTS 3

typedef struct {
  wave_session session;
  bool active;
} wave_calibration;

void wave_calib_start(wave_calibration *c, float acq_rate_hz,
                      float full_scale_mg);

/* Feed the same batches the app receives. Ignored when not active. */
void wave_calib_push(wave_calibration *c, const wave_accel_sample *samples,
                     int n);

bool wave_calib_active(const wave_calibration *c);

/* Segments accepted so far, and how many are wanted. */
int wave_calib_progress(const wave_calibration *c);
int wave_calib_target(const wave_calibration *c);

/* Roughly how long a run takes, for the benefit of documentation and the UI:
 * the settling window plus one segment per target. */
#define WAVE_CALIB_SECONDS   ((int)(WAVE_G_SETTLE_S + WAVE_CALIB_SEGMENTS * WAVE_SEG_SAMPLES /                                WAVE_PROC_RATE_HZ))

/* Segments thrown out, which is what the user needs to see if they are holding
 * the watch instead of resting it on something. */
int wave_calib_rejected(const wave_calibration *c);

bool wave_calib_done(const wave_calibration *c);

/* The measured noise floor, in (m/s^2)^2/Hz. Valid once done. */
float wave_calib_result(const wave_calibration *c);

void wave_calib_stop(wave_calibration *c);

#endif /* WAVE_CALIBRATION_H */
