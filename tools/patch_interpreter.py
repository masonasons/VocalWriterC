"""Give the reference interpreter (both cores) the XER carry the sequencer's
code needs: srawi/addze signed division, and the carry-chained adds."""
import os
import re

VW = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), '..', 'VocalWriter')
VW = os.path.abspath(VW)


def patch(path, edits):
    s = open(path).read()
    for a, b in edits:
        assert s.count(a) == 1, (path, a[:60])
        s = s.replace(a, b)
    open(path, 'w', newline='\n').write(s)


# -- core.c ------------------------------------------------------------------
patch(os.path.join(VW, 'ppc', 'core.c'), [
    ("""    uint32_t lr, ctr, xer, pc;
    uint64_t steps;""",
     """    uint32_t lr, ctr, xer, pc;
    uint32_t ca;                /* XER[CA] */
    uint64_t steps;"""),
    ("""        case 8: { /* subfic */
            int d = (word >> 21) & 31, a = (word >> 16) & 31;
            r[d] = (uint32_t)(s16v(word) - s32(r[a]));
            break; }""",
     """        case 8: { /* subfic */
            int d = (word >> 21) & 31, a = (word >> 16) & 31;
            uint64_t sum = (uint64_t)(uint32_t)~r[a] + (uint32_t)(int32_t)s16v(word) + 1;
            r[d] = (uint32_t)sum;
            c->ca = (uint32_t)(sum >> 32);
            break; }"""),
    ("""            case 266: case 10: r[d] = r[a] + r[b]; break;      /* add/addc */
            case 40:  case 8:  r[d] = r[b] - r[a]; break;      /* subf/subfc */""",
     """            case 266: r[d] = r[a] + r[b]; break;               /* add */
            case 10: { uint64_t s = (uint64_t)r[a] + r[b];      /* addc */
                       r[d] = (uint32_t)s; c->ca = (uint32_t)(s >> 32); break; }
            case 40:  r[d] = r[b] - r[a]; break;               /* subf */
            case 8: { uint64_t s = (uint64_t)(uint32_t)~r[a] + r[b] + 1;   /* subfc */
                      r[d] = (uint32_t)s; c->ca = (uint32_t)(s >> 32); break; }
            case 138: { uint64_t s = (uint64_t)r[a] + r[b] + c->ca;        /* adde */
                        r[d] = (uint32_t)s; c->ca = (uint32_t)(s >> 32); break; }
            case 202: { uint64_t s = (uint64_t)r[a] + c->ca;               /* addze */
                        r[d] = (uint32_t)s; c->ca = (uint32_t)(s >> 32); break; }
            case 234: { uint64_t s = (uint64_t)r[a] + c->ca + 0xFFFFFFFFu; /* addme */
                        r[d] = (uint32_t)s; c->ca = (uint32_t)(s >> 32); break; }
            case 136: { uint64_t s = (uint64_t)(uint32_t)~r[a] + r[b] + c->ca;   /* subfe */
                        r[d] = (uint32_t)s; c->ca = (uint32_t)(s >> 32); break; }
            case 200: { uint64_t s = (uint64_t)(uint32_t)~r[a] + c->ca;          /* subfze */
                        r[d] = (uint32_t)s; c->ca = (uint32_t)(s >> 32); break; }
            case 232: { uint64_t s = (uint64_t)(uint32_t)~r[a] + c->ca + 0xFFFFFFFFu;  /* subfme */
                        r[d] = (uint32_t)s; c->ca = (uint32_t)(s >> 32); break; }"""),
    ("""            case 792: { int sh = r[b] & 63; if (sh > 31) sh = 31;
                        r[a] = (uint32_t)(s32(r[d]) >> sh); break; }
            case 824: r[a] = (uint32_t)(s32(r[d]) >> b); break;""",
     """            case 792: { int sh = r[b] & 63; uint32_t lost;
                        if (sh > 31) { lost = r[d]; sh = 31; } else lost = r[d] & ((1u << sh) - 1);
                        c->ca = (s32(r[d]) < 0 && lost != 0) ? 1 : 0;
                        r[a] = (uint32_t)(s32(r[d]) >> sh); break; }
            case 824: c->ca = (s32(r[d]) < 0 && (r[d] & ((1u << b) - 1)) != 0) ? 1 : 0;
                      r[a] = (uint32_t)(s32(r[d]) >> b); break;"""),
    ("""                case 266: case 40:
                    setcr(c, 0, s32(r[d]), 0); break;""",
     """                case 266: case 40: case 10: case 8: case 138: case 202:
                case 234: case 136: case 200: case 232:
                    setcr(c, 0, s32(r[d]), 0); break;"""),
])

# -- cpu.py --------------------------------------------------------------------
patch(os.path.join(VW, 'ppc', 'cpu.py'), [
    ("""        self.xer = 0""",
     """        self.xer = 0
        self.ca = 0                  # XER[CA]"""),
    ("""        elif op == 8:     # subfic
            d, a, si = (word >> 21) & 31, (word >> 16) & 31, ((word & 0xFFFF) - ((word & 0x8000) << 1))
            r[d] = (si - s32(r[a])) & MASK32""",
     """        elif op == 8:     # subfic
            d, a, si = (word >> 21) & 31, (word >> 16) & 31, ((word & 0xFFFF) - ((word & 0x8000) << 1))
            s = ((~r[a]) & MASK32) + (si & MASK32) + 1
            r[d] = s & MASK32
            self.ca = s >> 32"""),
    ("""        if xo == 266 or xo == 10:            # add / addc
            r[d] = (r[a] + r[b]) & MASK32
        elif xo == 40:                       # subf
            r[d] = (r[b] - r[a]) & MASK32
        elif xo == 8:                        # subfc
            r[d] = (r[b] - r[a]) & MASK32""",
     """        if xo == 266:                        # add
            r[d] = (r[a] + r[b]) & MASK32
        elif xo == 10:                       # addc
            s = r[a] + r[b]
            r[d] = s & MASK32
            self.ca = s >> 32
        elif xo == 40:                       # subf
            r[d] = (r[b] - r[a]) & MASK32
        elif xo == 8:                        # subfc
            s = ((~r[a]) & MASK32) + r[b] + 1
            r[d] = s & MASK32
            self.ca = s >> 32
        elif xo in (138, 202, 234, 136, 200, 232):   # adde addze addme subfe subfze subfme
            x = r[a] if xo in (138, 202, 234) else ((~r[a]) & MASK32)
            y = r[b] if xo in (138, 136) else (MASK32 if xo in (234, 232) else 0)
            s = x + y + self.ca
            r[d] = s & MASK32
            self.ca = s >> 32"""),
    ("""        elif xo == 792:                      # sraw
            sh = r[b] & 63
            r[a] = (s32(r[d]) >> min(sh, 31)) & MASK32
        elif xo == 824:                      # srawi
            sh = b
            r[a] = (s32(r[d]) >> sh) & MASK32""",
     """        elif xo == 792:                      # sraw
            sh = r[b] & 63
            lost = r[d] if sh > 31 else r[d] & ((1 << sh) - 1)
            self.ca = 1 if (s32(r[d]) < 0 and lost) else 0
            r[a] = (s32(r[d]) >> min(sh, 31)) & MASK32
        elif xo == 824:                      # srawi
            sh = b
            self.ca = 1 if (s32(r[d]) < 0 and (r[d] & ((1 << sh) - 1))) else 0
            r[a] = (s32(r[d]) >> sh) & MASK32"""),
])

# -- a way to force the Python core ---------------------------------------------
patch(os.path.join(VW, 'ppc', 'image.py'), [
    ("""    from ppc import fastcpu
    FAST = fastcpu.AVAILABLE""",
     """    from ppc import fastcpu
    FAST = fastcpu.AVAILABLE and not os.environ.get('VW_PURE_CPU')"""),
])
print('ok')
