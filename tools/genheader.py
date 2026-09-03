#!/usr/bin/env python3
"""Emit the synthesiser's data structures as a C header, from the STABS.

The layouts are the original's: every struct comes out with the same field
order. Structs that hold no pointers get `_Static_assert`s on their size and
offsets, so the compiler proves the layout; those with pointers cannot match
on a 64-bit host, and the differential test matches their fields by name
instead, through the table `--layout` generates.

    python tools/genheader.py > include/vw_types.h
    python tools/genheader.py --layout > test/layout.c
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from macho import Binary                                   # noqa: E402
from stabs import CEmitter, resolve, sizeof                # noqa: E402

# structs the port needs, by tag; their by-value members are pulled in too
ROOTS = ['formantVar', 'synthVars', 'voiceData', 'ControlBlock', 'Frame', 'shellVar', 'REVERBCONFIG',
         'ConvertTextRec', 'FEToken', 'MIDI_Event', 'MIDI_Item', 'Dict',
         'PlayRec', 'VoiceCtrlBlock', 'VCB_Gen', 'OCB', 'DOC_Regs', 'InstDef', 'WaveListDef',
         'WaveList', 'MIDI_Dur_Type', 'SeqHeader', 'SeqEvent', 'Convert_Event', 'Expand_SMF_Rec',
         'E_TrackEditInfo', 'TMinfo', 'TMTask', 'SndCommand', 'ExtSoundHeader', 'DeferredTask',
         'UnsignedWide', 'QElem', 'MsgRec',
         'WaveDef', 'SeqInfo', 'TrackInfo']


def members_by_value(t):
    """Tags of struct/union types embedded (not pointed to) in t."""
    out = []
    for fname, ftype, bitoff, bitsize in t.fields:
        r = resolve(ftype)
        while r.kind == 'array':
            r = resolve(r.target)
        if r.kind in ('struct', 'union') and r.name:
            out.append(r.name)
    return out


def has_pointer(t):
    for fname, ftype, bitoff, bitsize in t.fields:
        r = resolve(ftype)
        while r.kind == 'array':
            r = resolve(r.target)
        if r.kind == 'ptr':
            return True
        if r.kind in ('struct', 'union') and has_pointer(r):
            return True
    return False


def flatten(t, prefix=''):
    """Every scalar field of t, recursively: (path, guest offset, kind, count)."""
    out = []
    for fname, ftype, bitoff, bitsize in t.fields:
        off = bitoff // 8
        r = resolve(ftype)
        path = prefix + fname
        count = 1
        while r.kind == 'array':
            count *= (r.high - r.low + 1)
            r = resolve(r.target)
        if r.kind in ('struct', 'union'):
            esz = r.size
            arr = resolve(ftype).kind == 'array'
            for i in range(count):
                sub = flatten(r, path + ('[%d].' % i if arr else '.'))
                out.extend((p, off + i * esz + o, k, c) for p, o, k, c in sub)
            continue
        if r.kind == 'ptr':
            kind = 'ptr'
        elif r.kind == 'float':
            kind = 'f32' if r.size == 4 else 'f64'
        elif r.kind in ('int', 'enum'):
            kind = ('i' if r.signed else 'u') + str(r.size * 8)
        else:
            kind = 'ptr'
        out.append((path, off, kind, count))
    return out


def referenced_typedefs(t):
    """(name, Type) of every typedef name the fields of t spell."""
    out = []

    def walk(ty):
        seen = set()
        while ty is not None and id(ty) not in seen:
            seen.add(id(ty))
            if ty.tname:
                out.append((ty.tname, ty))
            if ty.kind in ('typedef', 'const', 'volatile', 'ptr', 'array'):
                ty = ty.target
            else:
                break

    for fname, ftype, bitoff, bitsize in t.fields:
        walk(ftype)
        r = resolve(ftype)
        while r.kind == 'array':
            r = resolve(r.target)
        if r.kind in ('struct', 'union') and r.name is None:
            out.extend(referenced_typedefs(r))
    return out


def collect(b):
    units = {u.base: u for u in b.units}
    tags = {}
    for uname in ('Speech.c', 'Macintosh.c', 'Music.c', 'ParsePhons.c', 'OrthToPhon.c',
                  'ConvertSMF.c', 'ExpandTracks.c'):
        u = units.get(uname)
        if u is None:
            continue
        for key, t in u.types.order:
            kind, name = key.split(':', 1)
            if kind == 'tag' and t.kind in ('struct', 'union') and name not in tags:
                tags[name] = (t, u)
            if kind == 'typedef' and name not in tags:
                r = resolve(t)
                if r.kind in ('struct', 'union') and (not r.name or r.name.startswith('anon_')):
                    r.name = name             # give the anonymous struct the typedef's name
                    tags[name] = (r, u)
    emitted = []
    seen = set()

    def visit(name):
        if name in seen or name not in tags:
            return
        seen.add(name)
        t, u = tags[name]
        for dep in members_by_value(t):
            visit(dep)
        emitted.append((name, t, u))

    for r in ROOTS:
        visit(r)
    return emitted


#: The callback types, with the arguments the engine passes (see
#: INDIRECT_PROTOS in lift.py); STABS records them without parameters.
FUNC_PROTOS = {
    '_i_CvtSMFProg_Ptr': 'void (*_i_CvtSMFProg_Ptr)(int32_t what, intptr_t a, int32_t b, int32_t refCon)',
    'SeqDoneProcPtr': 'void (*SeqDoneProcPtr)(int32_t refCon)',
    'OverloadProcPtr': 'void (*OverloadProcPtr)(int32_t refCon)',
    'MeterProcPtr': 'void (*MeterProcPtr)(int32_t maxL, int32_t maxR, int32_t refCon)',
    'BeatProcPtr': 'void (*BeatProcPtr)(int32_t clock, int32_t refCon)',
    'TempoProcPtr': 'void (*TempoProcPtr)(int32_t tempo, int32_t refCon)',
    'KaraProcPtr': 'void (*KaraProcPtr)(int32_t index, int32_t refCon)',
    'SeqErrorProcPtr': 'void (*SeqErrorProcPtr)(int32_t refCon, int16_t errorCode, uint32_t where)',
    'SeqItemProcPtr': 'void (*SeqItemProcPtr)(int32_t refCon, uint32_t where)',
    'SeqMarkProcPtr': 'void (*SeqMarkProcPtr)(int32_t refCon, uint32_t where)',
    'TimerProcPtr': 'void (*TimerProcPtr)(int32_t data, int32_t refCon)',
    'OMSOutProcPtr': 'void (*OMSOutProcPtr)(void *buffer, int32_t count)',
    'DeferredTaskProcPtr': 'void (*DeferredTaskProcPtr)(int32_t dtParam)',
    'SndCallBackProcPtr': 'void (*SndCallBackProcPtr)(void *chan, void *cmd)',
}
# the UPP names carry the same signatures
for _n in list(FUNC_PROTOS):
    if _n.endswith('ProcPtr'):
        FUNC_PROTOS[_n[:-7] + 'UPP'] = FUNC_PROTOS[_n].replace('(*' + _n + ')', '(*' + _n[:-7] + 'UPP)')



def with_protos(lines):
    out = []
    for ln in lines:
        for name, sig in FUNC_PROTOS.items():
            if ln == 'typedef void (*%s)(void);' % name:
                ln = 'typedef %s;' % sig
        out.append(ln)
    return out


def alignment(t):
    r = resolve(t)
    if r.kind in ('struct', 'union'):
        return max([alignment(ft) for _n, ft, _o, _b in r.fields] or [1])
    if r.kind == 'array':
        return alignment(r.target)
    if r.kind == 'ptr':
        return 4
    return min(r.size or 1, 4) if r.size else 1


def natural_size(t):
    """The struct's size under natural (4-byte) alignment, as PowerPC lays it out."""
    end = 0
    for _n, ft, bitoff, _b in t.fields:
        end = max(end, bitoff // 8 + (sizeof(ft) or 0))
    a = alignment(t)
    return (end + a - 1) // a * a


def header():
    b = Binary()
    emitted = collect(b)
    em = {u.base: CEmitter(u) for u in b.units}
    out = []
    out.append('/* Generated by tools/genheader.py from the STABS records of')
    out.append('   VocalWriter 2.0.1; do not edit. Layouts are the original\'s. */')
    out.append('#ifndef VW_TYPES_H')
    out.append('#define VW_TYPES_H')
    out.append('#include <stdint.h>')
    out.append('#include <stddef.h>')
    out.append('')
    out.append('typedef float rShort;        /* the engine\'s "real short": a float */')
    out.append('typedef float rLong;')
    out.append('typedef float rUSC;')
    out.append('typedef float rUSShort;')
    out.append('typedef float mFloat;')
    out.append('typedef int32_t Fixed;')
    out.append('typedef int32_t SInt32;')
    out.append('typedef int16_t SInt16;')
    out.append('typedef uint32_t UInt32;')
    out.append('typedef uint16_t UInt16;')
    out.append('typedef unsigned char UInt8;')
    out.append('typedef int16_t OSErr;')
    out.append('typedef char *Ptr;')
    out.append('typedef char **Handle;')
    out.append('typedef unsigned char Str255[256];')
    out.append('')
    for name, t, u in emitted:
        out.append('typedef struct %s %s;' % (name, name))
    # Macintosh toolbox names the structs mention but the port never uses:
    # declared just well enough to keep the layouts
    known = {'rShort', 'rLong', 'rUSC', 'rUSShort', 'mFloat', 'Fixed', 'SInt32',
             'SInt16', 'UInt32', 'UInt16', 'UInt8', 'OSErr', 'Ptr', 'Handle',
             'formantVarPtr', 'voiceDataPtr', 'synthVarsPtr', 'FramePtr',
             'WaveDefPtr', 'SeqInfoPtr', '_i_CvtSMFProg_Ptr', 'shellVarPtr',
             'FETokenPtr', 'ConvertTextRecPtr', 'MIDI_EventPtr', 'MIDI_ItemPtr', 'Str255'}
    known |= {n for n, _, _ in emitted}
    stubs = []
    for name, t, u in emitted:
        for tn, tt in referenced_typedefs(t):
            if tn in known or tn in [s[0] for s in stubs] or (tn.endswith('Ptr') and tn[:-3] in known):
                continue          # pointer typedefs of emitted structs come later
            r = resolve(tt)
            if r.kind == 'ptr':
                stubs.append((tn, 'typedef void *%s;' % tn if resolve(r.target).kind != 'func'
                              else 'typedef void (*%s)(void);' % tn))
            elif r.kind == 'int':
                stubs.append((tn, 'typedef %sint%d_t %s;' % ('' if r.signed else 'u', r.size * 8, tn)))
            elif r.kind == 'float':
                stubs.append((tn, 'typedef %s %s;' % ('float' if r.size == 4 else 'double', tn)))
            elif r.kind in ('struct', 'union') and r.name in known:
                stubs.append((tn, 'typedef %s %s;' % (r.name, tn)))
            elif r.kind == 'array':
                stubs.append((tn, 'typedef %s;' % em[u.base].declare(r, tn)))
    for tn, decl in stubs:
        out.append(decl)
    out.append('typedef formantVar *formantVarPtr;')
    out.append('typedef voiceData *voiceDataPtr;')
    out.append('typedef synthVars *synthVarsPtr;')
    out.append('typedef Frame *FramePtr;')
    out.append('typedef WaveDef *WaveDefPtr;')
    out.append('typedef SeqInfo *SeqInfoPtr;')
    out.append('typedef shellVar *shellVarPtr;')
    out.append('typedef void (*_i_CvtSMFProg_Ptr)(int32_t what, intptr_t a, int32_t b, int32_t refCon);')
    out.append('typedef FEToken *FETokenPtr;')
    out.append('typedef ConvertTextRec *ConvertTextRecPtr;')
    out.append('typedef MIDI_Event *MIDI_EventPtr;')
    out.append('typedef MIDI_Item *MIDI_ItemPtr;')
    explicit = {'formantVar', 'voiceData', 'synthVars', 'Frame', 'WaveDef', 'SeqInfo', 'shellVar',
                'FEToken', 'ConvertTextRec', 'MIDI_Event', 'MIDI_Item'}
    for name, t, u in emitted:
        if name not in explicit and name + 'Ptr' not in [s[0] for s in stubs]:
            out.append('typedef %s *%sPtr;' % (name, name))
    out.append('')
    for name, t, u in emitted:
        e = em[u.base]
        packed = natural_size(t) != t.size
        if packed:
            out.append('#pragma pack(push, 2)')
        out.append('struct %s {  /* %d bytes in the original%s */'
                   % (name, t.size, ', 2-byte aligned' if packed else ''))
        out.append(e.struct_body(t))
        out.append('};')
        if packed:
            out.append('#pragma pack(pop)')
        out.append('')
    out.append('#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L')
    out.append('#define VW_LAYOUT_ASSERT(cond, msg) _Static_assert(cond, msg)')
    out.append('#else')
    out.append('#define VW_LAYOUT_ASSERT(cond, msg) typedef char vw_layout_assert_##__LINE__[(cond) ? 1 : -1]')
    out.append('#endif')
    for name, t, u in emitted:
        if has_pointer(t):
            continue
        out.append('VW_LAYOUT_ASSERT(sizeof(struct %s) == %d, "%s size");' % (name, t.size, name))
        for fname, ftype, bitoff, bitsize in t.fields:
            out.append('VW_LAYOUT_ASSERT(offsetof(struct %s, %s) == %d, "%s.%s");'
                       % (name, fname, bitoff // 8, name, fname))
    out.append('')
    out.append('#endif /* VW_TYPES_H */')
    print('\n'.join(with_protos(out)))


def layout():
    """C source describing the host layout of formantVar, for the test."""
    b = Binary()
    u = [x for x in b.units if x.base == 'Speech.c'][0]
    t = u.types.names['tag:formantVar']
    out = ['/* Generated by tools/genheader.py --layout; do not edit. */',
           '#include <stddef.h>',
           '#include "vw_engine.h"',
           '#include "layout.h"',
           'const vw_field vw_formantVar_fields[] = {']
    for path, off, kind, count in flatten(t):
        out.append('    {"%s", %d, offsetof(formantVar, %s), "%s", %d},'
                   % (path, off, path, kind, count))
    out.append('    {NULL, 0, 0, NULL, 0}')
    out.append('};')
    print('\n'.join(out))


if __name__ == '__main__':
    if '--layout' in sys.argv:
        layout()
    else:
        header()
