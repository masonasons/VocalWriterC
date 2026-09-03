/* bank.c -- loading VocalWriter's General MIDI bank (GMBank) for the port.
 *
 * The application read two resources out of GMBank and handed them to the
 * engine as they lay in memory: `mwav` id 1, the wavetable samples behind a
 * table of wave records, and `mdef` id 1, the instrument definitions. Both
 * are big-endian, as the PowerPC read them natively; the port byte-swaps
 * the fields and samples the engine indexes and leaves everything else in
 * place, so the offsets inside each resource stay valid.
 *
 *   mwav:  +0x04 u32 version (0x10000)   +0x08 u32 offset of the WaveDef table
 *          +0x0e i16 PCM type (1 = 16-bit) +0x10 copyright
 *          WaveDef: 16-byte Pascal name, i32 waveOffset (from the record
 *          itself), i32 waveLen (samples); samples follow the table
 *   mdef:  +0x04 u32 version   +0x0c u32 offset of the InstDef array
 *          +0x10 u32 offset of the WaveListDef array   +0x14 u32 instruments + 1
 *
 * main() checked that both versions are 0x10000 and equal, then called
 * Synth_SetWaveBank(svv, mwav + table, PCMType, voices) and
 * Synth_SetInstrument(svv, &InstDefs[i], WaveListDefs, i) for i below the
 * count; vw_bank_install repeats that sequence.
 */
#include <stdlib.h>
#include <string.h>
#include "vw_engine.h"
#include "vocalwriter.h"

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void swap16(unsigned char *p)
{
    unsigned char t = p[0];
    p[0] = p[1];
    p[1] = t;
}

static void swap32(unsigned char *p)
{
    unsigned char t = p[0];
    p[0] = p[3];
    p[3] = t;
    t = p[1];
    p[1] = p[2];
    p[2] = t;
}

int vw_bank_load(vw_bank *bank, const unsigned char *mwav, size_t mwav_len,
                 const unsigned char *mdef, size_t mdef_len)
{
    size_t table, entry, first_sample, k;
    uint32_t inst_off, wl_off, count;

    memset(bank, 0, sizeof *bank);
    if (mwav_len < 0x100 || mdef_len < 0x18)
        return VW_ERR_FORMAT;
    if (be32(mwav + 4) != 0x10000 || be32(mdef + 4) != be32(mwav + 4))
        return VW_ERR_FORMAT;                    /* what main() checked */

    bank->mwav = (unsigned char *)malloc(mwav_len);
    bank->mdef = (unsigned char *)malloc(mdef_len);
    if (bank->mwav == NULL || bank->mdef == NULL)
        return VW_ERR_MEMORY;
    memcpy(bank->mwav, mwav, mwav_len);
    memcpy(bank->mdef, mdef, mdef_len);
    bank->mwav_len = mwav_len;
    bank->mdef_len = mdef_len;
    bank->pcmType = (int16_t)((mwav[0xe] << 8) | mwav[0xf]);

    /* the wave table: records until the first sample they point at */
    table = be32(mwav + 8);
    first_sample = mwav_len;
    for (entry = table; entry + 24 <= first_sample; entry += 24) {
        unsigned char *rec = bank->mwav + entry;
        int32_t off = (int32_t)be32(rec + 16);
        int32_t len = (int32_t)be32(rec + 20);
        if (rec[0] > 15)
            break;                               /* not a Pascal name: past the table */
        swap32(rec + 16);
        swap32(rec + 20);
        if (len > 0 && off > 0 && entry + (size_t)off < first_sample)
            first_sample = entry + (size_t)off;
    }
    bank->waveTable = bank->mwav + table;
    bank->waveCount = (int)((entry - table) / 24);
    /* the samples: 16-bit words from there to the end */
    if (bank->pcmType == 1)
        for (k = first_sample; k + 1 < mwav_len; k += 2)
            swap16(bank->mwav + k);

    /* the instrument definitions */
    inst_off = be32(mdef + 0xc);
    wl_off = be32(mdef + 0x10);
    count = be32(mdef + 0x14);
    if (count < 1 || inst_off + (size_t)(count - 1) * 68 > mdef_len || wl_off > mdef_len)
        return VW_ERR_FORMAT;
    bank->instCount = (int)count - 1;            /* main() stopped one short */
    bank->instDefs = (InstDefPtr)(bank->mdef + inst_off);
    bank->waveLists = (WaveListDefPtr)(bank->mdef + wl_off);
    for (k = 0; k < (size_t)bank->instCount; k++) {
        unsigned char *rec = bank->mdef + inst_off + k * 68;
        int i;
        for (i = 0; i < 16; i++)
            swap16(rec + 0x24 + 2 * i);          /* U_WLRef[16] */
    }
    for (k = wl_off; k + 20 <= mdef_len; k += 20) {
        swap16(bank->mdef + k + 4);              /* U_WaveRefA */
        swap16(bank->mdef + k + 0xa);            /* U_WaveRefB */
    }
    return 0;
}

void vw_bank_free(vw_bank *bank)
{
    free(bank->mwav);
    free(bank->mdef);
    memset(bank, 0, sizeof *bank);
}

/* What main() did with the bank once Synth_Startup had run. */
int vw_bank_install(shellVarPtr svv, const vw_bank *bank, Ptr speechVoices)
{
    int i;
    int16_t err = Synth_SetWaveBank(svv, (Ptr)bank->waveTable, bank->pcmType, speechVoices);
    if (err != 0)
        return err;
    for (i = 0; i < bank->instCount; i++) {
        err = Synth_SetInstrument(svv, &bank->instDefs[i], bank->waveLists, (int16_t)i);
        if (err != 0)
            return err;
    }
    return 0;
}
