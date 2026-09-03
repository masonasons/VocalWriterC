#!/usr/bin/env python3
"""A structured PowerPC decoder for the subset VocalWriter's SynthLib uses.

Capstone gives text; the lifter wants fields. Each instruction decodes to an
`Ins` with a mnemonic and the operands broken out, in the notation of the
PowerPC architecture manual (rD/rA/rB, SIMM/UIMM, BO/BI, ...).
"""


class Ins(object):
    __slots__ = ('addr', 'word', 'op', 'd', 'a', 'b', 'c', 'imm', 'rc',
                 'bo', 'bi', 'target', 'lk', 'aa', 'sh', 'mb', 'me', 'crf',
                 'spr')

    def __init__(self, addr, word):
        self.addr = addr
        self.word = word
        self.op = None
        self.d = self.a = self.b = self.c = 0
        self.imm = 0
        self.rc = 0
        self.bo = self.bi = 0
        self.target = None
        self.lk = self.aa = 0
        self.sh = self.mb = self.me = 0
        self.crf = 0
        self.spr = 0

    def __repr__(self):
        return '%06x %s d=%d a=%d b=%d imm=%s' % (
            self.addr, self.op, self.d, self.a, self.b, self.imm)


def _simm(word):
    v = word & 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


# primary opcodes with D-form int immediates
_DFORM = {
    7: 'mulli', 8: 'subfic', 10: 'cmpli', 11: 'cmpi', 12: 'addic', 13: 'addic.',
    14: 'addi', 15: 'addis', 24: 'ori', 25: 'oris', 26: 'xori', 27: 'xoris',
    28: 'andi.', 29: 'andis.',
    32: 'lwz', 33: 'lwzu', 34: 'lbz', 35: 'lbzu', 36: 'stw', 37: 'stwu',
    38: 'stb', 39: 'stbu', 40: 'lhz', 41: 'lhzu', 42: 'lha', 43: 'lhau',
    44: 'sth', 45: 'sthu', 46: 'lmw', 47: 'stmw',
    48: 'lfs', 49: 'lfsu', 50: 'lfd', 51: 'lfdu', 52: 'stfs', 53: 'stfsu',
    54: 'stfd', 55: 'stfdu',
}

_UNSIGNED_IMM = {'ori', 'oris', 'xori', 'xoris', 'andi.', 'andis.', 'cmpli'}

_OP31 = {
    0: 'cmp', 32: 'cmpl', 8: 'subfc', 10: 'addc', 11: 'mulhwu', 40: 'subf',
    75: 'mulhw', 104: 'neg', 138: 'adde', 202: 'addze', 234: 'addme',
    235: 'mullw', 266: 'add', 459: 'divwu', 491: 'divw',
    24: 'slw', 28: 'and', 60: 'andc', 124: 'nor', 284: 'eqv', 316: 'xor',
    412: 'orc', 444: 'or', 476: 'nand', 536: 'srw', 792: 'sraw', 824: 'srawi',
    922: 'extsh', 954: 'extsb', 26: 'cntlzw',
    339: 'mfspr', 467: 'mtspr', 19: 'mfcr',
    23: 'lwzx', 55: 'lwzux', 87: 'lbzx', 119: 'lbzux', 279: 'lhzx',
    311: 'lhzux', 343: 'lhax', 151: 'stwx', 183: 'stwux', 215: 'stbx',
    407: 'sthx', 535: 'lfsx', 599: 'lfdx', 663: 'stfsx', 727: 'stfdx',
    598: 'sync', 86: 'dcbf', 54: 'dcbst', 246: 'dcbtst', 278: 'dcbt',
    982: 'icbi', 1014: 'dcbz',
}

_FP_A = {21: 'fadd', 20: 'fsub', 25: 'fmul', 18: 'fdiv', 29: 'fmadd',
         28: 'fmsub', 31: 'fnmadd', 30: 'fnmsub', 22: 'fsqrt', 24: 'fres',
         26: 'frsqrte', 23: 'fsel'}
_FP_X = {72: 'fmr', 40: 'fneg', 264: 'fabs', 136: 'fnabs', 12: 'frsp',
         15: 'fctiwz', 14: 'fctiw', 0: 'fcmpu', 32: 'fcmpo', 583: 'mffs',
         711: 'mtfsf', 38: 'mtfsb1', 70: 'mtfsb0', 64: 'mcrfs'}


def decode(addr, word):
    ins = Ins(addr, word)
    op = word >> 26
    ins.d = (word >> 21) & 31
    ins.a = (word >> 16) & 31
    ins.b = (word >> 11) & 31
    ins.c = (word >> 6) & 31
    ins.rc = word & 1

    if op in _DFORM:
        ins.op = _DFORM[op]
        ins.imm = (word & 0xFFFF) if ins.op in _UNSIGNED_IMM else _simm(word)
        if ins.op in ('cmpi', 'cmpli'):
            ins.crf = (word >> 23) & 7
        return ins
    if op == 16:
        ins.op = 'bc'
        ins.bo, ins.bi = ins.d, ins.a
        bd = word & 0xFFFC
        if bd & 0x8000:
            bd -= 0x10000
        ins.aa, ins.lk = (word >> 1) & 1, word & 1
        ins.target = (bd if ins.aa else addr + bd) & 0xFFFFFFFF
        return ins
    if op == 18:
        ins.op = 'b'
        li = word & 0x03FFFFFC
        if li & 0x02000000:
            li -= 0x04000000
        ins.aa, ins.lk = (word >> 1) & 1, word & 1
        ins.target = (li if ins.aa else addr + li) & 0xFFFFFFFF
        return ins
    if op == 19:
        xo = (word >> 1) & 0x3FF
        ins.bo, ins.bi = ins.d, ins.a
        ins.lk = word & 1
        if xo == 16:
            ins.op = 'bclr'
        elif xo == 528:
            ins.op = 'bcctr'
        elif xo == 449:
            ins.op = 'cror'
        elif xo == 257:
            ins.op = 'crand'
        elif xo == 193:
            ins.op = 'crxor'
        elif xo == 225:
            ins.op = 'crnand'
        elif xo == 33:
            ins.op = 'crnor'
        elif xo == 289:
            ins.op = 'creqv'
        elif xo == 129:
            ins.op = 'crandc'
        elif xo == 417:
            ins.op = 'crorc'
        elif xo == 0:
            ins.op = 'mcrf'
        elif xo == 150:
            ins.op = 'isync'
        else:
            ins.op = 'op19_%d' % xo
        return ins
    if op in (20, 21):
        ins.op = 'rlwimi' if op == 20 else 'rlwinm'
        ins.sh, ins.mb, ins.me = ins.b, (word >> 6) & 31, (word >> 1) & 31
        return ins
    if op == 23:
        ins.op = 'rlwnm'
        ins.mb, ins.me = (word >> 6) & 31, (word >> 1) & 31
        return ins
    if op == 31:
        xo = (word >> 1) & 0x3FF
        ins.op = _OP31.get(xo, 'op31_%d' % xo)
        if ins.op in ('cmp', 'cmpl'):
            ins.crf = (word >> 23) & 7
        if ins.op == 'srawi':
            ins.sh = ins.b
        if ins.op in ('mfspr', 'mtspr'):
            ins.spr = ((word >> 16) & 31) | (((word >> 11) & 31) << 5)
        return ins
    if op in (59, 63):
        xo5 = (word >> 1) & 0x1F
        xo10 = (word >> 1) & 0x3FF
        if xo5 in _FP_A and (op == 59 or xo5 in (21, 20, 25, 18, 29, 28, 31, 30, 22, 23)):
            ins.op = _FP_A[xo5] + ('s' if op == 59 else '')
        elif xo10 in _FP_X:
            ins.op = _FP_X[xo10]
            if ins.op in ('fcmpu', 'fcmpo', 'mcrfs'):
                ins.crf = (word >> 23) & 7
        else:
            ins.op = 'fp%d_%d' % (op, xo10)
        return ins
    ins.op = 'op%d' % op
    return ins


def mask(mb, me):
    if mb <= me:
        return ((0xFFFFFFFF >> mb) & (0xFFFFFFFF << (31 - me))) & 0xFFFFFFFF
    return (~(((0xFFFFFFFF >> (me + 1)) & (0xFFFFFFFF << (31 - mb + 1))))) & 0xFFFFFFFF
