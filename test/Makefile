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

# Host build of the wave estimation core. The sources under src/c/wave are free
# of Pebble SDK dependencies precisely so that they can be built and tested
# here, where a synthetic sea with a known Hs is available.

CC      ?= gcc
# -MMD -MP emits header dependencies. Without them, editing a struct in a header
# leaves stale objects linked against the old layout, and the result is a stack
# smash at runtime rather than a compile error -- which is exactly what happened
# once already.
CFLAGS  += -std=c99 -Wall -Wextra -Wshadow -O2 -g -MMD -MP -I../src/c/wave
LDLIBS  += -lm

CORE_SRCS := $(wildcard ../src/c/wave/*.c)
TEST_SRCS := $(wildcard test_*.c) synth.c main.c
OBJS      := $(CORE_SRCS:.c=.o) $(TEST_SRCS:.c=.o)

all: run_tests gen_emu_accel

run_tests: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Stimulus generator for `pebble emu-accel custom <file>`.
gen_emu_accel: gen_emu_accel.c synth.c $(CORE_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Replay a recorded CSV through the same estimator the watch runs.
analyze_csv: analyze_csv.c $(CORE_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

check: run_tests
	./run_tests

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) run_tests gen_emu_accel analyze_csv

-include $(OBJS:.o=.d)

.PHONY: all check clean
