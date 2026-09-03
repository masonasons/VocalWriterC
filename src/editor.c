/* editor.c -- the engine as a note editor drives it: one voice, a phoneme
 * sequence of the caller's own making, and frames pulled one at a time.
 *
 * The application's play path builds a song and lets the sequencer sing it;
 * an editor has already decided the phonemes and their lengths and wants to
 * hand them over directly. Both end up in the same synthesiser. This is the
 * second way in, and it is not a new one: the calls and their order are what
 * the VocalWriter repository's interpreter driver (`ppc/render.py`) makes,
 * which is how that project renders and how it was verified.
 *
 * The setup follows test/harness.c, which drives the same path for the
 * differential tests -- so a phrase rendered through here and the same phrase
 * rendered through the interpreter are the same samples.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "vw_engine.h"
#include "vw_editor.h"

#define OUT_SAMPLES (1 << 21)      /* what ppc/synth.py allocates */
#define TEMPO_SCALE (1.0f / 240.0f)

struct vw_editor {
    shellVar svv;
    synthVarsPtr xx;
    formantVarPtr zz;
    int16_t *seq;
    int16_t *blank_time_tbl;
    int voiceCount;
    char voiceName[17];
    int16_t lexRef;
    int reverbMemory;
};

/* InitSharedTables carves up one set of tables from the `ttvi` resource and
   leaves them in globals. They are read-only afterwards and every editor
   shares them, so this happens once. */
static int g_tables_ready;

vw_editor *vw_ed_open(const unsigned char *rsrc, size_t rsrc_len,
                      const unsigned char *gmspeech, size_t gmspeech_len)
{
    vw_editor *e;
    unsigned char *ttvi, *mvox;
    size_t ttvi_len, mvox_len;

    if (rsrc == NULL || gmspeech == NULL)
        return NULL;
    mvox = vw_resource(gmspeech, gmspeech_len, "mvox", 1, &mvox_len);
    if (mvox == NULL)
        return NULL;
    if (!g_tables_ready) {
        ttvi = vw_resource(rsrc, rsrc_len, "ttvi", 2, &ttvi_len);
        if (ttvi == NULL)
            return NULL;
        if (vw_load_ttvi(ttvi, ttvi_len) != 0)
            return NULL;
        InitSharedTables();
        g_tables_ready = 1;
    }

    e = (vw_editor *)calloc(1, sizeof *e);
    if (e == NULL)
        return NULL;
    e->xx = (synthVarsPtr)calloc(1, sizeof(synthVars));
    e->zz = (formantVarPtr)calloc(1, sizeof(formantVar));
    e->blank_time_tbl = (int16_t *)calloc(0x400, 1);
    if (e->xx == NULL || e->zz == NULL || e->blank_time_tbl == NULL) {
        vw_ed_close(e);
        return NULL;
    }
    e->xx->SpeechTbls = g_SpeechTbls;
    e->xx->speechVars[0] = e->zz;
    e->xx->GMVoicePtr = vw_load_voices(mvox, mvox_len, &e->voiceCount);
    e->xx->sampleBuffer = (int16_t *)calloc(OUT_SAMPLES, sizeof(int16_t));
    /* The glide table stays blank until vw_ed_defaults is asked for the real
       one: the engine's default portamento is read out of it, and a driver
       that wants notes to step rather than glide must set the defaults while
       there is nothing there. */
    e->xx->Time_Tbl = e->blank_time_tbl;
    e->xx->Freq_Tbl = g_Freq_Tbl;
    if (e->xx->GMVoicePtr == NULL || e->xx->sampleBuffer == NULL) {
        vw_ed_close(e);
        return NULL;
    }
    InitGlobals_Speech(e->xx, 0);
    e->xx->tempoMul = TEMPO_SCALE;
    /* The front end's tables, wired the way Synth_Startup wires them. They
       cost nothing until a word is looked up. */
    e->xx->phonFlags2 = g_phonFlags2;
    e->xx->maxDurTbl = g_maxDurTbl;
    e->xx->minDurTbl = g_minDurTbl;
    e->xx->Opcode_To_ASCII = g_Opcode_To_ASCII;
    e->xx->phonTypeTbl = g_phonTypeTbl;
    e->xx->hash = g_hash;
    e->xx->rule = g_rule;
    e->xx->kind = g_kind;
    e->xx->dashruletab = g_dashruletab;
    e->xx->atruletab = g_atruletab;
    e->xx->lruletab = g_lruletab;
    e->xx->mruletab = g_mruletab;
    e->xx->zruletab = g_zruletab;
    e->xx->percentruletab = g_percentruletab;
    e->xx->bruletab = g_bruletab;
    e->xx->SuffixTab = g_SuffixTab;
    e->xx->SuffixType = g_SuffixType;
    return e;
}

void vw_ed_close(vw_editor *e)
{
    if (e == NULL)
        return;
    if (e->lexRef)
        vw_fs_close(e->lexRef);
    if (e->xx != NULL) {
        free(e->xx->sampleBuffer);
        free(e->xx);
    }
    free(e->zz);
    free(e->blank_time_tbl);
    free(e->seq);
    free(e);
}

/* -- setting up ----------------------------------------------------------- */

void vw_ed_tempo_scale(vw_editor *e, float mul)
{
    e->xx->tempoMul = mul;
}

void vw_ed_tempo(vw_editor *e, int bpm)
{
    SetTempo(e->xx, (int16_t)bpm);
}

void vw_ed_program(vw_editor *e, int program)
{
    PgmChange_Speech(e->xx, 0, (int16_t)program);
}

int vw_ed_sequence(vw_editor *e, const unsigned char *blob, size_t len)
{
    size_t words = len / 2, i;
    int16_t voice;
    int16_t *seq = (int16_t *)calloc(words + 1, sizeof(int16_t));

    if (seq == NULL)
        return -1;
    /* The block is big-endian: it is the original's own data format, and the
       engine reads it with the same byte order wherever it runs. */
    for (i = 0; i < words; i++)
        seq[i] = (int16_t)((blob[2 * i] << 8) | blob[2 * i + 1]);
    voice = e->zz->voiceRef;
    vw_SetSeqAddr(e->zz, seq);
    e->zz->voiceRef = voice;
    free(e->seq);
    e->seq = seq;
    return 0;
}

void vw_ed_start(vw_editor *e)
{
    InitSay(e->zz);
    Init_ControlBlocks(e->zz);
    Start_Speech(e->xx, 0);
    Sing_Speech(e->xx, 0, 1);
}

void vw_ed_defaults(vw_editor *e, int glide)
{
    vw_InitDefaultVoiceCntrls(e->zz);
    if (glide)
        e->zz->Time_Tbl = g_Time_Tbl;
}

void vw_ed_volume(vw_editor *e, int32_t value)
{
    Speech_Volume(e->xx, 0, value);
    vw_SetTotalVolume(e->zz);
}

int vw_ed_control(vw_editor *e, const char *name, int32_t value)
{
    synthVarsPtr xx = e->xx;
    if (strcmp(name, "Speech_Color") == 0) Speech_Color(xx, 0, value);
    else if (strcmp(name, "Speech_VibDepth") == 0) Speech_VibDepth(xx, 0, value);
    else if (strcmp(name, "Speech_VibFreq") == 0) Speech_VibFreq(xx, 0, value);
    else if (strcmp(name, "Speech_Chorus") == 0) Speech_Chorus(xx, 0, value);
    else if (strcmp(name, "Speech_Breath") == 0) Speech_Breath(xx, 0, value);
    else if (strcmp(name, "Speech_Detune") == 0) Speech_Detune(xx, 0, value);
    else if (strcmp(name, "Speech_Portamento") == 0) Speech_Portamento(xx, 0, value);
    else if (strcmp(name, "Speech_Noise") == 0) Speech_Noise(xx, 0, value);
    else if (strcmp(name, "Speech_PitchBend") == 0) Speech_PitchBend(xx, 0, value);
    else if (strcmp(name, "Speech_PBSens") == 0) Speech_PBSens(xx, 0, value);
    else if (strcmp(name, "Speech_TrackLevel") == 0) Speech_TrackLevel(xx, 0, value);
    else if (strcmp(name, "Speech_Volume") == 0) vw_ed_volume(e, value);
    else return -1;
    return 0;
}

/* -- rendering ------------------------------------------------------------- */

void vw_ed_note(vw_editor *e, int key, int nextKey, int velocity, double beats)
{
    Speech_Note(e->xx, 0, (int16_t)key, (int16_t)nextKey, (int16_t)velocity,
                (mFloat)beats);
}

int vw_ed_frames(vw_editor *e, int count)
{
    int done = 0;
    while (done < count) {
        e_Fill_Next_Frame(e->zz);
        SayFrame(e->zz);
        done++;
        if (e->zz->speakState == 3 || e->zz->freezeFrame)
            break;
    }
    return done;
}

int vw_ed_state(vw_editor *e)
{
    return e->zz->speakState;
}

int vw_ed_wants_note(vw_editor *e)
{
    return e->zz->freezeFrame;
}

int32_t vw_ed_wave_index(vw_editor *e)
{
    return e->zz->waveIndex;
}

const int16_t *vw_ed_wave(vw_editor *e)
{
    return e->xx->sampleBuffer;
}

/* -- the radiation shelf --------------------------------------------------- */

int vw_ed_hf_emph(vw_editor *e)
{
    return e->zz->hfEmph;
}

float vw_ed_emph_a(vw_editor *e)
{
    return e->zz->emphA;
}

float vw_ed_emph_b(vw_editor *e)
{
    return e->zz->emphB;
}

void vw_ed_set_emph(vw_editor *e, float a, float b)
{
    e->zz->emphA = a;
    e->zz->emphB = b;
}

float vw_ed_speech_volume(vw_editor *e)
{
    return e->zz->speechVolume;
}

void vw_ed_set_speech_volume(vw_editor *e, float v)
{
    e->zz->speechVolume = v;
}

/* -- the reverb -------------------------------------------------------------- */

int vw_ed_reverb(vw_editor *e, float room, float wet)
{
    if (!e->reverbMemory) {
        /* Synth_Startup would do this; the editor brings the engine up by
           hand, so the shell record is filled in far enough for the reverb
           and its delay lines are asked for once. */
        e->svv.ChannelGlobals = e->xx;
        e->svv.reverbEnabled = (GetReverbMemory(&e->svv) == 0);
        if (!e->svv.reverbEnabled)
            return -1;
        e->reverbMemory = 1;
    }
    Synth_SetReverb(&e->svv, room, wet, 1.0f - wet);
    XferReverbHold(e->xx);
    return e->xx->reverbON ? 0 : 1;
}

int vw_ed_reverberate(vw_editor *e, int16_t *samples, int32_t frames)
{
    int16_t *keptBuffer = e->xx->sampleBuffer;
    int32_t keptFrames = e->xx->SoundBufferFrames;

    if (!e->xx->reverbON || frames < 220)
        return -1;
    e->xx->sampleBuffer = samples;
    e->xx->SoundBufferFrames = frames / 220;
    Reverberator_Process(e->xx, 0);
    e->xx->sampleBuffer = keptBuffer;
    e->xx->SoundBufferFrames = keptFrames;
    return 0;
}

/* -- words ------------------------------------------------------------------ */

int vw_ed_lexicon(vw_editor *e, const unsigned char *data, size_t len)
{
    if (e->lexRef)
        vw_fs_close(e->lexRef);
    e->lexRef = vw_fs_open(data, len);
    return e->lexRef ? 0 : -1;
}

int vw_ed_word(vw_editor *e, const char *text, unsigned char *out)
{
    ConvertTextRec tRec;
    size_t n = 0, k;
    int16_t err;
    int i;

    if (e->lexRef == 0)
        return -1;
    memset(&tRec, 0, sizeof tRec);
    memset(out, 0, 10 * 9);
    for (k = 0; text[k] && n < 16; k++)
        if (isalpha((unsigned char)text[k]) || text[k] == '\'' || text[k] == '.')
            tRec.text_Input[n++] = (unsigned char)text[k];
    if (n == 0)
        return 0;
    tRec.textLen_Input = (int32_t)n;
    /* The original reads a block or two after freeing them; the shim holds
       freed memory back until the call is over rather than letting the
       allocator reuse it underneath. */
    vw_shim_defer_free = 1;
    err = OrthToPhon(e->xx, &tRec, e->lexRef);
    vw_shim_flush_deferred();
    vw_shim_defer_free = 0;
    if (err != 0)
        return 0;
    for (i = 0; i < tRec.syllables_Result && i < 10; i++) {
        int len = (int)tRec.phon_Result[i].syllLen;
        if (len > 8)
            len = 8;
        out[i * 9] = (unsigned char)len;
        memcpy(out + i * 9 + 1, tRec.phon_Result[i].syllStr, (size_t)len);
    }
    return tRec.syllables_Result < 10 ? (int)tRec.syllables_Result : 10;
}

const char *vw_ed_phoneme_name(int code)
{
    static char names[64][3];
    uint16_t v;
    if (code < 0 || code >= 57 || g_Opcode_To_ASCII == NULL)
        return NULL;
    v = (uint16_t)g_Opcode_To_ASCII[code];
    names[code][0] = (char)(v >> 8);
    names[code][1] = (char)(v & 0xff);
    names[code][2] = 0;
    if (names[code][0] == 0) {                   /* single letters pack low */
        names[code][0] = names[code][1];
        names[code][1] = 0;
    }
    return names[code];
}

/* -- the voice -------------------------------------------------------------- */

const char *vw_ed_voice_name(vw_editor *e)
{
    voiceDataPtr rec;
    int n;
    if (e->zz->GMVoiceData == NULL)
        return NULL;
    rec = e->zz->GMVoiceData[e->zz->voiceRef];
    if (rec == NULL)
        return NULL;
    n = rec->voiceName[0];                  /* a Pascal string */
    if (n > 15)
        n = 15;
    memcpy(e->voiceName, rec->voiceName + 1, (size_t)n);
    e->voiceName[n] = 0;
    return e->voiceName;
}
