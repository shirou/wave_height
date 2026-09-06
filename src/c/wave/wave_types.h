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
 * Shared types and constants for the wave estimation core.
 *
 * Everything under src/c/wave/ is free of Pebble SDK dependencies so the same
 * sources compile on a host machine for testing. Do not include pebble.h here.
 */

#ifndef WAVE_TYPES_H
#define WAVE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/* ---- Acquisition ------------------------------------------------------- */

/* Accelerometer output rate. Provisional: the final value is chosen by the
 * on-device spike (plan step 3b), which records at 100 Hz and replays the
 * candidate decimation chains offline. Keep this configurable rather than
 * baked into the algorithms. */
#define WAVE_ACQ_RATE_HZ 10.0f

/* Decimation from the acquisition rate down to the processing rate. A boxcar
 * average of this length has exact nulls at multiples of ACQ/FACTOR, which
 * coincide with the alias centres, so it suppresses fold-down well. It does
 * droop in-band, which spectrum.c compensates for. */
#define WAVE_DECIM_FACTOR 5
#define WAVE_PROC_RATE_HZ (WAVE_ACQ_RATE_HZ / WAVE_DECIM_FACTOR) /* 2 Hz */

/* ---- Segmentation and FFT ---------------------------------------------- */

#define WAVE_SEG_SAMPLES 64 /* 64 samples @ 2 Hz = 32 s */
#define WAVE_FFT_N WAVE_SEG_SAMPLES
#define WAVE_NBINS (WAVE_FFT_N / 2) /* usable one-sided bins: k = 1 .. NBINS-1 */

/* Frequency resolution: 2 Hz / 64 = 0.03125 Hz */
#define WAVE_DF (WAVE_PROC_RATE_HZ / (float)WAVE_FFT_N)

/* Integration band, in bin indices.
 *
 * The lower edge matters a great deal: S_z = S_a / (2*pi*f)^4 amplifies bin 1
 * (0.03125 Hz) by 673x, which turns sensor noise alone into an apparent Hs of
 * 0.24 m. Bin 2 is amplified by 42x, giving roughly 0.067 m. Starting at bin 2
 * keeps a flat sea reading close to zero. */
#define WAVE_BIN_LO 2  /* 0.0625 Hz -> 16 s */
#define WAVE_BIN_HI 16 /* 0.5 Hz    -> 2 s  */

/* Exponential moving average horizon, in segments. Below this count the
 * average is a plain arithmetic mean; beyond it the weight settles to 1/N_MAX,
 * giving an effective window of about 8.5 minutes. Using an EMA rather than a
 * ring buffer is deliberate: a ring buffer cannot be restored from persisted
 * storage, because the mean alone does not tell you which segment to evict. */
#define WAVE_EMA_MAX_SEG 16

/* ---- Units ------------------------------------------------------------- */

/* The SDK reports acceleration in milli-g as int16. Convert before any
 * spectral work: Hs = 4*sqrt(m0) only holds when m0 is in m^2. */
#define WAVE_MG_TO_MS2 9.80665e-3f

/* ---- Data types -------------------------------------------------------- */

/* One accelerometer sample, decoupled from the SDK's AccelData so that the
 * core stays host-testable. main.c performs the translation. */
typedef struct {
  int16_t x; /* milli-g */
  int16_t y;
  int16_t z;
  bool did_vibrate; /* watch vibrated during collection; sample is untrustworthy */
} wave_accel_sample;

typedef struct {
  float x;
  float y;
  float z;
} wave_vec3;

/* Confidence tiers, driven by the number of averaged segments. The measured
 * relative standard deviation of Hs is 31% at one segment, 19% at three and
 * 10% at seven, so the display precision is tied to this. */
typedef enum {
  WAVE_CONF_NONE = 0, /* nothing valid yet: show "--" */
  WAVE_CONF_LOW,      /* 1-2 segments, sd ~23-31%: round Hs to 0.5 m */
  WAVE_CONF_MID,      /* 3-6 segments, sd ~13-19%: round Hs to 0.1 m */
  WAVE_CONF_HIGH      /* 7+ segments,  sd ~10%    */
} wave_confidence;

typedef struct {
  bool valid;
  float hs;             /* significant wave height, m */
  float period;         /* Tm-1,0 = m_-1 / m_0, s */
  float h_one_tenth;    /* 1.27 * hs, m */
  int n_seg;            /* segments folded into the average */
  wave_confidence conf;
} wave_result;

#endif /* WAVE_TYPES_H */
