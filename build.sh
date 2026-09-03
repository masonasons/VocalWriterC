#!/bin/sh
# Build the library, the command line tool and the test harness with GCC.
#
#   sh build.sh              (uses $CC, default gcc)
#
# The flags matter: -ffp-contract=off keeps a*b+c as two roundings, the way
# the PowerPC code did it (it never used fmadd), and -fwrapv gives the
# wrap-around integer arithmetic the original relied on.
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
AR=${AR:-ar}
CFLAGS="-O2 -g -std=c11 -Wall -Wextra -Wno-unused-variable -Wno-unused-parameter \
 -Wno-unused-but-set-variable -Wno-sign-compare -Wno-unused-function \
 -ffp-contract=off -fwrapv -fno-fast-math -Iinclude -Itest"

# Flags the compiler may not have. Clang takes -fexcess-precision=standard
# only from 17 on and merely warns about it before that, so the probe compiles
# with -Werror; and it has no -static-libgcc at all, which is a GCC thing and
# only matters on Windows, where it keeps the runtime out of the DLL's way.
PROBE="${TMPDIR:-/tmp}/vw_flag_probe"
echo "int main(void){return 0;}" > "$PROBE.c"
have_flag() {
    $CC -Werror "$1" -c "$PROBE.c" -o "$PROBE.o" 2>/dev/null
}
if have_flag -fexcess-precision=standard; then
    CFLAGS="$CFLAGS -fexcess-precision=standard"
else
    echo "note: $CC does not take -fexcess-precision=standard; going without it"
fi
SHAREDFLAGS=""
if have_flag -static-libgcc; then
    SHAREDFLAGS="-static-libgcc"
fi
rm -f "$PROBE.c" "$PROBE.o"

mkdir -p build
OBJS=""
for f in speech tables music reverb macshim orthtophon parsephons convertsmf expandtracks \
         synthapi synthglue bank song vw_api editor; do
    $CC $CFLAGS -c src/$f.c -o build/$f.o
    OBJS="$OBJS build/$f.o"
done
rm -f build/libvocalwriter.a
$AR rcs build/libvocalwriter.a $OBJS
$CC $CFLAGS src/vw.c build/libvocalwriter.a -o build/vw -lm

# The shared library, for callers that are not C: the note editor in the
# VocalWriter repository loads it and drives the engine through vw_editor.h.
# -fPIC is needed for the objects it is made of, so they are built again;
# the GCC runtime goes inside, so the library stands on its own.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) SO=build/libvocalwriter.dll ;;
    Darwin) SO=build/libvocalwriter.dylib ;;
    *) SO=build/libvocalwriter.so ;;
esac
PICOBJS=""
mkdir -p build/pic
for f in speech tables music reverb macshim orthtophon parsephons convertsmf expandtracks \
         synthapi synthglue bank song vw_api editor; do
    $CC $CFLAGS -fPIC -c src/$f.c -o build/pic/$f.o
    PICOBJS="$PICOBJS build/pic/$f.o"
done
$CC $CFLAGS -shared $PICOBJS -o $SO -lm $SHAREDFLAGS
echo built $SO
$CC $CFLAGS -c test/layout.c -o build/layout.o
$CC $CFLAGS test/harness.c build/libvocalwriter.a build/layout.o -o build/harness -lm
echo built build/vw and build/harness
