#!/usr/bin/env python3
# Copyright 2026 The wave_height Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Turn a DataLogging download into CSV for offline analysis.

The watch logs raw three-axis samples, six bytes each, little-endian int16 in
x, y, z order. Pull a session off the watch with:

    pebble data-logging list
    pebble data-logging download --session-id <id> raw.bin

then convert:

    python3 tools/decode_log.py raw.bin > sea.csv

and run the same estimator the watch runs over it:

    cd test && make analyze_csv && ./analyze_csv ../sea.csv

Reprocessing on a host is the point of logging at all. When a sea trial
disagrees with the reference buoy, the question is always which of several
things went wrong -- body motion, aliasing, engine vibration, the boat not
following short waves -- and the only way to separate them is to re-run the
recording with the parameters changed.
"""

import argparse
import struct
import sys

RECORD = struct.Struct("<hhh")


def decode(data):
    if len(data) % RECORD.size:
        print(
            f"warning: {len(data) % RECORD.size} trailing byte(s) ignored",
            file=sys.stderr,
        )
    count = len(data) // RECORD.size
    for i in range(count):
        yield RECORD.unpack_from(data, i * RECORD.size)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", help="binary file from `pebble data-logging download`")
    ap.add_argument(
        "--rate",
        type=float,
        default=10.0,
        help="acquisition rate in Hz, for the summary only (default 10)",
    )
    args = ap.parse_args()

    with open(args.input, "rb") as fh:
        data = fh.read()

    n = 0
    lo = [32767] * 3
    hi = [-32768] * 3
    total = [0, 0, 0]

    for x, y, z in decode(data):
        print(f"{x},{y},{z}")
        for axis, v in enumerate((x, y, z)):
            lo[axis] = min(lo[axis], v)
            hi[axis] = max(hi[axis], v)
            total[axis] += v
        n += 1

    if n == 0:
        print("no records found", file=sys.stderr)
        return 1

    print(
        f"{n} samples, {n / args.rate:.1f} s at {args.rate:g} Hz",
        file=sys.stderr,
    )
    for axis, name in enumerate("xyz"):
        print(
            f"  {name}: mean {total[axis] / n:8.1f} mG   "
            f"range {lo[axis]:6d} .. {hi[axis]:6d}",
            file=sys.stderr,
        )

    # Clipping poisons the whole spectrum with harmonics, so it is worth
    # flagging up front rather than discovering it as a strange result.
    for axis, name in enumerate("xyz"):
        if hi[axis] >= 3900 or lo[axis] <= -3900:
            print(
                f"  WARNING: {name} approaches full scale; segments may be clipped",
                file=sys.stderr,
            )
    return 0


if __name__ == "__main__":
    sys.exit(main())
