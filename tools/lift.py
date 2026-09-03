#!/usr/bin/env python3
"""Lift VocalWriter's unoptimised PowerPC back into C.

The synthesiser was compiled with GCC at -O0, so every C statement is a
self-contained run of instructions: load the operands from their stack slots
or struct fields, compute, store the result. Nothing lives in a register from
one statement to the next. With the STABS records naming every local, every
parameter and every struct field, that code can be read back almost
mechanically:

  * each basic block is executed symbolically, registers holding expressions
    rather than values;
  * a store to a named location becomes an assignment, a compare-and-branch
    becomes `if (...) goto`, and the PowerPC idioms GCC uses for int/float
    conversion and division by a constant are folded back into casts and `/`;
  * the resulting goto-graph is restructured into if/else, while, for and
    do-while using the shapes GCC -O0 always emits.

The output is meant to be read and then checked, function by function, against
the interpreter running the original code. What it is not is a guess: every
operator carries the type PowerPC gave it, so the C compiles to the same
arithmetic.

    python tools/lift.py Calc_Pole_Coefficients
    python tools/lift.py --unit Speech.c > gen/speech_lifted.c
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from macho import Binary                                   # noqa: E402
from ppc import decode, mask as rlmask                     # noqa: E402
from stabs import CEmitter, Type, resolve, sizeof          # noqa: E402

# ---------------------------------------------------------------------------
# types

INT_TAGS = {(1, True): 'i8', (1, False): 'u8', (2, True): 'i16', (2, False): 'u16',
            (4, True): 'i32', (4, False): 'u32', (8, True): 'i64', (8, False): 'u64'}
CNAME = {'i8': 'int8_t', 'u8': 'uint8_t', 'i16': 'int16_t', 'u16': 'uint16_t',
         'i32': 'int32_t', 'u32': 'uint32_t', 'i64': 'int64_t', 'u64': 'uint64_t',
         'f32': 'float', 'f64': 'double', 'void': 'void', 'bool': 'int',
         'ptr': 'void *', 'charptr': 'char *'}
SIZE = {'i8': 1, 'u8': 1, 'i16': 2, 'u16': 2, 'i32': 4, 'u32': 4, 'i64': 8,
        'u64': 8, 'f32': 4, 'f64': 8}


def tag_of(t):
    """A scalar tag for a STABS type, or the Type itself for aggregates."""
    r = resolve(t)
    if r.kind == 'int':
        return INT_TAGS.get((r.size, r.signed), 'i32')
    if r.kind == 'enum':
        return 'i32'
    if r.kind == 'float':
        return 'f32' if r.size == 4 else 'f64'
    if r.kind == 'void':
        return 'void'
    return t


def is_scalar(ty):
    return isinstance(ty, str)


def is_int(ty):
    return isinstance(ty, str) and ty[0] in 'iu'


def is_float(ty):
    return ty in ('f32', 'f64')


def is_ptr(ty):
    return isinstance(ty, Type) and resolve(ty).kind == 'ptr'


def pointee(ty):
    return resolve(ty).target


def tsize(ty):
    if isinstance(ty, str):
        return SIZE.get(ty, 4)
    return sizeof(ty) or 4


def same_type(a, b):
    """Do two types name the same thing, typedefs aside?"""
    if a is b:
        return True
    if isinstance(a, str) or isinstance(b, str):
        return a == b
    ra, rb = resolve(a), resolve(b)
    if ra is rb:
        return True
    if ra.kind != rb.kind:
        return False
    if ra.kind in ('struct', 'union', 'enum'):
        return ra.name == rb.name and ra.size == rb.size
    if ra.kind == 'ptr':
        return same_type(ra.target, rb.target)
    if ra.kind == 'array':
        return ra.high == rb.high and same_type(ra.target, rb.target)
    if ra.kind in ('int', 'float', 'void'):
        return ra.size == rb.size and ra.signed == rb.signed
    return False


# ---------------------------------------------------------------------------
# expressions


class Expr(object):
    __slots__ = ('kind', 'ty', 'a', 'b', 'c', 'op', 'name', 'val', 'flt',
                 'lvalue', 'extra')

    def __init__(self, kind, ty='i32', **kw):
        self.kind = kind
        self.ty = ty
        self.a = kw.get('a')
        self.b = kw.get('b')
        self.c = kw.get('c')
        self.op = kw.get('op')
        self.name = kw.get('name')
        self.val = kw.get('val')
        self.flt = kw.get('flt', False)
        self.lvalue = kw.get('lvalue', False)
        self.extra = kw.get('extra')

    def __repr__(self):
        return 'E(%s %s %r %r)' % (self.kind, self.ty, self.name or self.op,
                                   self.val)


def const(v, ty='i32'):
    return Expr('const', ty, val=v)


def is_const(e, v=None):
    return e is not None and e.kind == 'const' and (v is None or e.val == v)


def var(name, ty):
    return Expr('var', ty, name=name, lvalue=True)


def binop(op, x, y, ty):
    return Expr('binop', ty, op=op, a=x, b=y)


def cast(ty, x):
    if x.ty == ty:
        return x
    # (float)(double)int -> (float)int: both round the exact integer once
    if ty == 'f32' and x.kind == 'cast' and x.ty == 'f64' and is_int(x.a.ty):
        return Expr('cast', ty, a=x.a)
    if x.kind == 'cast' and is_int(ty) and is_int(x.ty) and is_int(x.a.ty):
        # (u8)(u16)x -> (u8)x ; (i16)(u16)x -> (i16)x
        if SIZE[ty] <= SIZE[x.ty]:
            return cast(ty, x.a)
    return Expr('cast', ty, a=x)


def same(x, y):
    """Structural equality, good enough to spot `x = x + 1`."""
    if x is y:
        return True
    if x is None or y is None:
        return False
    if x.kind != y.kind or x.ty != y.ty or x.op != y.op or x.name != y.name:
        return False
    if x.kind == 'const':
        return x.val == y.val
    if x.kind == 'call':
        return False
    if x.kind == 'addr':
        if x.val != y.val or len(x.b) != len(y.b):
            return False
        return same(x.a, y.a) and all(same(p[0], q[0]) and p[1] == q[1]
                                      for p, q in zip(x.b, y.b))
    for p, q in ((x.a, y.a), (x.b, y.b), (x.c, y.c)):
        if (p is None) != (q is None):
            return False
        if p is not None and not same(p, q):
            return False
    return True


def linear(e):
    """(x, c) if e is x*c for an integer constant c, else (e, 1)."""
    if e.kind == 'binop' and e.op == '<<' and is_const(e.b) and is_int(e.ty):
        x, c = linear(e.a)
        return x, c << e.b.val
    if e.kind == 'binop' and e.op == '*' and is_const(e.b) and is_int(e.ty):
        x, c = linear(e.a)
        return x, c * e.b.val
    if e.kind == 'binop' and e.op == '*' and is_const(e.a) and is_int(e.ty):
        x, c = linear(e.b)
        return x, c * e.a.val
    return e, 1


def mulc(x, c):
    if c == 1:
        return x
    if c == 0:
        return const(0)
    if is_const(x):
        return const(x.val * c)
    return binop('*', x, const(c), 'i32')


def shl(x, k):
    if k == 0:
        return x
    if is_const(x):
        return const((x.val << k) & 0xFFFFFFFF if x.val >= 0 else x.val << k)
    base, c = linear(x)
    if c != 1:
        return mulc(base, c << k)
    return binop('<<', x, const(k), 'i32')


# ---------------------------------------------------------------------------
# statements


class Stmt(object):
    def __init__(self, kind, **kw):
        self.kind = kind          # assign call if goto return label
        self.lhs = kw.get('lhs')
        self.rhs = kw.get('rhs')
        self.cond = kw.get('cond')
        self.target = kw.get('target')
        self.line = kw.get('line')
        self.addr = kw.get('addr')

    def __repr__(self):
        return 'S(%s %r %r)' % (self.kind, self.lhs or self.cond, self.rhs)


# ---------------------------------------------------------------------------
# the lifter proper


#: What the engine passes when it calls back through a function pointer,
#: by the pointer's typedef: (return tag, argument tags). Read off the
#: registers each call site loads. The Mac OS UPP types resolve to these.
INDIRECT_PROTOS = {
    '_i_CvtSMFProg_Ptr': ('void', ['i32', 'i32', 'i32', 'i32']),   # what, a, b, refCon
    'SeqDoneUPP': ('void', ['i32']),                # refCon
    'SeqDoneProcPtr': ('void', ['i32']),
    'OverloadUPP': ('void', ['i32']),               # refCon
    'OverloadProcPtr': ('void', ['i32']),
    'MeterUPP': ('void', ['i32', 'i32', 'i32']),    # maxL, maxR, refCon
    'MeterProcPtr': ('void', ['i32', 'i32', 'i32']),
    'BeatUPP': ('void', ['i32', 'i32']),            # clock, refCon
    'BeatProcPtr': ('void', ['i32', 'i32']),
    'TempoUPP': ('void', ['i32', 'i32']),           # tempo, refCon
    'TempoProcPtr': ('void', ['i32', 'i32']),
    'KaraUPP': ('void', ['i32', 'i32']),            # index, refCon
    'KaraProcPtr': ('void', ['i32', 'i32']),
    'SeqErrorUPP': ('void', ['i32', 'i16', 'u32']),   # refCon, errorCode, where
    'SeqErrorProcPtr': ('void', ['i32', 'i16', 'u32']),
    'SeqItemUPP': ('void', ['i32', 'u32']),         # refCon, where
    'SeqItemProcPtr': ('void', ['i32', 'u32']),
    'SeqMarkUPP': ('void', ['i32', 'u32']),         # refCon, where
    'SeqMarkProcPtr': ('void', ['i32', 'u32']),
    'TimerUPP': ('void', ['i32', 'i32']),           # data, refCon
    'TimerProcPtr': ('void', ['i32', 'i32']),
    'OMSOutUPP': ('void', ['ptr', 'i32']),          # buffer, count
    'OMSOutProcPtr': ('void', ['ptr', 'i32']),
    'DeferredTaskUPP': ('void', ['i32']),           # dtParam
    'DeferredTaskProcPtr': ('void', ['i32']),
}


class Lifter(object):
    STUBS = {
        'pow': ('f64', ['f64', 'f64']), 'floor': ('f64', ['f64']),
        'log10': ('f64', ['f64']), 'sqrt': ('f64', ['f64']),
        'NewPtrClear': ('ptr', ['i32']), 'NewPtr': ('ptr', ['i32']),
        'DisposePtr': ('void', ['ptr']),
        'NewHandle': ('ptr', ['i32']), 'NewHandleClear': ('ptr', ['i32']),
        # Mac OS the sequencer's glue talks to; src/synthglue.c stands in
        'SetA5': ('u32', ['u32']), 'Microseconds': ('void', ['ptr']),
        'InsTime': ('i16', ['ptr']), 'RmvTime': ('i16', ['ptr']), 'PrimeTime': ('i16', ['ptr', 'i32']),
        'SndDoImmediate': ('i16', ['ptr', 'ptr']), 'SndDoCommand': ('i16', ['ptr', 'ptr', 'i16']),
        'SndNewChannel': ('i16', ['ptr', 'i16', 'i32', 'ptr']), 'SndDisposeChannel': ('i16', ['ptr', 'i16']),
        'Gestalt': ('i16', ['u32', 'ptr']), 'DTInstall': ('i16', ['ptr']),
        'NewTimerUPP': ('ptr', ['ptr']), 'NewSndCallBackUPP': ('ptr', ['ptr']),
        'NewDeferredTaskUPP': ('ptr', ['ptr']), 'NumToString': ('void', ['i32', 'ptr']),
        'GetResource': ('ptr', ['u32', 'i16']), 'DetachResource': ('void', ['ptr']),
        'DisposeHandle': ('void', ['ptr']), 'SetHandleSize': ('void', ['ptr', 'i32']),
        'GetHandleSize': ('i32', ['ptr']), 'HLock': ('void', ['ptr']),
        'HUnlock': ('void', ['ptr']), 'MemError': ('i16', []),
        'DebugStr': ('void', ['ptr']),
        'SetFPos': ('i16', ['i16', 'i16', 'i32']),
        'FSRead': ('i16', ['i16', 'ptr', 'ptr']),
        'BlockMoveData': ('void', ['ptr', 'ptr', 'i32']),
        'BlockMove': ('void', ['ptr', 'ptr', 'i32']),
        'strlen': ('i32', ['ptr']), 'strcpy': ('ptr', ['ptr', 'ptr']),
        'strcmp': ('i32', ['ptr', 'ptr']), 'memcpy': ('ptr', ['ptr', 'ptr', 'i32']),
        'memset': ('ptr', ['ptr', 'i32', 'i32']),
        'sprintf': ('i32', ['ptr', 'ptr']),
    }

    def __init__(self, binary, name, verbose=False, unit=None):
        self.bin = binary
        self.name = name
        self.verbose = verbose
        self.unit, self.func = binary.unit_of(name, unit)
        if self.func is None:
            raise KeyError(name)
        self.em = CEmitter(self.unit)
        self.start, self.end = binary.extent(name, self.func.addr)
        self.warnings = []
        self.temps = {}            # frame offset -> (name, ty)
        self.materialise = set()   # temp offsets that must be real variables
        self.unknown_reads = set()
        self.stmts = []
        self._build_locals()
        self._decode()

    def warn(self, msg):
        self.warnings.append(msg)

    # -- locals -------------------------------------------------------------

    def _build_locals(self):
        self.params = []
        self.locals = {}           # offset -> (name, Type)
        for p in self.func.params:
            self.params.append((p.name, p.type, p.loc))
            self.locals[p.loc] = (p.name, p.type)
        for depth, v in self.func.locals:
            if v.kind == 'l':
                self.locals[v.loc] = (v.name, v.type)
        self.rettype = self.func.rettype
        self.ret_tag = tag_of(self.rettype)

    def slot_var(self, off):
        """(Var expr, inner offset, Type) for a frame offset, or None."""
        for base, (name, ty) in self.locals.items():
            sz = sizeof(ty) or 4
            if base <= off < base + sz:
                return var(name, tag_of(ty)), off - base, ty
        return None

    # -- decoding -----------------------------------------------------------

    def _decode(self):
        self.ins = []
        a = self.start
        while a < self.end:
            self.ins.append(decode(a, self.bin.u32(a)))
            a += 4
        self._find_epilogue()
        self._find_prologue()

    def _find_epilogue(self):
        last = None
        for i in self.ins:
            if i.op == 'bclr' and i.bo == 20:
                last = i
        if last is None:
            raise ValueError('no blr in %s' % self.name)
        k = self.ins.index(last)
        j = k
        while j > 0:
            p = self.ins[j - 1]
            if (p.op == 'lwz' and p.d == 1 and p.a == 1) or p.op == 'lmw' or \
               (p.op == 'lwz' and p.d == 0 and p.a == 1) or \
               (p.op == 'mtspr' and p.spr == 8) or \
               (p.op == 'addi' and p.d == 1 and p.a == 1):
                j -= 1
            else:
                break
        self.epilogue = self.ins[j].addr
        self.epilogue_end = last.addr

    def _find_prologue(self):
        self.picbase = {}
        k = 0
        n = len(self.ins)
        while k < n:
            i = self.ins[k]
            if i.op == 'ori' and i.word == 0x60000000:
                k += 1
                continue
            if i.op == 'mfspr' and i.spr == 8 and i.d == 0:
                k += 1
                continue
            if i.op in ('stmw', 'stwu') or (i.op == 'stw' and i.d == 0 and i.a == 1):
                k += 1
                continue
            if i.op == 'or' and i.d == 1 and i.b == 1 and i.a == 30:
                k += 1
                continue
            if i.op == 'bc' and i.lk and i.target == i.addr + 4:
                nxt = self.ins[k + 1]
                if nxt.op == 'mfspr' and nxt.spr == 8:
                    self.picbase[nxt.d] = nxt.addr
                    k += 2
                    continue
            if i.op == 'mtspr' and i.spr == 8:
                k += 1
                continue
            break
        self.body_start = self.ins[k].addr

    # -- blocks -------------------------------------------------------------

    def _is_data(self, addr):
        return any(a <= addr < b for a, b in self.data_ranges)

    def _blocks(self):
        targets = set()
        for i in self.ins:
            if self._is_data(i.addr):
                continue
            if i.op in ('b', 'bc') and not i.lk:
                targets.add(i.target)
        targets.add(self.epilogue)
        for a, b in self.data_ranges:
            targets.add(b)
        targets |= self.switch_targets
        starts = sorted(t for t in targets if self.body_start <= t < self.end)
        starts = sorted(set(starts) | {self.body_start})
        blocks = []
        for k, s in enumerate(starts):
            e = starts[k + 1] if k + 1 < len(starts) else self.end
            blocks.append([i for i in self.ins if s <= i.addr < e and not self._is_data(i.addr)])
        return [b for b in blocks if b]

    # -- symbolic execution -------------------------------------------------

    def lift(self):
        self.data_ranges = set()
        self.switch_targets = set()
        self.last_bound = None
        blocks = self._blocks()
        self._find_return_block(blocks)
        for _round in range(6):
            self.stmts = []
            self.unknown_reads = set()
            self.last_bound = None
            ndata = len(self.data_ranges)
            for blk in blocks:
                if blk[0].addr >= self.epilogue:
                    self._emit(blk[0].addr, Stmt('label', target=blk[0].addr))
                    self._emit(blk[0].addr, Stmt('return', rhs=self._ret_expr(blk[0].addr)))
                    continue
                self._emit(blk[0].addr, Stmt('label', target=blk[0].addr))
                self._run_block(blk)
            new = self.unknown_reads - self.materialise
            if len(self.data_ranges) != ndata:
                blocks = self._blocks()          # a jump table was found
                self.warnings = []
                continue
            if not new:
                break
            self.materialise |= new
            self.warnings = []
        return self.stmts

    def _find_return_block(self, blocks):
        """A block that only loads the return value before the epilogue.

        GCC -O0 puts `return x;` as a load of x into r3 followed by a jump to
        that block, so a branch to it is a return of x.
        """
        self.ret_block = None
        self.ret_value = None
        if self.ret_tag == 'void':
            return
        for k, blk in enumerate(blocks):
            if blk[0].addr == self.epilogue and k > 0:
                prev = blocks[k - 1]
                if any(i.op in ('stw', 'sth', 'stb', 'stfs', 'stfd', 'b', 'bc') for i in prev):
                    return
                self.stmts = []
                self._run_block(prev)
                if not [s for s in self.stmts if s.kind != 'label']:
                    self.ret_block = prev[0].addr
                    self.ret_value = self._ret_expr(prev[0].addr)
                self.stmts = []
                return

    def _ret_expr(self, addr):
        if self.ret_tag == 'void':
            return None
        if is_float(self.ret_tag):
            reg = self.fregs.get(1)
        else:
            reg = self.regs.get(3)
        if reg is None:
            self.warn('return value unknown at %x' % addr)
            return Expr('raw', self.ret_tag, name='/* r3 */ 0')
        return self.coerce(reg, self.ret_tag)

    def _emit(self, addr, st):
        st.addr = addr
        if st.line is None:
            st.line = self.line_of(addr)
        self.stmts.append(st)

    def line_of(self, addr):
        a = addr
        while a >= self.start:
            if a in self.bin.lines:
                return self.bin.lines[a]
            a -= 4
        return None

    def _reset(self, entry):
        self.regs = {}
        self.fregs = {}
        self.cr = {}
        self.slots = {}
        self.pending = None
        self.postincs = []          # increments folded into a later use
        self.ctr = None
        for r, a in self.picbase.items():
            self.regs[r] = Expr('picbase', 'u32', val=a)
        self.regs[30] = Expr('frame', 'u32')
        self.regs[1] = Expr('frame', 'u32')
        if not entry:
            return
        gpr, fpr = 3, 1
        for name, ty, loc in self.params:
            tg = tag_of(ty)
            if tg in ('f32', 'f64'):
                self.fregs[fpr] = var(name, tg)
                fpr += 1
                gpr += 1 if tg == 'f32' else 2
            else:
                self.regs[gpr] = var(name, tg)
                gpr += 1

    def reg(self, n):
        v = self.regs.get(n)
        if v is None:
            self.warn('r%d read undefined at %x' % (n, self.cur.addr))
            return Expr('raw', 'i32', name='r%d' % n)
        return v

    def freg(self, n):
        v = self.fregs.get(n)
        if v is None:
            self.warn('f%d read undefined at %x' % (n, self.cur.addr))
            return Expr('raw', 'f64', name='f%d' % n)
        return v

    def _run_block(self, blk):
        self._reset(blk[0].addr == self.body_start)
        self.live = self._liveness(blk)
        for k, i in enumerate(blk):
            self.cur = i
            self.live_after = self.live[k]
            self._step(i)
        self._flush_pending()

    def _call_reads(self, i):
        """The argument registers a call actually consumes."""
        name = self.bin.func_by_addr.get(i.target) or self.bin.stubs.get(i.target)
        if name is None:
            return {('r', n) for n in range(3, 11)} | {('f', n) for n in range(1, 14)}
        rett, ptypes = self._prototype(name, i.target)
        R = set()
        gpr, fpr = 3, 1
        for pt in ptypes:
            tg = tag_of(pt) if isinstance(pt, Type) else pt
            if tg in ('f32', 'f64'):
                R.add(('f', fpr))
                fpr += 1
                gpr += 1 if tg == 'f32' else 2
            else:
                R.add(('r', gpr))
                gpr += 1
        return R

    def _rw(self, i):
        """(reads, writes) register sets of one instruction."""
        op = i.op
        R = set()
        W = set()
        ra = {('r', i.a)} if i.a else set()
        if op in ('lwz', 'lhz', 'lha', 'lbz'):
            R |= ra
            W.add(('r', i.d))
        elif op in ('lfs', 'lfd'):
            R |= ra
            W.add(('f', i.d))
        elif op in ('lwzx', 'lhzx', 'lbzx', 'lhax'):
            R |= ra | {('r', i.b)}
            W.add(('r', i.d))
        elif op == 'lfsx':
            R |= ra | {('r', i.b)}
            W.add(('f', i.d))
        elif op in ('stw', 'sth', 'stb'):
            R |= ra | {('r', i.d)}
        elif op in ('stfs', 'stfd'):
            R |= ra | {('f', i.d)}
        elif op in ('stwx', 'sthx', 'stbx'):
            R |= ra | {('r', i.b), ('r', i.d)}
        elif op == 'stfsx':
            R |= ra | {('r', i.b), ('f', i.d)}
        elif op in ('addi', 'addis', 'subfic', 'mulli', 'neg', 'addze'):
            R |= ra
            W.add(('r', i.d))
        elif op in ('add', 'subf', 'mullw', 'mulhw', 'divw', 'divwu'):
            R |= {('r', i.a), ('r', i.b)}
            W.add(('r', i.d))
        elif op in ('srawi', 'rlwinm', 'ori', 'oris', 'xori', 'xoris', 'andi.', 'andis.',
                    'extsh', 'extsb', 'cntlzw'):
            R.add(('r', i.d))
            W.add(('r', i.a))
        elif op == 'rlwimi':
            R |= {('r', i.d), ('r', i.a)}
            W.add(('r', i.a))
        elif op in ('sraw', 'slw', 'srw', 'and', 'or', 'xor', 'nand', 'nor', 'andc'):
            R |= {('r', i.d), ('r', i.b)}
            W.add(('r', i.a))
        elif op in ('cmpi', 'cmpli'):
            R.add(('r', i.a))
        elif op in ('cmp', 'cmpl'):
            R |= {('r', i.a), ('r', i.b)}
        elif op in ('fcmpu', 'fcmpo'):
            R |= {('f', i.a), ('f', i.b)}
        elif op in ('fadd', 'fadds', 'fsub', 'fsubs', 'fdiv', 'fdivs'):
            R |= {('f', i.a), ('f', i.b)}
            W.add(('f', i.d))
        elif op in ('fmul', 'fmuls'):
            R |= {('f', i.a), ('f', i.c)}
            W.add(('f', i.d))
        elif op.startswith('fm') or op.startswith('fnm'):
            if op in ('fmr',):
                R.add(('f', i.b))
            else:
                R |= {('f', i.a), ('f', i.b), ('f', i.c)}
            W.add(('f', i.d))
        elif op in ('fneg', 'fabs', 'frsp', 'fctiwz', 'fctiw'):
            R.add(('f', i.b))
            W.add(('f', i.d))
        elif op == 'mtspr':
            R.add(('r', i.d))
        elif op == 'mfspr':
            W.add(('r', i.d))
        elif op == 'b' and i.lk:
            R |= self._call_reads(i)
            W |= {('r', n) for n in range(0, 13) if n != 1} | {('f', n) for n in range(0, 14)}
        elif op == 'bcctr' and i.lk:
            R |= {('r', n) for n in range(3, 11)} | {('f', n) for n in range(1, 14)}
            W |= {('r', n) for n in range(0, 13) if n != 1} | {('f', n) for n in range(0, 14)}
        return R, W

    def _liveness(self, blk):
        """For each instruction, the registers read later in the block."""
        n = len(blk)
        live = [set() for _ in range(n)]
        cur = set()
        for k in range(n - 1, -1, -1):
            live[k] = set(cur)
            R, W = self._rw(blk[k])
            cur = (cur - W) | R
        return live

    def _flush_pending(self):
        if self.pending is not None:
            call, addr = self.pending
            self.pending = None
            self._emit(addr, Stmt('call', rhs=call))
        self._flush_postincs(None)

    def _flush_postincs(self, st):
        """Increments not consumed by `st` become statements of their own."""
        keep = []
        for e, addr in self.postincs:
            if st is not None and self._mentions(st, e):
                keep.append((e, addr))
                continue
            if st is not None and self._mentions(st, e.a):
                self.warn('post-increment ordering hazard at %x' % addr)
            self._emit(addr, Stmt('assign', lhs=e.a, rhs=e.b))
        self.postincs = keep

    def _statement(self, addr, st):
        """Emit a statement, first flushing a call whose result it ignores."""
        if self.pending is not None:
            call, caddr = self.pending
            self.pending = None
            if not self._mentions(st, call):
                self._emit(caddr, Stmt('call', rhs=call))
        self._flush_postincs(st)
        self._emit(addr, st)
        self.postincs = [(e, a) for e, a in self.postincs if not self._mentions(st, e)]

    def _mentions(self, st, e):
        def walk(x):
            if x is None:
                return False
            if x is e:
                return True
            if isinstance(x, Expr):
                if walk(x.a) or walk(x.b) or walk(x.c):
                    return True
                if isinstance(x.val, list) and any(walk(v) for v in x.val):
                    return True
                if x.kind == 'addr' and any(walk(t[0]) for t in x.b):
                    return True
            return False
        return walk(st.lhs) or walk(st.rhs) or walk(st.cond)

    # -- addresses ----------------------------------------------------------

    def addr_of(self, base_reg, disp):
        b = self.reg(base_reg) if base_reg else const(0)
        return self._add(b, const(disp))

    def _is_addr_like(self, x):
        return x.kind in ('addr', 'frame', 'picbase', 'addrof', 'addrof_global') \
            or is_ptr(x.ty) or (x.lvalue and isinstance(x.ty, Type)
                                and resolve(x.ty).kind == 'array')

    def _add(self, x, y):
        """Add two expressions, keeping track of what is an address."""
        if self._is_addr_like(y) and not self._is_addr_like(x):
            x, y = y, x
        if x.kind == 'addr':
            base, off, terms = x.a, x.val, list(x.b)
        elif self._is_addr_like(x):
            base, off, terms = x, 0, []
        else:
            return self._int_add(x, y)
        if is_const(y):
            off += y.val
        else:
            for idx, scale in self._scaled(y):
                terms.append((idx, scale))
        return Expr('addr', 'u32', a=base, val=off, b=self._merge_terms(terms))

    def _int_add(self, x, y):
        if is_const(x) and is_const(y):
            v = x.val + y.val
            return const(v & 0xFFFFFFFF if v > 0x7FFFFFFF else v)
        if is_const(y) and y.val == 0:
            return x
        if is_const(x):
            x, y = y, x
        if is_const(y) and x.kind == 'binop' and x.op == '+' and is_const(x.b):
            return binop('+', x.a, const(x.b.val + y.val), x.ty)
        if is_const(y) and x.kind == 'binop' and x.op == '-' and is_const(x.b):
            return binop('+', x.a, const(y.val - x.b.val), x.ty)
        # (x*a) + (x*b) -> x*(a+b), the strength-reduced multiplies of -O0
        bx, cx = linear(x)
        by, cy = linear(y)
        if not is_const(y) and same(bx, by) and is_int(x.ty) and is_int(y.ty):
            return mulc(bx, cx + cy)
        ty = x.ty if x.ty != 'i32' else y.ty
        return binop('+', x, y, ty if is_int(ty) else 'i32')

    def _scaled(self, e):
        if e.kind == 'binop' and e.op == '<<' and is_const(e.b):
            inner = self._scaled(e.a)
            return [(i, s << e.b.val) for i, s in inner]
        if e.kind == 'binop' and e.op == '*' and is_const(e.b):
            return [(i, s * e.b.val) for i, s in self._scaled(e.a)]
        if e.kind == 'binop' and e.op == '*' and is_const(e.a):
            return [(i, s * e.a.val) for i, s in self._scaled(e.b)]
        if e.kind == 'binop' and e.op == '+':
            return self._scaled(e.a) + self._scaled(e.b)
        return [(e, 1)]

    def _merge_terms(self, terms):
        out = []
        for idx, sc in terms:
            for k, (i2, s2) in enumerate(out):
                if same(idx, i2):
                    out[k] = (i2, s2 + sc)
                    break
            else:
                out.append((idx, sc))
        return out

    def lvalue(self, addr, size, kind):
        """Resolve a symbolic address to a typed lvalue of `size` bytes."""
        if addr.kind != 'addr':
            addr = self._add(addr, const(0))
        if addr.kind != 'addr':
            addr = Expr('addr', 'u32', a=addr, val=0, b=[])
        base, off, terms = addr.a, addr.val, addr.b
        if base.kind == 'frame':
            return self._frame_lvalue(off, terms, size, kind)
        if base.kind == 'picbase':
            return self._static_lvalue(base.val + off, terms, size, kind)
        if is_const(base):
            return self._static_lvalue(base.val + off, terms, size, kind)
        if base.kind in ('addrof', 'addrof_global'):
            inner = base.a
            t = inner.extra if isinstance(inner.extra, Type) else None
            if t is not None:
                return self._walk(inner, False, t, off, terms, size, kind)
            return self._raw_lvalue(base, off, terms, size, kind)
        ty = base.ty
        if isinstance(ty, Type):
            r = resolve(ty)
            if r.kind == 'ptr':
                return self._walk(base, True, r.target, off, terms, size, kind)
            if base.lvalue and r.kind in ('struct', 'union', 'array'):
                return self._walk(base, False, ty, off, terms, size, kind)
        return self._raw_lvalue(base, off, terms, size, kind)

    def _fallback_tag(self, size, kind):
        if kind == 'f':
            return 'f32' if size == 4 else 'f64'
        return {1: 'u8', 2: 'i16', 4: 'i32', 8: 'i64'}[size]

    def _raw_lvalue(self, base, off, terms, size, kind):
        tg = self._fallback_tag(size, kind)
        e = base
        if off or terms:
            parts = []
            if off:
                parts.append(const(off))
            for idx, scale in terms:
                parts.append(mulc(idx, scale))
            tot = parts[0]
            for p in parts[1:]:
                tot = binop('+', tot, p, 'i32')
            e = binop('+', cast('charptr', base), tot, 'charptr')
        self.warn('untyped access at %x' % self.cur.addr)
        return Expr('deref', tg, a=e, lvalue=True, extra=tg)

    def _frame_lvalue(self, off, terms, size, kind):
        sv = self.slot_var(off)
        if sv is not None:
            v, inner, ty = sv
            r = resolve(ty)
            if r.kind in ('struct', 'union', 'array'):
                return self._walk(v, False, ty, inner, terms, size, kind)
            if inner == 0 and not terms and tsize(tag_of(ty)) == size:
                return v
            if inner == 0 and not terms:
                return Expr('deref', self._fallback_tag(size, kind),
                            a=Expr('addrof', 'ptr', a=v), lvalue=True,
                            extra='partial')
        return self._temp(off, size, kind)

    def _temp(self, off, size, kind):
        tg = self._fallback_tag(size, kind)
        name = self.temps.get(off)
        if name is None:
            name = ('t_%x' % off, tg)
            self.temps[off] = name
        return var(name[0], name[1])

    def _static_lvalue(self, addr, terms, size, kind):
        sec = self.bin.section_of(addr)
        tg = self._fallback_tag(size, kind)
        if terms and all(sc == size for _, sc in terms) and sec is not None \
                and sec[1] == '__text':
            # a jump table inside the code, indexed by a case number
            idx = terms[0][0]
            for ie, _ in terms[1:]:
                idx = self._int_add(idx, ie)
            return Expr('tableload', 'i32', a=const(addr, 'u32'), b=idx, val=size)
        if sec is not None and sec[1] == '__nl_symbol_ptr' and size == 4:
            target, nm = self.bin.nl_pointer(addr)
            g = self._global(nm, target)
            return Expr('addrof_global', 'ptr', a=g, lvalue=False)
        if sec is not None and sec[1] in ('__literal8', '__literal4'):
            v = self.bin.f64(addr) if size == 8 else self.bin.f32(addr)
            return const(v, 'f64' if size == 8 else 'f32')
        if sec is not None and sec[1] in ('__cstring', '__const') and size is not None \
                and not terms and kind != 'f':
            raw = self.bin.read(addr, size)
            v = int.from_bytes(raw, 'big', signed=(tg[0] == 'i'))
            e = const(v, tg)
            if sec[1] == '__cstring':
                e.extra = 'bytes:' + raw.decode('mac-roman', 'replace')
            return e
        if sec is not None and sec[1] == '__cstring':
            return Expr('str', 'ptr', val=self.bin.cstring(addr))
        nm = self.bin.by_addr.get(addr)
        if nm:
            return self._global(nm, addr)
        self.warn('static access to %x at %x' % (addr, self.cur.addr))
        return Expr('deref', tg, a=const(addr, 'u32'), lvalue=True, extra='static')

    def _global(self, nm, addr):
        ty = None
        for u in self.bin.units:
            for g in u.globals + u.statics:
                if g.name == nm:
                    ty = g.type
                    break
            if ty:
                break
        if ty is None:
            self.warn('global %s has no type' % nm)
            return Expr('global', 'i32', name=nm, lvalue=True)
        return Expr('global', tag_of(ty), name=nm, lvalue=True, extra=ty)

    def _walk(self, base, arrow, ty, off, terms, size, kind, whole=False):
        """Descend from `base` (of type ty) by off bytes plus index terms.

        `whole` stops at a struct boundary when the offset is exhausted, which
        gives `&zz->vd` rather than `&zz->vd.voiceName[0]`.
        """
        terms = list(terms)
        e = base
        cur = ty
        first = arrow
        descended = False
        while True:
            r = resolve(cur)
            if r.kind in ('struct', 'union'):
                if first and terms:
                    # a pointer to structs indexed by a multiple of their size
                    ssz = r.size or 1
                    idx, rest = self._take_index([t for t in terms if t[1] % ssz == 0], ssz)
                    if idx is not None:
                        terms = rest + [t for t in terms if t[1] % ssz != 0]
                        e = Expr('index', tag_of(cur), a=base, b=idx, lvalue=True, extra=cur)
                        first = False
                        descended = True
                        continue
                if whole and off == 0 and not terms and descended:
                    return e
                fld = None
                for fname, ftype, bitoff, bitsize in r.fields:
                    fo = bitoff // 8
                    fs = sizeof(ftype) or 0
                    if fo <= off < fo + max(fs, 1):
                        fld = (fname, ftype, fo)
                        if fs == 0:
                            continue
                        break
                if fld is None:
                    for fname, ftype, bitoff, bitsize in r.fields:
                        fo = bitoff // 8
                        if resolve(ftype).kind == 'array' and off >= fo:
                            fld = (fname, ftype, fo)
                    if fld is None:
                        return self._raw_lvalue(e, off, terms, size, kind)
                fname, ftype, fo = fld
                if first:
                    e = Expr('deref', ty, a=base, lvalue=True, extra=ty)
                e = Expr('field', tag_of(ftype), a=e, name=fname, lvalue=True,
                         flt=first, extra=ftype)
                if first:
                    e.a = base
                first = False
                descended = True
                off -= fo
                cur = ftype
                continue
            if r.kind == 'array':
                esize = sizeof(r.target) or 1
                idx, terms = self._take_index(terms, esize)
                n = off // esize
                off = off % esize
                if idx is None:
                    idx = const(n)
                elif n:
                    idx = binop('+', idx, const(n), 'i32')
                if first:
                    e = Expr('deref', ty, a=base, lvalue=True, extra=ty)
                    first = False
                e = Expr('index', tag_of(r.target), a=e, b=idx, lvalue=True,
                         extra=r.target)
                descended = True
                cur = r.target
                continue
            # scalar or pointer
            if first:
                esz = sizeof(cur) or 4
                idx, rest = self._take_index(terms, esz) if terms else (None, terms)
                if idx is not None and not rest and off % esz == 0:
                    if off:
                        idx = binop('+', idx, const(off // esz), 'i32')
                    e = Expr('index', tag_of(cur), a=base, b=idx, lvalue=True, extra=cur)
                    terms = []
                    off = 0
                elif not terms and off == 0:
                    e = Expr('deref', tag_of(cur), a=base, lvalue=True, extra=cur)
                elif not terms and off % esz == 0:
                    e = Expr('index', tag_of(cur), a=base, b=const(off // esz),
                             lvalue=True, extra=cur)
                    off = 0
                else:
                    return self._raw_lvalue(base, off, terms, size, kind)
                first = False
            if off or terms:
                return self._raw_lvalue(Expr('addrof', 'ptr', a=e), off, terms, size, kind)
            want = sizeof(cur) or 4
            if size is not None and want != size:
                tg = self._fallback_tag(size, kind)
                if want > size:
                    return Expr('deref', tg, a=Expr('addrof', 'ptr', a=e), lvalue=True,
                                extra='partial')
                if e.kind == 'deref' and not isinstance(e.extra, str):
                    addr = e.a
                else:
                    addr = Expr('addrof', 'ptr', a=e)
                return Expr('deref', tg, a=addr, lvalue=True, extra='bewide', val=size)
            return e

    def _take_index(self, terms, esize):
        """Fold every term that is a multiple of esize into one index."""
        idx = None
        rest = []
        for ie, sc in terms:
            if sc % esize == 0:
                part = mulc(ie, sc // esize)
                idx = part if idx is None else binop('+', idx, part, 'i32')
            else:
                rest.append((ie, sc))
        if idx is None and esize == 1 and rest:
            ie, sc = rest.pop(0)
            idx = mulc(ie, sc)
        return idx, rest

    def resolve_addr(self, e, whole=True):
        """The lvalue an 'addr' expression points at, or None."""
        if e.kind != 'addr':
            return None
        base, off, terms = e.a, e.val, e.b
        try:
            if base.kind == 'frame':
                sv = self.slot_var(off)
                if sv is None:
                    return None
                v, inner, ty = sv
                r = resolve(ty)
                if r.kind in ('struct', 'union') and whole and inner == 0 and not terms:
                    v.extra = ty
                    return v                      # &the_struct itself
                if r.kind in ('struct', 'union', 'array'):
                    return self._walk(v, False, ty, inner, terms, None, 'i', whole=whole)
                if inner == 0 and not terms:
                    return v
                return None
            if is_ptr(base.ty):
                return self._walk(base, True, pointee(base.ty), off, terms, None, 'i', whole=whole)
            if base.kind in ('addrof', 'addrof_global'):
                inner = base.a
                t = inner.extra if isinstance(inner.extra, Type) else None
                if t is None:
                    return None
                return self._walk(inner, False, t, off, terms, None, 'i', whole=whole)
            if base.lvalue and isinstance(base.ty, Type):
                return self._walk(base, False, base.ty, off, terms, None, 'i', whole=whole)
        except Exception:
            return None
        return None

    # -- helpers for values ---------------------------------------------------

    def coerce(self, e, ty):
        if e.ty == ty:
            return e
        if is_float(ty) and is_float(e.ty):
            return cast(ty, e)
        if is_float(ty) and e.kind == 'const' and is_int(e.ty):
            return const(float(e.val), ty)
        if is_int(ty) and is_int(e.ty):
            if SIZE[ty] > SIZE[e.ty] or (SIZE[ty] == SIZE[e.ty] and ty[0] == e.ty[0]):
                return e
            return cast(ty, e)
        if isinstance(ty, Type) and e.kind == 'const' and e.val == 0:
            return Expr('raw', ty, name='NULL')
        if isinstance(ty, Type):
            if e.kind in ('addr', 'addrof', 'addrof_global'):
                return e
            if isinstance(e.ty, Type) and same_type(e.ty, ty):
                return e
            return cast(ty, e)
        return e

    # -- one instruction ------------------------------------------------------

    def _step(self, i):
        op = i.op
        R, F = self.regs, self.fregs

        def setr(n, e):
            R[n] = e

        def setf(n, e):
            F[n] = e

        # ---- loads
        if op in ('lwz', 'lhz', 'lha', 'lbz', 'lfs', 'lfd'):
            size = {'lwz': 4, 'lhz': 2, 'lha': 2, 'lbz': 1, 'lfs': 4, 'lfd': 8}[op]
            kind = 'f' if op in ('lfs', 'lfd') else 'i'
            addr = self.addr_of(i.a, i.imm)
            if addr.kind == 'addr' and addr.a.kind == 'frame' and not addr.b \
                    and self.slot_var(addr.val) is None:
                v = self.slots.get(addr.val)
                if v is not None:
                    if op == 'lfd' and v.kind == 'i2d':
                        setf(i.d, v)
                        return
                    if op == 'lwz' and is_int(v.ty):
                        setr(i.d, v)
                        return
                    if op == 'lfs' and is_float(v.ty):
                        setf(i.d, cast('f32', v))
                        return
                    if op == 'lfd' and v.ty == 'f64':
                        setf(i.d, v)
                        return
                else:
                    self.unknown_reads.add(addr.val)
            lv = self.lvalue(addr, size, kind)
            if op in ('lfs', 'lfd'):
                setf(i.d, lv)
                return
            setr(i.d, self._load_int(lv, op))
            return
        if op in ('lwzx', 'lhzx', 'lbzx', 'lhax', 'lfsx'):
            size = {'lwzx': 4, 'lhzx': 2, 'lbzx': 1, 'lhax': 2, 'lfsx': 4}[op]
            base = self.reg(i.a) if i.a else const(0)
            addr = self._add(base, self.reg(i.b))
            lv = self.lvalue(addr, size, 'f' if op == 'lfsx' else 'i')
            if op == 'lfsx':
                setf(i.d, lv)
            else:
                setr(i.d, self._load_int(lv, op[:-1]))
            return
        # ---- stores
        if op in ('stw', 'sth', 'stb', 'stfs', 'stfd'):
            size = {'stw': 4, 'sth': 2, 'stb': 1, 'stfs': 4, 'stfd': 8}[op]
            addr = self.addr_of(i.a, i.imm)
            val = self.freg(i.d) if op in ('stfs', 'stfd') else self.reg(i.d)
            self._store(i, addr, size, val, op)
            return
        if op in ('stwx', 'sthx', 'stbx', 'stfsx'):
            size = {'stwx': 4, 'sthx': 2, 'stbx': 1, 'stfsx': 4}[op]
            base = self.reg(i.a) if i.a else const(0)
            addr = self._add(base, self.reg(i.b))
            val = self.freg(i.d) if op == 'stfsx' else self.reg(i.d)
            self._store(i, addr, size, val, op[:-1])
            return
        # ---- integer arithmetic
        if op == 'addi':
            if i.a == 0:
                setr(i.d, const(i.imm))
            else:
                setr(i.d, self._add(self.reg(i.a), const(i.imm)))
            return
        if op == 'addis':
            if i.a == 0:
                setr(i.d, const((i.imm << 16) & 0xFFFFFFFF if i.imm >= 0 else i.imm << 16))
            else:
                setr(i.d, self._add(self.reg(i.a), const(i.imm << 16)))
            return
        if op == 'add':
            setr(i.d, self._add(self.reg(i.a), self.reg(i.b)))
            return
        if op == 'subf':
            setr(i.d, self._sub(self.reg(i.b), self.reg(i.a)))
            return
        if op == 'subfic':
            setr(i.d, self._sub(const(i.imm), self.reg(i.a)))
            return
        if op == 'neg':
            x = self.reg(i.a)
            setr(i.d, const(-x.val) if is_const(x) else Expr('unop', 'i32', op='-', a=x))
            return
        if op == 'mulli':
            x = self.reg(i.a)
            setr(i.d, const(x.val * i.imm) if is_const(x) else mulc(x, i.imm))
            return
        if op == 'mullw':
            x, y = self.reg(i.a), self.reg(i.b)
            if is_const(y):
                setr(i.d, mulc(x, y.val))
            elif is_const(x):
                setr(i.d, mulc(y, x.val))
            else:
                setr(i.d, binop('*', x, y, 'i32'))
            return
        if op == 'mulhw':
            setr(i.d, Expr('call', 'i32', name='MULHW', val=[self.reg(i.a), self.reg(i.b)]))
            return
        if op == 'divw':
            setr(i.d, binop('/', self.reg(i.a), self.reg(i.b), 'i32'))
            return
        if op == 'divwu':
            setr(i.d, binop('/', cast('u32', self.reg(i.a)), cast('u32', self.reg(i.b)), 'u32'))
            return
        if op == 'srawi':
            src = self.reg(i.d)
            e = binop('>>', src, const(i.sh), 'i32')
            e.extra = ('sra', src, i.sh)
            setr(i.a, e)
            return
        if op == 'addze':
            x = self.reg(i.a)
            if x.kind == 'binop' and x.extra and x.extra[0] == 'sra':
                setr(i.d, binop('/', x.extra[1], const(1 << x.extra[2]), 'i32'))
            else:
                self.warn('addze without srawi at %x' % i.addr)
                setr(i.d, x)
            return
        if op == 'sraw':
            setr(i.a, binop('>>', self.reg(i.d), self.reg(i.b), 'i32'))
            return
        if op == 'slw':
            setr(i.a, binop('<<', self.reg(i.d), self.reg(i.b), 'i32'))
            return
        if op == 'srw':
            setr(i.a, binop('>>', cast('u32', self.reg(i.d)), self.reg(i.b), 'u32'))
            return
        if op in ('rlwinm', 'rlwimi'):
            setr(i.a, self._rlwinm(i, self.reg(i.d)))
            if i.rc:
                self._setcr(0, R[i.a], const(0), False)
            return
        if op in ('ori', 'oris', 'xori', 'xoris', 'andi.', 'andis.'):
            if op == 'ori' and i.word == 0x60000000:
                return
            imm = i.imm << 16 if op in ('oris', 'xoris', 'andis.') else i.imm
            x = self.reg(i.d)
            c = {'ori': '|', 'oris': '|', 'xori': '^', 'xoris': '^',
                 'andi.': '&', 'andis.': '&'}[op]
            if is_const(x) and c == '|':
                setr(i.a, const((x.val | imm) & 0xFFFFFFFF if x.val >= 0 else x.val | imm))
            else:
                e = binop(c, x, const(imm if imm < 0x80000000 else imm - 0x100000000), x.ty)
                setr(i.a, e)
                if i.rc:
                    self._setcr(0, e, const(0), False)
            return
        if op in ('and', 'or', 'xor', 'nand', 'nor', 'andc'):
            x, y = self.reg(i.d), self.reg(i.b)
            if op == 'or' and i.d == i.b:                 # mr
                setr(i.a, x)
                return
            c = {'and': '&', 'or': '|', 'xor': '^'}.get(op)
            if c:
                e = binop(c, x, y, x.ty)
            elif op == 'andc':
                e = binop('&', x, Expr('unop', 'i32', op='~', a=y), x.ty)
            else:
                e = Expr('unop', 'i32', op='~', a=binop('&' if op == 'nand' else '|', x, y, 'i32'))
            setr(i.a, e)
            if i.rc:
                self._setcr(0, e, const(0), False)
            return
        if op == 'extsh':
            setr(i.a, self._exts(self.reg(i.d), 'i16'))
            if i.rc:
                self._setcr(0, R[i.a], const(0), False)
            return
        if op == 'extsb':
            setr(i.a, self._exts(self.reg(i.d), 'i8'))
            return
        if op == 'cntlzw':
            setr(i.a, Expr('call', 'i32', name='CNTLZW', val=[self.reg(i.d)]))
            return
        # ---- compares and branches
        if op in ('cmpi', 'cmp'):
            y = const(i.imm) if op == 'cmpi' else self.reg(i.b)
            self._setcr(i.crf, self.reg(i.a), y, False)
            return
        if op in ('cmpli', 'cmpl'):
            y = const(i.imm, 'u32') if op == 'cmpli' else cast('u32', self.reg(i.b))
            self._setcr(i.crf, cast('u32', self.reg(i.a)), y, False)
            if op == 'cmpli':
                self.last_bound = i.imm
            return
        if op == 'fcmpu' or op == 'fcmpo':
            self._setcr(i.crf, self.freg(i.a), self.freg(i.b), True)
            return
        if op in ('cror', 'crand', 'crxor', 'crnor', 'crnand', 'crandc', 'crorc', 'creqv'):
            x = self._crbit(i.a)
            y = self._crbit(i.b)
            if op == 'cror':
                e = Expr('logic', 'bool', op='||', a=x, b=y)
                if x.kind == 'cmp' and y.kind == 'cmp' and same(x.a, y.a) and same(x.b, y.b):
                    ops = {x.op, y.op}
                    # with the unordered bit: (a > b || unordered) is !(a <= b)
                    if 'unord' in ops:
                        other = (ops - {'unord'}).pop() if len(ops) == 2 else None
                        inv = {'>': '<=', '<': '>=', '==': '!='}.get(other)
                        if inv:
                            e = Expr('not', 'bool', a=Expr('cmp', 'bool', op=inv, a=x.a, b=x.b, flt=True))
                            if inv == '!=':
                                e.a.extra = 'ordered'
                            self._setcrbit(i.d, e)
                            return
                    if ops == {'>', '=='}:
                        e = Expr('cmp', 'bool', op='>=', a=x.a, b=x.b, flt=x.flt)
                    elif ops == {'<', '=='}:
                        e = Expr('cmp', 'bool', op='<=', a=x.a, b=x.b, flt=x.flt)
                    elif ops == {'<', '>'}:
                        e = Expr('cmp', 'bool', op='!=', a=x.a, b=x.b, flt=x.flt)
                        e.extra = 'ordered'
            elif op == 'crand':
                e = Expr('logic', 'bool', op='&&', a=x, b=y)
            else:
                self.warn('unusual cr op %s at %x' % (op, i.addr))
                e = Expr('raw', 'bool', name='/* %s */ 0' % op)
            self._setcrbit(i.d, e)
            return
        if op == 'b':
            if i.lk:
                self._call(i)
                return
            if i.target == self.epilogue:
                self._statement(i.addr, Stmt('return', rhs=self._ret_expr(i.addr)))
            elif i.target == self.ret_block:
                self._statement(i.addr, Stmt('return', rhs=self.ret_value))
            else:
                self._statement(i.addr, Stmt('goto', target=i.target))
            return
        if op == 'bc':
            if i.lk:
                return
            cond = self._branch_cond(i)
            if cond is None:
                self._statement(i.addr, Stmt('goto', target=i.target))
                return
            if i.target == self.epilogue:
                self._statement(i.addr, Stmt('if', cond=cond, target=None,
                                             rhs=self._ret_expr(i.addr)))
            elif i.target == self.ret_block:
                self._statement(i.addr, Stmt('if', cond=cond, target=None,
                                             rhs=self.ret_value))
            else:
                self._statement(i.addr, Stmt('if', cond=cond, target=i.target))
            return
        if op == 'bclr':
            return
        if op == 'bcctr':
            if i.lk:
                self._call(i, indirect=True)
            else:
                sw = self._jump_table(self.ctr)
                if sw is not None:
                    idx, targets = sw
                    self._statement(i.addr, Stmt('switch_table', rhs=idx, target=targets))
                else:
                    self._statement(i.addr, Stmt('goto_indirect', rhs=self.ctr))
            return
        if op == 'mtspr':
            if i.spr == 9:
                self.ctr = self.reg(i.d)
            return
        if op == 'mfspr':
            return
        if op in ('lmw', 'stmw', 'stwu', 'sync', 'isync'):
            return
        # ---- floating point
        if op in ('fadd', 'fadds', 'fsub', 'fsubs', 'fmul', 'fmuls', 'fdiv', 'fdivs'):
            single = op.endswith('s')
            base = op[:-1] if single else op
            c = {'fadd': '+', 'fsub': '-', 'fmul': '*', 'fdiv': '/'}[base]
            if base == 'fmul':
                x, y = self.freg(i.a), self.freg(i.c)
            else:
                x, y = self.freg(i.a), self.freg(i.b)
            setf(i.d, self._fbin(c, x, y, single, i))
            return
        if op in ('fmadd', 'fmadds', 'fmsub', 'fmsubs', 'fnmadd', 'fnmadds', 'fnmsub', 'fnmsubs'):
            self.warn('fused multiply-add at %x' % i.addr)
            single = op.endswith('s')
            a, b, c = self.freg(i.a), self.freg(i.b), self.freg(i.c)
            name = op.rstrip('s').upper() + ('S' if single else '')
            setf(i.d, Expr('call', 'f32' if single else 'f64', name=name, val=[a, c, b]))
            return
        if op == 'fmr':
            setf(i.d, self.freg(i.b))
            return
        if op == 'fneg':
            x = self.freg(i.b)
            setf(i.d, Expr('unop', x.ty, op='-', a=x))
            return
        if op == 'fabs':
            x = self.freg(i.b)
            setf(i.d, Expr('call', x.ty, name='fabsf' if x.ty == 'f32' else 'fabs', val=[x]))
            return
        if op == 'frsp':
            setf(i.d, cast('f32', self.freg(i.b)))
            return
        if op in ('fctiwz', 'fctiw'):
            x = self.freg(i.b)
            setf(i.d, Expr('call', 'intpat', name='FTOI' if op == 'fctiwz' else 'FTOI_RN', val=[x]))
            return
        if op == 'mcrf':
            self.cr[i.d >> 2] = self.cr.get(i.a >> 2)
            return
        self.warn('unhandled %s at %x' % (op, i.addr))

    # -- pieces of the above ---------------------------------------------------

    def _load_int(self, lv, op):
        if lv.kind in ('addrof_global', 'const', 'str'):
            return lv
        ty = lv.ty
        if op == 'lwz':
            if is_float(ty):
                return Expr('bits', 'i32', a=lv)
            return lv
        if op == 'lhz':
            return lv if ty == 'u16' else cast('u16', lv)
        if op == 'lha':
            return lv if ty == 'i16' else cast('i16', lv)
        if op == 'lbz':
            return lv if ty == 'u8' else cast('u8', lv)
        return lv

    def _exts(self, x, ty):
        if x.kind == 'cast' and x.ty in ('u16', 'u8') and x.a.ty == ty:
            return x.a
        if x.ty == ty:
            return x
        if x.kind == 'const':
            v = x.val & (0xFFFF if ty == 'i16' else 0xFF)
            if v & (0x8000 if ty == 'i16' else 0x80):
                v -= 0x10000 if ty == 'i16' else 0x100
            return const(v)
        # an arithmetic right shift of a narrow value already fits
        if x.kind == 'binop' and x.op == '>>' and x.a.ty == ty and is_const(x.b) and x.b.val >= 1:
            return x
        if x.kind == 'cast' and x.ty in ('u16', 'u8') and SIZE[x.ty] == SIZE[ty]:
            return cast(ty, x.a)
        return cast(ty, x)

    def _sub(self, x, y):
        # division by a constant: q = mulhw(x, M) >> s ;  q - (x >> 31)
        if x.kind == 'binop' and x.op == '>>' and x.extra and x.extra[0] == 'sra' \
                and y.kind == 'binop' and y.op == '>>' and y.extra and y.extra[0] == 'sra' \
                and y.extra[2] == 31:
            d = self._magic_divisor(x.extra[1], y.extra[1], x.extra[2])
            if d is not None:
                return binop('/', y.extra[1], const(d), 'i32')
        if x.kind == 'call' and x.name == 'MULHW' and y.kind == 'binop' and y.op == '>>' \
                and y.extra and y.extra[2] == 31:
            d = self._magic_divisor(x, y.extra[1], 0)
            if d is not None:
                return binop('/', y.extra[1], const(d), 'i32')
        if is_const(x) and is_const(y):
            return const(x.val - y.val)
        if is_const(y):
            return self._add(x, const(-y.val))
        if x.kind == 'addr' and y.kind == 'addr' and same(x.a, y.a):
            return const(x.val - y.val)
        bx, cx = linear(x)
        by, cy = linear(y)
        if same(bx, by) and is_int(x.ty) and is_int(y.ty):
            return mulc(bx, cx - cy)
        return binop('-', x, y, x.ty if x.ty != 'i32' else y.ty)

    def _magic_divisor(self, q, x, s):
        if q.kind == 'binop' and q.op == '+':
            inner, extra = q.a, q.b
            if not (inner.kind == 'call' and inner.name == 'MULHW'):
                inner, extra = q.b, q.a
            if inner.kind == 'call' and inner.name == 'MULHW' and same(extra, x):
                mx, m = inner.val
                if not same(mx, x) or not is_const(m):
                    return None
                M = m.val + (1 << 32)
                for d in range(2, 1 << 16):
                    if (2 ** (32 + s) + d - 1) // d == M or (2 ** (32 + s)) // d + 1 == M:
                        return d
            return None
        if q.kind == 'call' and q.name == 'MULHW':
            mx, m = q.val
            if not same(mx, x) or not is_const(m):
                return None
            M = m.val & 0xFFFFFFFF
            for d in range(2, 1 << 16):
                if (2 ** (32 + s) + d - 1) // d == M:
                    return d
        return None

    def _rlwinm(self, i, x):
        m = rlmask(i.mb, i.me)
        sh = i.sh
        if i.op == 'rlwimi':
            self.warn('rlwimi at %x' % i.addr)
            return Expr('raw', 'i32', name='/* rlwimi */ 0')
        if sh == 0:
            if m == 0xFFFF:
                return cast('u16', x)
            if m == 0xFF:
                return cast('u8', x)
            return binop('&', x, const(m), 'i32')
        if i.mb == 0 and i.me == 31 - sh:                    # slwi
            return shl(x, sh)
        if i.me == 31 and i.mb == 32 - sh:                   # srwi
            return binop('>>', cast('u32', x), const(32 - sh), 'u32')
        if i.me == 31:
            # rotate right then mask: (x >> n) & m -- a bit-field extract
            e = binop('&', binop('>>', cast('u32', x), const(32 - sh), 'u32'),
                      const(m), 'u32')
            if m == 1:
                e.extra = ('bit', x, 32 - sh)
            return e
        if i.mb == 0:
            return binop('&', shl(x, sh), const(m), 'i32')
        return binop('&', Expr('call', 'u32', name='ROTL32', val=[x, const(sh)]), const(m), 'u32')

    def _fbin(self, c, x, y, single, i):
        if single:
            if x.ty == 'f64' or y.ty == 'f64':
                self.warn('double operand in single-precision %s at %x' % (i.op, i.addr))
            if c == '/' and os.environ.get('VW_FDIVS_MACRO'):
                # an experiment switch: single-precision division through a
                # macro, to test what the reference interpreter does
                return Expr('call', 'f32', name='FDIVS',
                            val=[self.coerce(x, 'f32'), self.coerce(y, 'f32')])
            return binop(c, self.coerce(x, 'f32'), self.coerce(y, 'f32'), 'f32')
        if c == '-' and x.kind == 'i2d' and is_const(y):
            if y.val == 4503601774854144.0:
                return cast('f64', x.a)
            if y.val == 4503599627370496.0:
                return cast('f64', cast('u32', x.a))
        return binop(c, x, y, 'f64')

    def _store(self, i, addr, size, val, op):
        if addr.kind == 'addr' and addr.a.kind == 'frame' and not addr.b \
                and self.slot_var(addr.val) is None:
            off = addr.val
            if op == 'stfd' and val.kind == 'call' and val.name in ('FTOI', 'FTOI_RN'):
                self.slots[off + 4] = Expr('call', 'i32', name=val.name, val=val.val)
                self._materialise(i, off + 4, self.slots[off + 4])
                return
            if op == 'stw' and is_const(val) and val.val == 0x43300000:
                self.slots[off] = val
                self._check_i2d(off)
                return
            if op == 'stw':
                self.slots[off] = val
                self._check_i2d(off - 4)
                self._materialise(i, off, val)
                return
            if op == 'stfs':
                self.slots[off] = cast('f32', val)
                self._materialise(i, off, self.slots[off])
                return
            self.slots[off] = val
            self._materialise(i, off, val)
            return
        lv = self.lvalue(addr, size, 'f' if op in ('stfs', 'stfd') else 'i')
        if lv.kind == 'var' and lv.name in [p[0] for p in self.params] and val.kind == 'var' \
                and val.name == lv.name:
            return                                         # the parameter spill
        rhs = self._store_value(lv, val, size, op)
        if self._fold_postinc(i, lv, rhs):
            return
        self._statement(i.addr, Stmt('assign', lhs=lv, rhs=rhs))

    def _refers(self, e, lv):
        """Does expression e read the lvalue lv?"""
        if e is None:
            return False
        if same(e, lv):
            return True
        if isinstance(e.val, list) and any(self._refers(v, lv) for v in e.val):
            return True
        if e.kind == 'addr':
            return self._refers(e.a, lv) or any(self._refers(t[0], lv) for t in e.b)
        return self._refers(e.a, lv) or self._refers(e.b, lv) or self._refers(e.c, lv)

    def _fold_postinc(self, i, lv, rhs):
        """`r = x; x = x + c; use(r)` is `use(x++)`: keep the old value live.

        A register loaded from `lv` before this store still names `lv`, which
        after the store would read the new value. If the store is an
        increment, the register becomes `lv++`, an expression that yields the
        old value and performs the store when it is used. Anything else that
        still refers to lv is snapshotted into a temporary first.
        """
        holders = [(R, n) for R, tag in ((self.regs, 'r'), (self.fregs, 'f'))
                   for n, v in R.items()
                   if (tag, n) in self.live_after and v.kind not in ('frame', 'picbase')
                   and self._refers(v, lv)]
        if not holders:
            return False
        inc = None
        if rhs.kind == 'binop' and rhs.op == '+' and same(rhs.a, lv) and is_const(rhs.b):
            inc = rhs.b.val
        elif rhs.kind == 'addr' and same(rhs.a, lv) and not rhs.b:
            esz = sizeof(pointee(lv.ty)) if is_ptr(lv.ty) else None
            if esz and rhs.val % esz == 0:
                inc = rhs.val // esz
        exact = [(R, n) for R, n in holders if same(R[n], lv) or
                 (R[n].kind == 'cast' and same(R[n].a, lv))]
        if inc is not None and len(exact) == len(holders):
            post = Expr('postinc', lv.ty, a=lv, b=rhs, val=inc)
            for R, n in exact:
                R[n] = post if same(R[n], lv) else cast(R[n].ty, post)
            self.postincs.append((post, i.addr))
            return True
        # general case: snapshot the old value
        name = 'old_%s' % (lv.name if lv.kind == 'var' else 'val')
        k = 0
        while any(t[0] == name + (str(k) if k else '') for t in self.temps.values()):
            k += 1
        name = name + (str(k) if k else '')
        key = -len(self.temps) - 1
        self.temps[key] = (name, lv.ty)
        snap = var(name, lv.ty)
        self._statement(i.addr, Stmt('assign', lhs=snap, rhs=lv))
        for R, n in holders:
            R[n] = self._replace(R[n], lv, snap)
        self.warn('snapshot of %s at %x' % (name, i.addr))
        return False

    def _replace(self, e, lv, new):
        if e is None:
            return None
        if same(e, lv):
            return new
        f = Expr(e.kind, e.ty, a=self._replace(e.a, lv, new), b=self._replace(e.b, lv, new),
                 c=self._replace(e.c, lv, new), op=e.op, name=e.name, val=e.val,
                 flt=e.flt, lvalue=e.lvalue, extra=e.extra)
        if isinstance(e.val, list):
            f.val = [self._replace(v, lv, new) for v in e.val]
        if e.kind == 'addr':
            f.b = [(self._replace(t[0], lv, new), t[1]) for t in e.b]
        return f

    def _materialise(self, i, off, val):
        """A compiler temporary read in another block becomes a variable."""
        if off not in self.materialise:
            return
        if val.kind == 'i2d':
            return
        tg = val.ty if is_scalar(val.ty) else 'i32'
        if tg in ('intpat', 'bool'):
            tg = 'i32'
        name = self.temps.get(off)
        if isinstance(val.ty, Type) and is_ptr(val.ty):
            tg = val.ty                       # a pointer parked in a slot stays one
        elif name is not None and isinstance(name[1], Type) and is_ptr(name[1]):
            tg = name[1]                      # ... even when NULL is stored in it
        if name is None or name[1] != tg:
            self.temps[off] = (name[0] if name else 't_%x' % off, tg)
        v = var(self.temps[off][0], tg)
        self._statement(i.addr, Stmt('assign', lhs=v, rhs=val))
        self.slots[off] = v

    def _check_i2d(self, off):
        hi = self.slots.get(off)
        lo = self.slots.get(off + 4)
        if hi is not None and is_const(hi) and hi.val == 0x43300000 and lo is not None:
            if lo.kind == 'binop' and lo.op == '^' and is_const(lo.b) and \
                    (lo.b.val & 0xFFFFFFFF) == 0x80000000:
                self.slots[off] = Expr('i2d', 'f64', a=lo.a)
            else:
                self.slots[off] = Expr('i2d', 'f64', a=lo)

    def _store_value(self, lv, val, size, op):
        ty = lv.ty
        if op in ('stfs', 'stfd'):
            if is_float(ty):
                return self.coerce(val, ty)
            return val
        if is_float(ty):
            if is_const(val) and is_int(val.ty):
                v = val.val & 0xFFFFFFFF
                if ty == 'f32':
                    return const(struct.unpack('>f', struct.pack('>I', v))[0], 'f32')
            return Expr('bits', ty, a=val)
        if isinstance(ty, Type):
            r = resolve(ty)
            if r.kind == 'ptr' and isinstance(val.ty, Type) and not same_type(val.ty, ty):
                return cast(ty, val)
            if r.kind == 'ptr' and val.kind == 'addr':
                lv = self.resolve_addr(val)
                lt = lv.extra if lv is not None and isinstance(lv.extra, Type) else None
                if lv is None or lv.kind == 'deref' or lt is None or \
                        not (same_type(lt, r.target) or (resolve(lt).kind == 'array' and
                                                         same_type(resolve(lt).target, r.target))):
                    return cast(ty, val)
            return self.coerce(val, ty)
        if ty in ('i16', 'u16', 'i8', 'u8'):
            return self._narrow(val, ty)
        return val

    def _narrow(self, val, ty):
        bits = SIZE[ty] * 8

        def strip(e):
            if e.kind == 'cast' and is_int(e.ty) and is_int(e.a.ty) and SIZE[e.ty] * 8 >= bits:
                return strip(e.a)
            if e.kind == 'binop' and e.op in ('+', '-', '*', '&', '|', '^', '<<'):
                f = Expr('binop', e.ty, op=e.op, a=strip(e.a), b=strip(e.b))
                f.extra = e.extra
                return f
            if e.kind == 'unop' and e.op in ('-', '~'):
                return Expr('unop', e.ty, op=e.op, a=strip(e.a))
            return e
        return strip(val)

    # -- condition register -----------------------------------------------------

    def _setcr(self, f, x, y, flt):
        self.cr[f] = {'lt': Expr('cmp', 'bool', op='<', a=x, b=y, flt=flt),
                      'gt': Expr('cmp', 'bool', op='>', a=x, b=y, flt=flt),
                      'eq': Expr('cmp', 'bool', op='==', a=x, b=y, flt=flt),
                      # the fourth bit: unordered for a float compare, never
                      # set by the integer compares this code uses
                      'so': Expr('cmp', 'bool', op='unord', a=x, b=y, flt=True) if flt
                      else Expr('raw', 'bool', name='0')}

    def _crbit(self, n):
        f, b = n >> 2, n & 3
        cr = self.cr.get(f)
        if cr is None:
            self.warn('cr%d read undefined at %x' % (f, self.cur.addr))
            return Expr('raw', 'bool', name='cr%d' % f)
        return cr[('lt', 'gt', 'eq', 'so')[b]]

    def _setcrbit(self, n, e):
        f, b = n >> 2, n & 3
        cr = self.cr.setdefault(f, {'lt': None, 'gt': None, 'eq': None})
        cr[('lt', 'gt', 'eq', 'so')[b]] = e

    def _branch_cond(self, i):
        bo = i.bo
        if bo & 0x14 == 0x14:
            return None
        if not (bo & 0x04):
            self.warn('ctr branch at %x' % i.addr)
        bit = self._crbit(i.bi)
        if bo & 0x08:
            return bit
        return self._not(bit)

    def _not(self, e):
        if e.kind == 'cmp' and not e.flt:
            inv = {'<': '>=', '>': '<=', '==': '!=', '>=': '<', '<=': '>', '!=': '=='}[e.op]
            return Expr('cmp', 'bool', op=inv, a=e.a, b=e.b)
        if e.kind == 'cmp' and e.flt and e.op == '==':
            return Expr('cmp', 'bool', op='!=', a=e.a, b=e.b, flt=True)
        if e.kind == 'cmp' and e.flt and e.op == '!=' and e.extra != 'ordered':
            return Expr('cmp', 'bool', op='==', a=e.a, b=e.b, flt=True)
        if e.kind == 'not':
            return e.a
        if e.kind == 'logic' and self._int_only(e):
            # De Morgan is exact when no comparison involves NaN
            return Expr('logic', 'bool', op='&&' if e.op == '||' else '||',
                        a=self._not(e.a), b=self._not(e.b))
        return Expr('not', 'bool', a=e)

    def _int_only(self, e):
        if e.kind == 'cmp':
            return not e.flt
        if e.kind == 'logic':
            return self._int_only(e.a) and self._int_only(e.b)
        if e.kind == 'not':
            return self._int_only(e.a)
        return False

    # -- calls -------------------------------------------------------------------

    def _indirect_proto(self, fn):
        """(return tag, arg tags) of a call through fn, by its typedef name."""
        t = fn.extra if isinstance(fn.extra, Type) else fn.ty
        while isinstance(t, Type):
            for nm in (t.tname, t.name):
                if nm in INDIRECT_PROTOS:
                    return INDIRECT_PROTOS[nm]
            if t.kind not in ('typedef', 'const', 'volatile'):
                break
            t = t.target
        return None

    def _call(self, i, indirect=False):
        if indirect:
            fn = self.ctr
            proto = self._indirect_proto(fn) if fn is not None else None
            if proto is None:
                self.warn('indirect call at %x' % i.addr)
                e = Expr('call', 'i32', name='(*fn)', val=[])
                self._statement(i.addr, Stmt('call', rhs=e))
                return
            rett, ptypes = proto
            name = None
            target = None
        else:
            target = i.target
            name = self.bin.func_by_addr.get(target) or self.bin.stubs.get(target)
            if name is None:
                self.warn('call to unknown %x at %x' % (target, i.addr))
                name = 'sub_%x' % target
            rett, ptypes = self._prototype(name, target)
        args = []
        gpr, fpr = 3, 1
        for pt in ptypes:
            tg = tag_of(pt) if isinstance(pt, Type) else pt
            if tg in ('f32', 'f64'):
                args.append(self.coerce(self.freg(fpr), tg))
                fpr += 1
                gpr += 1 if tg == 'f32' else 2
            else:
                a = self.reg(gpr)
                if tg in ('i16', 'i8'):
                    a = self._exts(a, tg)
                elif tg in ('u16', 'u8'):
                    a = cast(tg, a) if a.ty != tg else a
                elif isinstance(pt, Type) and not is_scalar(tg):
                    a = self.coerce(a, pt)
                args.append(a)
                gpr += 1
        if self.pending is not None:
            pcall, paddr = self.pending
            self.pending = None
            self._emit(paddr, Stmt('call', rhs=pcall))
        rtag = tag_of(rett) if isinstance(rett, Type) else rett
        call = Expr('call', rett if (isinstance(rett, Type) and not is_scalar(rtag)) else rtag,
                    name=name, val=args, a=None if not indirect else fn)
        if rtag == 'void':
            self._statement(i.addr, Stmt('call', rhs=call))
            return
        self.pending = (call, i.addr)
        if rtag in ('f32', 'f64'):
            self.fregs[1] = call
        else:
            self.regs[3] = call

    def _jump_table(self, ctr):
        """(index expression, [target addresses]) for a `bctr` through a table.

        GCC's PIC switch: `entry = table[idx]; goto entry + table` with the
        entries relative to the table's own address, guarded just before by
        `if ((unsigned)idx > N) goto default`, which gives the table's length.
        """
        if ctr is None:
            return None
        base = None
        tl = ctr
        if ctr.kind == 'binop' and ctr.op == '+':
            for x, y in ((ctr.a, ctr.b), (ctr.b, ctr.a)):
                if x.kind == 'tableload' and is_const(y):
                    tl, base = x, y.val
        if tl.kind == 'addr' and tl.a.kind == 'tableload' and not tl.b:
            tl, base = tl.a, tl.val
        if tl.kind == 'addr' and tl.a.kind == 'picbase' and len(tl.b) == 1 \
                and tl.b[0][1] == 1 and tl.b[0][0].kind == 'tableload':
            # entry + table address, the address being PIC-relative
            base = tl.a.val + tl.val
            tl = tl.b[0][0]
        if tl.kind != 'tableload':
            return None
        table = tl.a.val
        n = (self.last_bound + 1) if self.last_bound is not None else None
        if n is None or n > 512:
            return None
        targets = []
        for k in range(n):
            w = self.bin.u32(table + 4 * k)
            if w is None:
                return None
            if base is not None:
                if w & 0x80000000:
                    w -= 1 << 32
                w = (base + w) & 0xFFFFFFFF
            if not (self.start <= w < self.end):
                return None
            targets.append(w)
        self.data_ranges.add((table, table + 4 * n))
        self.switch_targets |= set(targets)
        return tl.b, targets

    def _prototype(self, name, target):
        if name in self.bin.funcs:
            u, f = self.bin.unit_of(name)
            return f.rettype, [p.type for p in f.params]
        if name in self.STUBS:
            return self.STUBS[name]
        self.warn('no prototype for %s' % name)
        return 'i32', []


# ---------------------------------------------------------------------------
# rendering

PREC = {'||': 1, '&&': 2, '|': 3, '^': 4, '&': 5, '==': 6, '!=': 6,
        '<': 7, '>': 7, '<=': 7, '>=': 7, '<<': 8, '>>': 8, '+': 9, '-': 9,
        '*': 10, '/': 10, '%': 10}
UNARY = 11
POSTFIX = 12


def float_literal(v, ty):
    if v != v:
        return 'NAN'
    if v in (float('inf'), float('-inf')):
        return 'INFINITY' if v > 0 else '-INFINITY'
    if ty == 'f32':
        def ok(s):
            try:
                return struct.unpack('>f', struct.pack('>f', float(s)))[0] == v
            except (OverflowError, ValueError):
                return False
    else:
        def ok(s):
            try:
                return float(s) == v
            except ValueError:
                return False
    best = None
    for n in range(0, 18):
        for fmt in ('%.*f', '%.*g'):
            s = fmt % (n, v)
            if ok(s) and (best is None or len(s) < len(best)):
                best = s
        if best is not None:
            break
    s = best if best is not None else repr(v)
    if '.' not in s and 'e' not in s and 'n' not in s:
        s += '.0'
    if 'e' in s and '.' not in s.split('e')[0]:
        s = s.replace('e', '.0e')
    return s + ('f' if ty == 'f32' else '')


def int_literal(v):
    if v < 0:
        return '-' + int_literal(-v)
    if v >= 0x80000000:
        return '0x%x' % v
    if v in (0x7F, 0xFF, 0x7FFF, 0x8000, 0xFFFF):
        return '0x%x' % v
    if v >= 0x10000 and (v & 0xFF) in (0, 0xFF):
        return '0x%x' % v
    if v >= 0x10000 and (v & (v + 1)) == 0:
        return '0x%x' % v
    return str(v)


class Renderer(object):
    def __init__(self, lifter):
        self.L = lifter
        self.em = lifter.em

    def ctype(self, ty):
        if isinstance(ty, str):
            return CNAME.get(ty, ty)
        return self.em.declare(ty, '')

    def render(self, e, prec=0):
        s, p = self._render(e)
        if p < prec:
            return '(' + s + ')'
        return s

    def _render(self, e):
        k = e.kind
        if k == 'const':
            if is_float(e.ty) or isinstance(e.val, float):
                return float_literal(e.val, e.ty if is_float(e.ty) else 'f64'), POSTFIX
            v = e.val
            if isinstance(e.extra, str) and e.extra.startswith('bytes:'):
                txt = e.extra[6:].replace('*/', '* /')
                return '0x%x /* %r */' % (v & ((1 << (SIZE[e.ty] * 8)) - 1), txt), POSTFIX
            if v < 0:
                return int_literal(v), UNARY
            return int_literal(v), POSTFIX
        if k in ('var', 'global', 'raw'):
            return e.name, POSTFIX
        if k == 'str':
            return '"%s"' % e.val.replace('\\', '\\\\').replace('"', '\\"'), POSTFIX
        if k == 'field':
            return '%s%s%s' % (self.render(e.a, POSTFIX), '->' if e.flt else '.', e.name), POSTFIX
        if k == 'index':
            return '%s[%s]' % (self.render(e.a, POSTFIX), self.render(e.b, 0)), POSTFIX
        if k == 'deref':
            if e.extra == 'bewide':
                return 'VW_LD%dBE(%s)' % (e.val * 8, self.render(e.a, 0)), POSTFIX
            if isinstance(e.extra, str):
                return '*(%s *)%s' % (self.ctype(e.ty), self.render(e.a, UNARY)), UNARY
            return '*%s' % self.render(e.a, UNARY), UNARY
        if k in ('addrof', 'addrof_global'):
            return '&%s' % self.render(e.a, UNARY), UNARY
        if k == 'addr':
            return self._render_addr(e)
        if k == 'frame':
            return '/* frame */ 0', POSTFIX
        if k == 'picbase':
            return '/* pic 0x%x */ 0' % e.val, POSTFIX
        if k == 'cast':
            if e.a.kind == 'addr' and isinstance(e.ty, Type) and is_ptr(e.ty):
                lv = self.L.resolve_addr(e.a)
                if lv is not None and lv.kind != 'deref':
                    lt = lv.extra if isinstance(lv.extra, Type) else None
                    if lt is not None and same_type(lt, pointee(e.ty)):
                        return self._render(e.a)
                    if lv.kind == 'index' and lt is not None and resolve(lt).kind == 'array' \
                            and same_type(resolve(lt).target, pointee(e.ty)):
                        return self._render(e.a)
            if e.a.kind == 'addrof' and isinstance(e.ty, Type) and is_ptr(e.ty):
                lt = e.a.a.extra if isinstance(e.a.a.extra, Type) else None
                if lt is not None and same_type(lt, pointee(e.ty)):
                    return self._render(e.a)
            return '(%s)%s' % (self.ctype(e.ty), self.render(e.a, UNARY)), UNARY
        if k == 'bits':
            return 'BITS_%s(%s)' % ('F' if is_float(e.ty) else 'I', self.render(e.a, 0)), POSTFIX
        if k == 'i2d':
            return '(double)%s' % self.render(e.a, UNARY), UNARY
        if k == 'postinc':
            if e.val == 1:
                return '%s++' % self.render(e.a, POSTFIX), POSTFIX
            if e.val == -1:
                return '%s--' % self.render(e.a, POSTFIX), POSTFIX
            return '(%s += %d)' % (self.render(e.a, POSTFIX), e.val), POSTFIX
        if k == 'unop':
            return '%s%s' % (e.op, self.render(e.a, UNARY)), UNARY
        if k == 'not':
            return '!%s' % self.render(e.a, UNARY), UNARY
        if k == 'binop':
            return self._render_binop(e)
        if k == 'cmp':
            return self._render_cmp(e)
        if k == 'logic':
            p = PREC[e.op]
            # && inside || is bracketed, which is what the compiler asks for
            l = self.render(e.a, p + 1 if e.a.kind == 'logic' and e.a.op != e.op else p)
            r = self.render(e.b, p + 1)
            return '%s %s %s' % (l, e.op, r), p
        if k == 'call':
            args = ', '.join(self.render(a, 0) for a in (e.val or []))
            if e.name is None:
                return '%s(%s)' % (self.render(e.a, POSTFIX), args), POSTFIX
            return '%s(%s)' % (e.name, args), POSTFIX
        return '/* ? %s */' % k, POSTFIX

    def _bit_test(self, e):
        """(x >> n) & 1 as a mask test, or None."""
        if e.kind == 'binop' and e.extra and isinstance(e.extra, tuple) and e.extra[0] == 'bit':
            return e.extra[1], 1 << e.extra[2], False
        if e.kind == 'binop' and e.op == '^' and is_const(e.b, 1):
            r = self._bit_test(e.a)
            if r:
                return r[0], r[1], not r[2]
        return None

    def _render_cmp(self, e):
        if e.op == 'unord':
            return 'isunordered(%s, %s)' % (self.render(e.a, 0), self.render(e.b, 0)), POSTFIX
        p = PREC[e.op]
        if e.op in ('==', '!=') and is_const(e.b, 0):
            bt = self._bit_test(e.a)
            if bt:
                x, m, inv = bt
                op = e.op if not inv else ('!=' if e.op == '==' else '==')
                return '(%s & %s) %s 0' % (self.render(x, PREC['&'] + 1), int_literal(m), op), p
        return '%s %s %s' % (self.render(e.a, p + 1), e.op, self.render(e.b, p + 1)), p

    def _render_binop(self, e):
        p = PREC[e.op]
        a, b = e.a, e.b
        if e.op == '&' and is_const(b) and a.kind == 'cast' and a.ty in ('u16', 'u8') \
                and is_int(a.a.ty) and 0 <= b.val < (1 << (SIZE[a.ty] * 8)):
            a = a.a
        if e.op == '+' and is_const(b) and not is_float(b.ty) and b.val < 0:
            return '%s - %s' % (self.render(a, p), int_literal(-b.val)), p
        if e.ty == 'f64' and a.ty != 'f64' and b.ty != 'f64':
            a = cast('f64', a)
        if e.op in ('<<', '>>'):
            left = self.render(a, PREC['*'] + 1)
            right = self.render(b, PREC['*'] + 1)
        elif e.op in ('&', '|', '^'):
            left = self.render(a, PREC['*'] + 1)
            right = self.render(b, PREC['*'] + 1)
        else:
            left = self.render(a, p)
            right = self.render(b, p + 1)
        return '%s %s %s' % (left, e.op, right), p

    def _render_addr(self, e):
        base, off, terms = e.a, e.val, e.b
        lv = self.L.resolve_addr(e)
        if lv is not None and lv.kind != 'deref':
            return '&%s' % self.render(lv, UNARY), UNARY
        if lv is not None and lv.kind == 'deref' and not isinstance(lv.extra, str) and not off and not terms:
            return self.render(base, POSTFIX), POSTFIX
        if base.kind == 'frame':
            return '/* frame+0x%x */ 0' % off, POSTFIX
        parts = [self.render(cast('charptr', base), PREC['+'])]
        if off:
            parts.append(int_literal(off) if off > 0 else '- %d' % -off)
        for idx, sc in terms:
            parts.append(self.render(mulc(idx, sc), PREC['+'] + 1))
        return ' + '.join(parts).replace('+ - ', '- '), PREC['+']

    def stmt(self, st, indent):
        pad = '    ' * indent
        if st.kind == 'assign':
            if st.lhs.kind == 'deref' and st.lhs.extra == 'bewide':
                return '%sVW_ST%dBE(%s, %s);' % (pad, st.lhs.val * 8, self.render(st.lhs.a, 0),
                                                self.render(st.rhs, 0))
            lhs = self.render(st.lhs, 0)
            if st.lhs.kind == 'deref':
                lhs = '(%s)' % lhs          # (*p)++ is not *p++
            rhs = st.rhs
            inc = self._pointer_step(st.lhs, rhs)
            if inc is not None:
                return '%s%s%s;' % (pad, lhs, inc)
            if rhs.kind == 'binop' and rhs.op in ('+', '-', '*', '/', '&', '|', '^', '<<', '>>') \
                    and same(rhs.a, st.lhs) and (not is_float(rhs.ty) or rhs.a.ty == rhs.ty):
                if rhs.op in ('+', '-') and is_const(rhs.b) and rhs.b.val == 1:
                    return '%s%s%s;' % (pad, lhs, rhs.op * 2)
                if rhs.op in ('+', '-') and is_const(rhs.b) and rhs.b.val == -1:
                    return '%s%s%s;' % (pad, lhs, ('-' if rhs.op == '+' else '+') * 2)
                if rhs.op == '+' and is_const(rhs.b) and not is_float(rhs.b.ty) and rhs.b.val < 0:
                    return '%s%s -= %s;' % (pad, lhs, int_literal(-rhs.b.val))
                return '%s%s %s= %s;' % (pad, lhs, rhs.op, self.render(rhs.b, 0))
            return '%s%s = %s;' % (pad, lhs, self.render(rhs, 0))
        if st.kind == 'call':
            return '%s%s;' % (pad, self.render(st.rhs, 0))
        if st.kind == 'return':
            if st.rhs is None:
                return '%sreturn;' % pad
            return '%sreturn %s;' % (pad, self.render(st.rhs, 0))
        if st.kind == 'goto':
            return '%sgoto L_%x;' % (pad, st.target)
        if st.kind == 'goto_indirect':
            return '%s/* jump table */ goto *%s;' % (pad, self.render(st.rhs, 0))
        if st.kind == 'label':
            return 'L_%x:' % st.target
        if st.kind == 'if':
            if st.target is None:
                r = 'return;' if st.rhs is None else 'return %s;' % self.render(st.rhs, 0)
                return '%sif (%s) %s' % (pad, self.render(st.cond, 0), r)
            return '%sif (%s) goto L_%x;' % (pad, self.render(st.cond, 0), st.target)
        return '%s/* %s */' % (pad, st.kind)

    def _pointer_step(self, lhs, rhs):
        """`p = &p[n]` rendered as `p += n` / `p++`."""
        e = rhs
        if e.kind == 'cast' and isinstance(e.ty, Type):
            e = e.a
        if e.kind != 'addr' or not same(e.a, lhs) or not is_ptr(lhs.ty):
            return None
        esz = sizeof(pointee(lhs.ty)) or 1
        off, terms = e.val, e.b
        if not terms and off % esz == 0:
            n = off // esz
            if n == 1:
                return '++'
            if n == -1:
                return '--'
            return ' += %d' % n if n >= 0 else ' -= %d' % -n
        if len(terms) == 1 and off == 0 and terms[0][1] == esz:
            return ' += %s' % self.render(terms[0][0], 0)
        return None


# ---------------------------------------------------------------------------
# structuring


class Block(object):
    def __init__(self, addr):
        self.addr = addr
        self.body = []
        self.term = None     # None | ('goto', t) | ('if', cond, t) | ('return', e) | ('ifret', cond, e)
        self.preds = set()
        self.synthetic = False

    def __repr__(self):
        return 'B(%x %r)' % (self.addr, self.term)


class Node(object):
    def __init__(self, kind, **kw):
        self.kind = kind
        self.__dict__.update(kw)


def build_blocks(stmts):
    blocks = []
    cur = None
    for st in stmts:
        if st.kind == 'label':
            cur = Block(st.target)
            blocks.append(cur)
            continue
        if cur is None:
            cur = Block(st.addr)
            cur.synthetic = True
            blocks.append(cur)
        if st.kind in ('assign', 'call'):
            cur.body.append(st)
        elif st.kind == 'goto':
            cur.term = ('goto', st.target)
            cur = None
        elif st.kind == 'return':
            cur.term = ('return', st.rhs)
            cur = None
        elif st.kind == 'if':
            cur.term = ('ifret', st.cond, st.rhs) if st.target is None else ('if', st.cond, st.target)
            nb = Block(st.addr + 4)
            nb.synthetic = True
            blocks.append(nb)
            cur = nb
        elif st.kind == 'goto_indirect':
            cur.term = ('goto_indirect', st.rhs)
            cur = None
        elif st.kind == 'switch_table':
            cur.term = ('switch', st.rhs, st.target)
            cur = None
    return [b for b in blocks if not (b.synthetic and not b.body and b.term is None)]


class Structurer(object):
    def __init__(self, lifter, stmts):
        self.L = lifter
        self.blocks = build_blocks(stmts)
        self._reindex()
        self._normalise()
        self._merge_conditions()
        self._preds()
        self.used_labels = set()

    def _reindex(self):
        self.index = {b.addr: k for k, b in enumerate(self.blocks)}

    def _pure_cond(self, b):
        return not b.body and b.term is not None and b.term[0] == 'if'

    def _pure_goto(self, b):
        return not b.body and b.term is not None and b.term[0] == 'goto'

    def _has_other_preds(self, Y, X):
        for k, b in enumerate(self.blocks):
            if b is X:
                continue
            if b.term and b.term[0] in ('goto', 'if') and b.term[-1] == Y.addr:
                return True
            if b.term is None or b.term[0] in ('if', 'ifret'):
                if k + 1 < len(self.blocks) and self.blocks[k + 1] is Y and b is not X:
                    return True
        return False

    def _normalise(self):
        """`if (c) goto A; goto B; A:` becomes `if (!c) goto B`."""
        changed = True
        while changed:
            changed = False
            for k in range(len(self.blocks) - 2):
                X, Y, Z = self.blocks[k], self.blocks[k + 1], self.blocks[k + 2]
                if X.term and X.term[0] == 'if' and self._pure_goto(Y) \
                        and X.term[2] == Z.addr and not self._has_other_preds(Y, X):
                    X.term = ('if', self.L._not(X.term[1]), Y.term[1])
                    del self.blocks[k + 1]
                    self._reindex()
                    changed = True
                    break

    def _merge_conditions(self):
        changed = True
        while changed:
            changed = False
            for k in range(len(self.blocks) - 1):
                X, Y = self.blocks[k], self.blocks[k + 1]
                if X.term is None or X.term[0] != 'if' or not self._pure_cond(Y):
                    continue
                if self._has_other_preds(Y, X):
                    continue
                a, jX = X.term[1], X.term[2]
                b, jY = Y.term[1], Y.term[2]
                fY = self.blocks[k + 2].addr if k + 2 < len(self.blocks) else None
                if jX == fY:
                    cond = Expr('logic', 'bool', op='&&', a=self.L._not(a), b=b)
                    X.term = ('if', cond, jY)
                    del self.blocks[k + 1]
                    self._reindex()
                    changed = True
                    break
                if jX == jY:
                    cond = Expr('logic', 'bool', op='||', a=a, b=b)
                    X.term = ('if', cond, jX)
                    del self.blocks[k + 1]
                    self._reindex()
                    changed = True
                    break

    def _preds(self):
        for b in self.blocks:
            b.preds = set()
        for k, b in enumerate(self.blocks):
            t = b.term
            if t is None or t[0] in ('if', 'ifret'):
                if k + 1 < len(self.blocks):
                    self.blocks[k + 1].preds.add(b.addr)
            if t and t[0] in ('goto', 'if'):
                tgt = t[-1]
                if tgt in self.index:
                    self.blocks[self.index[tgt]].preds.add(b.addr)
            if t and t[0] == 'switch':
                for tgt in t[2]:
                    if tgt in self.index:
                        self.blocks[self.index[tgt]].preds.add(b.addr)

    # -- structuring -------------------------------------------------------------

    def structure(self):
        self.looping = set()
        self.consumed = set()
        return self._region(0, len(self.blocks), None)

    def _closed(self, lo, hi, entry):
        """Is blocks[lo:hi] entered only through its first block, from entry?"""
        for b in self.blocks[lo:hi]:
            for p in b.preds:
                pi = self.index[p]
                if lo <= pi < hi:
                    continue
                if b is self.blocks[lo] and pi == entry:
                    continue
                return False
        return True

    def _backward_into(self, lo, hi):
        """Does any block in [lo, hi) branch back to blocks[lo]?"""
        head = self.blocks[lo].addr
        for b in self.blocks[lo + 1:hi]:
            if b.term and b.term[0] in ('goto', 'if') and b.term[-1] == head:
                return True
        return False

    def _addr(self, k):
        return self.blocks[k].addr if k < len(self.blocks) else None

    # -- switch recovery ----------------------------------------------------------

    @staticmethod
    def _cmp_var_const(cond):
        """(var, op, consts) if cond compares a variable with constants.

        `op` is one of the comparison operators, or 'in' for a chain of
        equalities joined by || (the merged form of consecutive case tests).
        """
        if cond.kind == 'logic':
            # a == 1 || a == 2  (taken for members)  /  a != 1 && a != 2 (taken for non-members)
            want = '==' if cond.op == '||' else '!='
            parts = []
            todo = [cond]
            while todo:
                e = todo.pop()
                if e.kind == 'logic' and e.op == cond.op:
                    todo.extend([e.a, e.b])
                else:
                    parts.append(e)
            var = None
            vals = []
            for e in parts:
                if e.kind != 'cmp' or e.flt or e.op != want or e.a.kind != 'var'                         or not is_const(e.b) or not is_int(e.b.ty):
                    return None
                if var is not None and e.a.name != var.name:
                    return None
                var = e.a
                vals.append(e.b.val)
            return var, 'in' if cond.op == '||' else 'notin', vals
        if cond.kind != 'cmp' or cond.flt:
            return None
        a, b = cond.a, cond.b
        if a.kind == 'var' and is_const(b) and is_int(b.ty):
            return a, cond.op, [b.val]
        return None

    def _try_switch(self, k, hi, exit_addr):
        """Recognise GCC's compare tree for `switch` starting at block k."""
        b = self.blocks[k]
        first = self._cmp_var_const(b.term[1])
        if first is None:
            return None
        var = first[0]

        def decision(idx):
            if idx is None or idx < k or idx >= hi:
                return None
            blk = self.blocks[idx]
            if idx != k and not self._pure_cond(blk):
                return None
            c = self._cmp_var_const(blk.term[1])
            if c is None or c[0].name != var.name:
                return None
            return c

        # collect the decision blocks reachable from k
        decisions = {}
        todo = [k]
        while todo:
            idx = todo.pop()
            if idx in decisions:
                continue
            c = decision(idx)
            if c is None:
                continue
            decisions[idx] = c
            tgt = self.index.get(self.blocks[idx].term[2])
            for nxt in (tgt, idx + 1):
                if nxt is not None and decision(nxt) is not None:
                    todo.append(nxt)
        if len(decisions) < 2:
            return None
        consts = sorted({v for c in decisions.values() for v in c[2]})

        tramp = set()            # bare `goto` blocks the tree dispatches through

        def leaf(v):
            """The address control reaches for value v, past any trampolines."""
            idx = k
            seen = set()
            while idx in decisions:
                if idx in seen:
                    return None
                seen.add(idx)
                _, op, cvals = decisions[idx]
                cval = cvals[0]
                take = {'==': v == cval, '!=': v != cval, '<': v < cval, '<=': v <= cval,
                        '>': v > cval, '>=': v >= cval, 'in': v in cvals,
                        'notin': v not in cvals}[op]
                idx = self.index.get(self.blocks[idx].term[2]) if take else idx + 1
                if idx is None:
                    return None
            while idx is not None and idx < len(self.blocks) and self._pure_goto(self.blocks[idx]):
                if idx in seen:
                    return None
                seen.add(idx)
                tramp.add(idx)
                tgt = self.blocks[idx].term[1]
                if tgt not in self.index:
                    return tgt
                idx = self.index[tgt]
            return self._addr(idx) if idx is not None and idx < len(self.blocks) else exit_addr

        far_lo, far_hi = leaf(consts[0] - 2), leaf(consts[-1] + 2)
        if far_lo is None or far_lo != far_hi:
            return None
        default = far_lo
        cases = {}
        for v in range(consts[0] - 1, consts[-1] + 2):
            lf = leaf(v)
            if lf is None:
                return None
            if lf != default:
                if v not in consts:
                    return None
                cases.setdefault(lf, []).append(v)
        if not cases:
            return None
        # every case leaf must lie inside this region
        leaf_idx = {}
        for lf in cases:
            idx = self.index.get(lf)
            if idx is None or idx <= k or idx >= hi:
                return None
            leaf_idx[lf] = idx
        # the join: the common target the case bodies jump to (or fall into)
        default_idx = self.index.get(default)
        if default == exit_addr:
            default_idx = None
        if default_idx is not None and (default_idx <= k or default_idx > hi):
            return None
        join = default
        join_idx = default_idx if default_idx is not None else hi
        if default_idx is not None and default_idx < max(leaf_idx.values()):
            return None                   # a default body between cases: give up
        bounds = sorted(set(leaf_idx.values()) | set(decisions) | tramp | {join_idx})
        # regions, in address order
        order = sorted(leaf_idx.items(), key=lambda kv: kv[1])
        body = []
        for n, (lf, idx) in enumerate(order):
            nxt = [x for x in bounds if x > idx]
            end = nxt[0] if nxt else join_idx
            if end > join_idx:
                return None
            for p in self.blocks[idx].preds:
                pi = self.index[p]
                if not (pi in decisions or pi in tramp or idx <= pi < end):
                    return None
            for blk in self.blocks[idx + 1:end]:
                for p in blk.preds:
                    if not (idx <= self.index[p] < end):
                        return None
            last = self.blocks[end - 1]
            falls = last.term is None or (last.term[0] in ('if', 'ifret'))
            if falls and end < join_idx and end not in leaf_idx.values():
                return None               # would fall into dispatch code
            nodes = self._region(idx, end, join)
            body.append((cases.get(lf), nodes, falls and end < join_idx))
        # the switch expression: a temporary assigned just before is inlined
        expr = var
        if b.body and b.body[-1].kind == 'assign' and b.body[-1].lhs.kind == 'var' \
                and b.body[-1].lhs.name == var.name and var.name.startswith('t_'):
            expr = b.body[-1].rhs
            b.body = b.body[:-1]
        return Node('switch', expr=expr, cases=body, brk=join, addr=b.addr), join_idx

    def _table_switch(self, k, hi, exit_addr):
        """Structure a jump-table switch at block k."""
        b = self.blocks[k]
        idx, targets = b.term[1], b.term[2]
        # the switch expression and the case numbers: idx is usually var - c
        expr, offset = idx, 0
        if idx.kind == 'binop' and idx.op == '+' and is_const(idx.b):
            expr, offset = idx.a, -idx.b.val
        if expr.kind == 'cast' and is_int(expr.ty) and is_int(expr.a.ty):
            expr = expr.a
        cases = {}
        for v, tgt in enumerate(targets):
            cases.setdefault(tgt, []).append(v + offset)
        # the join: where the cases go when they are done -- the region's
        # exit (the default, branched to before the table)
        join = exit_addr
        join_idx = hi
        leaf_idx = {}
        for lf in cases:
            ti = self.index.get(lf)
            if ti is None or ti <= k or ti >= hi:
                # a case that is the join itself: nothing to emit for it
                if lf == join:
                    continue
                self.used_labels.add(lf)
                continue
            leaf_idx[lf] = ti
        order = sorted(leaf_idx.items(), key=lambda kv: kv[1])
        bounds = sorted(leaf_idx.values()) + [join_idx]
        body = []
        for lf, ti in order:
            nxt = [x for x in bounds if x > ti]
            end = nxt[0]
            for blk in self.blocks[ti + 1:end]:
                for p in blk.preds:
                    if not (ti <= self.index[p] < end):
                        break
            last = self.blocks[end - 1]
            falls = last.term is None or (last.term[0] in ('if', 'ifret'))
            nodes = self._region(ti, end, join)
            body.append((cases[lf], nodes, falls and end < join_idx))
        # cases that jump straight to the join need no body
        for lf, vals in cases.items():
            if lf == join:
                body.append((vals, [], False))
        return Node('switch', expr=expr, cases=body, brk=join, addr=b.addr), join_idx

    def _region(self, lo, hi, exit_addr):
        """Structure blocks[lo:hi]; control leaves the region to exit_addr."""
        out = []
        if lo < hi and lo not in self.looping and self._backward_into(lo, hi):
            # a loop whose back edges come from inside nested structure:
            # for (;;) { ... continue ... break }
            self.looping.add(lo)
            head = self.blocks[lo].addr
            body = self._region(lo, hi, exit_addr)
            self.looping.discard(lo)
            return [Node('loop', body=body, cont=head, brk=exit_addr)]
        k = lo
        while k < hi:
            b = self.blocks[k]
            if k in self.consumed and lo not in self.consumed:
                k += 1
                continue
            out.append(Node('label', addr=b.addr))
            for st in b.body:
                out.append(Node('stmt', st=st))
            t = b.term
            if t is None:
                k += 1
                continue
            if t[0] == 'return':
                out.append(Node('return', expr=t[1]))
                k += 1
                continue
            if t[0] == 'ifret':
                out.append(Node('ifret', cond=t[1], expr=t[2]))
                k += 1
                continue
            if t[0] == 'goto_indirect':
                out.append(Node('goto_indirect', expr=t[1]))
                k += 1
                continue
            if t[0] == 'switch':
                node, nk = self._table_switch(k, hi, exit_addr)
                out.append(node)
                k = nk
                continue
            if t[0] == 'goto':
                k = self._handle_goto(k, t[1], lo, hi, exit_addr, out)
                continue
            if t[0] == 'if':
                cond, tgt = t[1], t[2]
                sw = self._try_switch(k, hi, exit_addr)
                if sw is not None:
                    node, nk = sw
                    # the block's own statements were emitted above; if the
                    # switch inlined the temp assignment, drop it again
                    while out and out[-1].kind == 'stmt' and out[-1].st not in b.body \
                            and out[-1].st.kind == 'assign' and out[-1].st.lhs.kind == 'var' \
                            and out[-1].st.lhs.name.startswith('t_'):
                        out.pop()
                    out.append(node)
                    k = nk
                    continue
                ti = self.index.get(tgt)
                if tgt == exit_addr and self._closed(k + 1, hi, k):
                    then = self._region(k + 1, hi, exit_addr)
                    out.append(Node('if', cond=self.L._not(cond), then=then, els=None))
                    k = hi
                    continue
                if ti is not None and k < ti < hi and not self._closed(k + 1, ti, k):
                    # the then-branch ends by jumping where the if goes --
                    # to a loop's test placed after the loop's body, say --
                    # and what lies between belongs to the loop, not the if
                    for ej in range(k + 1, ti):
                        bj = self.blocks[ej]
                        if bj.term and (bj.term[0] == 'return' or
                                        (bj.term[0] == 'goto' and bj.term[1] == tgt)):
                            if self._closed(k + 1, ej + 1, k):
                                then = self._region(k + 1, ej + 1, tgt)
                                out.append(Node('if', cond=self.L._not(cond), then=then, els=None))
                                k = self._handle_goto(ej, tgt, lo, hi, exit_addr, out)
                            break
                    else:
                        ej = None
                    if ej is not None and k > ej:
                        continue
                if ti is None or ti >= hi or not self._closed(k + 1, ti, k):
                    inl = self._inline_target(tgt, k)
                    if inl is not None:
                        out.append(Node('if', cond=cond, then=inl, els=None))
                    else:
                        out.append(Node('goto_if', cond=cond, target=tgt))
                        self.used_labels.add(tgt)
                    k += 1
                    continue
                if ti <= k:
                    if ti > lo and self._closed(ti, k + 1, ti - 1):
                        start = self._find_label_node(out, self.blocks[ti].addr)
                        if start is not None:
                            body = out[start:]
                            del out[start:]
                            after = self._addr(k + 1) if k + 1 < hi else exit_addr
                            out.append(Node('dowhile', cond=cond, body=body, addr=tgt,
                                            brk=after, cont=None))
                            k += 1
                            continue
                    out.append(Node('goto_if', cond=cond, target=tgt))
                    self.used_labels.add(tgt)
                    k += 1
                    continue
                # forward: if (!cond) { then } [else { else }]
                then_lo, then_hi = k + 1, ti
                last = self.blocks[then_hi - 1] if then_hi - 1 >= then_lo else None
                if last is not None and last.term and last.term[0] == 'goto':
                    E = last.term[1]
                    ei = self.index.get(E)
                    if E == exit_addr:
                        ei = hi
                    if ei is not None and ei > ti and ei <= hi and self._closed(ti, ei, k):
                        then = self._region(then_lo, then_hi, E)
                        els = self._region(ti, ei, E if ei < hi else exit_addr)
                        out.append(Node('if', cond=self.L._not(cond), then=then, els=els))
                        k = ei
                        continue
                    # both branches jump to E, which is not what follows the
                    # else branch (a loop's test, say): the else region runs
                    # to the block that makes the same jump, and that jump is
                    # then handled once, after the if/else
                    for ej in range(ti, min(hi, ei if ei is not None else hi)):
                        bj = self.blocks[ej]
                        if bj.term and bj.term[0] == 'goto' and bj.term[1] == E:
                            if self._closed(ti, ej + 1, k):
                                then = self._region(then_lo, then_hi, E)
                                els = self._region(ti, ej + 1, E)
                                out.append(Node('if', cond=self.L._not(cond), then=then, els=els))
                                k = self._handle_goto(ej, E, lo, hi, exit_addr, out)
                            break
                    else:
                        ej = None
                    if ej is not None and k > ej:
                        continue
                then = self._region(then_lo, then_hi, tgt)
                out.append(Node('if', cond=self.L._not(cond), then=then, els=None))
                k = ti
                continue
            k += 1
        return out

    def _inline_target(self, tgt, src):
        """A loop entered by one jump to its condition, written at the jump.

        GCC puts `if (x) while (c) body;` as a jump to the loop test, with the
        body before it and the test's fall-through after it. When that whole
        stretch is reached from nowhere else, it can be written where the
        jump is, as the loop followed by what comes after it.
        """
        ti = self.index.get(tgt)
        if ti is None:
            return None
        cb = self.blocks[ti]
        if not self._pure_cond(cb):
            return None
        bi = self.index.get(cb.term[2])
        if bi is None or bi >= ti or bi <= src:
            return None
        if not self._closed(bi, ti, ti):
            return None
        # the test itself must be entered only from the jump and the body
        for p in cb.preds:
            pi = self.index[p]
            if pi != src and not (bi <= pi < ti):
                return None
        # the tail: a straight run of blocks after the test, each entered
        # only from the one before, ending in a return or a jump
        tail = []
        after = None             # where the tail falls through to, if it does
        t = ti + 1
        while t < len(self.blocks):
            b = self.blocks[t]
            if b.preds != {self.blocks[t - 1].addr}:
                if tail and self.blocks[t - 1].term is None:
                    after = b.addr            # shared by others: stop here
                    break
                return None
            tail.append(t)
            if b.term is not None and b.term[0] in ('return', 'goto'):
                break
            if b.term is not None and b.term[0] in ('if', 'ifret', 'goto_indirect'):
                return None
            t += 1
        else:
            return None
        if not tail:
            return None
        if any(x in self.consumed for x in range(bi, tail[-1] + 1)):
            return None
        for x in range(bi, tail[-1] + 1):
            self.consumed.add(x)
        body = self._region(bi, ti, cb.addr)
        nodes = [Node('while', cond=cb.term[1], body=body, addr=cb.addr,
                      brk=self.blocks[tail[0]].addr, cont=cb.addr)]
        nodes.extend(self._region(tail[0], tail[-1] + 1, after))
        if after is not None:
            nodes.append(Node('goto', target=after))
            self.used_labels.add(after)
        return nodes

    def _handle_goto(self, k, tgt, lo, hi, exit_addr, out):
        """Block k ends with `goto tgt`; emit what that is and return the next k."""
        if tgt == exit_addr and k == hi - 1:
            return k + 1
        ti = self.index.get(tgt)
        if ti == k + 1:                          # a jump to the next block
            return k + 1
        if ti is not None and lo <= ti < hi and ti > k:
            cb = self.blocks[ti]
            body_start = self._addr(k + 1)
            if cb.term and cb.term[0] == 'if' and cb.term[2] == body_start and k + 1 <= ti \
                    and self._closed(k + 1, ti, ti):
                after = self._addr(ti + 1) if ti + 1 < hi else exit_addr
                body = self._region(k + 1, ti, cb.addr)
                if self._pure_cond(cb):
                    out.append(Node('while', cond=cb.term[1], body=body,
                                    addr=cb.addr, brk=after, cont=cb.addr))
                else:
                    pre = [Node('stmt', st=s) for s in cb.body]
                    out.append(Node('while_split', body=body, pre=pre,
                                    cond=cb.term[1], addr=cb.addr, brk=after,
                                    cont=cb.addr))
                return ti + 1
        inl = self._inline_target(tgt, k)
        if inl is not None:
            out.extend(inl)
            return k + 1
        if ti is not None and lo <= ti <= k and ti > lo and self._loop_closed(ti, k):
            start = self._find_label_node(out, tgt)
            if start is not None:
                body = out[start:] + [Node('goto', target=tgt)]     # renders as continue
                del out[start:]
                after = self._addr(k + 1) if k + 1 < hi else exit_addr
                out.append(Node('loop', body=body, cont=tgt, brk=after))
                return k + 1
        out.append(Node('goto', target=tgt))
        self.used_labels.add(tgt)
        return k + 1

    def _is_loop_head(self, addr):
        ti = self.index.get(addr)
        if ti is None:
            return False
        cb = self.blocks[ti]
        if cb.term and cb.term[0] == 'if':
            tj = self.index.get(cb.term[2])
            if tj is not None and tj < ti:
                return True
        return False

    def _targeted_from_outside(self, lo, hi, tlo, thi):
        for b in self.blocks[lo:hi]:
            for p in b.preds:
                pi = self.index[p]
                if not (tlo <= pi < hi) and not (pi == tlo - 1):
                    return True
        return False

    def _succs(self, k):
        b = self.blocks[k]
        t = b.term
        out = []
        if t is None:
            if k + 1 < len(self.blocks):
                out.append(k + 1)
        elif t[0] == 'goto':
            out.append(self.index.get(t[1]))
        elif t[0] == 'if':
            out.append(self.index.get(t[2]))
            if k + 1 < len(self.blocks):
                out.append(k + 1)
        elif t[0] == 'ifret':
            if k + 1 < len(self.blocks):
                out.append(k + 1)
        elif t[0] == 'switch':
            out.extend(self.index.get(x) for x in t[2])
        return [x for x in out if x is not None]

    def _natural_loop(self, head, tail):
        """Block indices of the loop through head: what head reaches that
        reaches head again (the strongly connected component)."""
        fwd = {head}
        stack = [head]
        while stack:
            for n in self._succs(stack.pop()):
                if n not in fwd:
                    fwd.add(n)
                    stack.append(n)
        bwd = {head}
        stack = [head]
        while stack:
            for p in self.blocks[stack.pop()].preds:
                pi = self.index.get(p)
                if pi is not None and pi not in bwd:
                    bwd.add(pi)
                    stack.append(pi)
        loop = fwd & bwd
        loop.add(tail)
        return loop

    def _loop_closed(self, head, tail):
        """Is the loop tail -> head entered only through its head, once?"""
        loop = self._natural_loop(head, tail)
        entries = set()
        for j in loop:
            for p in self.blocks[j].preds:
                pi = self.index.get(p)
                if pi is None or pi in loop:
                    continue
                if j != head:
                    return False
                entries.add(pi)
        return len(entries) == 1

    def _find_label_node(self, nodes, addr):
        for k, n in enumerate(nodes):
            if n.kind == 'label' and n.addr == addr:
                return k
            # a loop already built on that block: its node stands for the label
            if n.kind in ('while', 'while_split', 'dowhile') and n.addr == addr:
                return k
        return None


# ---------------------------------------------------------------------------
# output


class Writer(object):
    def __init__(self, lifter, nodes, lines=True):
        self.L = lifter
        self.R = Renderer(lifter)
        self.nodes = nodes
        self.lines = lines
        self.out = []
        self.loops = []          # (brk, cont)
        self.used = set()
        self.gotos = []

    def emit(self):
        L = self.L
        em = L.em
        # first pass: find which labels survive as goto targets
        self.out = []
        self.gotos = []
        self._nodes(self.nodes, 1)
        self.used = set(self.gotos)
        self.out = []
        self.gotos = []
        ps = ', '.join(em.declare(t, n) for n, t, _ in L.params) or 'void'
        rt = em.declare(L.rettype, '')
        head = '%s%s%s%s(%s)' % ('static ' if L.func.static else '', rt,
                                 '' if rt.endswith('*') else ' ', L.name, ps)
        line = self.L.line_of(self.L.start)
        self.out.append('/* %s:%s  (0x%x) */' % (self.L.unit.base, line if line else '?', self.L.start))
        self.out.append(head)
        self.out.append('{')
        decls = []
        for depth, v in L.func.locals:
            if v.kind == 'l':
                decls.append('    %s;' % em.declare(v.type, v.name))
        for off, (name, ty) in sorted(L.temps.items()):
            if off in L.materialise or off in L.unknown_reads or off < 0:
                decls.append('    %s;' % (em.declare(ty, name) if isinstance(ty, Type)
                                          else '%s %s' % (CNAME.get(ty, ty), name)))
        if decls:
            self.out.extend(decls)
            self.out.append('')
        self._nodes(self.nodes, 1)
        if self.out[-1].strip() == 'return;':
            self.out.pop()
        self.out.append('}')
        return '\n'.join(self.out)

    def _line(self, text, st=None):
        if self.lines and st is not None and st.line is not None:
            self.out.append('%-60s /* %d */' % (text, st.line) if len(text) < 60 else
                            '%s /* %d */' % (text, st.line))
        else:
            self.out.append(text)

    def _goto(self, pad, target):
        if self.loops:
            brk, cont = self.loops[-1]
            if target == brk:
                self.out.append('%sbreak;' % pad)
                return
            if target == cont:
                self.out.append('%scontinue;' % pad)
                return
        self.gotos.append(target)
        self.out.append('%sgoto L_%x;' % (pad, target))

    def _nodes(self, nodes, indent):
        pad = '    ' * indent
        R = self.R
        for n in nodes:
            k = n.kind
            if k == 'label':
                if n.addr in self.used:
                    self.out.append('L_%x:' % n.addr)
                continue
            if k == 'stmt':
                self._line(R.stmt(n.st, indent), n.st)
                continue
            if k == 'return':
                if n.expr is None:
                    self.out.append('%sreturn;' % pad)
                else:
                    self.out.append('%sreturn %s;' % (pad, R.render(n.expr, 0)))
                continue
            if k == 'ifret':
                r = 'return;' if n.expr is None else 'return %s;' % R.render(n.expr, 0)
                self.out.append('%sif (%s) {' % (pad, R.render(n.cond, 0)))
                self.out.append('%s    %s' % (pad, r))
                self.out.append('%s}' % pad)
                continue
            if k == 'goto':
                self._goto(pad, n.target)
                continue
            if k == 'goto_if':
                self.out.append('%sif (%s) {' % (pad, R.render(n.cond, 0)))
                self._goto(pad + '    ', n.target)
                self.out.append('%s}' % pad)
                continue
            if k == 'goto_indirect':
                self.out.append('%s/* indirect jump */' % pad)
                continue
            if k == 'if':
                if n.els and not [x for x in n.then if x.kind != 'label']:
                    n.cond, n.then, n.els = self.L._not(n.cond), n.els, None
                if not n.els and not [x for x in n.then if x.kind != 'label']:
                    continue                          # an empty if
                self.out.append('%sif (%s) {' % (pad, R.render(n.cond, 0)))
                self._nodes(n.then, indent + 1)
                if n.els:
                    real = [x for x in n.els if x.kind != 'label']
                    if len(real) == 1 and real[0].kind == 'if':
                        self.out.append('%s} else' % pad)
                        self._else_if(real[0], indent)
                    else:
                        self.out.append('%s} else {' % pad)
                        self._nodes(n.els, indent + 1)
                        self.out.append('%s}' % pad)
                else:
                    self.out.append('%s}' % pad)
                continue
            if k == 'while':
                fl = self._for_parts(nodes, n)
                if fl is not None:
                    init, incr, body, cont = fl
                    self.out.pop()
                    self.loops.append((n.brk, cont))
                    self.out.append('%sfor (%s %s; %s) {' % (
                        pad, R.stmt(init, 0), R.render(n.cond, 0), R.stmt(incr, 0).rstrip(';')))
                    self._nodes(body, indent + 1)
                else:
                    self.loops.append((n.brk, n.cont))
                    self.out.append('%swhile (%s) {' % (pad, R.render(n.cond, 0)))
                    self._nodes(n.body, indent + 1)
                self.loops.pop()
                self.out.append('%s}' % pad)
                continue
            if k == 'while_split':
                # `while (stmt, cond) body`: the loop is entered at the test,
                # so the test's statements run before the first iteration
                self.loops.append((n.brk, n.cont))
                self.out.append('%sfor (;;) {' % pad)
                self._nodes(n.pre, indent + 1)
                self.out.append('%s    if (%s) break;' % (pad, R.render(self.L._not(n.cond), 0)))
                self._nodes(n.body, indent + 1)
                self.loops.pop()
                self.out.append('%s}' % pad)
                continue
            if k == 'switch':
                cont = self.loops[-1][1] if self.loops else None
                self.loops.append((n.brk, cont))
                self.out.append('%sswitch (%s) {' % (pad, R.render(n.expr, 0)))
                for values, body, falls in n.cases:
                    if values is None:
                        self.out.append('%sdefault:' % pad)
                    else:
                        for v in values:
                            self.out.append('%scase %d:' % (pad, v))
                    self._nodes(body, indent + 1)
                    if not falls and not self.out[-1].strip().startswith(
                            ('return', 'break', 'continue', 'goto')):
                        self.out.append('%s    break;' % pad)
                self.loops.pop()
                self.out.append('%s}' % pad)
                continue
            if k == 'loop':
                self.loops.append((n.brk, n.cont))
                self.out.append('%sfor (;;) {' % pad)
                self._nodes(n.body, indent + 1)
                if not self.out[-1].strip().startswith(('return', 'break', 'continue', 'goto')):
                    self.out.append('%s    break;' % pad)
                self.loops.pop()
                self.out.append('%s}' % pad)
                continue
            if k == 'dowhile':
                self.loops.append((n.brk, n.cont))
                self.out.append('%sdo {' % pad)
                self._nodes(n.body, indent + 1)
                self.loops.pop()
                self.out.append('%s} while (%s);' % (pad, R.render(n.cond, 0)))
                continue
            self.out.append('%s/* node %s */' % (pad, k))

    def _for_parts(self, nodes, n):
        """Recognise `for (init; cond; incr)` from the line numbers."""
        k = nodes.index(n)
        prev = None
        for j in range(k - 1, -1, -1):
            if nodes[j].kind == 'label':
                continue
            prev = nodes[j]
            break
        if prev is None or prev.kind != 'stmt' or prev.st.kind != 'assign':
            return None
        body = list(n.body)
        last = None
        last_i = None
        for i in range(len(body) - 1, -1, -1):
            if body[i].kind == 'label':
                continue
            last, last_i = body[i], i
            break
        if last is None or last.kind != 'stmt' or last.st.kind != 'assign':
            return None
        line = self.L.line_of(n.addr)
        if line is None or prev.st.line != line or last.st.line != line:
            return None
        inner = [x for x in body if x.kind == 'stmt' and x is not last]
        if inner and min(x.st.line or line for x in inner) < line:
            return None
        cont = n.cont
        if last_i > 0 and body[last_i - 1].kind == 'label':
            cont = body[last_i - 1].addr
        del body[last_i]
        return prev.st, last.st, body, cont

    def _else_if(self, n, indent):
        pad = '    ' * indent
        R = self.R
        if n.els and not [x for x in n.then if x.kind != 'label']:
            n.cond, n.then, n.els = self.L._not(n.cond), n.els, None
        self.out[-1] += ' if (%s) {' % R.render(n.cond, 0)
        self._nodes(n.then, indent + 1)
        if n.els:
            real = [x for x in n.els if x.kind != 'label']
            if len(real) == 1 and real[0].kind == 'if':
                self.out.append('%s} else' % pad)
                self._else_if(real[0], indent)
            else:
                self.out.append('%s} else {' % pad)
                self._nodes(n.els, indent + 1)
                self.out.append('%s}' % pad)
        else:
            self.out.append('%s}' % pad)


def lift_function(binary, name, lines=True, verbose=False, unit=None):
    L = Lifter(binary, name, verbose, unit)
    stmts = L.lift()
    S = Structurer(L, stmts)
    nodes = S.structure()
    text = Writer(L, nodes, lines).emit()
    return text, L.warnings


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 2
    lines = '--no-lines' not in a
    a = [x for x in a if x != '--no-lines']
    b = Binary()
    unit = None
    if a[0] == '--unit':
        u = [x for x in b.units if x.base == a[1]][0]
        names = [f.name for f in u.funcs]
        unit = u.base
    else:
        names = a
    for n in names:
        try:
            text, warns = lift_function(b, n, lines, unit=unit)
        except Exception as e:
            import traceback
            traceback.print_exc()
            print('/* FAILED %s: %s */' % (n, e))
            continue
        for w in sorted(set(warns)):
            print('/* WARNING: %s */' % w)
        print(text)
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
