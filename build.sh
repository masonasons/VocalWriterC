#!/bin/sh
# Build the library objects and the test harness with GCC.
#
#   sh build.sh              (uses $CC, default gcc)
#
# The flags matter: -ffp-contract=off keeps a*b+c as two roundings, the way
# the PowerPC code did it (it never used fmadd), and -fwrapv gives the
# wrap-around integer arithmetic the original relied on.
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
CFLAGS="-O2 -std=c11 -Wall -Wextra -Wno-unused-variable -Wno-unused-parameter \
 -Wno-unused-but-set-variable -Wno-sign-compare -Wno-unused-function \
 -ffp-contract=off -fwrapv -fexcess-precision=standard -fno-fast-math -Iinclude -Itest"
mkdir -p build
for f in speech tables music reverb; do
    $CC $CFLAGS -c src/$f.c -o build/$f.o
done
$CC $CFLAGS -c test/layout.c -o build/layout.o
$CC $CFLAGS test/harness.c build/speech.o build/tables.o build/music.o build/reverb.o build/layout.o -o build/harness -lm
echo built build/harness
