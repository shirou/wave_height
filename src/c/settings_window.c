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

#include "settings_window.h"

#include <pebble.h>
#include <stdio.h>

#include "wave/calibration.h"

/* Lengths offered, in metres. The app is only honest up to about 12 m -- beyond
 * that the boat stops following the waves and the reading is an underestimate
 * whatever the setting says -- so the list stops there rather than inviting a
 * number the algorithm cannot support. */
static const uint8_t s_lengths[] = {5, 6, 8, 10, 12};
#define NUM_LENGTHS (sizeof(s_lengths) / sizeof(s_lengths[0]))

enum {
  ROW_BOAT = 0,
  ROW_UNITS,
  ROW_CALIBRATE,
  ROW_DIAGNOSTIC,
  NUM_ROWS
};

static Window *s_menu_window;
static MenuLayer *s_menu;
static wh_settings *s_cfg;

static Window *s_calib_window;
static Layer *s_calib_layer;
static AppTimer *s_calib_timer;
static wave_calibration s_calib;
static bool s_calib_finished;
static float s_calib_value;

/* ---- calibration screen ------------------------------------------------ */

static void calib_update_proc(Layer *layer, GContext *ctx) {
  const GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);

  graphics_draw_text(ctx, "Calibrating",
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(b.origin.x + 6, b.origin.y + 6, b.size.w - 12, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                     NULL);

  if (s_calib_finished) {
    char msg[64];
    /* Shown in the same units as the plan and the code: a spectral density.
     * Scaled by 1e6 purely so it fits on a watch screen as an integer. */
    snprintf(msg, sizeof(msg), "Done\n\nfloor = %d e-6",
             (int)(s_calib_value * 1.0e6f + 0.5f));
    graphics_draw_text(ctx, msg, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(b.origin.x + 8, b.origin.y + 50, b.size.w - 16, 120),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  graphics_draw_text(ctx, "Rest the watch on\na flat surface and\nleave it alone.",
                     fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(b.origin.x + 8, b.origin.y + 42, b.size.w - 16, 80),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  char prog[48];
  snprintf(prog, sizeof(prog), "%d / %d", wave_calib_progress(&s_calib),
           wave_calib_target(&s_calib));
  graphics_draw_text(ctx, prog, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(b.origin.x, b.origin.y + 130, b.size.w, 36),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                     NULL);

  /* Rejected segments are the signal that the watch is being held rather than
   * resting. Without this the screen would just appear stuck. */
  const int rejected = wave_calib_rejected(&s_calib);
  if (rejected > 0) {
    char note[48];
    snprintf(note, sizeof(note), "%d discarded - keep it still", rejected);
    graphics_context_set_text_color(ctx, GColorRed);
    graphics_draw_text(ctx, note, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(b.origin.x + 4, b.origin.y + 172, b.size.w - 8, 40),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}

static void calib_tick(void *context) {
  s_calib_timer = NULL;
  if (s_calib_layer == NULL) {
    return;
  }

  if (!s_calib_finished && wave_calib_done(&s_calib)) {
    s_calib_value = wave_calib_result(&s_calib);
    s_calib_finished = true;
    wave_calib_stop(&s_calib);

    if (s_cfg != NULL) {
      s_cfg->noise_floor = s_calib_value;
      wh_settings_save(s_cfg);
    }
    vibes_short_pulse();
  }

  layer_mark_dirty(s_calib_layer);
  s_calib_timer = app_timer_register(500, calib_tick, NULL);
}

static void calib_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_calib_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_calib_layer, calib_update_proc);
  layer_add_child(root, s_calib_layer);
  window_set_background_color(window, GColorWhite);

  s_calib_finished = false;
  s_calib_value = 0.0f;
  wave_calib_start(&s_calib, WAVE_ACQ_RATE_HZ,
                   (s_cfg != NULL) ? s_cfg->full_scale_mg
                                   : WH_DEFAULT_FULL_SCALE_MG);
  s_calib_timer = app_timer_register(500, calib_tick, NULL);
}

static void calib_window_unload(Window *window) {
  /* Leaving early simply abandons the measurement; the stored floor is only
   * replaced once a run completes. */
  wave_calib_stop(&s_calib);
  if (s_calib_timer != NULL) {
    app_timer_cancel(s_calib_timer);
    s_calib_timer = NULL;
  }
  layer_destroy(s_calib_layer);
  s_calib_layer = NULL;
  window_destroy(s_calib_window);
  s_calib_window = NULL;
  if (s_menu != NULL) {
    menu_layer_reload_data(s_menu);
  }
}

static void push_calibration(void) {
  s_calib_window = window_create();
  window_set_window_handlers(s_calib_window, (WindowHandlers){
                                                 .load = calib_window_load,
                                                 .unload = calib_window_unload,
                                             });
  window_stack_push(s_calib_window, true);
}

bool wh_settings_feed_accel(const wave_accel_sample *samples, int n) {
  if (!wave_calib_active(&s_calib)) {
    return false;
  }
  wave_calib_push(&s_calib, samples, n);
  return true;
}

/* ---- menu -------------------------------------------------------------- */

static uint16_t menu_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  return NUM_ROWS;
}

static int16_t menu_cell_height(MenuLayer *ml, MenuIndex *index, void *ctx) {
  return 44;
}

static void menu_draw_row(GContext *ctx, const Layer *cell_layer,
                          MenuIndex *index, void *context) {
  char value[32];
  const char *title = "";

  switch (index->row) {
    case ROW_BOAT:
      title = "Boat length";
      snprintf(value, sizeof(value), "%d m", (int)(s_cfg->boat_length_m + 0.5f));
      break;
    case ROW_UNITS:
      title = "Units";
      snprintf(value, sizeof(value), "%s", s_cfg->use_feet ? "feet" : "metres");
      break;
    case ROW_CALIBRATE:
      title = "Noise floor";
      if (s_cfg->noise_floor > 0.0f) {
        snprintf(value, sizeof(value), "%d e-6 - redo",
                 (int)(s_cfg->noise_floor * 1.0e6f + 0.5f));
      } else {
        snprintf(value, sizeof(value), "not calibrated");
      }
      break;
    case ROW_DIAGNOSTIC:
      title = "Diagnostic";
      snprintf(value, sizeof(value), "%s", s_cfg->diagnostic_mode ? "ON" : "off");
      break;
    default:
      value[0] = '\0';
      break;
  }
  menu_cell_basic_draw(ctx, cell_layer, title, value, NULL);
}

static void menu_select(MenuLayer *ml, MenuIndex *index, void *context) {
  switch (index->row) {
    case ROW_BOAT: {
      /* Cycle rather than offering a sub-menu: five values, and the setting only
       * drives a warning threshold, so precision beyond this is not useful. */
      int i = 0;
      for (; i < (int)NUM_LENGTHS; i++) {
        if ((int)(s_cfg->boat_length_m + 0.5f) == (int)s_lengths[i]) {
          break;
        }
      }
      i = (i + 1) % (int)NUM_LENGTHS;
      s_cfg->boat_length_m = (float)s_lengths[i];
      break;
    }
    case ROW_UNITS:
      s_cfg->use_feet = !s_cfg->use_feet;
      break;
    case ROW_CALIBRATE:
      push_calibration();
      return;
    case ROW_DIAGNOSTIC:
      s_cfg->diagnostic_mode = !s_cfg->diagnostic_mode;
      break;
    default:
      break;
  }
  menu_layer_reload_data(s_menu);
}

static void menu_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);

  s_menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
                                             .get_num_rows = menu_num_rows,
                                             .draw_row = menu_draw_row,
                                             .select_click = menu_select,
                                             .get_cell_height = menu_cell_height,
                                         });
  menu_layer_set_click_config_onto_window(s_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void menu_window_unload(Window *window) {
  /* Persist on the way out, so a setting changed here survives the app being
   * closed -- which is the whole reason the store exists. */
  if (s_cfg != NULL) {
    wh_settings_save(s_cfg);
  }
  menu_layer_destroy(s_menu);
  s_menu = NULL;
  window_destroy(s_menu_window);
  s_menu_window = NULL;
}

void wh_settings_window_push(wh_settings *cfg) {
  s_cfg = cfg;
  s_menu_window = window_create();
  window_set_window_handlers(s_menu_window, (WindowHandlers){
                                                .load = menu_window_load,
                                                .unload = menu_window_unload,
                                            });
  window_stack_push(s_menu_window, true);
}
