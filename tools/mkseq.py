#!/usr/bin/env python3
"""Produce the sequencer and its API -- src/music.c, src/convertsmf.c,
src/expandtracks.c, src/synthapi.c and include/vw_synth.h -- from the
lifter's output.

    python tools/mkseq.py

Music.c is the instrument synthesiser and the sequencer that drives both it
and the voices; ConvertSMF.c imports Standard MIDI Files; ExpandTracks.c
rearranges imported tracks; Macintosh.c is the library's public face
(Synth_*) over the Sound Manager, the Time Manager and the callbacks into
the application. The functions that talk to Mac OS itself -- the sound
channel, the timers, the resource manager -- are not lifted: src/synthglue.c
provides them over nothing, for rendering offline.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

MUSIC_PROLOGUE = '''/* Music.c -- VocalWriter's sequencer and instrument synthesiser.
 *
 * Plays the tracks of a song: the instrument notes through 96 wavetable
 * oscillators (PlayFrame16) with envelopes and vibrato (Compute_Envelope),
 * the vocal tracks through the speech engine (Speech_Note), 220 sample
 * frames at a time (FillSampBuf), and mixes both into the sound buffer the
 * reverb then runs over. Lifted from the original's machine code like the
 * rest; see src/speech.c.
 */
#include <math.h>
#include "vw_engine.h"

'''

SMF_PROLOGUE = '''/* ConvertSMF.c -- importing a Standard MIDI File as VocalWriter tracks.
 *
 * Lifted from the original's machine code; see src/speech.c.
 */
#include "vw_engine.h"

'''

EXPAND_PROLOGUE = '''/* ExpandTracks.c -- rearranging imported tracks.
 *
 * Lifted from the original's machine code; see src/speech.c.
 */
#include "vw_engine.h"

int16_t g_GenOverflow;          /* set when an output track overflows */

'''

API_PROLOGUE = '''/* synthapi.c -- the library's public face: Macintosh.c's Synth_* calls.
 *
 * Lifted from the original's machine code; see src/speech.c. What talked
 * to Mac OS itself -- the sound channel, the Time Manager, the Resource
 * Manager, the deferred task that filled the next buffer -- lives in
 * src/synthglue.c instead, written for rendering offline.
 */
#include "vw_engine.h"

'''

#: Music.c functions that already live in src/reverb.c
IN_REVERB = {'XferReverbHold', 'Reverb_Demux16', 'Reverb_Copy16_16', 'Reverb_Mix16_16',
             'Reverb_Mux16', 'ProcessReverbModule', 'ProcessReverbBuffer',
             'Reverberator_Process'}

#: Macintosh.c functions that are elsewhere (tables.c, reverb.c) or that are
#: Mac OS glue, replaced by src/synthglue.c
MAC_SKIP = {
    # tables.c
    'Make_F_Table', 'GetThePtr', 'SetTblAddr', 'InitSharedTables',
    # reverb.c
    'DecibelToInternalVol', 'ClearReverbModule', 'ClearReverbHistory', 'DeleteReverbModules',
    'Reverberator_Init', 'GetReverbMemory', 'Synth_SetReverb',
    # glue: the shared data resource (src/synthglue.c loads it from a file)
    'InitSynth', 'LoadSynthResource',
}


def lift(*args):
    text = subprocess.check_output(
        [sys.executable, os.path.join(HERE, 'lift.py'), '--no-lines'] + list(args),
        cwd=ROOT).decode('utf-8').replace('\r\n', '\n')
    return text


def functions(text):
    """Split lifted text into [(name, static, body)] in order."""
    out = []
    cur = None
    for ln in text.split('\n'):
        if ln.startswith('/* WARNING:') or ln.startswith('/* FAILED'):
            continue
        m = re.match(r'/\* (\w+\.c):\d+  \(0x[0-9a-f]+\) \*/$', ln)
        if m:
            cur = [None, False, [ln]]
            out.append(cur)
            continue
        if cur is None:
            continue
        cur[2].append(ln)
        if cur[0] is None and ln and not ln.startswith((' ', '/', '}', '#')) and '(' in ln:
            cur[0] = ln.split('(')[0].split()[-1].lstrip('*')
            cur[1] = ln.startswith('static ')
    return [(n, st, '\n'.join(b).rstrip() + '\n') for n, st, b in out]


def unknown_returns(text):
    """The original returned whatever r3 held; callers ignore it."""
    return text.replace('return /* r3 */ 0;', 'return 0;   /* the original returned r3, undefined */')


def prototypes(funcs, static=False):
    out = []
    for name, is_static, body in funcs:
        if is_static != static:
            continue
        for ln in body.split('\n'):
            if ln and not ln.startswith((' ', '}', '/', '#')) and '(' in ln and ln.endswith(')'):
                out.append(ln + ';')
                break
    return out


def write(path, text):
    with open(path, 'w', newline='\n') as fh:
        fh.write(text)
    print('wrote', path, text.count('\n'), 'lines')


def main():
    music = [f for f in functions(lift('--unit', 'Music.c')) if f[0] not in IN_REVERB]
    smf = functions(lift('--unit', 'ConvertSMF.c'))
    expand = functions(lift('--unit', 'ExpandTracks.c'))
    mac = [f for f in functions(lift('--unit', 'Macintosh.c')) if f[0] not in MAC_SKIP]

    def unit(funcs, fix=None):
        # the file-static functions, declared before use
        text = '\n'.join(prototypes(funcs, static=True)) + '\n\n'
        text += '\n'.join(b for _n, _s, b in funcs)
        text = unknown_returns(text)
        # QueueMIDIMsg returns r3 untouched on two paths, which held xx
        text = text.replace('            return xx;\n', '            return 0;   /* r3, undefined */\n')
        text = text.replace('        return xx;\n', '        return 0;   /* r3, undefined */\n')
        return fix(text) if fix else text

    def music_fix(text):
        # the file-static entry point the driver calls directly
        text = text.replace('static void UpdateTimer(', 'void UpdateTimer(')
        # a pointer difference between two byte pointers of different signedness
        return text.replace('sp->RbufLen = xx->rec_BufPtr - sp->RbufStart;',
                            'sp->RbufLen = (int32_t)(xx->rec_BufPtr - (unsigned char *)sp->RbufStart);')

    def mac_fix(text):
        # keep every Synth_* entry point and the callbacks visible
        text = re.sub(r'^static (\S+ (?:\*)?(?:_i_|Synth_|Make|CalibrateSndBuffer)\w*\()', r'\1', text, flags=re.M)
        # pointers parked in the Sound Manager's and Deferred Task Manager's
        # long fields (never read back here: there is no sound channel)
        text = re.sub(r'(\.param2|\.dtParam) = (svv|&svv->SH);', r'\1 = (int32_t)(intptr_t)\2;', text)
        # a double zeroed as two words
        return text.replace('        (*(int32_t *)&tempD) = 0;\n        t_2c = 0;', '        tempD = 0.0;')

    write(os.path.join(ROOT, 'src', 'music.c'), MUSIC_PROLOGUE + unit(music, music_fix))
    def smf_fix(text):
        # the progress callback's second argument carries a pointer once
        return text.replace('xx->convert_InfoCB(2, &xx->inSeq[metaMsg],',
                            'xx->convert_InfoCB(2, (intptr_t)&xx->inSeq[metaMsg],')

    write(os.path.join(ROOT, 'src', 'convertsmf.c'), SMF_PROLOGUE + unit(smf, smf_fix))
    def expand_fix(text):
        return re.sub(r'= (\w+) - e_TrackInfos\[0\];', r'= (int32_t)(\1 - (Ptr)e_TrackInfos[0]);', text)

    write(os.path.join(ROOT, 'src', 'expandtracks.c'), EXPAND_PROLOGUE + unit(expand, expand_fix))
    write(os.path.join(ROOT, 'src', 'synthapi.c'), API_PROLOGUE + unit(mac, mac_fix))

    hdr = ['/* vw_synth.h -- the sequencer and the Synth_* API: generated by tools/mkseq.py. */',
           '#ifndef VW_SYNTH_H', '#define VW_SYNTH_H', '',
           'extern int16_t g_GenOverflow;', '', '/* Music.c */'] + prototypes(music) + ['', '/* ConvertSMF.c */'] + prototypes(smf) + \
        ['', '/* ExpandTracks.c */'] + prototypes(expand) + ['', '/* Macintosh.c */'] + \
        prototypes(mac) + ['', '#endif /* VW_SYNTH_H */', '']
    write(os.path.join(ROOT, 'include', 'vw_synth.h'), '\n'.join(hdr))


if __name__ == '__main__':
    main()
