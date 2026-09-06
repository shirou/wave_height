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

/*
 * Thin adapter. Everything with a decision in it lives in src/c/wave, which
 * knows nothing about the SDK and is therefore testable on a host machine
 * against a synthetic sea of known height. This file only translates SDK types,
 * owns the window, and moves state in and out of persistent storage.
 */

#include <pebble.h>

#include "datalog.h"
#include "settings.h"
#include "settings_window.h"
#include "ui.h"
#include "wave/session.h"

#define WH_PERSIST_KEY_SNAPSHOT 2

/* How stale a saved accumulation may be and still be worth resuming. The sea
 * state will have moved on well before this. */
#define WH_SNAPSHOT_MAX_AGE_S 600

/* The SDK caps a batch at 25 samples. */
#define WH_BATCH_MAX 25

static Window *s_window;
static wave_session s_session;
static wh_settings s_settings;

static AccelSamplingRate sampling_rate_for(float hz) {
  if (hz >= 100.0f) {
    return ACCEL_SAMPLING_100HZ;
  }
  if (hz >= 50.0f) {
    return ACCEL_SAMPLING_50HZ;
  }
  if (hz >= 25.0f) {
    return ACCEL_SAMPLING_25HZ;
  }
  return ACCEL_SAMPLING_10HZ;
}

/* Cross-check the configured rate against the timestamps the SDK actually
 * delivers, once, on the first full batch.
 *
 * Belt and braces for the ordering issue above: even if set_sampling_rate
 * reports success, this catches the case where the hardware is running at a
 * different rate, which would otherwise corrupt every derived quantity silently. */
static bool s_rate_ok = true;

static void verify_sampling_rate(const AccelData *data, int n) {
  static bool checked = false;
  if (checked || n < 2) {
    return;
  }
  checked = true;

  const uint64_t span_ms = data[n - 1].timestamp - data[0].timestamp;
  if (span_ms == 0) {
    return; /* emulator injection can deliver a batch with no time spread */
  }
  const float measured_hz = 1000.0f * (float)(n - 1) / (float)span_ms;
  const float ratio = measured_hz / WAVE_ACQ_RATE_HZ;
  if (ratio < 0.75f || ratio > 1.33f) {
    APP_LOG(APP_LOG_LEVEL_ERROR,
            "accelerometer is running at ~%d Hz, not the %d Hz assumed; "
            "measurement disabled",
            (int)(measured_hz + 0.5f), (int)WAVE_ACQ_RATE_HZ);
    /* Detecting this and carrying on would be pointless: the frequency axis,
     * the decimation ratio and the 1/(2*pi*f)^4 conversion are all built on the
     * assumed rate, so what reaches the screen would be wrong by a large factor
     * while looking entirely plausible. Stop feeding the estimator instead. */
    s_rate_ok = false;
    wh_ui_set_rate_ok(false);
  }
}

static void accel_handler(AccelData *data, uint32_t num_samples) {
  wave_accel_sample buf[WH_BATCH_MAX];
  const int n =
      (num_samples > WH_BATCH_MAX) ? WH_BATCH_MAX : (int)num_samples;

  verify_sampling_rate(data, n);

  for (int i = 0; i < n; i++) {
    buf[i].x = data[i].x;
    buf[i].y = data[i].y;
    buf[i].z = data[i].z;
    buf[i].did_vibrate = data[i].did_vibrate;
  }

  /* Raw logging continues either way: a recording made at the wrong rate is
   * still the evidence needed to work out what happened. */
  wh_datalog_push(buf, n);
  wh_ui_set_logging(wh_datalog_available());

  /* While the noise floor is being calibrated the samples belong to that run
   * and must not also be folded into the measurement, or the two would
   * accumulate from the same data. */
  if (wh_settings_feed_accel(buf, n)) {
    return;
  }

  if (!s_rate_ok) {
    wave_display stale;
    wave_session_get_display(&s_session, &stale);
    wh_ui_set(&stale);
    return;
  }

  wave_session_push(&s_session, buf, n);

  wave_display d;
  wave_session_get_display(&s_session, &d);
  wh_ui_set(&d);

  /* Report state changes so a run can be followed with `pebble logs`, on the
   * emulator and on the watch. Only on change, to keep the log readable. */
  static int s_last_seg = -1;
  static int s_last_state = -1;
  if (d.result.n_seg != s_last_seg || (int)d.state != s_last_state) {
    s_last_seg = d.result.n_seg;
    s_last_state = (int)d.state;
    /* Round to centimetres first, then split; see format_1dp in ui.c for why
     * splitting before rounding is wrong. */
    const int hs_cm = (int)(d.result.hs * 100.0f + 0.5f);
    APP_LOG(APP_LOG_LEVEL_INFO,
            "t=%ds state=%d seg=%d rej=%d Hs=%d.%02dm T=%ds conf=%d",
            (int)s_session.elapsed_s, (int)d.state, d.result.n_seg,
            s_session.rejected_total, hs_cm / 100, hs_cm % 100,
            (int)(d.result.period + 0.5f), (int)d.result.conf);
  }
}

static void save_snapshot(void) {
  if (s_session.acc.n_seg <= 0) {
    return;
  }
  wave_session_snapshot snap;
  wave_session_save(&s_session, &snap, (uint32_t)time(NULL));
  persist_write_data(WH_PERSIST_KEY_SNAPSHOT, &snap, sizeof(snap));
}

static void restore_snapshot(void) {
  if (!persist_exists(WH_PERSIST_KEY_SNAPSHOT)) {
    return;
  }
  wave_session_snapshot snap;
  const int read =
      persist_read_data(WH_PERSIST_KEY_SNAPSHOT, &snap, sizeof(snap));
  if (read != (int)sizeof(snap)) {
    return;
  }
  if (wave_session_restore(&s_session, &snap, (uint32_t)time(NULL),
                           WH_SNAPSHOT_MAX_AGE_S)) {
    APP_LOG(APP_LOG_LEVEL_INFO, "resumed %d segment(s)", s_session.acc.n_seg);
  }
}

/* Back exits on a long press only. A short press is far too easy to trigger
 * with the watch pressed against a fitting, and it would discard the
 * accumulation the user has been holding still to build up. */
static void back_long_click(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  wh_settings_window_push(&s_settings);
}

static void click_config_provider(void *context) {
  window_long_click_subscribe(BUTTON_ID_BACK, 0, back_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

static void window_load(Window *window) {
  wh_ui_create(window, &s_settings);
  window_set_click_config_provider(window, click_config_provider);
}

static void window_appear(Window *window) {
  /* Settings may have changed while this window was covered.
   *
   * The accumulated spectrum is deliberately kept: the noise floor is
   * subtracted when the result is computed, not when a segment is folded in, so
   * a freshly calibrated floor applies to everything already gathered. Throwing
   * the accumulation away would cost the user their measurement for no gain. */
  s_session.acc.noise_floor = s_settings.noise_floor;
  s_session.diagnostic = s_settings.diagnostic_mode;
}

static void window_unload(Window *window) {
  wh_ui_destroy();
}

static void init(void) {
  wh_settings_load(&s_settings);

  wave_session_init(&s_session, WAVE_ACQ_RATE_HZ, s_settings.noise_floor,
                    s_settings.full_scale_mg);
  s_session.diagnostic = s_settings.diagnostic_mode;

  restore_snapshot();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = window_load,
                                           .appear = window_appear,
                                           .unload = window_unload,
                                       });
  window_stack_push(s_window, true);

  wh_datalog_init();
  wh_ui_set_logging(wh_datalog_available());

  /* Subscribe BEFORE setting the rate, and check that the call was accepted.
   *
   * The service is only running once subscribed (accel_service_peek documents
   * "-1 if the accel is not running"), and set_sampling_rate returns an int it
   * would be careless to throw away: if the request is dropped the accelerometer
   * stays at its 25 Hz default while every constant here assumes 10 Hz. The
   * decimator would then produce 5 Hz rather than 2 Hz, WAVE_DF would be wrong
   * by 2.5x, and Hs -- which goes through 1/(2*pi*f)^4 -- would be out by a
   * factor of about 39. Nothing downstream could detect that. */
  accel_data_service_subscribe(WH_BATCH_MAX, accel_handler);

  const int rate_rc =
      accel_service_set_sampling_rate(sampling_rate_for(WAVE_ACQ_RATE_HZ));
  if (rate_rc != 0) {
    APP_LOG(APP_LOG_LEVEL_ERROR,
            "accel_service_set_sampling_rate returned %d; readings will be "
            "wrong if the rate is not %d Hz",
            rate_rc, (int)WAVE_ACQ_RATE_HZ);
  }
}

static void deinit(void) {
  accel_data_service_unsubscribe();
  save_snapshot();
  wh_datalog_deinit();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
