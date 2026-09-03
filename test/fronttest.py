#!/usr/bin/env python3
"""Differential test of the text front end: the C port against the original.

The synthesiser test (difftest.py) compares samples; this compares what the
letter-to-sound converter and the phoneme parser produce. The same script
runs through build/harness and through the interpreter, each writing one
line of results per command, and the lines are compared.

    word <text>                          OrthToPhon: syllables, then the
                                         buffers TunePhons left behind
    speech <rate> <vocals> <track>       MakeSpeechData: the speech data block
    recode <start> <end> <flags> <vocals> <track>
                                         AdjustBoundryPhons: the vocals after

`vocals` is a hex table of 26-byte records (a 13-byte text string and a
13-byte phoneme string, both length-prefixed); `track` a hex list of the
12-byte event records GetNextTrackEvent reads, ending in time 0xffffff.

    python test/fronttest.py             # the built-in scenarios
    python test/fronttest.py --words daisy bicycle   # just these words
"""
import argparse
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import difftest                                            # noqa: E402
from difftest import VW, RSRC, GMSPEECH, HARNESS           # noqa: E402
from ppc.synth import VocalWriter                          # noqa: E402
from tools.ttvi import load as load_ttvi, phoneme_order    # noqa: E402

LEXICON = os.path.join(VW, 'assets', 'EnglishLex')

# the stubs the front end calls, and what they get from the guest
NEWPTR, DISPOSEPTR = 0xa64c0, 0xa62c0
SETFPOS, FSREAD = 0xa65c0, 0xa65e0
SETHANDLESIZE, MEMERROR, HLOCK, HUNLOCK = 0xa64e0, 0xa6520, 0xa6280, 0xa6500

#: synthVars fields the front end reads, from the generated header
TABLE_FIELDS = ('phonFlags2', 'maxDurTbl', 'minDurTbl', 'Opcode_To_ASCII', 'phonTypeTbl',
                'hash', 'rule', 'kind', 'dashruletab', 'atruletab', 'lruletab', 'mruletab',
                'zruletab', 'percentruletab', 'bruletab', 'SuffixTab', 'SuffixType')


def synthvars_offsets():
    """Field name -> offset in the original's synthVars, from vw_types.h."""
    offs = {}
    inside = False
    with open(os.path.join(ROOT, 'include', 'vw_types.h')) as fh:
        for ln in fh:
            if ln.startswith('struct synthVars {'):
                inside = True
                continue
            if inside and ln.startswith('}'):
                break
            if inside:
                m = re.match(r'\s+.*?(\w+)(\[\d+\])*;\s+/\* \+0x([0-9a-f]+) \*/', ln)
                if m:
                    offs[m.group(1)] = int(m.group(3), 16)
    return offs


class Guest(object):
    def __init__(self, lexicon=LEXICON):
        self.vw = VocalWriter()
        self.m = self.vw.m
        self.lex = open(lexicon, 'rb').read()
        self.pos = 0
        self.fill = None
        self.lines = []
        self.off = synthvars_offsets()
        self._tables()
        self._hooks()

    def _tables(self):
        m = self.m
        for name in TABLE_FIELDS:
            m.mem.w32(self.vw.g + self.off[name], m.mem.r32(m.globals_ptr('_g_' + name)))

    def _hooks(self):
        m = self.m

        def newptr(cpu):
            n = max(cpu.r[3] & 0xFFFFFFFF, 16)
            addr = m.alloc(n, zero=self.fill is None)
            if self.fill is not None:
                m.mem.write(addr, bytes([self.fill]) * n)
            cpu.r[3] = addr

        def nop(cpu):
            cpu.r[3] = 0

        def setfpos(cpu):
            mode = cpu.r[4] & 0xFFFF
            off = cpu.r[5] & 0xFFFFFFFF
            if off & 0x80000000:
                off -= 1 << 32
            self.pos = {1: off, 2: len(self.lex) + off, 3: self.pos + off}.get(mode, self.pos)
            cpu.r[3] = 0

        def fsread(cpu):
            cnt_p, buf = cpu.r[4], cpu.r[5]
            want = m.mem.r32(cnt_p)
            got = self.lex[self.pos:self.pos + want]
            if got:
                m.mem.write(buf, got)
            self.pos += len(got)
            m.mem.w32(cnt_p, len(got))
            cpu.r[3] = 0 if len(got) == want else (-39 & 0xFFFFFFFF)

        def sethandlesize(cpu):
            h, n = cpu.r[3], cpu.r[4] & 0xFFFFFFFF
            m.mem.w32(h, m.alloc(max(n, 16)))
            cpu.r[3] = 0

        for addr, fn in ((NEWPTR, newptr), (DISPOSEPTR, nop), (SETFPOS, setfpos),
                         (FSREAD, fsread), (SETHANDLESIZE, sethandlesize),
                         (MEMERROR, nop), (HLOCK, nop), (HUNLOCK, nop)):
            m.cpu.hooks[addr] = fn

    def call(self, fn, *args):
        rc = self.m.call(fn, *args)
        rc &= 0xFFFF
        return rc - 0x10000 if rc & 0x8000 else rc

    def words(self, tag, addr, n):
        vals = struct.unpack('>%dH' % n, self.m.mem.read(addr, 2 * n)) if n > 0 else ()
        return ' %s %d:' % (tag, n) + ''.join(' %d' % v for v in vals)

    def run(self, script):
        m, g = self.m, self.vw.g
        for line in script:
            parts = line.split()
            if not parts or parts[0].startswith('#'):
                continue
            cmd = parts[0]
            if cmd == 'fill':
                v = int(parts[1])
                self.fill = None if v < 0 else v
            elif cmd == 'word':
                text = parts[1].encode('mac-roman')[:16]
                rec = m.alloc(272)
                m.mem.write(rec, text)
                m.mem.w32(rec + 0x10, len(text))
                self.pos = 0
                err = self.call('OrthToPhon', g, rec, 1)
                nsyll = m.mem.r32(rec + 0x14)
                out = 'word %s err %d syll %d:' % (parts[1], err, nsyll)
                for s in range(min(nsyll, 10)):
                    p = rec + 0x18 + 12 * s
                    n = m.mem.r32(p + 8)
                    out += ' %d:' % n + '.'.join('%d' % m.mem.r8(p + k) for k in range(min(n, 8)))
                out += self.words('src', m.mem.r32(g + self.off['srcParseBuf']),
                                  m.mem.r32(g + self.off['srcParseLen']))
                n2 = m.mem.r16(g + self.off['phonBuf_2_In_Index'])
                out += self.words('phon2', m.mem.r32(g + self.off['phon_Buf_2']), n2)
                out += self.words('ctrl2', m.mem.r32(g + self.off['phon_Ctrl_Buf_2']), n2)
                self.lines.append(out)
            elif cmd == 'speech':
                rate = int(parts[1])
                vocals, track = bytes.fromhex(parts[2]), bytes.fromhex(parts[3])
                vp, tp = m.alloc(len(vocals) + 16), m.alloc(len(track) + 16)
                m.mem.write(vp, vocals)
                m.mem.write(tp, track)
                h = m.alloc(16)
                m.mem.w32(h, m.alloc(16))
                lenp = m.alloc(16)
                m.mem.w32(lenp, 0xFFFFFFFF)
                err = self.call('MakeSpeechData', g, vp, tp, h, lenp, rate)
                n = m.mem.r32(lenp)
                if n & 0x80000000:
                    n -= 1 << 32
                out = 'speech err %d len %d data:' % (err, n)
                data = m.mem.read(m.mem.r32(h), max(n, 0))
                out += ''.join(' %d' % v for v in struct.unpack('>%dH' % (n // 2), data[:n // 2 * 2]))
                self.lines.append(out)
            elif cmd == 'recode':
                start, end, flags = int(parts[1]), int(parts[2]), int(parts[3])
                vocals, track = bytes.fromhex(parts[4]), bytes.fromhex(parts[5])
                vp, tp = m.alloc(len(vocals) + 16), m.alloc(len(track) + 16)
                m.mem.write(vp, vocals)
                m.mem.write(tp, track)
                err = self.call('AdjustBoundryPhons', g, vp, tp, start, end, flags)
                self.lines.append('recode err %d vocals %s' % (
                    err, m.mem.read(vp, len(vocals)).hex()))
        return self


# ---------------------------------------------------------------------------
# building the inputs


def pstr(b, size=13):
    b = b[:size - 1]
    return bytes([len(b)]) + b + bytes(size - 1 - len(b))


def vocals_table(entries):
    """entries: [(text, [phoneme codes])] -> the 26-byte records."""
    return b''.join(pstr(t.encode('mac-roman')) + pstr(bytes(p)) for t, p in entries)


def track_events(notes):
    """notes: [(time, key, vel, dur, vocals index)] -> the event records."""
    out = b''
    for t, key, vel, dur, idx in notes:
        out += struct.pack('>I', t << 8)[:3] + bytes([0, 6, key, vel])
        out += struct.pack('>I', dur << 8)[:3] + struct.pack('>H', idx)
    return out + b'\xff\xff\xff' + bytes(9)


def word_scenario(words, fills=(-1, 255)):
    lines = ['front', 'lexicon ' + LEXICON]
    for f in fills:
        lines.append('fill %d' % f)
        lines.extend('word ' + w for w in words)
    return lines


def speech_scenario(guest, words, rate=100):
    """Sing `words` on a scale: syllables from OrthToPhon, one note each,
    with a rest before the second phrase and a hyphenated word."""
    lines = ['front', 'lexicon ' + LEXICON, 'fill 0']
    g = Guest()
    g.run(['word ' + w for w in words])
    entries, notes, t, key = [], [], 0, 60
    for w, ln in zip(words, g.lines):
        m = re.match(r'word \S+ err -?\d+ syll (\d+):((?: \d+:[\d.]*)*)', ln)
        sylls = [list(map(int, s.split(':')[1].split('.'))) if s.split(':')[1] else []
                 for s in m.group(2).split()]
        for k, ph in enumerate(sylls):
            text = w[:12] if k == 0 else '-' + w[:8]
            if k + 1 < len(sylls):
                text += '-'
            entries.append((text, ph))
            notes.append((t, key, 100, 200, len(entries) - 1))
            t += 240
            key = 60 + (len(entries) % 7) * 2
        if w == words[len(words) // 2]:
            t += 480                          # a rest
    vocals, track = vocals_table(entries).hex(), track_events(notes).hex()
    for r in (rate, 60, 160):
        lines.append('speech %d %s %s' % (r, vocals, track))
    for flags in (0x3f, 0x01, 0x04, 0x38):
        lines.append('recode 0 16777215 %d %s %s' % (flags, vocals, track))
    lines.append('recode 480 2000 63 %s %s' % (vocals, track))
    return lines


WORDS = ['daisy', 'give', 'me', 'your', 'answer', 'do', 'bicycle', 'marriage',
         'crazy', 'love', 'stylish', 'carriage', 'afford', 'sweet', 'seat',
         'built', 'for', 'two', 'walking', 'walked', 'walks', 'happier', 'happiest',
         'happiness', 'kindnesses', 'organizing', 'organizers', 'realized',
         'nationalism', 'basically', 'probably', 'quickly', 'governments',
         'sentiment', 'flying', 'flies', 'boxes', 'churches', 'wishes', 'buses',
         'judges', 'cats', 'dogs', 'the', 'a', 'I', 'you', 'we', 'them',
         "don't", "it's", "we'll", 'mr.', 'st.', 'xylophone', 'zyxwv', 'qwertyuiop',
         'brontosaurus', 'gnarly', 'kntz', 'aeiou', 'strengths', 'phlegm',
         'Onomatopoeia', 'supercalifragil', 'vocalwriter', 'synthesizer', 'formant',
         'laryngeal', 'through', 'thought', 'rough', 'bough', 'cough', 'dough']


def random_scenario(seed=1, count=24):
    """Random syllable tables and tracks, for the parser's rarer branches."""
    import random
    rnd = random.Random(seed)
    lines = ['front', 'lexicon ' + LEXICON, 'fill 0']
    for _ in range(count):
        n = rnd.randint(1, 10)
        entries = []
        for i in range(n):
            text = ''.join(rnd.choice('abcdefghijklmnopqrstuvwxyz') for _ in range(rnd.randint(1, 9)))
            if rnd.random() < 0.3:
                text += '-'
            if rnd.random() < 0.2:
                text = '-' + text
            # never the rest symbol (23): a syllable of just a rest is what
            # the parser inserts itself, and SetVocals skips those
            phons = [rnd.choice([c for c in range(56) if c != 23]) for _ in range(rnd.randint(1, 8))]
            entries.append((text, phons))
        t, notes = 0, []
        for i in range(n):
            dur = rnd.choice((60, 120, 240, 480))
            notes.append((t, rnd.randint(40, 80), rnd.randint(1, 127), dur, i))
            t += dur + rnd.choice((0, 0, 0, 120, 480))
        vocals, track = vocals_table(entries).hex(), track_events(notes).hex()
        lines.append('speech %d %s %s' % (rnd.choice((40, 100, 180)), vocals, track))
        lines.append('recode 0 16777215 %d %s %s' % (rnd.randrange(64), vocals, track))
    return lines


def run_harness(script_path, prefix):
    subprocess.check_call([HARNESS, RSRC, GMSPEECH, script_path, prefix])
    with open(prefix + '.front') as fh:
        return [ln.rstrip('\n') for ln in fh]


def run_one(name, lines):
    tmp = os.path.join(ROOT, 'build', 'front_' + name)
    with open(tmp + '.txt', 'w') as fh:
        fh.write('\n'.join(lines) + '\n')
    host = run_harness(tmp + '.txt', tmp)
    guest = Guest().run(lines).lines
    ok = True
    if len(host) != len(guest):
        print('%s: result count guest %d host %d' % (name, len(guest), len(host)))
        ok = False
    for k, (gl, hl) in enumerate(zip(guest, host)):
        if gl != hl:
            print('%s: result %d differs' % (name, k))
            print('  guest: %s' % gl[:300])
            print('  host:  %s' % hl[:300])
            ok = False
            break
    print('%s: %s (%d results)' % (name, 'OK' if ok else 'FAIL', len(host)))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--words', nargs='*')
    args = ap.parse_args()
    words = args.words or WORDS
    ok = run_one('words', word_scenario(words))
    ok = run_one('speech', speech_scenario(None, words[:24])) and ok
    ok = run_one('random', random_scenario()) and ok
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
