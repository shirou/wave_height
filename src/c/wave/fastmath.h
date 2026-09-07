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

#ifndef WAVE_FASTMATH_H
#define WAVE_FASTMATH_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Float maths without libm.
 *
 * Not an optimisation -- a workaround. On a real Pebble Time 2, calling libm's
 * sqrtf faults immediately, every time, at the same instruction: the `ldr` that
 * dereferences a pointer loaded from __ieee754_sqrtf's constant pool. The
 * relocation for that pointer is present in the ELF and the emulator handles it
 * fine, so something about how the firmware loads the app leaves those
 * libm-internal data pointers unusable. Three separate crash reports landed on
 * the same offset into the function.
 *
 * The first sqrtf call happens the moment the gravity estimator settles, about
 * sixteen seconds in, which is exactly when the app died on hardware.
 *
 * Everything here therefore uses only the compiler's soft-float builtins
 * (__aeabi_fadd and friends), which do work, and no library data at all.
 * Accuracy is checked against the host's libm in test_fastmath.
 */

/* Exponent-halving initial guess plus three Newton steps. Returns 0 for
 * negative or zero input, which suits every caller here: they are all taking
 * the root of a magnitude or a spectral moment. */
float wave_sqrtf(float x);

/* Round toward negative infinity. Values beyond int32 range are returned
 * unchanged; no caller here goes anywhere near that. */
float wave_floorf(float x);

/* Bit test on the exponent field: no library, no traps. */
bool wave_isfinite(float x);

float wave_fabsf(float x);

/*
 * sin and cos by range reduction onto [-pi/4, pi/4] plus a Taylor polynomial.
 *
 * Used for the Hann window, the FFT twiddle factors and the decimation droop
 * curve. All three are computed once at startup, so speed is irrelevant and
 * only accuracy matters; the polynomials are good to about 1e-7, at or below
 * float resolution.
 */
float wave_sinf(float x);
float wave_cosf(float x);

#endif /* WAVE_FASTMATH_H */
