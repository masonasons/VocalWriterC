#!/usr/bin/env python3
"""STABS debug-info reader for the VocalWriter PowerPC binary.

The shipped executable is a debug build. Besides function names, its symbol
table carries the complete STABS type graph -- every struct with its field
names, offsets and sizes -- plus the stack offset, register or address of every
parameter, local and global, grouped by source file. This module parses that
into Python objects and can emit C declarations that reproduce the original
layouts exactly.

    python tools/stabs.py units                 # the compilation units
    python tools/stabs.py header Speech.c       # C header with all types
    python tools/stabs.py func SayFrame         # params/locals of a function
    python tools/stabs.py funcs Speech.c        # prototypes of every function
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_BINARY = os.path.join(os.path.dirname(ROOT), 'VocalWriter', 'assets',
                              'VocalWriter.app', 'Contents', 'MacOS',
                              'VocalWriter')

N_GSYM, N_FUN, N_STSYM, N_LCSYM, N_RSYM = 0x20, 0x24, 0x26, 0x28, 0x40
N_SLINE, N_SO, N_SOL, N_LSYM, N_PSYM = 0x44, 0x64, 0x84, 0x80, 0xa0
N_LBRAC, N_RBRAC = 0xc0, 0xe0


def load_symbols(path=DEFAULT_BINARY):
    with open(path, 'rb') as fh:
        blob = fh.read()
    ncmds = struct.unpack('>I', blob[16:20])[0]
    off, sym = 28, None
    for _ in range(ncmds):
        cmd, size = struct.unpack('>2I', blob[off:off + 8])
        if cmd == 2:
            sym = struct.unpack('>4I', blob[off + 8:off + 24])
        off += size
    symoff, nsyms, stroff, strsize = sym
    strs = blob[stroff:stroff + strsize]
    out = []
    for i in range(nsyms):
        p = symoff + i * 12
        strx, ntype, nsect, ndesc, value = struct.unpack('>IBBhI', blob[p:p + 12])
        end = strs.find(b'\0', strx)
        name = strs[strx:end].decode('latin-1') if strx < strsize else ''
        out.append((name, ntype, nsect, ndesc, value))
    return blob, out


# ---------------------------------------------------------------------------
# type graph


class Type(object):
    """One node of the STABS type graph.

    kind: 'void' 'int' 'float' 'ptr' 'array' 'struct' 'union' 'enum' 'func'
          'typedef' 'const' 'volatile' 'xref' 'ref'(unresolved forward)
    """

    def __init__(self, num, kind, **kw):
        self.num = num
        self.kind = kind
        self.name = kw.get('name')
        self.size = kw.get('size')          # bytes, where known
        self.target = kw.get('target')      # ptr/array/func/typedef
        self.low = kw.get('low')
        self.high = kw.get('high')
        self.fields = kw.get('fields')      # struct: [(name, Type, bitoff, bitsize)]
        self.values = kw.get('values')      # enum: [(name, value)]
        self.signed = kw.get('signed', True)
        self.tname = None                   # typedef name, if one was given

    def __repr__(self):
        return 'Type(%r,%s,%r)' % (self.num, self.kind, self.name)


class TypeTable(object):
    def __init__(self):
        self.types = {}      # (file, n) -> Type
        self.names = {}      # 'tag:Name' / 'typedef:Name' -> Type
        self.order = []      # named definitions, in order of appearance

    def get(self, num):
        t = self.types.get(num)
        if t is None:
            t = Type(num, 'ref')
            self.types[num] = t
        return t

    def builtin(self, n):
        """GCC's predefined negative type numbers."""
        key = (-1, n)
        if key in self.types:
            return self.types[key]
        name, kind, size, signed = _BUILTINS.get(
            n, ('int', 'int', 4, True))
        t = Type(key, kind, name=name, size=size, signed=signed)
        self.types[key] = t
        return t


_BUILTINS = {
    -1: ('int', 'int', 4, True), -2: ('char', 'int', 1, True),
    -3: ('short', 'int', 2, True), -4: ('long', 'int', 4, True),
    -5: ('unsigned char', 'int', 1, False), -6: ('signed char', 'int', 1, True),
    -7: ('unsigned short', 'int', 2, False), -8: ('unsigned int', 'int', 4, False),
    -9: ('unsigned long', 'int', 4, False), -10: ('unsigned long long', 'int', 8, False),
    -11: ('void', 'void', 0, True), -12: ('float', 'float', 4, True),
    -13: ('double', 'float', 8, True), -14: ('long double', 'float', 8, True),
    -15: ('int', 'int', 4, True), -16: ('Boolean', 'int', 4, False),
    -31: ('long long', 'int', 8, True), -32: ('unsigned long long', 'int', 8, False),
}

_FUNDAMENTAL_NAMES = {
    'int': ('int', 4, True), 'char': ('int', 1, True),
    'short int': ('int', 2, True), 'long int': ('int', 4, True),
    'unsigned int': ('int', 4, False), 'long unsigned int': ('int', 4, False),
    'short unsigned int': ('int', 2, False), 'unsigned char': ('int', 1, False),
    'signed char': ('int', 1, True), 'float': ('float', 4, True),
    'double': ('float', 8, True), 'long double': ('float', 8, True),
    'long long int': ('int', 8, True), 'long long unsigned int': ('int', 8, False),
    'void': ('void', 0, True),
}

_NUM = re.compile(r'-?\d+')
_TNUM = re.compile(r'\((\d+),(\d+)\)')


class Parser(object):
    """Recursive-descent parser for one STABS type string."""

    def __init__(self, table, s):
        self.tt = table
        self.s = s
        self.i = 0

    def peek(self, n=1):
        return self.s[self.i:self.i + n]

    def take(self, n=1):
        v = self.s[self.i:self.i + n]
        self.i += n
        return v

    def expect(self, ch):
        if self.peek(len(ch)) != ch:
            raise ValueError('expected %r at %d in %r' % (ch, self.i, self.s))
        self.i += len(ch)

    def number(self):
        m = _NUM.match(self.s, self.i)
        if not m:
            raise ValueError('number expected at %d in %r' % (self.i, self.s))
        self.i = m.end()
        v = m.group()
        neg = v.startswith('-')
        d = v.lstrip('-')
        n = int(d, 8) if len(d) > 1 and d.startswith('0') else int(d)
        return -n if neg else n

    def typenum(self):
        m = _TNUM.match(self.s, self.i)
        if m:
            self.i = m.end()
            return (int(m.group(1)), int(m.group(2)))
        n = self.number()
        if n < 0:
            self.tt.builtin(n)
            return (-1, n)
        return (0, n)

    def ident(self, stop=':'):
        j = self.s.index(stop, self.i)
        v = self.s[self.i:j]
        self.i = j + 1
        return v

    # -- type references and definitions ------------------------------------

    def type(self):
        """Parse a type reference, possibly with an inline definition."""
        num = self.typenum()
        if self.peek() == '=':
            self.take()
            return self.definition(num)
        return self.tt.get(num)

    def definition(self, num):
        c = self.peek()
        if c == '(' or c.isdigit():
            # (0,5)=(0,7): plain alias of another type; (0,1)=(0,1) is void
            j = self.i
            tnum = self.typenum()
            self.i = j
            if tnum == num:
                self.typenum()
                return self._install(num, Type(num, 'void', size=0))
            target = self.type()
            return self._install(num, Type(num, 'typedef', target=target))
        self.take()
        if c == 'r':
            base = self.type()
            self.expect(';')
            low = self.number()
            self.expect(';')
            high = self.number()
            self.expect(';')
            if num == base.num:
                # self-referential: a fundamental type
                if low == 0 and high == 0:
                    kind, size = 'void', 0
                elif high == 0 and low != 0:
                    # floats are written with the size in `low`
                    kind, size = 'float', abs(low)
                else:
                    kind = 'int'
                    if high in (127, 255):
                        size = 1
                    elif high in (32767, 65535):
                        size = 2
                    else:
                        size = 4
                return self._install(num, Type(num, kind, size=size,
                                               low=low, high=high,
                                               signed=(low < 0)))
            # a subrange of another type
            t = Type(num, 'int', low=low, high=high, signed=(low < 0))
            if high in (127, 255):
                t.size = 1
            elif high in (32767, 65535):
                t.size = 2
            elif high == 0 and low != 0:
                t.kind, t.size = 'float', abs(low)
            else:
                t.size = 4
            return self._install(num, t)
        if c == '*':
            return self._install(num, Type(num, 'ptr', target=self.type(),
                                           size=4))
        if c == 'k':
            return self._install(num, Type(num, 'const', target=self.type()))
        if c == 'B':
            return self._install(num, Type(num, 'volatile', target=self.type()))
        if c == 'f':
            return self._install(num, Type(num, 'func', target=self.type()))
        if c == '@':
            # attribute: @s<bits>;<type>
            self.expect('s')
            bits = self.number()
            self.expect(';')
            if self.peek() in '(-' or self.peek().isdigit():
                inner = self.type()
            else:
                inner = self.definition(num)
            t = Type(num, inner.kind, size=bits // 8, low=inner.low,
                     high=inner.high, signed=inner.signed, target=inner.target,
                     fields=inner.fields, values=inner.values)
            return self._install(num, t)
        if c == 'a':
            self.expect('r')
            self.type()                     # index type
            self.expect(';')
            low = self.number()
            self.expect(';')
            high = self.number()
            self.expect(';')
            elem = self.type()
            return self._install(num, Type(num, 'array', target=elem,
                                           low=low, high=high))
        if c in 'su':
            size = self.number()
            fields = []
            while self.peek() != ';':
                fname = self.ident(':')
                ftype = self.type()
                self.expect(',')
                bitoff = self.number()
                self.expect(',')
                bitsize = self.number()
                self.expect(';')
                fields.append((fname, ftype, bitoff, bitsize))
            self.expect(';')
            return self._install(num, Type(num, 'struct' if c == 's' else 'union',
                                           size=size, fields=fields))
        if c == 'e':
            values = []
            while self.peek() != ';':
                vname = self.ident(':')
                v = self.number()
                self.expect(',')
                values.append((vname, v))
            self.expect(';')
            return self._install(num, Type(num, 'enum', values=values, size=4))
        if c == 'x':
            k = self.take()
            name = self.ident(':')
            return self._install(num, Type(num, 'xref', name=name,
                                           target={'s': 'struct', 'u': 'union',
                                                   'e': 'enum'}[k]))
        raise ValueError('unknown type code %r at %d in %r' % (c, self.i, self.s))

    def _install(self, num, t):
        old = self.tt.types.get(num)
        if old is not None and old.kind in ('ref', 'xref'):
            # fill in the forward reference in place so pointers to it resolve
            old.__dict__.update(t.__dict__)
            old.num = num
            return old
        self.tt.types[num] = t
        return t


# ---------------------------------------------------------------------------
# compilation units


class Var(object):
    def __init__(self, name, kind, type_, loc):
        self.name, self.kind, self.type, self.loc = name, kind, type_, loc

    def __repr__(self):
        return 'Var(%s %s @%s)' % (self.kind, self.name, self.loc)


class Func(object):
    def __init__(self, name, addr, rettype, unit):
        self.name, self.addr, self.rettype, self.unit = name, addr, rettype, unit
        self.params = []
        self.locals = []       # (block depth, Var)
        self.end = None
        self.static = False

    def __repr__(self):
        return 'Func(%s @0x%x)' % (self.name, self.addr)


class Unit(object):
    def __init__(self, path):
        self.path = path
        self.base = os.path.basename(path)
        self.types = TypeTable()
        self.funcs = []
        self.globals = []
        self.statics = []


def parse_units(syms):
    units = []
    unit = None
    func = None
    depth = 0
    pending_dir = ''
    for name, ntype, nsect, ndesc, value in syms:
        if ntype == N_SO:
            if not name:
                continue
            if name.endswith('/'):
                pending_dir = name
                continue
            unit = Unit(name if name.startswith('/') else pending_dir + name)
            units.append(unit)
            func = None
            depth = 0
            continue
        if unit is None:
            continue
        if ntype == N_FUN:
            if not name:
                if func is not None:
                    func.end = func.addr + value
                    func = None
                continue
            fname, rest = name.split(':', 1)
            p = Parser(unit.types, rest[1:])
            rt = p.type()
            func = Func(fname, value, rt, unit)
            func.static = rest[0] == 'f'
            unit.funcs.append(func)
            depth = 0
            continue
        if ntype == N_LBRAC:
            depth += 1
            continue
        if ntype == N_RBRAC:
            depth -= 1
            continue
        if ntype in (N_LSYM, N_PSYM, N_RSYM, N_GSYM, N_STSYM, N_LCSYM):
            if ':' not in name:
                continue
            vname, rest = name.split(':', 1)
            if not rest:
                continue
            desc = rest[0]
            if desc in 'tT':
                p = Parser(unit.types, rest[1:])
                try:
                    t = p.type()
                except ValueError as e:
                    sys.stderr.write('type parse error: %s\n' % e)
                    continue
                key = ('tag' if desc == 'T' else 'typedef') + ':' + vname
                if desc == 'T':
                    t.name = vname
                elif t.kind == 'ref' and vname in _FUNDAMENTAL_NAMES:
                    k, sz, sg = _FUNDAMENTAL_NAMES[vname]
                    t.kind, t.size, t.signed, t.name = k, sz, sg, vname
                elif t.kind in ('int', 'float', 'void'):
                    if t.name is None:
                        t.name = vname
                    elif t.name != vname and t.tname is None:
                        t.tname = vname
                else:
                    if t.tname is None:
                        t.tname = vname
                if key not in unit.types.names:
                    unit.types.names[key] = t
                    unit.types.order.append((key, t))
                continue
            if desc in 'pPrSVG':
                p = Parser(unit.types, rest[1:])
                kind = desc
            else:
                p = Parser(unit.types, rest)
                kind = 'l'
            try:
                t = p.type()
            except ValueError as e:
                sys.stderr.write('var parse error: %s\n' % e)
                continue
            v = Var(vname, kind, t, value)
            if desc == 'G':
                unit.globals.append(v)
            elif desc in 'SV':
                if func is None:
                    unit.statics.append(v)
                else:
                    func.locals.append((depth, v))
            elif func is not None:
                if desc in 'pP':
                    func.params.append(v)
                else:
                    func.locals.append((depth, v))
    return units


# ---------------------------------------------------------------------------
# C emission

_INT_NAMES = {
    (1, True): 'int8_t', (1, False): 'uint8_t',
    (2, True): 'int16_t', (2, False): 'uint16_t',
    (4, True): 'int32_t', (4, False): 'uint32_t',
    (8, True): 'int64_t', (8, False): 'uint64_t',
}


def resolve(t):
    """Strip typedef/const/volatile wrappers."""
    seen = set()
    while t.kind in ('typedef', 'const', 'volatile') and id(t) not in seen:
        seen.add(id(t))
        t = t.target
    return t


def sizeof(t):
    t = resolve(t)
    if t.kind in ('int', 'float', 'struct', 'union', 'enum', 'ptr'):
        return t.size
    if t.kind == 'array':
        e = sizeof(t.target)
        return None if e is None else (t.high - t.low + 1) * e
    if t.kind == 'void':
        return 0
    return None


class CEmitter(object):
    def __init__(self, unit):
        self.unit = unit
        self.tt = unit.types
        self.expand = False       # set to ignore typedef names

    def base_name(self, t):
        """The C spelling of a type, for use before a declarator."""
        if t.tname and not self.expand:
            return t.tname
        if t.kind == 'typedef':
            return self.base_name(t.target)
        if t.kind in ('const', 'volatile'):
            inner = self.base_name(t.target)
            return (t.kind + ' ' + inner) if inner else None
        if t.kind == 'void':
            return 'void'
        if t.kind == 'int':
            if t.name in ('char', 'unsigned char', 'signed char'):
                return t.name
            return _INT_NAMES.get((t.size, t.signed), 'int')
        if t.kind == 'float':
            return 'float' if t.size == 4 else 'double'
        if t.kind in ('struct', 'union', 'enum'):
            if t.name:
                return '%s %s' % (t.kind, t.name)
            return '%s anon_%d_%d' % (t.kind, t.num[0], t.num[1])
        if t.kind == 'xref':
            return '%s %s' % (t.target, t.name)
        if t.kind == 'ref':
            return 'void /*unresolved (%d,%d)*/' % t.num
        return None

    def declare(self, t, name):
        """A C declaration of `name` with type `t`."""
        base = self.base_name(t)
        if base is not None:
            return ('%s %s' % (base, name)) if name else base
        if t.kind == 'ptr':
            tg = t.target
            if resolve(tg).kind == 'func' and tg.kind != 'typedef':
                f = resolve(tg)
                return self.declare(f.target, '(*%s)()' % name)
            return self.declare(tg, '*%s' % name)
        if t.kind == 'array':
            n = t.high - t.low + 1
            return self.declare(t.target, '%s[%d]' % (name, n))
        if t.kind == 'func':
            return self.declare(t.target, '%s()' % name)
        return 'void /*?%s*/ %s' % (t.kind, name)

    def struct_body(self, t, indent='    '):
        lines = []
        for fname, ftype, bitoff, bitsize in t.fields:
            decl = self.declare(ftype, fname)
            sz = sizeof(ftype)
            if (sz is not None and bitsize != sz * 8
                    and resolve(ftype).kind in ('int', 'enum')):
                decl += ' : %d' % bitsize
            lines.append('%s%s;  /* +0x%x */' % (indent, decl, bitoff // 8))
        return '\n'.join(lines)

    def header(self, only=None):
        out = []
        done = set()
        for key, t in self.tt.order:
            if only and not only(key, t):
                continue
            kind, name = key.split(':', 1)
            if kind == 'tag':
                if id(t) in done:
                    continue
                done.add(id(t))
                if t.kind in ('struct', 'union'):
                    out.append('%s %s {  /* %d bytes */\n%s\n};' % (
                        t.kind, name, t.size, self.struct_body(t)))
                elif t.kind == 'enum':
                    out.append('enum %s {\n%s\n};' % (name, ',\n'.join(
                        '    %s = %d' % v for v in t.values)))
            else:
                if t.kind in ('int', 'float', 'void') and t.name == name:
                    continue         # fundamental
                if t.tname != name:
                    continue         # a second name for the same number
                target = t.target if t.kind == 'typedef' else t
                if target is None:
                    continue
                save, t.tname = t.tname, None
                out.append('typedef %s;' % self.declare(target, name))
                t.tname = save
        return '\n\n'.join(out)


def find_unit(units, name):
    for u in units:
        if u.base == name or u.path == name:
            return u
    raise KeyError(name)


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 2
    blob, syms = load_symbols()
    units = parse_units(syms)
    cmd = a[0]
    if cmd == 'units':
        for u in units:
            print('%-70s %3d funcs %3d types' % (u.path, len(u.funcs),
                                                len(u.types.order)))
    elif cmd in ('types', 'header'):
        u = find_unit(units, a[1])
        print(CEmitter(u).header())
    elif cmd == 'func':
        for u in units:
            for f in u.funcs:
                if f.name == a[1]:
                    em = CEmitter(u)
                    print('%s  0x%x..%s  in %s' % (
                        f.name, f.addr, hex(f.end) if f.end else '?', u.base))
                    print('  returns', em.declare(f.rettype, ''))
                    for p in f.params:
                        print('  param %-24s %-30s stack+0x%x' % (
                            p.name, em.declare(p.type, ''), p.loc))
                    for d, v in f.locals:
                        where = ('r%d' % v.loc if v.kind == 'r' else
                                 'stack+0x%x' % v.loc if v.kind == 'l' else
                                 'static 0x%x' % v.loc)
                        print('  %slocal %-24s %-30s %s' % (
                            '  ' * d, v.name, em.declare(v.type, ''), where))
    elif cmd == 'funcs':
        u = find_unit(units, a[1])
        em = CEmitter(u)
        for f in u.funcs:
            ps = ', '.join(em.declare(p.type, p.name) for p in f.params)
            print('0x%06x %s%s%s(%s)' % (f.addr, 'static ' if f.static else '',
                                         em.declare(f.rettype, ''), f.name, ps))
    elif cmd == 'globals':
        u = find_unit(units, a[1])
        em = CEmitter(u)
        for v in u.globals + u.statics:
            print('%s %s  @0x%x' % (v.kind, em.declare(v.type, v.name), v.loc))
    return 0


if __name__ == '__main__':
    sys.exit(main())
