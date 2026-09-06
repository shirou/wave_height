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

#ifndef WAVE_SESSION_H
#define WAVE_SESSION_H

#include "accumulator.h"
#include "decimate.h"
#include "gravity.h"
#include "quality.h"
#include "wave_types.h"

/*
 * The measurement state machine: raw accelerometer batches in, display state
 * out.
 *
 * This lives in the SDK-free core on purpose. Pausing, resuming, segment
 * boundaries, settling and the rejection bookkeeping are the fiddliest part of
 * the app, and if they sat in main.c alongside the Pebble calls they could only
 * ever be exercised on the watch. Here they can be driven from a host test with
 * a synthetic sea, including the awkward cases: a rejected segment in the
 * middle of a run, a restore from persisted state, the exponential average
 * taking over from the arithmetic one.
 */

typedef enum {
  WAVE_STATE_SETTLING = 0, /* gravity estimate not yet usable */
  WAVE_STATE_MEASURING,    /* filling a segment */
  WAVE_STATE_PAUSED        /* body motion; segments are being discarded */
} wave_session_state;

/* How many consecutive rejected segments before the advice changes from "hold
 * still" to "put your hand back". One bad segment is a twitch; three in a row
 * means the posture itself is wrong. */
#define WAVE_REPOSITION_AFTER 3

typedef struct {
  wave_gravity grav;
  wave_decimator dec;
  wave_quality qual;
  wave_accumulator acc;

  wave_session_state state;
  float seg[WAVE_SEG_SAMPLES]; /* decimated vertical acceleration, m/s^2 */
  int seg_filled;

  int rejected_run;   /* consecutive rejected segments */
  int rejected_total;
  float elapsed_s;    /* since init or restore */
  float valid_s;      /* time folded into the average */
  float acq_rate;
  float dt;

  /* Immediate contamination indicator, refreshed every batch rather than every
   * segment. Waiting for the segment to close would delay the warning by up to
   * 32 s, which is far too late to change what the user is doing. */
  float live_hf_ratio;

  /* Accept every segment regardless of the quality verdict.
   *
   * Required for the known-motion bench test: waving the watch by hand at a
   * fixed period trips the high-frequency check on every segment, so without a
   * bypass that test cannot be run at all. Never enable it for real
   * measurements. */
  bool diagnostic;
} wave_session;

typedef struct {
  wave_session_state state;
  wave_result result;
  float hs_display;   /* result.hs rounded to match the confidence */
  float valid_s;
  bool warn_hold_still;
  bool warn_reposition;
} wave_display;

/* full_scale_mg and noise_floor both come from the on-device spike; see the
 * plan. noise_floor is a one-sided PSD in (m/s^2)^2/Hz, not a sigma. */
void wave_session_init(wave_session *s, float acq_rate_hz, float noise_floor,
                       float full_scale_mg);

/* Feed one accelerometer batch, exactly as the SDK delivers it. */
void wave_session_push(wave_session *s, const wave_accel_sample *samples, int n);

void wave_session_get_display(const wave_session *s, wave_display *out);

/* Round Hs to a step the measurement can actually support: 0.5 m while only one
 * or two segments are in (relative sd 23-31%), 0.1 m after that. Showing 0.1 m
 * steps on a +/-31% estimate would be inventing precision. */
float wave_session_round_hs(float hs, wave_confidence conf);

/*
 * Persisted state, so that an app relaunch inside a few minutes can carry on
 * accumulating.
 *
 * This is not a convenience. The design asks the user to build up segments
 * across several separate holds, because they cannot keep still for more than a
 * minute; if closing the app threw the accumulation away, reaching three stars
 * would be impossible in practice. Being an exponential average rather than a
 * ring buffer is what makes it restorable at all -- there is no oldest element
 * that would have to be reconstructed.
 *
 * 140 bytes: float[WAVE_NBINS] (128) + magic (4) + n_seg (4) + time (4), inside
 * PERSIST_DATA_MAX_LENGTH (256). Note that raising WAVE_SEG_SAMPLES would double
 * WAVE_NBINS and push this over the limit.
 */

/* Identifies the layout. persist_read_data only tells us the byte count, and a
 * struct change that happens to keep the same size would otherwise be
 * reinterpreted silently -- restoring an old spectrum under a new meaning, which
 * the exponential average would then carry for its whole horizon. Bump this
 * whenever the layout or the meaning of the bins changes. */
#define WAVE_SNAPSHOT_MAGIC 0x57530001u

/* Plausibility cap on the restored segment count. Well above anything a real
 * session reaches, but bounded so a corrupt value cannot run away. */
#define WAVE_SNAPSHOT_MAX_SEG 100000

/* Plausibility ceiling on a restored spectral density.
 *
 * Finiteness alone is not enough: a corrupt but finite value such as FLT_MAX
 * restores cleanly, drives m0 to infinity, and shows as "calm" -- and because
 * the exponential average decays by at most 1/16 per segment, it would take
 * something like twelve hours of measuring to wash out. The accelerometer
 * saturates around 4 g, so even an all-clipping segment stays far below this. */
#define WAVE_SNAPSHOT_MAX_PSD 1.0e4f

typedef struct {
  float s_avg[WAVE_NBINS];
  uint32_t magic;
  int32_t n_seg;
  uint32_t unix_time; /* when it was saved */
} wave_session_snapshot;

void wave_session_save(const wave_session *s, wave_session_snapshot *out,
                       uint32_t unix_time);

/* Restore only if the snapshot is recent, well-formed and finite.
 *
 * Rejects: a wrong magic, a segment count outside the plausible range, a
 * timestamp in the future (which is what a clock change looks like), and any
 * non-finite spectrum value. The last one matters because a single NaN would
 * survive every subsequent exponential-average update and make the app report
 * "calm" forever, with no way for the user to clear it. */
bool wave_session_restore(wave_session *s, const wave_session_snapshot *snap,
                          uint32_t unix_time, uint32_t max_age_s);

#endif /* WAVE_SESSION_H */
