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
 * Run a recorded three-axis CSV through the very same estimator the watch runs.
 *
 * This is what makes a sea trial diagnosable. The watch shows one number; when
 * it disagrees with the reference buoy, this replays the recording so the
 * parameters can be varied and the per-segment behaviour inspected. Because it
 * links the actual src/c/wave sources, there is no risk of the analysis and the
 * firmware quietly diverging.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/c/wave/session.h"

int main(int argc, char **argv) {
  const char *path = NULL;
  float noise_floor = 0.0f;
  bool verbose = false;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--noise-floor") && i + 1 < argc) {
      noise_floor = (float)atof(argv[++i]);
    } else if (!strcmp(argv[i], "-v")) {
      verbose = true;
    } else if (argv[i][0] != '-') {
      path = argv[i];
    } else {
      fprintf(stderr,
              "usage: %s [--noise-floor <psd>] [-v] <accel.csv>\n"
              "  CSV is x,y,z in milli-g at the acquisition rate.\n",
              argv[0]);
      return 1;
    }
  }
  if (path == NULL) {
    fprintf(stderr, "no input file\n");
    return 1;
  }

  FILE *fh = fopen(path, "r");
  if (fh == NULL) {
    perror(path);
    return 1;
  }

  wave_session s;
  wave_session_init(&s, WAVE_ACQ_RATE_HZ, noise_floor, 4000.0f);

  char line[128];
  wave_accel_sample batch[25];
  int held = 0;
  long total = 0;
  int last_seg = 0;

  while (fgets(line, sizeof(line), fh) != NULL) {
    int x, y, z;
    if (sscanf(line, "%d,%d,%d", &x, &y, &z) != 3) {
      continue;
    }
    batch[held].x = (int16_t)x;
    batch[held].y = (int16_t)y;
    batch[held].z = (int16_t)z;
    batch[held].did_vibrate = false;
    held++;
    total++;

    if (held == 25) {
      wave_session_push(&s, batch, held);
      held = 0;

      if (verbose && s.acc.n_seg != last_seg) {
        last_seg = s.acc.n_seg;
        wave_display d;
        wave_session_get_display(&s, &d);
        printf("  t=%6.1fs  seg=%2d  rej=%d  Hs=%.2f m  T=%.1f s\n",
               (double)s.elapsed_s, d.result.n_seg, s.rejected_total,
               (double)d.result.hs, (double)d.result.period);
      }
    }
  }
  if (held > 0) {
    wave_session_push(&s, batch, held);
  }
  fclose(fh);

  wave_display d;
  wave_session_get_display(&s, &d);

  printf("\n%s\n", path);
  printf("  samples          %ld (%.1f s at %.0f Hz)\n", total,
         total / (double)WAVE_ACQ_RATE_HZ, (double)WAVE_ACQ_RATE_HZ);
  printf("  segments used    %d\n", d.result.n_seg);
  printf("  segments dropped %d\n", s.rejected_total);
  if (!d.result.valid) {
    printf("  result           none (calm, or everything below the noise floor)\n");
    return 0;
  }
  printf("  Hs               %.3f m\n", (double)d.result.hs);
  printf("  Tm-1,0           %.2f s\n", (double)d.result.period);
  printf("  H1/10            %.3f m\n", (double)d.result.h_one_tenth);
  printf("  confidence       %d/3\n", (int)d.result.conf);
  return 0;
}
