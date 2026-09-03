# VocalWriter in C. Build with GCC or Clang:
#
#   make            the library, the vw command line tool, the test harness
#   make check      the differential tests against the interpreter (needs
#                   the VocalWriter repository beside this one, with its assets)
#
# The flags matter: -ffp-contract=off keeps a*b+c as two roundings, the way
# the PowerPC code did it (it never used fmadd), and -fwrapv gives the
# wrap-around integer arithmetic the original relied on.
#
# build.sh does the same without make (on msys, whose make is unhappy).

CC      ?= gcc
AR      ?= ar
PYTHON  ?= python
CFLAGS  ?= -O2 -g -std=c11 -Wall -Wextra -Wno-unused-variable -Wno-unused-parameter \
           -Wno-unused-but-set-variable -Wno-sign-compare -Wno-unused-function \
           -ffp-contract=off -fwrapv -fexcess-precision=standard -fno-fast-math
INCLUDE  = -Iinclude -Itest

UNITS = speech tables music reverb macshim orthtophon parsephons convertsmf expandtracks \
        synthapi synthglue bank song vw_api editor
OBJS  = $(addprefix build/,$(addsuffix .o,$(UNITS)))
PICOBJS = $(addprefix build/pic/,$(addsuffix .o,$(UNITS)))
LIB   = build/libvocalwriter.a

# The shared library the note editor in the VocalWriter repository loads.
UNAME := $(shell uname -s)
ifneq (,$(findstring MINGW,$(UNAME))$(findstring MSYS,$(UNAME)))
SHARED = build/libvocalwriter.dll
else ifeq ($(UNAME),Darwin)
SHARED = build/libvocalwriter.dylib
else
SHARED = build/libvocalwriter.so
endif

all: build/vw build/harness $(SHARED)

build:
	mkdir -p build

build/%.o: src/%.c include/vw_engine.h include/vw_types.h include/vocalwriter.h | build
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rcs $@ $(OBJS)

build/vw: src/vw.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDE) src/vw.c $(LIB) -o $@ -lm

build/pic:
	mkdir -p build/pic

build/pic/%.o: src/%.c include/vw_engine.h include/vw_types.h include/vocalwriter.h | build/pic
	$(CC) $(CFLAGS) $(INCLUDE) -fPIC -c $< -o $@

$(SHARED): $(PICOBJS)
	$(CC) $(CFLAGS) -shared $(PICOBJS) -o $@ -lm -static-libgcc

build/layout.o: test/layout.c test/layout.h | build
	$(CC) $(CFLAGS) $(INCLUDE) -c test/layout.c -o $@

build/harness: test/harness.c $(LIB) build/layout.o
	$(CC) $(CFLAGS) $(INCLUDE) test/harness.c $(LIB) build/layout.o -o $@ -lm

# the generated sources, from the original binary's debug records
generate:
	$(PYTHON) tools/genheader.py > include/vw_types.h
	$(PYTHON) tools/genheader.py --layout > test/layout.c
	$(PYTHON) tools/mksrc.py
	$(PYTHON) tools/mkfront.py
	$(PYTHON) tools/mkseq.py

check: build/harness build/vw
	$(PYTHON) test/difftest.py
	$(PYTHON) test/fronttest.py
	$(PYTHON) test/seqtest.py
	$(PYTHON) test/exporttest.py

clean:
	rm -rf build

.PHONY: all generate check clean
