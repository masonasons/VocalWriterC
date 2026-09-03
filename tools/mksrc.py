#!/usr/bin/env python3
"""Produce src/speech.c from the lifter's output.

The lifted text is the port; this adds the includes, the few adjustments the
C compiler needs (a return value the original never set, wrappers for the
file-static functions the driver calls) and a header comment.

    python tools/mksrc.py
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

PROLOGUE = '''/* Speech.c -- VocalWriter 2.0.1's Resonant Articulatory Synthesis engine.
 *
 * Recreated in C from the shipped PowerPC binary (KAE Labs, 2005). The
 * function and variable names are the original's, from its debug records;
 * the bodies were lifted from its unoptimised machine code by tools/lift.py
 * and are verified statement by statement against an interpreter running
 * the original code. Comments of the form Speech.c:NNN give the line in the
 * original source each function started on.
 *
 * Arithmetic is kept in the width PowerPC used: float where the original
 * used fmuls/fadds/fdivs, double where it used the double forms, with the
 * conversions the compiler inserted. Compile with -ffp-contract=off and
 * -fwrapv so the C compiler does not re-associate or fuse it.
 */
#include "vw_engine.h"

/* One stack slot of the original, modelled. SayFrame reads its local
 * `cycleIndex` on the first sample of every frame before it has assigned it,
 * so what it gets is whatever the previous call at that stack depth left
 * there: the saved stack pointer stored by StartNewPhon,
 * Init_Ctrls_for_New_Phon or SaveFrame (all called just before it, all with
 * frames that land their back chain on that word), or its own final value
 * from the previous frame when none of them ran. See VW_STACK_ADDRESS. */
int32_t vw_stack_slot_c0 = VW_STACK_ADDRESS;

'''


#: Functions whose stack frame, when called from e_Fill_Next_Frame, puts their
#: saved stack pointer exactly where SayFrame's `cycleIndex` lives. SayFrame
#: tests cycleIndex before assigning it on the first sample of every frame,
#: so the original read that saved pointer -- negative, on a Macintosh whose
#: stack sits under 0xC0000000. Modelled with one shared variable, which is
#: also faithful: the original's slot was one place on one stack.
STACK_SLOT_WRITERS = ('StartNewPhon', 'Init_Ctrls_for_New_Phon', 'SaveFrame')

DECL_WORDS = ('int8_t', 'uint8_t', 'int16_t', 'uint16_t', 'int32_t', 'uint32_t',
              'float', 'double', 'rShort', 'rLong', 'rUSC', 'rUSShort', 'mFloat',
              'Fixed', 'Ptr', 'ControlBlock', 'FramePtr', 'synthVarsPtr',
              'formantVarPtr', 'voiceDataPtr', 'Frame', 'voiceData', 'char',
              'unsigned', 'int', 'short', 'long')


def _is_declaration(ln):
    s = ln.strip()
    if not s:
        return True
    return s.split()[0].rstrip('*') in DECL_WORDS


def main():
    text = subprocess.check_output(
        [sys.executable, os.path.join(HERE, 'lift.py'), '--unit', 'Speech.c', '--no-lines'],
        cwd=ROOT).decode('utf-8').replace('\r\n', '\n')
    lines = text.split('\n')
    out = []
    func = None
    for ln in lines:
        if ln.startswith('/* WARNING: return value unknown'):
            continue
        # MakePulse never sets a return value in the original (r3 is whatever
        # the last call left). It is unused; return the waveform it built.
        if ln.strip() == 'return /* r3 */ 0;':
            ln = ln.replace('/* r3 */ 0', 'zz->voiceWaveform')
        # track which function we are in, to place the stack-slot model
        if ln and not ln.startswith((' ', '}', '/', '#')) and '(' in ln:
            func = ln.split('(')[0].split()[-1].lstrip('*')
            first_stmt = True
        elif ln == '}':
            if func == 'SayFrame':
                out.append('    vw_stack_slot_c0 = cycleIndex;')
            func = None
        elif func in STACK_SLOT_WRITERS and ln.startswith('    ') and not ln.startswith('     ') \
                and first_stmt and not ln.strip().endswith(';') is False and \
                not _is_declaration(ln):
            first_stmt = False
            out.append('    vw_stack_slot_c0 = VW_STACK_ADDRESS;   /* see STACK_SLOT_WRITERS */')
        elif func == 'SayFrame' and ln.startswith('    ') and not ln.startswith('     ') \
                and first_stmt and not _is_declaration(ln):
            first_stmt = False
            out.append('    cycleIndex = vw_stack_slot_c0;           /* what the stack held */')
        out.append(ln)
    body = '\n'.join(out).rstrip() + '\n'
    epilogue = '''
/* -- entry points for the driver: the original kept these file-static ------- */

void vw_InitDefaultVoiceCntrls(formantVarPtr zz)
{
    InitDefaultVoiceCntrls(zz);
}

void vw_SetTotalVolume(formantVarPtr zz)
{
    SetTotalVolume(zz);
}

void vw_SetSeqAddr(formantVarPtr zz, int16_t *speechData)
{
    SetSeqAddr(zz, speechData);
}
'''
    path = os.path.join(ROOT, 'src', 'speech.c')
    with open(path, 'w', newline='\n') as fh:
        fh.write(PROLOGUE + body + epilogue)
    print('wrote', path, len(out), 'lines')
    reverb()


REVERB_FUNCS = ['DecibelToInternalVol', 'Reverberator_Init', 'Synth_SetReverb',
                'ClearReverbModule', 'ClearReverbHistory', 'DeleteReverbModules',
                'GetReverbMemory', 'XferReverbHold', 'Reverb_Demux16',
                'Reverb_Copy16_16', 'Reverb_Mix16_16', 'Reverb_Mux16',
                'ProcessReverbModule', 'ProcessReverbBuffer', 'Reverberator_Process']

REVERB_PROLOGUE = '''/* reverb.c -- VocalWriter's reverberator, from Macintosh.c and Music.c.
 *
 * Four all-pass delay modules per channel, with the delays scaled by a room
 * size and mixed wet against dry. The application runs it over each sound
 * buffer after the voices and instruments have been mixed into it
 * (FillSampBuf), so it is the last stage before the samples leave. Lifted
 * from the original's machine code like the rest; see src/speech.c.
 */
#include <stdlib.h>
#include "vw_engine.h"

/* the four delays (ms) and gains (dB) of each side, Macintosh.c's statics */
float g_Reverb_LeftDelay[4] = {193.25f, 131.61f, 84.96f, 52.26f};
float g_Reverb_LeftGain[4] = {-2.498f, -2.2533f, -2.7551f, -2.5828f};
float g_Reverb_RightDelay[4] = {199.92f, 124.61f, 85.73f, 51.5f};
float g_Reverb_RightGain[4] = {-2.4533f, -2.2104f, -2.7953f, -2.5305f};

'''


def reverb():
    text = subprocess.check_output(
        [sys.executable, os.path.join(HERE, 'lift.py'), '--no-lines'] + REVERB_FUNCS,
        cwd=ROOT).decode('utf-8').replace('\r\n', '\n')
    out = []
    for ln in text.split('\n'):
        if ln.startswith('/* WARNING: no prototype for DebugStr'):
            continue
        # the original breaks into the debugger if a delay pointer overruns;
        # the check cannot fire (the pointers are wrapped just above it)
        if ln.strip().startswith('DebugStr('):
            ln = ln[:len(ln) - len(ln.lstrip())] + '/* DebugStr(...); -- cannot happen */'
        ln = ln.replace('NewPtrClear(', 'calloc(1, ').replace('DisposePtr(', 'free(')
        ln = ln.replace('static float DecibelToInternalVol', 'float DecibelToInternalVol')
        ln = ln.replace('static void Reverberator_Init', 'void Reverberator_Init')
        ln = ln.replace('static void ClearReverbHistory', 'void ClearReverbHistory')
        ln = ln.replace('static void DeleteReverbModules', 'void DeleteReverbModules')
        ln = ln.replace('static void XferReverbHold', 'void XferReverbHold')
        out.append(ln)
    path = os.path.join(ROOT, 'src', 'reverb.c')
    with open(path, 'w', newline='\n') as fh:
        fh.write(REVERB_PROLOGUE + '\n'.join(out).rstrip() + '\n')
    print('wrote', path, len(out), 'lines')


if __name__ == '__main__':
    main()
