# VocalWriter in C. Build with GCC or Clang:
#
#   make            the library objects and the test harness
#   make check      the differential test against the interpreter
#
# The flags matter: -ffp-contract=off keeps a*b+c as two roundings, the way
# the PowerPC code did it (it never used fmadd), and -fwrapv gives the
# wrap-around integer arithmetic the original relied on.

CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-variable -Wno-unused-parameter \
           -Wno-unused-but-set-variable -Wno-sign-compare \
           -ffp-contract=off -fwrapv -fexcess-precision=standard -fno-fast-math
INCLUDE  = -Iinclude

OBJS = build/speech.o build/tables.o build/music.o

all: build/harness

build:
	mkdir -p build

build/%.o: src/%.c include/vw_engine.h include/vw_types.h | build
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

build/harness: test/harness.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDE) test/harness.c $(OBJS) -o $@ -lm

src/speech.c: tools/lift.py tools/mksrc.py
	python tools/mksrc.py

include/vw_types.h: tools/genheader.py tools/stabs.py
	python tools/genheader.py > include/vw_types.h

check: build/harness
	python test/difftest.py

clean:
	rm -rf build

.PHONY: all check clean
