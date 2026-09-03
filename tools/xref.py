#!/usr/bin/env python3
"""Who calls whom in the VocalWriter binary.

    python tools/xref.py --callees StartCurSeq     # what a function calls, in order
    python tools/xref.py --callers Synth_Startup   # every function that calls it

Direct `bl` targets only: functions by their STABS names, library calls by
their stub names. Works on the optimised editor units too, which the lifter
cannot structure -- the call sequence is often all one needs from those.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from macho import Binary          # noqa: E402
from ppc import decode            # noqa: E402


def callees(b, name):
    start, end = b.extent(name)
    out = []
    for addr in range(start, end, 4):
        w = b.u32(addr)
        if w is None:
            break
        i = decode(addr, w)
        if i is not None and i.op == 'b' and i.lk:
            t = i.target
            out.append((addr, b.func_by_addr.get(t) or b.stubs.get(t) or b.by_addr.get(t) or '0x%x' % t))
    return out


def main():
    a = sys.argv[1:]
    if len(a) != 2 or a[0] not in ('--callees', '--callers'):
        print(__doc__)
        return 2
    b = Binary()
    if a[0] == '--callees':
        for addr, n in callees(b, a[1]):
            print('  %08x  %s' % (addr, n))
        return 0
    target = a[1]
    for name in sorted(b.funcs, key=lambda n: b.funcs[n]):
        try:
            cs = callees(b, name)
        except Exception:
            continue
        n = sum(1 for _, c in cs if c == target)
        if n:
            u, _f = b.unit_of(name)
            print('  %-40s %s  (%d)' % (name, u.base if u else '?', n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
