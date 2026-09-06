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

#ifndef WAVE_FFT_H
#define WAVE_FFT_H

/*
 * In-place radix-2 decimation-in-time complex FFT.
 *
 * n must be a power of two and at most WAVE_FFT_MAX_N. Real input is passed by
 * zeroing the imaginary array; the redundant negative-frequency half of the
 * result is simply ignored by the caller.
 *
 * At 64 points this is 192 butterflies, roughly 2000 flops. Even with software
 * floating point that is about 1 ms, and it runs once per 32 s segment, so
 * there is no need for a fixed-point implementation.
 */

#define WAVE_FFT_MAX_N 64

void wave_fft(float *re, float *im, int n);

#endif /* WAVE_FFT_H */
