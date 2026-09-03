#!/usr/bin/env python3
"""Differential test: the C port against the original code under emulation.

The same script is run twice -- through build/harness (the C) and through the
PowerPC interpreter in the VocalWriter repository (the original's own
machine code) -- and the synthesiser context is compared field by field at
every snapshot, then the output samples word for word. The first difference
is reported with the field, both values and the snapshot it appeared in.

    python test/difftest.py                 # the built-in scenarios
    python test/difftest.py --script s.txt  # one script
    python test/difftest.py --frames 400    # longer render

Requires the VocalWriter repository beside this one (../VocalWriter) with
its assets, and its compiled interpreter core is used if present.
"""
import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
VW = os.path.join(os.path.dirname(ROOT), 'VocalWriter')
sys.path.insert(0, VW)

import ppc.image as image                                  # noqa: E402
# Put the guest stack where Mac OS X puts the main thread's: just under
# 0xC0000000. One uninitialised local in SayFrame reads a saved stack
# pointer, and its sign is what the original saw.
image.STACK_TOP = 0xC0000000
import ppc.synth as synth                                  # noqa: E402
# the interpreter's driver gives the synthesiser globals 16 KB; the struct is
# 200836 bytes, and the reverb lives near its end
synth.GLOBALS_SIZE = 0x31200
from ppc.synth import VocalWriter, G_VOICEBANK            # noqa: E402
from ppc import render                                     # noqa: E402
from tools.ttvi import load as load_ttvi, phoneme_order, durations   # noqa: E402

RSRC = os.path.join(VW, 'assets', 'VocalWriter.app', 'Contents', 'Resources', 'VocalWriter.rsrc')
GMSPEECH = os.path.join(VW, 'assets', 'GMSpeech.rsrc')
HARNESS = os.path.join(ROOT, 'build', 'harness.exe' if os.name == 'nt' else 'harness')

CTX_OUT_POS = 0x1080
G_TEMPO_SCALE = 0x3274
G_MAXL, G_MAXR, G_BPM = 0x6c70, 0x6c74, 0x3256
CTX_SIZE = 4396


# ---------------------------------------------------------------------------
# the guest side


class Guest(object):
    """Runs a harness script through the interpreter, collecting snapshots."""

    def __init__(self):
        self.vw = VocalWriter()
        self.m = self.vw.m
        self.snaps = []
        self.seq = None
        self.svv = None

    def run(self, script):
        vw, m = self.vw, self.m
        for line in script:
            parts = line.split()
            if not parts or parts[0].startswith('#'):
                continue
            cmd = parts[0]
            if cmd == 'tempo':
                m.mem.wf32(vw.g + G_TEMPO_SCALE, 1.0 / 240.0)
                m.call('SetTempo', vw.g, int(parts[1]))
            elif cmd == 'tempomul':
                m.mem.wf32(vw.g + G_TEMPO_SCALE, float(parts[1]))
            elif cmd == 'seq':
                blob = bytes.fromhex(parts[1])
                seq = m.alloc(len(blob), zero=False)
                m.mem.write(seq, blob)
                voice = m.mem.r16(vw.ctx + render.CTX_VOICE_IDX)
                m.call('SetSeqAddr', vw.ctx, seq)
                m.mem.w16(vw.ctx + render.CTX_VOICE_IDX, voice)
            elif cmd == 'program':
                vw.program(int(parts[1]))
            elif cmd == 'init':
                for fn, args in (('InitSay', (vw.ctx,)),
                                 ('Init_ControlBlocks', (vw.ctx,)),
                                 ('Start_Speech', (vw.g, 0)),
                                 ('Sing_Speech', (vw.g, 0, 1)),
                                 ('InitDefaultVoiceCntrls', (vw.ctx,))):
                    m.call(fn, *args)
            elif cmd == 'timetbl':
                m.mem.w32(vw.ctx + render.CTX_GLIDE_TBL,
                          m.mem.r32(m.globals_ptr('_g_Time_Tbl')))
            elif cmd == 'volume':
                m.call('Speech_Volume', vw.g, 0, int(parts[1]))
                m.call('SetTotalVolume', vw.ctx)
            elif cmd == 'ctrl':
                m.call(parts[1], vw.g, 0, int(parts[2]) & 0xFFFFFFFF)
            elif cmd == 'note':
                key, nxt, vel, dur = int(parts[1]), int(parts[2]), int(parts[3]), float(parts[4])
                m.call('Speech_Note', vw.g, 0, key, nxt, vel, floats=(dur,))
            elif cmd in ('frame', 'frames'):
                n = int(parts[1]) if cmd == 'frames' else 1
                for _ in range(n):
                    m.call('e_Fill_Next_Frame', vw.ctx)
                    m.call('SayFrame', vw.ctx)
            elif cmd == 'snapshot':
                ctx = m.mem.read(vw.ctx, CTX_SIZE)
                extra = (m.mem.r32(vw.g + G_MAXL), m.mem.r32(vw.g + G_MAXR),
                         m.mem.r16(vw.g + G_BPM))
                self.snaps.append((ctx, extra))
            elif cmd == 'reverb':
                if self.svv is None:
                    self.svv = m.alloc(668)
                    m.mem.w32(self.svv + 0xa8, vw.g)          # ChannelGlobals
                    rc = m.call('GetReverbMemory', self.svv)
                    m.mem.w16(self.svv + 0x298, 1 if rc == 0 else 0)   # reverbEnabled
                m.call('Synth_SetReverb', self.svv,
                       floats=(float(parts[1]), float(parts[2]), float(parts[3])))
                m.call('XferReverbHold', vw.g)
            elif cmd == 'reverbrun':
                frames = int(parts[1]) if len(parts) > 1 else 0
                m.mem.w32(vw.g + 0x3107c, frames if frames > 0 else m.mem.r32(vw.ctx + CTX_OUT_POS) // 440)
                if m.mem.r16(vw.g + 0x67bc):                  # reverbON
                    m.call('Reverberator_Process', vw.g, 0)
            elif cmd == 'reverbdump':
                self.rvb = []
                for i in range(8):
                    base = vw.g + (0x67dc if i < 4 else 0x683c) + (i % 4) * 24
                    size = m.mem.r32(base + 4)
                    buf, pin, pout = m.mem.r32(base + 8), m.mem.r32(base + 12), m.mem.r32(base + 16)
                    data = m.mem.read(buf, size * 4)
                    self.rvb.append((size, (pin - buf) // 4, (pout - buf) // 4,
                                     struct.unpack('>%dI' % size, data)))
            elif cmd in ('wave', 'layout'):
                pass
        halfwords = m.mem.r32(vw.ctx + CTX_OUT_POS)
        self.wave = m.mem.read(vw.outbuf, halfwords * 2)
        return self


# ---------------------------------------------------------------------------
# the host side


def run_harness(script_path, prefix):
    subprocess.check_call([HARNESS, RSRC, GMSPEECH, script_path, prefix])
    layout = {}
    size = None
    with open(prefix + '.layout') as fh:
        for line in fh:
            p = line.split()
            if p[0] == 'sizeof':
                size = int(p[1])
                continue
            layout[p[0]] = (int(p[1]), int(p[2]), p[3], int(p[4]))
    with open(prefix + '.ctx', 'rb') as fh:
        raw = fh.read()
    step = size + 12
    snaps = []
    for i in range(0, len(raw), step):
        chunk = raw[i:i + step]
        if len(chunk) < step:
            break
        snaps.append((chunk[:size], struct.unpack('<3i', chunk[size:size + 12])))
    wave = b''
    if os.path.exists(prefix + '.wav16'):
        with open(prefix + '.wav16', 'rb') as fh:
            wave = fh.read()
    return layout, snaps, wave


FMT = {'i8': 'b', 'u8': 'B', 'i16': 'h', 'u16': 'H', 'i32': 'i', 'u32': 'I',
       'f32': 'f', 'f64': 'd'}
SIZES = {'i8': 1, 'u8': 1, 'i16': 2, 'u16': 2, 'i32': 4, 'u32': 4, 'f32': 4, 'f64': 8}


def field_values(buf, off, kind, count, endian):
    sz = SIZES[kind]
    return struct.unpack(endian + FMT[kind] * count, buf[off:off + sz * count])


def raw_bits(buf, off, kind, count, endian):
    """The bit patterns, so that NaNs and -0.0 compare exactly."""
    sz = SIZES[kind]
    f = {1: 'B', 2: 'H', 4: 'I', 8: 'Q'}[sz]
    return struct.unpack(endian + f * count, buf[off:off + sz * count])


def compare_snapshot(k, layout, guest, host, gextra, hextra, limit):
    diffs = []
    for name, (goff, hoff, kind, count) in sorted(layout.items(), key=lambda kv: kv[1][0]):
        if kind == 'ptr':
            continue
        gb = raw_bits(guest, goff, kind, count, '>')
        hb = raw_bits(host, hoff, kind, count, '<')
        if gb != hb:
            gv = field_values(guest, goff, kind, count, '>')
            hv = field_values(host, hoff, kind, count, '<')
            for i in range(count):
                if gb[i] != hb[i]:
                    idx = '[%d]' % i if count > 1 else ''
                    diffs.append('  %s%s: guest %r  host %r' % (name, idx, gv[i], hv[i]))
                    if len(diffs) >= limit:
                        return diffs
    for i, label in enumerate(('maxSampleL', 'maxSampleR', 'tempoBPM')):
        if gextra[i] != hextra[i]:
            diffs.append('  xx.%s: guest %r  host %r' % (label, gextra[i], hextra[i]))
    return diffs


def compare_wave(gwave, hwave):
    g = struct.unpack('>%dh' % (len(gwave) // 2), gwave)
    h = struct.unpack('<%dh' % (len(hwave) // 2), hwave)
    n = min(len(g), len(h))
    for i in range(n):
        if g[i] != h[i]:
            return 'sample halfword %d: guest %d host %d (of %d/%d)' % (i, g[i], h[i], len(g), len(h))
    if len(g) != len(h):
        return 'sample count: guest %d host %d' % (len(g), len(h))
    return None


# ---------------------------------------------------------------------------
# scenarios


def sequence_blob(notes, blob, order, nominal):
    """The SetSeqAddr block for notes of (midi, beats, [phonemes])."""
    index = {n: i for i, n in enumerate(order)}
    phon, ctrl, dur = [], [], []
    for midi, beats, syms in notes:
        syms = [s for s in syms if s in index] or ['%']
        for k, s in enumerate(syms):
            phon.append(index[s])
            ctrl.append(1 if k == 0 else 0)
            dur.append(max(1, int(round(nominal.get(s, 80)))))
    phon.append(index['%'])
    ctrl.append(1)
    dur.append(max(1, int(nominal.get('%', 300))))
    n = len(phon)
    pack = lambda a: struct.pack('>%dH' % n, *a)
    return struct.pack('>HH', 0, n) + pack(phon) + pack(ctrl) + pack(dur) + pack(dur)


def scenario(notes, program=0, bpm=120, frames_per_note=60, controls=(), bend=None,
             reverb=None):
    """A script that sings `notes` and snapshots after every note and frame batch."""
    blob = load_ttvi()
    order = phoneme_order(blob)
    nominal = {r[0]: r[1] for r in durations(blob)}
    seq = sequence_blob(notes, blob, order, nominal)
    lines = ['tempo %d' % bpm, 'seq ' + seq.hex(), 'program %d' % program, 'init',
             'timetbl', 'volume 127']
    for name, val in controls:
        lines.append('ctrl %s %d' % (name, val))
    if reverb is not None:
        lines.append('reverb %g %g %g' % reverb)
    lines.append('snapshot')
    for midi, beats, syms in notes:
        lines.append('note %d %d 100 %g' % (midi, midi, beats))
        lines.append('snapshot')
        for _ in range(frames_per_note // 10):
            lines.append('frames 10')
            lines.append('snapshot')
            if bend is not None:
                lines.append('ctrl Speech_PitchBend %d' % bend)
    if reverb is not None:
        lines.append('reverbrun')
    lines.append('layout')
    lines.append('wave')
    return lines


DAISY = [(69, 1.5, ['d', 'EY']), (65, 1.5, ['z', 'IY']), (62, 1.5, ['d', 'EY']),
         (57, 1.5, ['z', 'IY']), (64, 0.75, ['g', 'IH', 'v']), (65, 0.75, ['m', 'IY']),
         (67, 0.75, ['y', 'AO', 'r']), (64, 0.75, ['%']), (62, 1.0, ['AE', 'n']),
         (64, 0.5, ['s', 'ER']), (65, 0.5, ['%']), (62, 1.5, ['d', 'UW'])]

SCENARIOS = {
    'daisy': lambda a: scenario(DAISY[:a.notes], program=0, frames_per_note=a.frames),
    'female': lambda a: scenario(DAISY[:a.notes], program=4, frames_per_note=a.frames),
    'controls': lambda a: scenario(DAISY[:a.notes], program=1, frames_per_note=a.frames,
                                   controls=[('Speech_Color', 40), ('Speech_VibDepth', 60),
                                             ('Speech_VibFreq', 30), ('Speech_Chorus', 20),
                                             ('Speech_Breath', 30), ('Speech_Detune', 300),
                                             ('Speech_Portamento', 64)]),
    'bend': lambda a: scenario(DAISY[:a.notes], program=2, frames_per_note=a.frames, bend=3000),
    'reverb': lambda a: scenario(DAISY[:a.notes], program=0, frames_per_note=a.frames,
                                 reverb=(0.8, 0.35, 0.65)),
}


def run_one(name, lines, args):
    tmp = os.path.join(ROOT, 'build', 'diff_' + name)
    script_path = tmp + '.txt'
    with open(script_path, 'w') as fh:
        fh.write('\n'.join(lines) + '\n')
    layout, hsnaps, hwave = run_harness(script_path, tmp)
    guest = Guest().run(lines)
    ok = True
    if len(hsnaps) != len(guest.snaps):
        print('%s: snapshot count guest %d host %d' % (name, len(guest.snaps), len(hsnaps)))
        ok = False
    for k, ((gctx, gextra), (hctx, hextra)) in enumerate(zip(guest.snaps, hsnaps)):
        diffs = compare_snapshot(k, layout, gctx, hctx, gextra, hextra, args.limit)
        if diffs:
            print('%s: snapshot %d differs:' % (name, k))
            print('\n'.join(diffs))
            ok = False
            if not args.all:
                break
    w = compare_wave(guest.wave, hwave)
    if w:
        print('%s: %s' % (name, w))
        ok = False
    print('%s: %s (%d snapshots, %d samples)' % (name, 'OK' if ok else 'FAIL',
                                                 len(hsnaps), len(hwave) // 2))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--script')
    ap.add_argument('--scenario', action='append')
    ap.add_argument('--frames', type=int, default=60)
    ap.add_argument('--notes', type=int, default=4)
    ap.add_argument('--limit', type=int, default=12)
    ap.add_argument('--all', action='store_true', help='report every snapshot, not the first')
    args = ap.parse_args()
    if args.script:
        with open(args.script) as fh:
            lines = [l.rstrip('\n') for l in fh]
        return 0 if run_one(os.path.basename(args.script), lines, args) else 1
    names = args.scenario or list(SCENARIOS)
    ok = True
    for n in names:
        ok = run_one(n, SCENARIOS[n](args), args) and ok
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
