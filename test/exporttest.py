#!/usr/bin/env python3
"""The port against VocalWriter itself: the demo songs rendered by `vw`
compared, sample by sample, with the AIFF files the application exported.

The exports live in the VocalWriter repository's emu/out (made by the
application on a Macintosh: File > Play to Disk). Every sample of every
song must match.

    python test/exporttest.py
"""
import array
import os
import struct
import subprocess
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
VW = os.path.join(os.path.dirname(ROOT), 'VocalWriter')
ASSETS = os.path.join(VW, 'assets')
EXPORTS = os.path.join(VW, 'emu', 'out')
VWEXE = os.path.join(ROOT, 'build', 'vw.exe' if os.name == 'nt' else 'vw')

#: (export, song, extra arguments)
CASES = [
    ('DaisyFull.aiff', 'Daisy.trk', []),
    ('Daisy.aiff', 'Daisy.trk', []),           # the first 12 seconds of the same
    ('HALsolo.aiff', 'Daisy.trk', ['--solo', '1']),
    ('Acappella.aiff', 'Acappella.trk', []),
]


def aiff_samples(path):
    b = open(path, 'rb').read()
    pos = 12
    comm = ssnd = None
    while pos + 8 <= len(b):
        ck, sz = b[pos:pos + 4], struct.unpack('>I', b[pos + 4:pos + 8])[0]
        if ck == b'COMM':
            comm = b[pos + 8:pos + 8 + sz]
        elif ck == b'SSND':
            ssnd = b[pos + 8:pos + 8 + sz]
        pos += 8 + sz + (sz & 1)
    ch, nf, bits = struct.unpack('>hIh', comm[:8])
    off = struct.unpack('>I', ssnd[:4])[0]
    out = array.array('h', ssnd[8 + off:8 + off + nf * ch * 2])
    out.byteswap()
    return out


def wav_samples(path):
    with wave.open(path) as w:
        return array.array('h', w.readframes(w.getnframes()))


def main():
    ok = True
    env = dict(os.environ, VW_DATA=ASSETS)
    for export, song, extra in CASES:
        ref_path = os.path.join(EXPORTS, export)
        if not os.path.exists(ref_path):
            print('%s: no export to compare with' % export)
            continue
        out = os.path.join(ROOT, 'build', 'export_' + os.path.splitext(export)[0] + '.wav')
        subprocess.check_call([VWEXE, 'render', os.path.join(ASSETS, 'Demo Music', song), out] + extra,
                              env=env, stdout=subprocess.DEVNULL)
        ours, ref = wav_samples(out), aiff_samples(ref_path)
        n = min(len(ours), len(ref))
        bad = [i for i in range(n) if ours[i] != ref[i]]
        if bad or (len(ours) != len(ref) and export != 'Daisy.aiff'):
            ok = False
            print('%s: FAIL -- %d of %d samples differ, first at %.3f s; lengths %d and %d'
                  % (export, len(bad), n, bad[0] / 88200.0 if bad else 0, len(ours), len(ref)))
        else:
            print('%s: OK -- %d samples identical (%.2f s)' % (export, n, n / 88200.0))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
