/* vw_api.c -- bringing the engine up, and words to phonemes.
 *
 * vw_engine_open does what the application's main() does before its
 * event loop: InitSynth, Synth_Startup with 48 voices, the speech voices
 * and the instrument bank through Synth_SetWaveBank/Synth_SetInstrument,
 * the tempo scale, and the dictionary opened for Synth_ConvertWord.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "vw_engine.h"
#include "vocalwriter.h"

#define POLYPHONY 48

int vw_engine_open(vw_engine *e, const char *rsrc_path, const char *gmspeech_path,
                   const char *gmbank_path, const char *lexicon_path)
{
    size_t fork_len, gm_len, n, mwav_len, mdef_len;
    unsigned char *fork, *gm, *ttvi, *mvox, *bank, *mwav, *mdef;
    int16_t err;
    int rc;

    memset(e, 0, sizeof *e);
    fork = vw_read_file(rsrc_path, &fork_len);
    gm = vw_read_file(gmspeech_path, &gm_len);
    if (fork == NULL || gm == NULL) {
        free(fork);
        free(gm);
        return VW_ERR_FILE;
    }
    ttvi = vw_resource(fork, fork_len, "ttvi", 2, &n);
    mvox = vw_resource(gm, gm_len, "mvox", 1, &mwav_len);
    free(fork);
    free(gm);
    if (ttvi == NULL || mvox == NULL) {
        free(ttvi);
        free(mvox);
        return VW_ERR_FORMAT;
    }
    if (vw_load_ttvi(ttvi, n) != 0) {
        free(ttvi);
        free(mvox);
        return VW_ERR_FORMAT;
    }
    e->ttvi = ttvi;
    if (InitSynth() != 0)
        return VW_ERR_ENGINE;
    err = Synth_Startup(&e->svv, POLYPHONY, 0);
    if (err != 0 || e->svv == NULL) {
        e->last_error = err;
        return VW_ERR_ENGINE;
    }
    e->xx = e->svv->ChannelGlobals;
    e->voices = vw_load_voices(mvox, mwav_len, &e->voiceCount);
    free(mvox);
    if (e->voices == NULL)
        return VW_ERR_FORMAT;

    if (gmbank_path != NULL) {
        bank = vw_read_file(gmbank_path, &n);
        if (bank == NULL)
            return VW_ERR_FILE;
        mwav = vw_resource(bank, n, "mwav", 1, &mwav_len);
        mdef = vw_resource(bank, n, "mdef", 1, &mdef_len);
        free(bank);
        if (mwav == NULL || mdef == NULL) {
            free(mwav);
            free(mdef);
            return VW_ERR_FORMAT;
        }
        rc = vw_bank_load(&e->bank, mwav, mwav_len, mdef, mdef_len);
        free(mwav);
        free(mdef);
        if (rc)
            return rc;
        err = (int16_t)vw_bank_install(e->svv, &e->bank, e->voices);
        if (err != 0) {
            e->last_error = err;
            return VW_ERR_ENGINE;
        }
        e->hasBank = 1;
    } else {
        Synth_SetWaveBank(e->svv, NULL, 1, e->voices);
    }
    Synth_SetTempoScale(e->svv, 0x10000);

    if (lexicon_path != NULL) {
        e->lexicon = vw_read_file(lexicon_path, &e->lexicon_len);
        if (e->lexicon == NULL)
            return VW_ERR_FILE;
        e->lexRef = vw_fs_open(e->lexicon, e->lexicon_len);
    }
    return VW_OK;
}

void vw_engine_close(vw_engine *e)
{
    if (e->svv != NULL)
        Synth_ShutDown(e->svv);
    if (e->lexRef)
        vw_fs_close(e->lexRef);
    free(e->lexicon);
    if (e->hasBank)
        vw_bank_free(&e->bank);
    free(e->voices);
    free(e->ttvi);
    memset(e, 0, sizeof *e);
}

/* -- words ---------------------------------------------------------------- */

int vw_word_syllables(vw_engine *e, const char *word, unsigned char syllables[10][9])
{
    ConvertTextRec tRec;
    size_t n = 0, k;
    int16_t err;
    int i;

    if (e->lexRef == 0)
        return VW_ERR_ARGUMENT;
    memset(&tRec, 0, sizeof tRec);
    for (k = 0; word[k] && n < 16; k++)
        if (isalpha((unsigned char)word[k]) || word[k] == '\'' || word[k] == '.')
            tRec.text_Input[n++] = (unsigned char)word[k];
    if (n == 0)
        return 0;
    tRec.textLen_Input = (int32_t)n;
    err = Synth_ConvertWord(e->svv, &tRec, e->lexRef);
    if (err != 0) {
        e->last_error = err;
        return VW_ERR_ENGINE;
    }
    for (i = 0; i < tRec.syllables_Result && i < 10; i++) {
        int len = (int)tRec.phon_Result[i].syllLen;
        if (len > 8)
            len = 8;
        syllables[i][0] = (unsigned char)len;
        memcpy(syllables[i] + 1, tRec.phon_Result[i].syllStr, (size_t)len);
    }
    return tRec.syllables_Result < 10 ? (int)tRec.syllables_Result : 10;
}

/* the application's own two-letter names for its phonemes */
const char *vw_phoneme_name(const vw_engine *e, int code)
{
    static char names[64][3];
    uint16_t v;
    (void)e;
    if (code < 0 || code >= 57 || g_Opcode_To_ASCII == NULL)
        return NULL;
    v = g_Opcode_To_ASCII[code];
    names[code][0] = (char)(v >> 8);
    names[code][1] = (char)(v & 0xff);
    names[code][2] = 0;
    if (names[code][0] == 0) {                   /* single letters pack low */
        names[code][0] = names[code][1];
        names[code][1] = 0;
    }
    return names[code];
}

int vw_phoneme_code(const vw_engine *e, const char *name)
{
    int c;
    for (c = 0; c < 57; c++) {
        const char *n = vw_phoneme_name(e, c);
        if (n != NULL && strcmp(n, name) == 0)
            return c;
    }
    return -1;
}
