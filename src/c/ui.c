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

#include "ui.h"

#include <stdio.h>
#include <string.h>

static Layer *s_layer;
static wave_display s_display;
static const wh_settings *s_cfg;
static bool s_have_display;
static bool s_logging_ok = true;
static bool s_rate_ok = true;

/*
 * Format a non-negative value to one decimal place.
 *
 * The obvious "%d.%d" with (v - (int)v) * 10 + 0.5 has no carry: once the
 * fraction reaches 0.95 the tenths digit rounds to 10 and prints as two digits,
 * so 1.9685 comes out "1.10" instead of "2.0". That is the main number on the
 * screen. Rounding the whole value to tenths first and then splitting avoids it.
 */
static void format_1dp(char *buf, size_t len, float v) {
  if (!(v >= 0.0f)) {
    v = 0.0f;
  }
  const int tenths = (int)(v * 10.0f + 0.5f);
  snprintf(buf, len, "%d.%d", tenths / 10, tenths % 10);
}

/* Confidence is drawn as three pips rather than written out: the number is
 * meant to be read at a glance on a moving boat. */
#define PIP_R 5
#define PIP_GAP 15

static void draw_pips(GContext *ctx, GRect bounds, wave_confidence conf) {
  const int filled = (conf == WAVE_CONF_HIGH)  ? 3
                     : (conf == WAVE_CONF_MID) ? 2
                     : (conf == WAVE_CONF_LOW) ? 1
                                               : 0;
  const int cy = bounds.origin.y + 12;
  int cx = bounds.origin.x + bounds.size.w - 12;

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorBlack);
  for (int i = 2; i >= 0; i--) {
    const GPoint p = GPoint(cx, cy);
    if (i < filled) {
      graphics_fill_circle(ctx, p, PIP_R);
    } else {
      graphics_draw_circle(ctx, p, PIP_R);
    }
    cx -= PIP_GAP;
  }
}

static const char *status_text(const wave_display *d) {
  if (d->warn_reposition) {
    return "Reposition hand";
  }
  if (d->warn_hold_still) {
    return "Hold still";
  }
  switch (d->state) {
    case WAVE_STATE_SETTLING:
      return "Levelling";
    case WAVE_STATE_PAUSED:
      return "Hold still";
    case WAVE_STATE_MEASURING:
    default:
      return "Measuring";
  }
}

static void update_proc(Layer *layer, GContext *ctx) {
  const GRect b = layer_get_bounds(layer);

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  if (!s_have_display || s_cfg == NULL) {
    return;
  }
  const wave_display *d = &s_display;

  /* ---- status line ---- */
  const bool warn = d->warn_hold_still || d->warn_reposition;
  graphics_context_set_text_color(ctx, warn ? GColorRed : GColorBlack);
  graphics_draw_text(ctx, status_text(d), fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(b.origin.x + 6, b.origin.y + 2, b.size.w - 60, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  draw_pips(ctx, b, d->result.conf);

  /* ---- the number ----
   *
   * TODO: a custom 70 px numeric subset (about 5 kB of resources) would read
   * better in direct sunlight. LECO_42_NUMBERS is the built-in fallback and is
   * what the first build ships with, so that font work cannot block bring-up. */
  char num[16];
  char unit[8];
  if (!s_rate_ok) {
    /* The frequency axis is wrong, so every height derived from it is wrong --
     * by about 39x if the watch fell back to its 25 Hz default. Show nothing
     * rather than something believable. */
    snprintf(num, sizeof(num), "rate?");
    unit[0] = '\0';
  } else if (d->result.valid && d->result.hs >= WAVE_CALM_BELOW_M) {
    const float h = wh_display_height(s_cfg, d->hs_display);
    /* One decimal is right for 0.1 m steps and harmless for 0.5 m steps. */
    format_1dp(num, sizeof(num), h);
    snprintf(unit, sizeof(unit), "%s", wh_height_unit(s_cfg));
  } else if (d->result.n_seg > 0) {
    /* A segment was measured and there is no energy in the band: that is a
     * calm, not a failure. Testing the state instead would claim "calm" for the
     * whole first segment -- 32 s of telling the user the sea is flat while
     * they are sitting in it. */
    snprintf(num, sizeof(num), "calm");
    unit[0] = '\0';
  } else {
    snprintf(num, sizeof(num), "--");
    unit[0] = '\0';
  }

  graphics_context_set_text_color(ctx, GColorBlack);
  const bool numeric =
      (d->result.valid && s_rate_ok && d->result.hs >= WAVE_CALM_BELOW_M);
  GFont big = numeric ? fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS)
                      : fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);

  graphics_draw_text(ctx, num, big,
                     GRect(b.origin.x, b.origin.y + 62, b.size.w - 48, 56),
                     GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
  if (unit[0] != '\0') {
    graphics_draw_text(ctx, unit, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(b.origin.x + b.size.w - 44, b.origin.y + 84, 40, 30),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }

  /* ---- period and one-tenth height ---- */
  /* Two lines rather than one. At 24 px "Period 16s  1/10 0.0m" would wrap,
   * and a wrapped line reads worse than a smaller one -- so the content is
   * split deliberately instead of being left to the layout engine. */
  char sub1[24];
  char sub2[24];
  if (!s_rate_ok) {
    snprintf(sub1, sizeof(sub1), "Wrong sample");
    snprintf(sub2, sizeof(sub2), "rate");
  } else if (d->result.valid && d->result.hs >= WAVE_CALM_BELOW_M &&
             d->result.period > 0.0f) {
    /* Whole seconds only: the single-segment spread of Tm-1,0 is about 0.57 s,
     * so a decimal place would be fiction. */
    snprintf(sub1, sizeof(sub1), "Period %ds", (int)(d->result.period + 0.5f));
    char tenth[16];
    format_1dp(tenth, sizeof(tenth),
               wh_display_height(s_cfg, d->result.h_one_tenth));
    snprintf(sub2, sizeof(sub2), "1/10 %s%s", tenth, wh_height_unit(s_cfg));
  } else {
    sub1[0] = 0;
    sub2[0] = 0;
  }
  GFont sub_font = fonts_get_system_font(FONT_KEY_GOTHIC_24);
  graphics_draw_text(ctx, sub1, sub_font,
                     GRect(b.origin.x + 4, b.origin.y + 110, b.size.w - 8, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                     NULL);
  graphics_draw_text(ctx, sub2, sub_font,
                     GRect(b.origin.x + 4, b.origin.y + 137, b.size.w - 8, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                     NULL);

  /* ---- short-wave warning ----
   *
   * The boat stops following waves shorter than this, so the reading is low.
   * Nothing in the number itself hints at that, hence the explicit note. */
  if (d->result.valid && d->result.hs >= WAVE_CALM_BELOW_M &&
      d->result.period > 0.0f &&
      d->result.period < wh_follow_limit_period(s_cfg)) {
    graphics_context_set_text_color(ctx, GColorRed);
    graphics_draw_text(ctx, "Short waves - reads low",
                       fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(b.origin.x + 4, b.origin.y + 168, b.size.w - 8, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                       NULL);
    graphics_context_set_text_color(ctx, GColorBlack);
  }

  /* ---- accumulated time ---- */
  const int secs = (int)d->valid_s;
  char timebuf[16];
  snprintf(timebuf, sizeof(timebuf), "%d:%02d", secs / 60, secs % 60);

  const int bar_x = b.origin.x + 6;
  const int bar_y = b.origin.y + b.size.h - 24;
  const int bar_w = b.size.w - 84;
  const int bar_h = 10;

  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_draw_rect(ctx, GRect(bar_x, bar_y, bar_w, bar_h));

  /* Full bar at the point where confidence stops improving much. */
  const float target_s = (float)WAVE_EMA_MAX_SEG * WAVE_SEG_SAMPLES / WAVE_PROC_RATE_HZ;
  float frac = (target_s > 0.0f) ? (d->valid_s / target_s) : 0.0f;
  if (frac > 1.0f) {
    frac = 1.0f;
  }
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, GRect(bar_x + 1, bar_y + 1, (int)((bar_w - 2) * frac), bar_h - 2),
                     0, GCornerNone);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, timebuf, fonts_get_system_font(FONT_KEY_GOTHIC_24),
                     GRect(bar_x + bar_w + 4, bar_y - 11, 76, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  /* Raw logging is the only way to work out afterwards why a sea trial
   * disagreed with the reference buoy, and it is expected to run unattended
   * while out of phone range. If it is not running, the user has to be told on
   * the watch -- discovering it back ashore is too late. */
  if (!s_logging_ok) {
    graphics_context_set_text_color(ctx, GColorRed);
    graphics_draw_text(ctx, "no raw log",
                       fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(bar_x, bar_y + 12, bar_w, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                       NULL);
  }
}

void wh_ui_create(Window *window, const wh_settings *cfg) {
  s_cfg = cfg;
  s_have_display = false;

  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, update_proc);
  layer_add_child(root, s_layer);

  window_set_background_color(window, GColorWhite);
}

void wh_ui_destroy(void) {
  if (s_layer != NULL) {
    layer_destroy(s_layer);
    s_layer = NULL;
  }
}

void wh_ui_set(const wave_display *d) {
  s_display = *d;
  s_have_display = true;
  if (s_layer != NULL) {
    layer_mark_dirty(s_layer);
  }
}

void wh_ui_set_logging(bool ok) {
  s_logging_ok = ok;
}

void wh_ui_set_rate_ok(bool ok) {
  s_rate_ok = ok;
}
