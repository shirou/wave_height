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
 * 100 (mG)^2 is 10 mG rms. Measured on the host: a still watch produces about
 * 4.5, a 40 mG shake about 440, and a 400 mG shake about 44000. So the floor
 * sits an order of magnitude above sensor noise and two below real hand
 * movement. The exact value is not critical -- anything from roughly 5 to 40000
 * behaves the same -- but both ends are pinned by tests, so it cannot drift far
 * enough to matter. */
#define WAVE_Q_HF_ABSOLUTE_MIN 100.0f

/* Maximum drift of the gravity direction across one segment, as a cosine (see
 * wave_vec3_angle_exceeds). cos(2 degrees). */
#define WAVE_Q_DRIFT_COS 0.9993908f

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

/*
 * Sustained machinery vibration.
 *
 * The noise floor is calibrated ashore, so it covers the sensor and nothing
 * else. Running an engine puts vibration into the accelerometer, and whatever
 * part of it lands in the 0.063-0.5 Hz integration band is added to the wave
 * height with nothing to offset it.
 *
 * It is not corrected for, deliberately. In-band vibration is indistinguishable
 * from wave energy, and estimating it from the high-frequency end is the same
 * mistake as estimating the noise floor that way: the acceleration spectrum of
 * a real sea goes as f^-1, so what looks like a vibration shoulder is largely
 * genuine signal, and subtracting it biases Hs low. Better to say the reading
 * is inflated than to silently deflate it.
 *
 * What distinguishes vibration from body motion is that it does not stop. Body
 * motion is a few seconds; an engine runs for the whole trip. So the test is
 * the live ratio staying above its warning threshold far longer than any
 * movement lasts.
 *
 * The ratio, not the absolute level. The first attempt compared live_hf_power
 * against the still-watch level measured during calibration, times four, and
 * that violated the rule stated at the top of this file: the high-frequency
 * check has to be relative, because the same 1 Hz first-order high pass that
 * feeds it passes 45% of 0.5 Hz and 16% of 0.167 Hz, so a real sea puts plenty
 * of genuine wave acceleration into it. Measured on clean synthetic seas with
 * no vibration at all, the still-watch reference is about 30 (mG)^2 while
 * live_hf_power reaches 2661 at Hs 2.0 m / Tp 6 s -- the warning fired on 44 of
 * 60 batches while the app was correctly reporting 2.10 m. Dividing by the wave
 * band variance of the last completed segment removes the sea state from the
 * comparison, which is exactly why the hold-still indicator already works that
 * way.
 *
 * Note this makes the vibration warning coincide with the hold-still
 * condition, so the display has to rank it FIRST or it stays masked -- see
 * status_text in ui.c.
 *
 * What this cannot see: machinery with no content above the 1 Hz corner. A
 * 0.25 Hz forced hull motion of 20 mG reads as Hs 0.46 m on a real 0.30 m sea
 * and raises nothing, because the evidence and the contamination are then the
 * same signal. That is the wall the whole design runs into, not a bug in the
 * threshold -- so the warning means "inflated", never "clean when absent".
 */

/* How long the live ratio must stay above WAVE_Q_LIVE_RATIO_WARN before the
 * cause is called machinery rather than the user. Shorter than one 32 s
 * segment, so the warning appears before a contaminated segment can complete,
 * and far longer than a movement: a 10 s reach for the throttle keeps the ratio
 * up for about 14 s once the 2 s live EMA has decayed. */
#define WAVE_Q_VIB_HOLD_S 25.0f

/* A dip this short does not break the case for machinery.
 *
 * Requiring every single batch to be over the threshold made the warning
 * useless: an engine's amplitude wanders with load and idle hunting, and a
 * measured 1.5 s dip below the threshold cleared a latched warning and demanded
 * another 25.8 s of continuous vibration to get it back -- 26 s of inflated
 * readings with nothing on screen. The same dead zone stopped the warning
 * appearing at all when the level sat near the threshold and the EMA rippled
 * across it.
 *
 * Before the warning latches, a gap longer than this resets the clock, because
 * what is being established is continuity, and repeated short movements
 * separated by real quiet are not continuity. After it latches, clearing needs
 * a full WAVE_Q_VIB_HOLD_S of quiet -- the same evidence to stop as to start,
 * and the accumulated spectrum stays contaminated for far longer than that
 * anyway. */
#define WAVE_Q_VIB_GAP_S 3.0f

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
