#!/usr/bin/env python3
"""Differential test of the sequencer: a whole song through the port and
through the original code under the interpreter.

Both sides make the same Synth_* calls the application makes -- startup,
the voice and instrument banks, the song's tracks, the speech data, the
reverb, Synth_SeqPlayer -- and then pull sound buffers with
Synth_GetNextBuffer, exactly as the application's own "Play to Disk" does.
The two sample streams must be identical.

    python test/seqtest.py                       # the demo song, 6 seconds
    python test/seqtest.py --song X.trk --seconds 10 --no-reverb
"""
import argparse
import os
import struct
import subprocess
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import difftest                                            # noqa: E402  (moves the guest stack)
from difftest import VW, RSRC, GMSPEECH                    # noqa: E402
from ppc.image import Machine                              # noqa: E402
from tools.machrsrc import ResourceFork                    # noqa: E402
from macho import Binary                                   # noqa: E402

GMBANK = os.path.join(VW, 'assets', 'GMBank.rsrc')
DAISY = os.path.join(VW, 'assets', 'Demo Music', 'Daisy.trk')
VWEXE = os.path.join(ROOT, 'build', 'vw.exe' if os.name == 'nt' else 'vw')

DONE_HOOK = 0xa66f0            # a free address above the code, for the done callback
POLYPHONY = 48
SPEECH_RATE = 140
SEQHEADER_SIZE = 758
TRACKINFO_SIZE = 88
SEQINFO_SIZE = 2816


class SeqGuest(object):
    def __init__(self):
        self.m = Machine()
        self.b = Binary()
        self.done = False
        self.handles = {}
        self._hooks()

    # -- Mac OS, as src/synthglue.c and src/macshim.c provide it ------------

    def _hooks(self):
        m = self.m
        by_name = {}
        for addr, name in self.b.stubs.items():
            by_name.setdefault(name, []).append(addr)

        def r3(v):
            def fn(cpu):
                cpu.r[3] = v & 0xFFFFFFFF
            return fn

        def newptr(cpu):
            cpu.r[3] = m.alloc(max(cpu.r[3] & 0xFFFFFFFF, 16))

        def newptr_dirty(cpu):
            n = max(cpu.r[3] & 0xFFFFFFFF, 16)
            cpu.r[3] = m.alloc(n)               # zeroed, as the port's shim fill 0 does

        def newhandle(cpu):
            n = max(cpu.r[3] & 0xFFFFFFFF, 16)
            cpu.r[3] = m.handle_to(m.alloc(n))

        def sethandlesize(cpu):
            h, n = cpu.r[3], cpu.r[4] & 0xFFFFFFFF
            old = m.mem.r32(h)
            new = m.alloc(max(n, 16))
            m.mem.write(new, m.mem.read(old, min(n, 16)))
            m.mem.w32(h, new)
            cpu.r[3] = 0

        def gestalt(cpu):
            m.mem.w32(cpu.r[4], 1 << 7)
            cpu.r[3] = 0

        def sndnewchannel(cpu):
            m.mem.w32(cpu.r[3], m.alloc(1060))
            cpu.r[3] = 0

        def blockmove(cpu):
            src, dst, n = cpu.r[3], cpu.r[4], cpu.r[5] & 0xFFFFFFFF
            if n:
                m.mem.write(dst, m.mem.read(src, n))

        self.clock = 0

        def microseconds(cpu):
            self.clock += 1000
            m.mem.w32(cpu.r[3], 0)
            m.mem.w32(cpu.r[3] + 4, self.clock)

        def primetime(cpu):
            # a primed Time Manager task fires at once: tmAddr(task)
            task = cpu.r[3]
            addr = m.mem.r32(task + 6)           # TMTask.tmAddr
            if addr:
                saved = (cpu.r[1], cpu.lr, cpu.pc)
                m.call(addr, task)
                cpu.r[1], cpu.lr, cpu.pc = saved
            cpu.r[3] = 0

        def dtinstall(cpu):
            dt = cpu.r[3]
            addr = m.mem.r32(dt + 8)             # DeferredTask.dtAddr
            param = m.mem.r32(dt + 12)
            if addr:
                saved = (cpu.r[1], cpu.lr, cpu.pc)
                m.call(addr, param)
                cpu.r[1], cpu.lr, cpu.pc = saved
            cpu.r[3] = 0

        def snddocommand(cpu):
            # the buffer command completes at once: soundCB_Count--
            chan = cpu.r[3]
            svv = self.svv
            if svv is not None:
                cnt = m.mem.r16(svv + 0x106)     # soundCB_Count
                if cnt > 0:
                    m.mem.w16(svv + 0x106, cnt - 1)
            cpu.r[3] = 0

        def getresource(cpu):
            cpu.r[3] = self.ttvi_handle

        def identity(cpu):
            pass

        def numtostring(cpu):
            s = str(cpu.r[3] if cpu.r[3] < 0x80000000 else cpu.r[3] - (1 << 32)).encode()
            m.mem.write(cpu.r[4], bytes([len(s)]) + s)

        import math

        def _pow(cpu):
            try:
                cpu.f[1] = math.pow(cpu.f[1], cpu.f[2])
            except (ValueError, OverflowError):
                cpu.f[1] = 0.0

        def _floor(cpu):
            cpu.f[1] = math.floor(cpu.f[1])

        table = {
            'NewPtrClear': newptr, 'NewPtr': newptr_dirty, 'DisposePtr': r3(0),
            'NewHandle': newhandle, 'NewHandleClear': newhandle, 'SetHandleSize': sethandlesize,
            'DisposeHandle': r3(0), 'HLock': r3(0), 'HUnlock': r3(0), 'MemError': r3(0),
            'DebugStr': r3(0), 'BlockMove': blockmove, 'BlockMoveData': blockmove,
            'SetA5': identity, 'Microseconds': microseconds,
            'InsTime': r3(0), 'RmvTime': r3(0), 'PrimeTime': primetime,
            'SndDoImmediate': r3(0), 'SndDoCommand': snddocommand,
            'SndNewChannel': sndnewchannel, 'SndDisposeChannel': r3(0),
            'Gestalt': gestalt, 'DTInstall': dtinstall,
            'NewTimerUPP': identity, 'NewSndCallBackUPP': identity, 'NewDeferredTaskUPP': identity,
            'GetResource': getresource, 'DetachResource': r3(0), 'NumToString': numtostring,
            'pow': _pow, 'floor': _floor,
        }
        for name, fn in table.items():
            for addr in by_name.get(name, ()):
                m.cpu.hooks[addr] = fn

        def done(cpu):
            self.done = True
        m.cpu.hooks[DONE_HOOK] = done
        self.svv = None

    # -- the application's launch --------------------------------------------

    def startup(self, gmspeech=GMSPEECH, gmbank=GMBANK):
        m = self.m
        addr, _size = m.load_resource('ttvi', 2)
        self.ttvi_handle = m.handle_to(addr)
        rc = m.call('InitSynth')
        assert rc == 0, 'InitSynth %d' % rc
        svvp = m.alloc(4)
        rc = m.call('Synth_Startup', svvp, POLYPHONY, 0)
        assert (rc & 0xFFFF) == 0, 'Synth_Startup %d' % rc
        self.svv = m.mem.r32(svvp)
        self.xx = m.mem.r32(self.svv + 0xa8)
        voices = self._load_voices(gmspeech)
        if gmbank:
            rf = ResourceFork.from_file(gmbank)
            mwav = rf.get('mwav', 1).data
            mdef = rf.get('mdef', 1).data
            wa = m.alloc(len(mwav) + 16, zero=False)
            m.mem.write(wa, mwav)
            da = m.alloc(len(mdef) + 16, zero=False)
            m.mem.write(da, mdef)
            table = struct.unpack('>I', mwav[8:12])[0]
            pcm = struct.unpack('>h', mwav[0xe:0x10])[0]
            rc = m.call('Synth_SetWaveBank', self.svv, wa + table, pcm, voices)
            self._check(rc, 'Synth_SetWaveBank')
            inst_off, wl_off, count = struct.unpack('>3I', mdef[0xc:0x18])
            for i in range(count - 1):
                rc = m.call('Synth_SetInstrument', self.svv, da + inst_off + 68 * i, da + wl_off, i)
                self._check(rc, 'Synth_SetInstrument %d' % i)
        else:
            m.call('Synth_SetWaveBank', self.svv, 0, 1, voices)
        m.call('Synth_SetTempoScale', self.svv, 0x10000)

    def _check(self, rc, what):
        rc &= 0xFFFF
        assert rc == 0, '%s: %d' % (what, rc - 0x10000 if rc & 0x8000 else rc)

    def _load_voices(self, path):
        m = self.m
        data = ResourceFork.from_file(path).get('mvox', 1).data
        base = m.alloc(len(data), zero=False)
        m.mem.write(base, data)
        n = (0x360 - 0x200) // 4
        for i in range(n):
            p = base + 0x200 + i * 4
            off = m.mem.r32(p)
            if 0 < off < len(data):
                m.mem.w32(p, base + off)
        return base

    # -- a song ----------------------------------------------------------------

    def load_song(self, trk):
        m = self.m
        self.header = trk[:SEQHEADER_SIZE]
        seq = struct.unpack('>I', trk[8:12])[0]
        body = trk[seq:]
        self.image = m.alloc(len(body) + 262144, zero=True)
        m.mem.write(self.image, body)
        self.image_len = len(body)
        self.beatVal = struct.unpack('>h', trk[0x10:0x12])[0]
        self.levels = struct.unpack('>32h', trk[0x16:0x56])
        self.play = struct.unpack('>32h', trk[0x56:0x96])
        for i in range(32):
            t = self.image + i * TRACKINFO_SIZE
            m.mem.w32(t + 0x10, 0)               # speechData: stale in the file
            m.mem.w32(t + 0x14, 0)

    def track(self, i, field):
        return self.m.mem.r32(self.image + i * TRACKINFO_SIZE + field)

    def play_setup(self, reverb, room, wet, has_bank=True):
        m, svv = self.m, self.svv
        m.call('Synth_MakeAllTrackskNotSpeech', svv)
        for i in range(32):
            if self.track(i, 4) & 1:
                rc = m.call('Synth_MakeTrackSpeech', svv, i)
                self._check(rc, 'Synth_MakeTrackSpeech')
        m.call('Synth_SetKaraokeTrack', svv, 0xFFFF)
        for i in range(32):
            flags = self.track(i, 4)
            if flags & 0x10:
                m.call('Synth_SetPlayTrack', svv, i, 0)
                m.call('Synth_SetKaraokeTrack', svv, i)
            else:
                play = 1 if self.play[i] else 0
                if not (flags & 1) and not has_bank:
                    play = 0
                m.call('Synth_SetPlayTrack', svv, i, play)
            m.call('Synth_TrackToChan', svv, i, 0xFFFF)
            m.call('Synth_SetTrackLevel', svv, i, self.levels[i] & 0xFFFF)
        for i in range(32):
            if self.track(i, 4) & 1:
                h = m.handle_to(m.alloc(16))
                lenp = m.alloc(4)
                rc = m.call('Synth_MakeSpeechData', svv, self.image + self.track(i, 0x18),
                            self.image + self.track(i, 8), h, lenp, SPEECH_RATE)
                self._check(rc, 'Synth_MakeSpeechData')
                n = m.mem.r32(lenp)
                data = m.mem.read(m.mem.r32(h), n)
                off = self.image_len
                m.mem.write(self.image + off, data)
                self.image_len += n
                m.mem.w32(self.image + i * TRACKINFO_SIZE + 0x10, off)
                m.mem.w32(self.image + i * TRACKINFO_SIZE + 0x14, n)
        m.call('Synth_SetKbdFlags', svv, 0)
        if reverb:
            wetGain = float(struct.unpack('>f', struct.pack('>f', wet / 100.0))[0])
            m.call('Synth_SetReverb', svv, floats=(room / 100.0, wet / 100.0, 1.0 - wetGain))
        else:
            m.call('Synth_SetReverb', svv, floats=(0.0, 0.0, 0.0))
        m.call('Synth_StartMusic', svv)
        pr = m.alloc(28)
        m.mem.w32(pr, self.image)
        m.mem.w32(pr + 0xc, 1)
        m.mem.w32(pr + 0x10, 960 >> self.beatVal)
        m.mem.w32(pr + 0x14, 0)
        m.mem.w32(pr + 0x18, POLYPHONY)
        self.pr = pr
        m.call('Synth_SetSeqDoneCB', svv, DONE_HOOK, 0)
        rc = m.call('Synth_SeqPlayer', svv, pr)
        self._check(rc, 'Synth_SeqPlayer')

    def render(self, seconds, progress=None):
        m, svv = self.m, self.svv
        bufp, lenp = m.alloc(4), m.alloc(4)
        out = []
        frames = 0
        while not self.done and frames < seconds * 44100:
            m.call('Synth_GetNextBuffer', svv, bufp, lenp)
            buf, n = m.mem.r32(bufp), m.mem.r32(lenp)
            out.append(m.mem.read(buf, n))
            frames += n // 4
            if progress and len(out) % 200 == 0:
                progress(frames)
        raw = b''.join(out)
        return struct.unpack('>%dh' % (len(raw) // 2), raw)


def host_render(song, seconds, reverb, out):
    args = [VWEXE, 'render', song, out, '--max-seconds', str(seconds)]
    if not reverb:
        args.append('--no-reverb')
    env = dict(os.environ, VW_DATA=os.path.join(VW, 'assets'))
    subprocess.check_call(args, env=env, stdout=subprocess.DEVNULL)
    with wave.open(out) as w:
        raw = w.readframes(w.getnframes())
    return struct.unpack('<%dh' % (len(raw) // 2), raw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--song', default=DAISY)
    ap.add_argument('--seconds', type=float, default=6.0)
    ap.add_argument('--no-reverb', action='store_true')
    ap.add_argument('--no-bank', action='store_true')
    args = ap.parse_args()
    reverb = not args.no_reverb
    trk = open(args.song, 'rb').read()
    rsrc_path = args.song + '.rsrc'
    room, wet = 40, 24
    if os.path.exists(rsrc_path):
        sdat = ResourceFork.from_file(rsrc_path).get('sDat', 1).data
        room, wet = struct.unpack('>h', sdat[0x214:0x216])[0], struct.unpack('>h', sdat[0x218:0x21a])[0]
    out = os.path.join(ROOT, 'build', 'seq_host.wav')
    host = host_render(args.song, args.seconds, reverb, out)
    print('host: %d samples' % len(host), flush=True)
    g = SeqGuest()
    g.startup(gmbank=None if args.no_bank else GMBANK)
    g.load_song(trk)
    g.play_setup(reverb, room, wet, has_bank=not args.no_bank)
    guest = g.render(args.seconds, progress=lambda f: print('  guest %.1f s' % (f / 44100.0), flush=True))
    print('guest: %d samples' % len(guest))
    n = min(len(host), len(guest))
    for i in range(n):
        if host[i] != guest[i]:
            print('FAIL: first difference at sample %d (%.3f s, %s): host %d guest %d'
                  % (i, i / 88200.0, 'L' if i % 2 == 0 else 'R', host[i], guest[i]))
            print('  host  %s' % list(host[max(0, i - 4):i + 8]))
            print('  guest %s' % list(guest[max(0, i - 4):i + 8]))
            return 1
    if len(host) != len(guest):
        print('lengths differ: host %d guest %d (compared %d)' % (len(host), len(guest), n))
    print('OK: %d samples identical (%.2f s)' % (n, n / 88200.0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
