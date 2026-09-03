#!/usr/bin/env python3
"""The VocalWriter Mach-O, read for lifting: sections, symbols, stubs, literals.

Everything the lifter needs to turn an address into a name or a value:

  * sections, to classify an address (code, literal pool, non-lazy pointer,
    data, C string);
  * the non-STABS symbol table, to name globals by address;
  * the indirect symbol table, to name the dyld stubs the code calls
    (`pow`, `floor`, `NewPtr`, ...);
  * the STABS line records, to tag each instruction with its source line.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stabs import (DEFAULT_BINARY, N_SLINE, N_SO, N_FUN,      # noqa: E402
                   load_symbols, parse_units)

LC_SEGMENT, LC_SYMTAB, LC_DYSYMTAB = 1, 2, 0xb


class Binary(object):
    def __init__(self, path=DEFAULT_BINARY):
        self.blob, self.syms = load_symbols(path)
        self.units = parse_units(self.syms)
        self._parse_commands()
        self._index_symbols()
        self._lines()

    # -- load commands ------------------------------------------------------

    def _parse_commands(self):
        blob = self.blob
        ncmds = struct.unpack('>I', blob[16:20])[0]
        off = 28
        self.sections = []       # (segname, sectname, addr, size, fileoff, reserved1)
        self.indirect = []
        for _ in range(ncmds):
            cmd, size = struct.unpack('>2I', blob[off:off + 8])
            if cmd == LC_SEGMENT:
                nsects = struct.unpack('>I', blob[off + 48:off + 52])[0]
                so = off + 56
                for _k in range(nsects):
                    sn = blob[so:so + 16].split(b'\0')[0].decode()
                    seg = blob[so + 16:so + 32].split(b'\0')[0].decode()
                    addr, sz, fo = struct.unpack('>3I', blob[so + 32:so + 44])
                    reserved1 = struct.unpack('>I', blob[so + 60:so + 64])[0]
                    self.sections.append((seg, sn, addr, sz, fo, reserved1))
                    so += 68
            elif cmd == LC_DYSYMTAB:
                indirectsymoff, nindirect = struct.unpack(
                    '>2I', blob[off + 56:off + 64])
                self.indirect = list(struct.unpack(
                    '>%dI' % nindirect,
                    blob[indirectsymoff:indirectsymoff + 4 * nindirect]))
            off += size

    def section_of(self, addr):
        for s in self.sections:
            if s[2] <= addr < s[2] + s[3]:
                return s
        return None

    def addr_to_off(self, addr):
        s = self.section_of(addr)
        if s is None or s[4] == 0 and s[1] == '__common':
            return None
        return s[4] + (addr - s[2])

    def read(self, addr, n):
        o = self.addr_to_off(addr)
        return None if o is None else self.blob[o:o + n]

    def u32(self, addr):
        b = self.read(addr, 4)
        return None if b is None or len(b) < 4 else struct.unpack('>I', b)[0]

    def f32(self, addr):
        b = self.read(addr, 4)
        return None if b is None else struct.unpack('>f', b)[0]

    def f64(self, addr):
        b = self.read(addr, 8)
        return None if b is None else struct.unpack('>d', b)[0]

    def cstring(self, addr):
        o = self.addr_to_off(addr)
        if o is None:
            return None
        e = self.blob.find(b'\0', o)
        return self.blob[o:e].decode('mac-roman', 'replace')

    # -- symbols ------------------------------------------------------------

    def _index_symbols(self):
        self.by_addr = {}        # address -> linker name (leading _ stripped)
        self.by_name = {}
        self.funcs = {}          # STABS function name -> address
        self.func_by_addr = {}
        self.sym_by_index = []
        for i, (nm, nt, ns, nd, v) in enumerate(self.syms):
            self.sym_by_index.append(nm)
            if nt & 0xe0:
                if nt == N_FUN and ':' in nm and v:
                    n = nm.split(':', 1)[0]
                    self.funcs[n] = v
                    self.func_by_addr.setdefault(v, n)
                continue
            if (nt & 0x0e) == 0x0e and v:
                name = nm[1:] if nm.startswith('_') else nm
                self.by_addr.setdefault(v, name)
                self.by_name.setdefault(name, v)
        # stubs: __picsymbolstub1 and __symbol_stub1 entries, named through
        # the lazy pointer each one loads
        self.stubs = {}
        self._name_stubs()

    def _name_stubs(self):
        la = [s for s in self.sections if s[1] == '__la_symbol_ptr']
        if not la:
            return
        la = la[0]
        for s in self.sections:
            if s[1] not in ('__picsymbolstub1', '__symbol_stub1'):
                continue
            size = 32 if s[1] == '__picsymbolstub1' else 16
            for k in range(s[3] // size):
                addr = s[2] + k * size
                ptr = self._stub_pointer(addr, size)
                if ptr is None:
                    continue
                idx = (ptr - la[2]) // 4
                if 0 <= idx < la[3] // 4:
                    symi = self.indirect[la[5] + idx]
                    if symi < len(self.sym_by_index):
                        nm = self.sym_by_index[symi]
                        self.stubs[addr] = nm[1:] if nm.startswith('_') else nm

    def _stub_pointer(self, addr, size):
        """The lazy-pointer address a stub loads through."""
        words = [self.u32(addr + 4 * i) for i in range(size // 4)]
        if size == 32:
            # mflr r0; bcl; mflr r11; addis r11,r11,ha; mtlr r0; lwzu r12,lo(r11)
            base = addr + 8
            ha = words[3] & 0xFFFF
            lo = words[5] & 0xFFFF
            if ha & 0x8000:
                ha -= 0x10000
            if lo & 0x8000:
                lo -= 0x10000
            return base + (ha << 16) + lo
        # lis r11,ha; lwz r12,lo(r11)
        ha = words[0] & 0xFFFF
        lo = words[1] & 0xFFFF
        if lo & 0x8000:
            lo -= 0x10000
        return (ha << 16) + lo

    def nl_pointer(self, addr):
        """A non-lazy symbol pointer: returns (target address, name)."""
        s = self.section_of(addr)
        if s is None or s[1] != '__nl_symbol_ptr':
            return None
        target = self.u32(addr)
        return target, self.by_addr.get(target)

    # -- line numbers -------------------------------------------------------

    def _lines(self):
        self.lines = {}          # address -> line
        cur = None
        for nm, nt, ns, nd, v in self.syms:
            if nt == N_SO:
                cur = nm
            elif nt == N_SLINE:
                self.lines[v] = nd

    def unit_of(self, name):
        for u in self.units:
            for f in u.funcs:
                if f.name == name:
                    return u, f
        return None, None

    def extent(self, name):
        start = self.funcs[name]
        later = sorted(a for a in self.func_by_addr if a > start)
        end = later[0] if later else start + 0x4000
        text = [s for s in self.sections if s[1] == '__text'][0]
        return start, min(end, text[2] + text[3])


if __name__ == '__main__':
    b = Binary()
    print('%d stubs' % len(b.stubs))
    for a in sorted(b.stubs):
        print('  %08x %s' % (a, b.stubs[a]))
