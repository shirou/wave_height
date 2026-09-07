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

#ifndef WAVE_GRAVITY_H
#define WAVE_GRAVITY_H

#include "wave_types.h"

/*
 * Gravity-direction estimation and vertical projection.
 *
 * The watch can be held at any angle, so the vertical component is recovered by
 * projecting onto a low-passed estimate of the gravity vector:
 *
 *   a_vert = dot(a, g_hat)/|g_hat| - |g_hat|
 *
 * Horizontal motion only leaks in through a cosine, i.e. at second order, and a
 * steady heel is tracked out by the low pass.
 *
 * What this canNOT remove is the lever-arm term. If the watch sits away from
 * the roll axis, the measurement point genuinely rises and falls by
 * r*sin(phi(t)); no projection can subtract a real displacement. A gyro would
 * let us separate it, but the public SDK does not expose one. That is why the
 * measurement position matters (see the plan's limitation (B)).
 */

/* Time constant used once the estimate has settled. It has to be well below the
 * wave band so that drift is followed but wave-band roll is not: tracking the
 * waves would drain the very signal being measured into g_hat.
 *
 * Settling itself does NOT use an IIR -- see WAVE_G_SETTLE_S below for why. */
#ifndef WAVE_G_TAU_SLOW_S
#define WAVE_G_TAU_SLOW_S 30.0f
#endif

/* Settling uses a PLAIN AVERAGE over a fixed window, then hands over to the IIR.
 *
 * Two earlier attempts were wrong in instructive ways:
 *
 *   Waiting for "the direction stopped moving" never completes on a rolling
 *   boat, so the fast time constant is kept forever -- and a 2 s constant
 *   tracks the waves themselves (gain 0.79 at 0.0625 Hz), draining the signal
 *   we want into g_hat. Cost: Hs low by 15%, whatever the roll amplitude.
 *
 *   Seeding the IIR with a fast time constant leaves the estimate carrying the
 *   wave and sway it absorbed while settling -- up to 111 mG of horizontal
 *   component. The slow constant then bleeds that off over its own time
 *   constant, and while it lasts, horizontal motion leaks straight into the
 *   projection. This is why a LONGER slow constant made things worse, not
 *   better: measured bias with 100 mG of sway at 0.0625 Hz went from +5% at
 *   tau = 30 s to +40% at tau = 240 s.
 *
 * A plain average over the settle window fixes both. Its response is a sinc, so
 * a 16 s window puts an exact null on 0.0625 Hz -- the bottom of the integration
 * band, and the frequency the 1/f^4 weighting punishes hardest -- and another on
 * 0.125 Hz, while attenuating everything in between. The estimate therefore
 * starts almost free of wave energy instead of saturated with it.
 *
 * 16 s also keeps the first reading at about 48 s after launch (16 s settling
 * plus one 32 s segment). */
#define WAVE_G_SETTLE_S 16.0f

/* A genuine change of posture -- the user lifting their hand and putting it back
 * somewhere else -- has to restart settling, otherwise the slow constant takes
 * over 90 s to follow it. Detected as the instantaneous acceleration sitting far
 * off the current estimate for a sustained period. The threshold is well above
 * anything roll produces (the wave-induced tilt of a small boat rarely exceeds
 * 20 degrees) so that ordinary motion does not trip it.
 *
 * Expressed as a cosine, because comparing cosines needs no inverse
 * trigonometry -- see wave_vec3_angle_exceeds. cos(35 degrees). */
#define WAVE_G_RESET_COS 0.8191520f
#define WAVE_G_RESET_HOLD_S 1.0f

/* Below this magnitude the direction is meaningless and projection would divide
 * by something close to zero. Free fall or a bad synthetic input looks like this. */
#define WAVE_G_MIN_MAGNITUDE_MG 100.0f

typedef struct {
  wave_vec3 g_hat;    /* gravity estimate, milli-g */
  wave_vec3 sum;      /* running sum during the settle window */
  int sum_count;
  float t_settle;     /* seconds since the estimator was last restarted */
  float t_off_axis;   /* seconds the input has been far from the estimate */
  float dt;           /* sample interval, seconds */
  bool initialized;
  bool converged;     /* false while averaging, true once the IIR has taken over */
} wave_gravity;

void wave_gravity_init(wave_gravity *g, float sample_rate_hz);

/* Feed one raw sample. The first one seeds the estimate directly, which stands
 * in for the peek() the SDK cannot give us while a data subscription is open. */
void wave_gravity_push(wave_gravity *g, wave_vec3 a_mg);

/* True once the estimate has settled. Segment collection must not start before
 * this; note that we delay the START of collection rather than discarding a
 * completed segment, so the first reading arrives about 48 s after launch
 * rather than 64 s. */
bool wave_gravity_converged(const wave_gravity *g);

/* Project a sample onto the gravity direction and remove the static component.
 * Returns false if the estimate is unusable. */
bool wave_gravity_project(const wave_gravity *g, wave_vec3 a_mg, float *out_mg);

/*
 * True if the angle between a and b is wider than the angle whose cosine is
 * cos_limit. False if either vector is degenerate.
 *
 * Phrased as a comparison rather than returning an angle so that no arc cosine
 * is needed, and squared internally so that no square root is either: every
 * caller only ever wanted to test a threshold. This matters because libm is
 * unusable on the watch (see fastmath.h) and because it keeps the hot path --
 * this runs on every sample -- down to multiplies and one compare.
 */
bool wave_vec3_angle_exceeds(wave_vec3 a, wave_vec3 b, float cos_limit);

#endif /* WAVE_GRAVITY_H */
