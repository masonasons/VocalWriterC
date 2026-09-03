/* harness.c -- drive the C engine from a command script, dumping its state.
 *
 * test/difftest.py runs the same script through the PowerPC interpreter in
 * the VocalWriter repository and compares the two: every field of the
 * synthesiser context after every snapshot, and every output sample.
 *
 *   harness <VocalWriter.rsrc> <GMSpeech.rsrc> <script> <out-prefix>
 *
 * Script commands, one per line:
 *   tempo BPM             SetTempo
 *   seq HEX               the packed sequence block (big-endian, as the
 *                         original expects), installed with SetSeqAddr
 *   program N             PgmChange_Speech
 *   init                  InitSay, Init_ControlBlocks, Start_Speech,
 *                         Sing_Speech, and the default voice controls
 *   timetbl               point the glide table at the real Time_Tbl
 *   volume N              Speech_Volume then SetTotalVolume
 *   ctrl NAME N           one of the Speech_* controls
 *   note KEY NEXT VEL DUR Speech_Note
 *   frame                 e_Fill_Next_Frame then SayFrame
 *   frames N              N of those
 *   snapshot              append the context to <prefix>.ctx
 *   wave                  write the samples written so far to <prefix>.wav16
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vw_engine.h"
#include "layout.h"

#define OUT_SAMPLES (1 << 21)

static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *fh = fopen(path, "rb");
    unsigned char *buf;
    long n;
    if (fh == NULL)
        return NULL;
    fseek(fh, 0, SEEK_END);
    n = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    buf = (unsigned char *)malloc(n + 1);
    if (buf == NULL || fread(buf, 1, n, fh) != (size_t)n) {
        fclose(fh);
        return NULL;
    }
    fclose(fh);
    *len = (size_t)n;
    return buf;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static unsigned char *unhex(const char *s, size_t *n)
{
    size_t len = 0;
    unsigned char *out;
    while (s[len] && s[len] != ' ' && s[len] != '\n' && s[len] != '\r')
        len++;
    out = (unsigned char *)NewPtr((int32_t)(len / 2 + 1));   /* guarded: overruns are reported */
    for (*n = 0; *n < len / 2; (*n)++)
        out[*n] = (unsigned char)(hexval(s[2 * *n]) * 16 + hexval(s[2 * *n + 1]));
    return out;
}

static char *next_word(char **p)
{
    char *s = *p;
    while (*s == ' ' || *s == '\t')
        s++;
    *p = s;
    while (**p && **p != ' ' && **p != '\t' && **p != '\n' && **p != '\r')
        (*p)++;
    if (**p)
        *(*p)++ = 0;
    return s;
}

static void dump_words(FILE *w, const char *tag, const int16_t *v, int n)
{
    int i;
    fprintf(w, " %s %d:", tag, n);
    for (i = 0; i < n; i++)
        fprintf(w, " %u", (unsigned)(uint16_t)v[i]);
}

static void dump_hex(FILE *w, const unsigned char *v, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        fprintf(w, "%02x", v[i]);
}

int main(int argc, char **argv)
{
    FILE *front = NULL;
    int16_t lexref = 0;
    size_t fork_len, gm_len, ttvi_len, mvox_len;
    unsigned char *fork, *gm, *ttvi, *mvox;
    synthVarsPtr xx;
    formantVarPtr zz;
    int16_t *seq = NULL;
    int nvoices = 0;
    shellVar svv;
    memset(&svv, 0, sizeof svv);
    FILE *script, *ctxout;
    char line[65536];
    char prefix[1024];

    if (argc < 5) {
        fprintf(stderr, "usage: harness VocalWriter.rsrc GMSpeech.rsrc script out-prefix\n");
        return 2;
    }
    fork = read_file(argv[1], &fork_len);
    gm = read_file(argv[2], &gm_len);
    if (fork == NULL || gm == NULL) {
        fprintf(stderr, "cannot read the resource files\n");
        return 1;
    }
    ttvi = vw_resource(fork, fork_len, "ttvi", 2, &ttvi_len);
    mvox = vw_resource(gm, gm_len, "mvox", 1, &mvox_len);
    if (ttvi == NULL || mvox == NULL) {
        fprintf(stderr, "resources missing: ttvi=%p mvox=%p\n", (void *)ttvi, (void *)mvox);
        return 1;
    }
    if (vw_load_ttvi(ttvi, ttvi_len) != 0) {
        fprintf(stderr, "bad ttvi\n");
        return 1;
    }
    InitSharedTables();

    xx = (synthVarsPtr)calloc(1, sizeof(synthVars));
    zz = (formantVarPtr)calloc(1, sizeof(formantVar));
    xx->SpeechTbls = g_SpeechTbls;
    xx->speechVars[0] = zz;
    xx->GMVoicePtr = vw_load_voices(mvox, mvox_len, &nvoices);
    xx->sampleBuffer = (int16_t *)calloc(OUT_SAMPLES * 2, 1);
    /* the interpreter driver gives the engine a blank glide table until the
       defaults are set, so the default portamento is zero; `timetbl` wires
       the real one afterwards */
    xx->Time_Tbl = (int16_t *)calloc(0x400, 1);
    xx->Freq_Tbl = g_Freq_Tbl;
    InitGlobals_Speech(xx, 0);
    /* the front end's tables, as Synth_Startup wires them */
    xx->phonFlags2 = g_phonFlags2;
    xx->maxDurTbl = g_maxDurTbl;
    xx->minDurTbl = g_minDurTbl;
    xx->Opcode_To_ASCII = g_Opcode_To_ASCII;
    xx->phonTypeTbl = g_phonTypeTbl;
    xx->hash = g_hash;
    xx->rule = g_rule;
    xx->kind = g_kind;
    xx->dashruletab = g_dashruletab;
    xx->atruletab = g_atruletab;
    xx->lruletab = g_lruletab;
    xx->mruletab = g_mruletab;
    xx->zruletab = g_zruletab;
    xx->percentruletab = g_percentruletab;
    xx->bruletab = g_bruletab;
    xx->SuffixTab = g_SuffixTab;
    xx->SuffixType = g_SuffixType;

    script = fopen(argv[3], "r");
    if (script == NULL) {
        fprintf(stderr, "cannot read the script\n");
        return 1;
    }
    snprintf(prefix, sizeof prefix, "%s.ctx", argv[4]);
    ctxout = fopen(prefix, "wb");

    while (fgets(line, sizeof line, script)) {
        char cmd[64];
        char *p = line;
        int n;
        if (sscanf(line, "%63s%n", cmd, &n) != 1)
            continue;
        p += n;
        if (strcmp(cmd, "tempo") == 0) {
            int bpm = atoi(p);
            xx->tempoMul = 1.0f / 240.0f;   /* TEMPO_SCALE in ppc/render.py */
            SetTempo(xx, (int16_t)bpm);
        } else if (strcmp(cmd, "tempomul") == 0) {
            xx->tempoMul = (float)atof(p);
        } else if (strcmp(cmd, "seq") == 0) {
            size_t k = 0, cap;
            int16_t voice;
            while (*p == ' ')
                p++;
            cap = strlen(p) / 4 + 2;
            seq = (int16_t *)calloc(cap, sizeof(int16_t));
            while (hexval(p[0]) >= 0 && hexval(p[1]) >= 0 && hexval(p[2]) >= 0 && hexval(p[3]) >= 0) {
                seq[k++] = (int16_t)((hexval(p[0]) << 12) | (hexval(p[1]) << 8) |
                                     (hexval(p[2]) << 4) | hexval(p[3]));
                p += 4;
            }
            /* PgmChange picks the voice; SetSeqAddr would override it from the
               sequence's own voice key, so put it back afterwards */
            voice = zz->voiceRef;
            vw_SetSeqAddr(zz, seq);
            zz->voiceRef = voice;
        } else if (strcmp(cmd, "program") == 0) {
            PgmChange_Speech(xx, 0, (int16_t)atoi(p));
        } else if (strcmp(cmd, "init") == 0) {
            InitSay(zz);
            Init_ControlBlocks(zz);
            Start_Speech(xx, 0);
            Sing_Speech(xx, 0, 1);
            vw_InitDefaultVoiceCntrls(zz);
        } else if (strcmp(cmd, "timetbl") == 0) {
            zz->Time_Tbl = g_Time_Tbl;
        } else if (strcmp(cmd, "volume") == 0) {
            Speech_Volume(xx, 0, atoi(p));
            vw_SetTotalVolume(zz);
        } else if (strcmp(cmd, "ctrl") == 0) {
            char name[64];
            long v;
            if (sscanf(p, "%63s %ld", name, &v) == 2) {
                if (strcmp(name, "Speech_Color") == 0) Speech_Color(xx, 0, (int32_t)v);
                else if (strcmp(name, "Speech_VibDepth") == 0) Speech_VibDepth(xx, 0, (int32_t)v);
                else if (strcmp(name, "Speech_VibFreq") == 0) Speech_VibFreq(xx, 0, (int32_t)v);
                else if (strcmp(name, "Speech_Chorus") == 0) Speech_Chorus(xx, 0, (int32_t)v);
                else if (strcmp(name, "Speech_Breath") == 0) Speech_Breath(xx, 0, (int32_t)v);
                else if (strcmp(name, "Speech_Detune") == 0) Speech_Detune(xx, 0, (int32_t)v);
                else if (strcmp(name, "Speech_Portamento") == 0) Speech_Portamento(xx, 0, (int32_t)v);
                else if (strcmp(name, "Speech_PitchBend") == 0) Speech_PitchBend(xx, 0, (int32_t)v);
                else if (strcmp(name, "Speech_PBSens") == 0) Speech_PBSens(xx, 0, (int32_t)v);
                else if (strcmp(name, "Speech_Noise") == 0) Speech_Noise(xx, 0, (int32_t)v);
                else if (strcmp(name, "Speech_TrackLevel") == 0) Speech_TrackLevel(xx, 0, (int32_t)v);
                else fprintf(stderr, "unknown control %s\n", name);
            }
        } else if (strcmp(cmd, "note") == 0) {
            int key, next, vel;
            double dur;
            if (sscanf(p, "%d %d %d %lf", &key, &next, &vel, &dur) == 4)
                Speech_Note(xx, 0, (int16_t)key, (int16_t)next, (int16_t)vel, (mFloat)dur);
        } else if (strcmp(cmd, "frame") == 0 || strcmp(cmd, "frames") == 0) {
            int count = cmd[5] ? atoi(p) : 1;
            while (count-- > 0) {
                e_Fill_Next_Frame(zz);
                SayFrame(zz);
            }
        } else if (strcmp(cmd, "snapshot") == 0) {
            int32_t extra[3];
            fwrite(zz, 1, sizeof(formantVar), ctxout);
            extra[0] = xx->maxSampleL;
            extra[1] = xx->maxSampleR;
            extra[2] = (int32_t)xx->tempoBPM;
            fwrite(extra, 4, 3, ctxout);
        } else if (strcmp(cmd, "reverb") == 0) {
            /* reverb DELAYGAIN WETGAIN DRYGAIN: the app's Synth_SetReverb */
            double d, w, dr;
            if (sscanf(p, "%lf %lf %lf", &d, &w, &dr) == 3) {
                if (svv.ChannelGlobals == NULL) {
                    svv.ChannelGlobals = xx;
                    svv.reverbEnabled = (GetReverbMemory(&svv) == 0);
                }
                Synth_SetReverb(&svv, (float)d, (float)w, (float)dr);
                XferReverbHold(xx);
            }
        } else if (strcmp(cmd, "reverbrun") == 0) {
            /* the output stage: reverberate everything rendered so far */
            int frames = atoi(p);
            xx->SoundBufferFrames = frames > 0 ? frames : zz->waveIndex / 440;
            if (xx->reverbON)
                Reverberator_Process(xx, 0);
        } else if (strcmp(cmd, "reverbdump") == 0) {
            /* the delay lines, for comparison: size, in, out offsets, samples */
            char path[1024];
            FILE *w;
            int i;
            snprintf(path, sizeof path, "%s.rvb", argv[4]);
            w = fopen(path, "wb");
            for (i = 0; i < 8; i++) {
                REVERBMOD *m = i < 4 ? &xx->m_LEFT_Mods[i] : &xx->m_RIGHT_Mods[i - 4];
                int32_t hdr[3];
                hdr[0] = m->m_dwDelayBufferSize;
                hdr[1] = (int32_t)(m->m_psDelayIn - m->m_psDelayBuffer);
                hdr[2] = (int32_t)(m->m_psDelayOut - m->m_psDelayBuffer);
                fwrite(hdr, 4, 3, w);
                fwrite(m->m_psDelayBuffer, 4, (size_t)m->m_dwDelayBufferSize, w);
            }
            fclose(w);
        } else if (strcmp(cmd, "layout") == 0) {
            char path[1024];
            FILE *w;
            const vw_field *f;
            snprintf(path, sizeof path, "%s.layout", argv[4]);
            w = fopen(path, "w");
            fprintf(w, "sizeof %u\n", (unsigned)sizeof(formantVar));
            for (f = vw_formantVar_fields; f->name; f++)
                fprintf(w, "%s %u %u %s %u\n", f->name, f->guest_off,
                        (unsigned)f->host_off, f->kind, f->count);
            fclose(w);
        } else if (strcmp(cmd, "wave") == 0) {
            char path[1024];
            FILE *w;
            snprintf(path, sizeof path, "%s.wav16", argv[4]);
            w = fopen(path, "wb");
            fwrite(xx->sampleBuffer, 2, (size_t)zz->waveIndex, w);
            fclose(w);
        } else if (strcmp(cmd, "front") == 0) {
            char path[1024];
            snprintf(path, sizeof path, "%s.front", argv[4]);
            front = fopen(path, "w");
        } else if (strcmp(cmd, "fill") == 0) {
            vw_shim_fill = atoi(p);
        } else if (strcmp(cmd, "lexicon") == 0) {
            size_t n;
            unsigned char *lex = read_file(next_word(&p), &n);
            if (lex == NULL) {
                fprintf(stderr, "cannot read the lexicon\n");
                return 1;
            }
            lexref = vw_fs_open(lex, n);
        } else if (strcmp(cmd, "word") == 0) {
            ConvertTextRec tRec;
            char *text = next_word(&p);
            size_t n = strlen(text);
            int16_t err;
            int32_t s, k;
            memset(&tRec, 0, sizeof tRec);
            if (n > 16)
                n = 16;
            memcpy(tRec.text_Input, text, n);
            tRec.textLen_Input = (int32_t)n;
            vw_shim_defer_free = 1;
            err = OrthToPhon(xx, &tRec, lexref);
            fprintf(front, "word %s err %d syll %d:", text, err, (int)tRec.syllables_Result);
            for (s = 0; s < tRec.syllables_Result && s < 10; s++) {
                fprintf(front, " %d:", (int)tRec.phon_Result[s].syllLen);
                for (k = 0; k < tRec.phon_Result[s].syllLen && k < 8; k++)
                    fprintf(front, "%s%u", k ? "." : "", tRec.phon_Result[s].syllStr[k]);
            }
            dump_words(front, "src", (const int16_t *)xx->srcParseBuf, (int)xx->srcParseLen);
            dump_words(front, "phon2", xx->phon_Buf_2, xx->phonBuf_2_In_Index);
            dump_words(front, "ctrl2", xx->phon_Ctrl_Buf_2, xx->phonBuf_2_In_Index);
            fprintf(front, "\n");
            vw_shim_flush_deferred();
            vw_shim_defer_free = 0;
        } else if (strcmp(cmd, "speech") == 0) {
            int rate = atoi(next_word(&p));
            size_t vn, tn;
            unsigned char *vocals = unhex(next_word(&p), &vn);
            unsigned char *track = unhex(next_word(&p), &tn);
            Handle h = NewHandle(0);
            int32_t len = -1, i;
            int16_t err = MakeSpeechData(xx, vocals, track, h, &len, rate);
            fprintf(front, "speech err %d len %d data:", err, (int)len);
            for (i = 0; i < len / 2; i++)
                fprintf(front, " %u", (unsigned)((uint16_t *)*h)[i]);
            fprintf(front, "\n");
            DisposeHandle(h);
            DisposePtr(vocals);
            DisposePtr(track);
        } else if (strcmp(cmd, "recode") == 0) {
            int start = atoi(next_word(&p));
            int end = atoi(next_word(&p));
            int flags = atoi(next_word(&p));
            size_t vn, tn;
            unsigned char *vocals = unhex(next_word(&p), &vn);
            unsigned char *track = unhex(next_word(&p), &tn);
            int16_t err = AdjustBoundryPhons(xx, vocals, track, start, end, flags);
            fprintf(front, "recode err %d vocals ", err);
            dump_hex(front, vocals, vn);
            fprintf(front, "\n");
            DisposePtr(vocals);
            DisposePtr(track);
        } else if (cmd[0] != '#') {
            fprintf(stderr, "unknown command %s\n", cmd);
        }
    }
    fclose(ctxout);
    if (front != NULL)
        fclose(front);
    return 0;
}
