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

#include "session.h"

#include <math.h>
#include <string.h>

#include "spectrum.h"

void wave_session_init(wave_session *s, float acq_rate_hz, float noise_floor,
                       float full_scale_mg) {
  memset(s, 0, sizeof(*s));
  s->acq_rate = (acq_rate_hz > 0.0f) ? acq_rate_hz : WAVE_ACQ_RATE_HZ;
  s->dt = 1.0f / s->acq_rate;

  wave_gravity_init(&s->grav, s->acq_rate);
  wave_decim_init(&s->dec);
  wave_quality_init(&s->qual, s->acq_rate, full_scale_mg);
  wave_accum_init(&s->acc, noise_floor);

  s->state = WAVE_STATE_SETTLING;
}

/* Close out a full segment: score it, and either fold it in or throw it away. */
static void finish_segment(wave_session *s) {
  const bool ok = s->diagnostic || wave_quality_segment_ok(&s->qual);

  /* Latch this segment's wave-band variance as the denominator for the live
   * warning. Only from accepted segments: a segment full of body motion has an
   * inflated variance, and using it would raise the bar just when the user most
   * needs to be told to hold still. */
  if (ok) {
    wave_quality_set_reference(&s->qual, wave_quality_band_var(&s->qual));
  }

  if (ok) {
    float psd[WAVE_NBINS];
    wave_spectrum_segment(s->seg, psd);
    wave_accum_add(&s->acc, psd);
    s->valid_s += (float)WAVE_SEG_SAMPLES / WAVE_PROC_RATE_HZ;
    s->rejected_run = 0;
    s->state = WAVE_STATE_MEASURING;
  } else {
    s->rejected_total++;
    s->rejected_run++;
    s->state = WAVE_STATE_PAUSED;
    /* Note what is NOT done here: the accumulator is left alone. Discarding a
     * contaminated segment must not throw away the good ones already banked,
     * or a single fidget would cost the user everything. */
  }

  s->seg_filled = 0;
  wave_quality_begin_segment(&s->qual, s->grav.g_hat);
}

void wave_session_push(wave_session *s, const wave_accel_sample *samples,
                       int n) {
  for (int i = 0; i < n; i++) {
    const wave_accel_sample *smp = &samples[i];
    wave_vec3 a;
    a.x = (float)smp->x;
    a.y = (float)smp->y;
    a.z = (float)smp->z;

    s->elapsed_s += s->dt;
    wave_gravity_push(&s->grav, a);

    if (!wave_gravity_converged(&s->grav)) {
      /* Still averaging the gravity direction. Collect nothing: starting a
       * segment now and discarding it later would push the first reading out to
       * two segment lengths, whereas simply waiting keeps it at one. */
      s->state = WAVE_STATE_SETTLING;
      s->seg_filled = 0;
      wave_decim_init(&s->dec);
      continue;
    }

    if (s->state == WAVE_STATE_SETTLING) {
      /* Just became usable. */
      s->state = WAVE_STATE_MEASURING;
      s->seg_filled = 0;
      wave_decim_init(&s->dec);
      wave_quality_begin_segment(&s->qual, s->grav.g_hat);
    }

    float vert_mg = 0.0f;
    if (!wave_gravity_project(&s->grav, a, &vert_mg)) {
      /* Degenerate gravity estimate: free fall, or a broken sensor. Drop the
       * segment in progress rather than feeding it nonsense, and re-anchor the
       * quality window with it -- otherwise the drift reference and the
       * high-frequency sums carry over from the abandoned segment and the next
       * one gets scored against statistics from a different interval. */
      s->seg_filled = 0;
      wave_decim_init(&s->dec);
      wave_quality_begin_segment(&s->qual, s->grav.g_hat);
      continue;
    }

    wave_quality_push_raw(&s->qual, a, vert_mg, smp->did_vibrate);
    wave_quality_update_gravity(&s->qual, s->grav.g_hat);

    float dec_out;
    if (wave_decim_push(&s->dec, vert_mg, &dec_out)) {
      wave_quality_push_decimated(&s->qual, dec_out);
      if (s->seg_filled < WAVE_SEG_SAMPLES) {
        s->seg[s->seg_filled++] = dec_out * WAVE_MG_TO_MS2;
      }
      if (s->seg_filled >= WAVE_SEG_SAMPLES) {
        finish_segment(s);
      }
    }
  }

  /* Refresh the live indicator once per batch. It is a separate, continuously
   * maintained estimate rather than the per-segment one, which restarts at every
   * boundary and spikes for the first seconds of a new segment. */
  s->live_hf_ratio = wave_quality_live_ratio(&s->qual);

  /* Leave PAUSED as soon as the motion actually stops.
   *
   * Clearing it only on the next successful segment made the warning unclearable
   * rather than prompt: a four-second twitch kept "hold still" on screen for a
   * further minute, most of it while the user was already perfectly still. The
   * rejection itself is still recorded in rejected_run, which is what drives the
   * stronger "reposition" advice. */
  if (s->state == WAVE_STATE_PAUSED &&
      s->live_hf_ratio <= WAVE_Q_LIVE_RATIO_WARN) {
    s->state = WAVE_STATE_MEASURING;
  }
}

float wave_session_round_hs(float hs, wave_confidence conf) {
  const float step = (conf <= WAVE_CONF_LOW) ? 0.5f : 0.1f;
  return floorf(hs / step + 0.5f) * step;
}

void wave_session_get_display(const wave_session *s, wave_display *out) {
  memset(out, 0, sizeof(*out));
  out->state = s->state;
  out->valid_s = s->valid_s;

  wave_accum_result(&s->acc, &out->result);
  out->hs_display =
      out->result.valid
          ? wave_session_round_hs(out->result.hs, out->result.conf)
          : 0.0f;

  /* Warn from the live indicator, not from the segment verdict, so that the
   * message appears within a batch of the user moving. */
  out->warn_hold_still = (s->state == WAVE_STATE_PAUSED) ||
                         (s->live_hf_ratio > WAVE_Q_LIVE_RATIO_WARN);
  out->warn_reposition = (s->rejected_run >= WAVE_REPOSITION_AFTER);
}

void wave_session_save(const wave_session *s, wave_session_snapshot *out,
                       uint32_t unix_time) {
  memset(out, 0, sizeof(*out));
  memcpy(out->s_avg, s->acc.s_avg, sizeof(out->s_avg));
  out->magic = WAVE_SNAPSHOT_MAGIC;
  out->n_seg = (int32_t)s->acc.n_seg;
  out->unix_time = unix_time;
}

bool wave_session_restore(wave_session *s, const wave_session_snapshot *snap,
                          uint32_t unix_time, uint32_t max_age_s) {
  if (snap->magic != WAVE_SNAPSHOT_MAGIC) {
    return false;
  }
  /* An implausible count means a corrupt entry, and it would also let
   * wave_accum_add's n_seg + 1 run away. */
  if (snap->n_seg <= 0 || snap->n_seg > WAVE_SNAPSHOT_MAX_SEG) {
    return false;
  }
  /* Reject anything from the future too: a clock change should not resurrect
   * arbitrarily old data. */
  if (unix_time < snap->unix_time ||
      (unix_time - snap->unix_time) > max_age_s) {
    return false;
  }
  /* One NaN would poison the exponential average permanently, and so, nearly as
   * badly, would a finite-but-absurd value. */
  for (int k = 0; k < WAVE_NBINS; k++) {
    if (!isfinite(snap->s_avg[k]) || snap->s_avg[k] < 0.0f ||
        snap->s_avg[k] > WAVE_SNAPSHOT_MAX_PSD) {
      return false;
    }
  }

  memcpy(s->acc.s_avg, snap->s_avg, sizeof(s->acc.s_avg));
  s->acc.n_seg = (int)snap->n_seg;

  /* Report the restored accumulation as elapsed valid time so the progress
   * indicator does not jump backwards. */
  s->valid_s = (float)snap->n_seg * (float)WAVE_SEG_SAMPLES / WAVE_PROC_RATE_HZ;
  return true;
}
