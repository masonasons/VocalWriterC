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
#include "vocalwriter.h"
#include "vw_editor.h"

/* How much output to start with: 1 << 21 halfwords is 23.8 seconds, which is
   what the interpreter driver allocates. A phrase can be longer than that --
   a legato line with no rest in it is one phrase however long the song is --
   so the buffer grows rather than being written past. It was written past
   before this: `SayFrame` takes the buffer from the context and writes 440
   halfwords wherever waveIndex points, with nothing to stop it, so a phrase
   of 24 seconds walked off the end of the block and took the process with
   it. */
#define OUT_SAMPLES (1 << 21)
#define FRAME_HALFWORDS 440
#define TEMPO_SCALE (1.0f / 240.0f)

struct vw_editor {
    shellVar svv;
    synthVarsPtr xx;
    formantVarPtr zz;
    int16_t *seq;
    int32_t sampleCapacity;            /* halfwords in xx->sampleBuffer */
    int16_t *blank_time_tbl;
    int voiceCount;
    char voiceName[17];
    int16_t lexRef;
    int reverbMemory;
    vw_bank bank;
    int hasBank;
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
    e->sampleCapacity = OUT_SAMPLES;
    /* The glide table stays blank until vw_ed_defaults is asked for the real
       one: the engine's default portamento is read out of it, and a driver
       that wants notes to step rather than glide must set the defaults while
       there is nothing there. */
    e->xx->Time_Tbl = e->blank_time_tbl;
    e->xx->Freq_Tbl = g_Freq_Tbl;
    /* The oscillator tables. A voice built on a wavetable rather than on the
       glottal source -- which is most of the bank, the ones with instrument
       names -- reads these while it is being selected, and reads them through
       the music globals rather than the speech ones. Synth_Startup wires
       them; here they are wired by hand, as the rest are. */
    e->xx->OscModeTbl = g_OscModeTbl;
    e->xx->Oct_Tbl = g_Oct_Tbl;
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
    if (e->hasBank)
        vw_bank_free(&e->bank);
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

int vw_ed_program(vw_editor *e, int program)
{
    int voice = vw_ed_program_voice(e, program);
    if (voice < 0)
        return -1;
    /* the same refusal as vw_ed_voice: the program map reaches the wavetable
       voices too, and a program change to one of those without the bank
       crashes in exactly the same place */
    if (vw_ed_voice_needs_bank(e, voice) > 0 && !vw_ed_has_bank(e))
        return -2;
    PgmChange_Speech(e->xx, 0, (int16_t)program);
    return 0;
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

void vw_ed_level(vw_editor *e, float level)
{
    /* Speech_TrackLevel with the rounding taken out: it is amt/100 into the
       same field, and a voice built on a wavetable can be thirty times too
       loud for full scale, which 1 of 100 does not divide finely enough. */
    e->zz->trackLevel = level;
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

/* Room for one more frame, doubling the buffer when there is not. The engine
   reads the buffer out of the context at the start of every frame, so moving
   it between frames is safe. */
static int room_for_a_frame(vw_editor *e)
{
    int32_t want = e->zz->waveIndex + FRAME_HALFWORDS;
    int32_t size;
    int16_t *bigger;

    if (want <= e->sampleCapacity)
        return 1;
    size = e->sampleCapacity;
    while (size < want && size < (1 << 30))
        size *= 2;
    if (size < want)
        return 0;
    bigger = (int16_t *)realloc(e->xx->sampleBuffer,
                                (size_t)size * sizeof(int16_t));
    if (bigger == NULL)
        return 0;
    memset(bigger + e->sampleCapacity, 0,
           (size_t)(size - e->sampleCapacity) * sizeof(int16_t));
    e->xx->sampleBuffer = bigger;
    e->sampleCapacity = size;
    return 1;
}

int vw_ed_frames(vw_editor *e, int count)
{
    int done = 0;
    while (done < count) {
        if (!room_for_a_frame(e))
            break;
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

void vw_ed_oscillator(vw_editor *e, int32_t *phase, int32_t *len,
                      int32_t *pitch)
{
    if (phase != NULL)
        *phase = e->zz->sGlottPhaseIncA;
    if (len != NULL)
        *len = e->zz->sGlottWaveLenA >> 12;     /* it is kept in 12-bit fixed */
    if (pitch != NULL)
        *pitch = e->zz->s_pitchA;
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

static const char *name_of(vw_editor *e, voiceDataPtr rec)
{
    int n;
    if (rec == NULL)
        return NULL;
    n = rec->voiceName[0];                  /* a Pascal string */
    if (n > 15)
        n = 15;
    memcpy(e->voiceName, rec->voiceName + 1, (size_t)n);
    e->voiceName[n] = 0;
    return e->voiceName;
}

int vw_ed_bank(vw_editor *e, const unsigned char *fork, size_t len)
{
    unsigned char *mwav, *mdef;
    size_t mwav_len, mdef_len;
    int rc;

    if (e->hasBank)
        return 0;
    mwav = vw_resource(fork, len, "mwav", 1, &mwav_len);
    mdef = vw_resource(fork, len, "mdef", 1, &mdef_len);
    if (mwav == NULL || mdef == NULL) {
        free(mwav);
        free(mdef);
        return -1;
    }
    rc = vw_bank_load(&e->bank, mwav, mwav_len, mdef, mdef_len);
    free(mwav);
    free(mdef);
    if (rc)
        return rc;
    /* Only the wavetables. `vw_bank_install` would also hand every
       instrument definition to Synth_SetInstrument, and those go into tables
       that Synth_Startup allocates -- the sequencer's, for playing instrument
       tracks. An editor sings, and what singing needs is the wave data the
       voices' oscillators read. */
    rc = SetWaveBank(e->xx, (Ptr)e->bank.waveTable, e->bank.pcmType);
    if (rc) {
        vw_bank_free(&e->bank);
        return rc;
    }
    e->hasBank = 1;
    return 0;
}

int vw_ed_voice_count(vw_editor *e)
{
    return e->voiceCount;
}

int vw_ed_program_voice(vw_editor *e, int program)
{
    if (e->zz->GMVoiceMap == NULL || program < 0 || program > 127)
        return -1;
    return e->zz->GMVoiceMap[program];
}

const char *vw_ed_voice_name_at(vw_editor *e, int index)
{
    if (e->zz->GMVoiceData == NULL || index < 0 || index >= e->voiceCount)
        return NULL;
    return name_of(e, e->zz->GMVoiceData[index]);
}

int vw_ed_has_bank(vw_editor *e)
{
    return e->hasBank && e->xx->Wave_Data != NULL;
}

int vw_ed_voice_needs_bank(vw_editor *e, int index)
{
    voiceDataPtr rec;
    if (e->zz->GMVoiceData == NULL || index < 0 || index >= e->voiceCount)
        return -1;
    rec = e->zz->GMVoiceData[index];
    /* waveType 1 is the sample glottis: the source is a wavetable out of the
       instrument bank rather than the modelled glottal pulse */
    return rec != NULL && rec->waveType == 1;
}

int vw_ed_voice(vw_editor *e, int index)
{
    int needs = vw_ed_voice_needs_bank(e, index);
    if (needs < 0)
        return -1;
    /* InitSampleGlott reads the wave data and the oscillator tables while the
       voice is being selected. Without an instrument bank that is a null
       pointer, and the engine has nothing of its own to stop it. */
    if (needs && !vw_ed_has_bank(e))
        return -2;
    /* what PgmChange_Speech does after the map lookup */
    e->zz->voiceRef = (int16_t)index;
    NewVoice(e->zz, (void *)e->zz->GMVoiceData[index]);
    return 0;
}

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
