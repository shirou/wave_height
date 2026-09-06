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

#ifndef WAVE_QUALITY_H
#define WAVE_QUALITY_H

#include "wave_types.h"

/*
 * Per-segment quality gating.
 *
 * Two independent checks, for two different failure modes:
 *
 *   Attitude drift -- the angle between the low-passed gravity direction at the
 *     start and at the end of the segment. Because g_hat is a 30 s low pass,
 *     wave-band roll is attenuated about 30:1 and barely moves it; a 15 degree
 *     threshold would therefore never fire. The useful threshold is 2 degrees,
 *     since a drift of only 20 mG (1.1 degrees) already fabricates 0.157 m of Hs.
 *
 *   High-frequency motion -- energy above 1 Hz as a fraction of energy in the
 *     wave band. This has to be a ratio, not an absolute level: a 2 s chop at
 *     Hs = 0.5 m has 13x the difference energy of a 6 s swell at Hs = 1.0 m, so
 *     an absolute threshold calibrated on swell would reject every segment in a
 *     choppy sea -- exactly the situation the gating was meant to survive.
 *
 * Note what is deliberately NOT rejected: wave-driven roll. It is not removed by
 * detrending or by projection -- r*sin(phi(t)) is a real displacement of the
 * measurement point. We accept the contamination because rejecting it would bias
 * the result: rough conditions roll more, so rolling segments would be dropped
 * preferentially and Hs would read systematically low. The countermeasure is
 * where the watch is placed, not which segments are discarded.
 */

/* Ratio of above-1 Hz energy to wave-band energy beyond which a segment is
 * treated as contaminated by body motion. */
#define WAVE_Q_HF_RATIO_MAX 0.3f

/* Absolute floor on the high-frequency power, in (milli-g)^2, below which a
 * segment is accepted whatever the ratio says.
 *
 * The ratio silently assumes there are waves to compare against. On a still
 * watch there are none: sensor noise is white, so the high-frequency and
 * wave-band powers come out comparable and the ratio sits near 1 with nothing
 * moving at all -- every segment rejected. That is not academic, it is exactly
 * the condition noise-floor calibration runs in, and without this floor
 * calibration can never complete.
 *
 * 100 (mG)^2 is 10 mG rms, several times the assumed sensor noise (which
 * contributes about 7) and well below any real hand movement (a 400 mG shake is
 * 80000). */
#define WAVE_Q_HF_ABSOLUTE_MIN 100.0f

/* Maximum drift of the gravity direction across one segment, in degrees. */
#define WAVE_Q_DRIFT_DEG_MAX 2.0f

/* Minimum decimated samples before the per-segment ratio means anything. A
 * variance over two or three samples is noise, not a measurement. */
#define WAVE_Q_MIN_BAND_SAMPLES 8

/* The LIVE indicator is a separate estimate from the per-segment one.
 *
 * The per-segment sums restart at every boundary, which is right for scoring a
 * segment but useless for a live warning: for the first seconds of a new segment
 * the wave-band variance spans less than one wave period and comes out far too
 * small, so the ratio spikes. Measured on a clean 1 m sea with no motion at all,
 * that read 0.57 just after a boundary and held a spurious "hold still" on
 * screen for a further 14 s.
 *
 * So the live estimate pairs a fast-reacting motion term with a STABLE
 * denominator: the wave-band variance of the last completed segment. That is a
 * full 32 s of data, immune to boundary effects, and it means no warning can be
 * raised before the first segment finishes -- which is fine, because that
 * segment is scored on its own merits anyway. */
#define WAVE_Q_LIVE_HF_TAU_S 2.0f

/* Warning threshold for the live indicator, deliberately above the rejection
 * threshold. A warning that fires on ordinary wave-to-wave variation trains the
 * user to ignore it; rejection can afford to be stricter because it costs only
 * a segment, not the user's trust. */
#define WAVE_Q_LIVE_RATIO_WARN 0.6f

/* Floor on the denominator of the live ratio, in (milli-g)^2.
 *
 * A dead-flat first segment -- a watch on a table, or a boat lying still in a
 * glassy calm -- has a wave-band variance of essentially zero. Dividing by it,
 * or refusing to divide at all, silences the warning entirely: hand motion
 * afterwards would go unremarked until the segment was rejected up to 32 s
 * later, which is exactly what the live indicator exists to avoid. This floor
 * is a few times the assumed sensor noise variance, so it never dominates a
 * real sea but always gives the ratio something to divide by. */
#define WAVE_Q_MIN_REF_VAR 4.0f

/* Samples within this margin of full scale count as clipped. Slamming on a
 * small boat can reach several g, and a clipped sample generates broadband
 * harmonics that pollute the whole spectrum. */
#define WAVE_Q_CLIP_MARGIN_MG 100.0f

typedef struct {
  /* High-pass state, run at the acquisition rate. */
  float hp_alpha;
  float hp_prev_in;
  float hp_prev_out;
  bool hp_primed;
  float hf_sum_sq;
  int hf_count;

  /* Wave-band energy, accumulated from the decimated stream. */
  float band_sum;
  float band_sum_sq;
  int band_count;

  /* Attitude drift across the segment. Both are anchored by
   * wave_quality_begin_segment, which every path that starts a segment calls. */
  wave_vec3 g_start;
  wave_vec3 g_end;

  /* Sticky flags. */
  bool vibrated;
  bool clipped;

  float clip_threshold_mg;

  /* Live indicator state, deliberately NOT reset at segment boundaries. */
  float live_hf_alpha;
  float live_hf_power;
  float ref_band_var;  /* wave-band variance of the last accepted segment */
  bool has_reference;  /* distinct from ref_band_var == 0, which is a valid calm */
} wave_quality;

/* full_scale_mg is measured on device during the spike (plan step 3d); pass the
 * datasheet value only as a fallback. */
void wave_quality_init(wave_quality *q, float sample_rate_hz, float full_scale_mg);

/* Start a fresh segment, anchoring the drift reference to the current gravity
 * direction. */
void wave_quality_begin_segment(wave_quality *q, wave_vec3 g_hat);

/* Feed one raw sample, before decimation. */
void wave_quality_push_raw(wave_quality *q, wave_vec3 a_mg, float a_vert_mg,
                           bool did_vibrate);

/* Feed one decimated vertical sample. */
void wave_quality_push_decimated(wave_quality *q, float a_vert_mg);

/* Update the end-of-segment gravity direction. Call whenever g_hat changes. */
void wave_quality_update_gravity(wave_quality *q, wave_vec3 g_hat);

/* Contamination ratio for the CURRENT SEGMENT, or 0 if too little of it has
 * elapsed to say. This is what decides whether the segment is kept. */
float wave_quality_hf_ratio(const wave_quality *q);

/* Contamination ratio from the live estimate. Drives the "hold still" warning:
 * reacts within a couple of seconds and does not jump at segment boundaries.
 * Returns 0 until a segment has completed and set the reference. */
float wave_quality_live_ratio(const wave_quality *q);

/* Wave-band variance of the current segment, for the caller to latch as the
 * reference once the segment is judged good. */
float wave_quality_band_var(const wave_quality *q);
void wave_quality_set_reference(wave_quality *q, float band_var);

/* Final verdict for the segment. */
bool wave_quality_segment_ok(const wave_quality *q);

/* Which check failed, for diagnostics and for choosing the on-screen message. */
typedef enum {
  WAVE_Q_OK = 0,
  WAVE_Q_FAIL_DRIFT,
  WAVE_Q_FAIL_HF,
  WAVE_Q_FAIL_CLIP,
  WAVE_Q_FAIL_VIBRATE
} wave_quality_verdict;

wave_quality_verdict wave_quality_verdict_of(const wave_quality *q);

#endif /* WAVE_QUALITY_H */
