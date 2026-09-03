/* tables.c -- the shared data tables, and loading them without a Macintosh.
 *
 * VocalWriter keeps every table the synthesiser uses in one resource, `ttvi`
 * id 2, addressed by a header of 36 offsets; one of those tables carries a
 * second header of 46 offsets for the speech tables. `InitSharedTables`,
 * `SetTblAddr` and `Make_F_Table` are the original's (Macintosh.c), lifted
 * like the rest. What the original left to the Resource Manager -- finding
 * the resource -- and what it never needed -- byte order -- is done here.
 */
#include <stdlib.h>
#include <string.h>
#include "vw_engine.h"

/* -- Macintosh.c globals -------------------------------------------------- */

Handle g_DataHandle;
int32_t *g_TopOctave;
int32_t *g_Freq_Tbl;
int16_t *g_Note_Tbl_Def;
int16_t *g_NoteDecayTbl;
int16_t *g_Time_Tbl;
int16_t *g_VG_Scale;
int16_t *g_VG_Add;
int16_t *g_OscModeTbl;
int16_t *g_InitialOscState;
int16_t *g_Oct_Tbl;
unsigned char *g_SineWavePtr;
int16_t *g_velToLinPtr;
int16_t *g_GM_Map;
int16_t *g_GM_DrumMap;
int16_t *g_MidiLengths;
char *g_metaNameStr;
char *g_trackNameStr;
int32_t *g_phonFlags2;
int16_t *g_maxDurTbl;
int16_t *g_minDurTbl;
uint16_t *g_Opcode_To_ASCII;
int16_t *g_phonTypeTbl;
Ptr g_RulesData;
int16_t *g_hash;
unsigned char *g_rule;
unsigned char *g_kind;
unsigned char *g_dashruletab;
unsigned char *g_atruletab;
unsigned char *g_lruletab;
unsigned char *g_mruletab;
unsigned char *g_zruletab;
unsigned char *g_percentruletab;
unsigned char *g_bruletab;
unsigned char *g_SuffixTab;
int16_t *g_SuffixType;
mFloat *g_CosTbl;
mFloat *g_BcoeffTbl;
mFloat *g_CcoeffTbl;
int32_t *g_SpeechTbls;

static Ptr g_DataPtr;            /* what g_DataHandle points at */

/* Macintosh.c:2461 */
static Ptr GetThePtr(Ptr basePtr, int32_t *tblPtr)
{
    Ptr dataPtr;
    int32_t offset;

    offset = *tblPtr;
    dataPtr = &basePtr[offset];
    return dataPtr;
}

/* Macintosh.c:2514 */
static void SetTblAddr(void)
{
    int32_t *tblPtr;
    Ptr basePtr;

    tblPtr = (int32_t *)*g_DataHandle;
    basePtr = (Ptr)tblPtr;
    g_TopOctave = (int32_t *)GetThePtr(basePtr, tblPtr++);
    g_Note_Tbl_Def = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_NoteDecayTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_Time_Tbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_VG_Scale = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_VG_Add = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_OscModeTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_InitialOscState = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_Oct_Tbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_SineWavePtr = (unsigned char *)GetThePtr(basePtr, tblPtr++);
    g_velToLinPtr = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_GM_Map = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_GM_DrumMap = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_MidiLengths = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_metaNameStr = GetThePtr(basePtr, tblPtr++);
    g_trackNameStr = GetThePtr(basePtr, tblPtr++);
    g_phonFlags2 = (int32_t *)GetThePtr(basePtr, tblPtr++);
    g_maxDurTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_minDurTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_Opcode_To_ASCII = (uint16_t *)GetThePtr(basePtr, tblPtr++);
    g_phonTypeTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_kind = (unsigned char *)GetThePtr(basePtr, tblPtr++);
    g_dashruletab = (unsigned char *)GetThePtr(basePtr, tblPtr++);
    g_atruletab = (unsigned char *)GetThePtr(basePtr, tblPtr++);
    g_lruletab = (unsigned char *)GetThePtr(basePtr, tblPtr++);
    g_mruletab = (unsigned char *)GetThePtr(basePtr, tblPtr++);
    g_zruletab = (unsigned char *)GetThePtr(basePtr, tblPtr++);
    g_percentruletab = (unsigned char *)GetThePtr(basePtr, tblPtr++);
    g_bruletab = (unsigned char *)GetThePtr(basePtr, tblPtr++);
    g_SuffixTab = (unsigned char *)GetThePtr(basePtr, tblPtr++);
    g_SuffixType = (int16_t *)GetThePtr(basePtr, tblPtr++);
    g_RulesData = GetThePtr(basePtr, tblPtr++);
    g_CosTbl = (mFloat *)GetThePtr(basePtr, tblPtr++);
    g_BcoeffTbl = (mFloat *)GetThePtr(basePtr, tblPtr++);
    g_CcoeffTbl = (mFloat *)GetThePtr(basePtr, tblPtr++);
    g_SpeechTbls = (int32_t *)GetThePtr(basePtr, tblPtr++);
    g_hash = (int16_t *)g_RulesData;
    g_rule = (unsigned char *)&g_RulesData[52];
}

/* Macintosh.c:2461 -- the frequency table is computed, not stored: the top
   octave (384 entries of 32 steps per semitone) is copied in and each lower
   octave is the one above shifted right. */
void Make_F_Table(void)
{
    int32_t *curOctPtr;
    int32_t *lastOctPtr;
    int32_t *freqTblPtr;
    int16_t oct_Cnt;
    int16_t semi_Cnt;
    int16_t frac_Cnt;

    freqTblPtr = &g_Freq_Tbl[4607];
    curOctPtr = &g_TopOctave[383];
    lastOctPtr = freqTblPtr;
    for (semi_Cnt = 0; semi_Cnt <= 11; semi_Cnt++) {
        for (frac_Cnt = 0; frac_Cnt <= 31; frac_Cnt++) {
            *freqTblPtr = *curOctPtr;
            freqTblPtr--;
            curOctPtr--;
        }
    }
    for (oct_Cnt = 0; oct_Cnt <= 10; oct_Cnt++) {
        curOctPtr = lastOctPtr;
        lastOctPtr = freqTblPtr;
        for (semi_Cnt = 0; semi_Cnt <= 11; semi_Cnt++) {
            for (frac_Cnt = 0; frac_Cnt <= 31; frac_Cnt++) {
                *freqTblPtr = *curOctPtr >> 1;
                freqTblPtr--;
                curOctPtr--;
            }
        }
    }
}

/* Macintosh.c:2567 */
void InitSharedTables(void)
{
    g_Freq_Tbl = (int32_t *)calloc(18432, 1);          /* NewPtrClear */
    SetTblAddr();
    Make_F_Table();
}

/* -- byte order ------------------------------------------------------------- */

/* Element size of each table, in slot order, from the pointer types the
   original declares for them: 4 for int32/float, 2 for int16, 1 for bytes.
   0 marks a table of records that nothing in the synthesiser reads. */
static const unsigned char outer_elem[36] = {
    4, 2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 1, 1, 4, 2, 2, 2,
    2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 4, 4, 4, 4,
};
static const unsigned char inner_elem[46] = {
    4, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 4, 4, 4, 4, 4, 4, 4, 2, 2,
    1, 4, 2, 2, 2, 2,
};

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void swap_region(unsigned char *p, size_t len, unsigned elem)
{
    size_t i;
    if (elem == 2) {
        for (i = 0; i + 1 < len; i += 2) {
            unsigned char t = p[i];
            p[i] = p[i + 1];
            p[i + 1] = t;
        }
    } else if (elem == 4) {
        for (i = 0; i + 3 < len; i += 4) {
            unsigned char t0 = p[i], t1 = p[i + 1];
            p[i] = p[i + 3];
            p[i + 1] = p[i + 2];
            p[i + 2] = t1;
            p[i + 3] = t0;
        }
    }
}

struct region {
    uint32_t off;
    unsigned elem;
};

static int cmp_region(const void *a, const void *b)
{
    uint32_t x = ((const struct region *)a)->off, y = ((const struct region *)b)->off;
    return x < y ? -1 : x > y;
}

int vw_load_ttvi(unsigned char *blob, size_t len)
{
    struct region regions[36 + 46 + 1];
    int n = 0, i;
    uint32_t sp;

    if (len < 36 * 4)
        return -1;
    /* the two headers are read while still big-endian */
    for (i = 0; i < 36; i++) {
        regions[n].off = be32(blob + i * 4);
        regions[n].elem = outer_elem[i];
        n++;
    }
    sp = be32(blob + 35 * 4);
    if (sp + 46 * 4 > len)
        return -1;
    for (i = 0; i < 46; i++) {
        regions[n].off = sp + be32(blob + sp + i * 4);
        regions[n].elem = inner_elem[i];
        n++;
    }
    regions[n].off = (uint32_t)len;
    regions[n].elem = 0;
    n++;
    qsort(regions, n, sizeof regions[0], cmp_region);
    /* the headers themselves */
    swap_region(blob, 36 * 4, 4);
    swap_region(blob + sp, 46 * 4, 4);
    for (i = 0; i < n; i++) {
        uint32_t a = regions[i].off, b;
        unsigned elem = regions[i].elem;
        int j = i;
        /* the same table can be listed under two names */
        while (j + 1 < n && regions[j + 1].off == a) {
            j++;
            if (regions[j].elem)
                elem = regions[j].elem;
        }
        b = j + 1 < n ? regions[j + 1].off : (uint32_t)len;
        i = j;
        if (a >= len || b > len || a == b || a == sp)
            continue;
        swap_region(blob + a, b - a, elem);
    }
    /* the hash table at the front of the rules data is int16 */
    {
        uint32_t rules = ((uint32_t *)blob)[31];
        if (rules + 52 <= len)
            swap_region(blob + rules, 52, 2);
    }
    g_DataPtr = (Ptr)blob;
    g_DataHandle = &g_DataPtr;
    return 0;
}

/* -- the Macintosh resource fork ------------------------------------------- */

static uint16_t be16(const unsigned char *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

unsigned char *vw_resource(const unsigned char *fork, size_t len,
                           const char type[4], int id, size_t *out_len)
{
    uint32_t data_off, map_off, type_list, ntypes;
    uint32_t i;
    if (len < 16)
        return NULL;
    data_off = be32(fork);
    map_off = be32(fork + 4);
    if (map_off + 30 > len)
        return NULL;
    type_list = map_off + be16(fork + map_off + 24);
    if (type_list + 2 > len)
        return NULL;
    ntypes = be16(fork + type_list) + 1;
    for (i = 0; i < ntypes; i++) {
        const unsigned char *t = fork + type_list + 2 + i * 8;
        uint32_t count, ref_off, k;
        if (t + 8 > fork + len)
            return NULL;
        if (memcmp(t, type, 4) != 0)
            continue;
        count = be16(t + 4) + 1;
        ref_off = type_list + be16(t + 6);
        for (k = 0; k < count; k++) {
            const unsigned char *r = fork + ref_off + k * 12;
            int16_t rid;
            uint32_t doff, dlen;
            unsigned char *out;
            if (r + 12 > fork + len)
                return NULL;
            rid = (int16_t)be16(r);
            if (rid != id)
                continue;
            doff = data_off + (be32(r + 4) & 0xFFFFFF);
            if (doff + 4 > len)
                return NULL;
            dlen = be32(fork + doff);
            if (doff + 4 + dlen > len)
                return NULL;
            out = (unsigned char *)malloc(dlen ? dlen : 1);
            if (out == NULL)
                return NULL;
            memcpy(out, fork + doff + 4, dlen);
            *out_len = dlen;
            return out;
        }
    }
    return NULL;
}

/* -- the voice bank ---------------------------------------------------------- */

/* `mvox` holds a 256-entry program map, then (at 0x200) an array of offsets
   to 336-byte voice records. The engine takes the record pointers to follow
   the map directly in memory, so the loaded block reproduces that: 512
   bytes of int16 map, then native pointers to native-order records. */
Ptr vw_load_voices(const unsigned char *mvox, size_t len, int *count)
{
    uint32_t first, n, i;
    Ptr block;
    voiceDataPtr *ptrs;
    voiceData *recs;

    if (len < 0x204)
        return NULL;
    first = be32(mvox + 0x200);
    if (first < 0x204 || first > len)
        return NULL;
    n = (first - 0x200) / 4;
    block = (Ptr)calloc(512 + n * sizeof(voiceDataPtr), 1);
    recs = (voiceData *)calloc(n ? n : 1, sizeof(voiceData));
    if (block == NULL || recs == NULL)
        return NULL;
    for (i = 0; i < 256 && i * 2 + 1 < 0x200; i++)
        ((int16_t *)block)[i] = (int16_t)be16(mvox + i * 2);
    ptrs = (voiceDataPtr *)(block + 512);
    for (i = 0; i < n; i++) {
        uint32_t off = be32(mvox + 0x200 + i * 4);
        const unsigned char *src;
        unsigned char *dst;
        unsigned k;
        if (off == 0 || off + sizeof(voiceData) > len) {
            ptrs[i] = NULL;
            continue;
        }
        src = mvox + off;
        dst = (unsigned char *)&recs[i];
        memcpy(dst, src, 16);                       /* voiceName */
        for (k = 0x10; k < 0x130; k += 2) {         /* the int16 fields */
            dst[k] = src[k + 1];
            dst[k + 1] = src[k];
        }
        for (k = 0x130; k < 0x150; k += 4) {        /* free1..free8 */
            dst[k] = src[k + 3];
            dst[k + 1] = src[k + 2];
            dst[k + 2] = src[k + 1];
            dst[k + 3] = src[k];
        }
        ptrs[i] = &recs[i];
    }
    if (count)
        *count = (int)n;
    return block;
}
