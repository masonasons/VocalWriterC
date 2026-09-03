/* song.c -- VocalWriter songs: loading .trk files, building them from notes,
 * and playing them through the sequencer into a sample sink.
 *
 * The rendering path is the application's own "Play to Disk": what its
 * main() and StartCurSeq do before Synth_SeqPlayer, then Synth_GetNextBuffer
 * until the sequencer reports the song done. Read off the application
 * binary (tools/xref.py, docs/PORT.md); the engine calls are the lifted
 * originals.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vw_engine.h"
#include "vocalwriter.h"

#define SEQINFO_SIZE 2816          /* 32 TrackInfo records */
#define TRACKINFO_SIZE 88
#define SEQHEADER_SIZE 758
#define SPEECH_RATE 140            /* what StartCurSeq passes MakeSpeechData */
#define POLYPHONY 48               /* what main() gives Synth_Startup */
#define VW_TRACK_VOCAL 1
#define VW_TRACK_KARAOKE 0x10

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t be16(const unsigned char *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
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

static void swap16(unsigned char *p)
{
    unsigned char t = p[0];
    p[0] = p[1];
    p[1] = t;
}

/* the TrackInfo records are read natively by StartSeq: swap them once */
static void swap_trackinfos(unsigned char *image)
{
    int i, k;
    for (i = 0; i < 32; i++) {
        unsigned char *t = image + i * TRACKINFO_SIZE;
        for (k = 0; k < 10; k++)
            swap32(t + 4 * k);                   /* trackNum .. trackChannels */
        swap16(t + 0x28);                        /* midiChannel */
        swap16(t + 0x2a);
        for (k = 0; k < 3; k++)
            swap32(t + 0x2c + 4 * k);
    }
}

static TrackInfoPtr track(vw_song *s, int i)
{
    return (TrackInfoPtr)(s->image + i * TRACKINFO_SIZE);
}

static int reserve(vw_song *s, size_t more)
{
    if (s->image_len + more > s->image_cap) {
        size_t cap = (s->image_len + more) * 2;
        unsigned char *p = (unsigned char *)realloc(s->image, cap);
        if (p == NULL)
            return VW_ERR_MEMORY;
        s->image = p;
        s->image_cap = cap;
    }
    return VW_OK;
}

static int append(vw_song *s, const void *data, size_t n, int32_t *offset)
{
    int err = reserve(s, n);
    if (err)
        return err;
    *offset = (int32_t)s->image_len;
    memcpy(s->image + s->image_len, data, n);
    s->image_len += n;
    return VW_OK;
}

/* -- loading a .trk ---------------------------------------------------- */

static void parse_header(SeqHeader *h, const unsigned char *b)
{
    int i;
    h->filetype = (int32_t)be32(b);
    h->version = (int32_t)be32(b + 4);
    h->seqData = (int32_t)be32(b + 8);
    h->tempo = (int16_t)be16(b + 0xc);
    h->bpm = (int16_t)be16(b + 0xe);
    h->beatVal = (int16_t)be16(b + 0x10);
    h->ticks = (int16_t)be16(b + 0x12);
    h->numOfTracks = (int16_t)be16(b + 0x14);
    for (i = 0; i < 32; i++) {
        h->trackVol[i] = (int16_t)be16(b + 0x16 + 2 * i);
        h->trackPlay[i] = (int16_t)be16(b + 0x56 + 2 * i);
        h->trackMap[i] = (int16_t)be16(b + 0x96 + 2 * i);
    }
    for (i = 0; i < 128; i++)
        h->pgmMap[i] = be16(b + 0xd6 + 2 * i);
    memcpy(h->bankName, b + 0x1d6, 32);
    memcpy(h->Copyright, b + 0x1f6, 256);
}

int vw_song_load(vw_song *s, const unsigned char *trk, size_t len,
                 const unsigned char *rsrc, size_t rsrc_len)
{
    size_t seq;
    int i;

    memset(s, 0, sizeof *s);
    s->reverbRoom = 40;                          /* the application's default preset */
    s->reverbWet = 24;
    if (len < SEQHEADER_SIZE + SEQINFO_SIZE)
        return VW_ERR_FORMAT;
    parse_header(&s->header, trk);
    seq = (size_t)s->header.seqData;
    if (seq < SEQHEADER_SIZE || seq + SEQINFO_SIZE > len)
        return VW_ERR_FORMAT;
    s->image_cap = (len - seq) + 65536;
    s->image = (unsigned char *)malloc(s->image_cap);
    if (s->image == NULL)
        return VW_ERR_MEMORY;
    memcpy(s->image, trk + seq, len - seq);
    s->image_len = len - seq;
    swap_trackinfos(s->image);
    for (i = 0; i < 32; i++) {
        TrackInfoPtr t = track(s, i);
        if (t->trackData < 0 || (size_t)t->trackData + (size_t)(t->trackDataLen > 0 ? t->trackDataLen : 0) > s->image_len)
            return VW_ERR_FORMAT;
        t->speechData = 0;                       /* a stale pointer in the file */
        t->speechDataLen = 0;
        s->trackLevels[i] = s->header.trackVol[i];
        s->trackPlay[i] = s->header.trackPlay[i];
    }
    if (rsrc != NULL) {
        size_t n;
        unsigned char *sdat = vw_resource(rsrc, rsrc_len, "sDat", 1, &n);
        if (sdat != NULL && n >= 0x220) {
            s->reverbRoom = (int16_t)be16(sdat + 0x214);
            s->reverbWet = (int16_t)be16(sdat + 0x218);
            s->reverbOff = (int16_t)be16(sdat + 0x21e);
        }
        free(sdat);
    }
    return VW_OK;
}

void vw_song_free(vw_song *s)
{
    free(s->image);
    memset(s, 0, sizeof *s);
}

/* -- building a song from notes ------------------------------------------ */

static void put24(unsigned char *p, int32_t v)
{
    p[0] = (unsigned char)(v >> 16);
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)v;
}

/* one 12-byte event record, as GetNextTrackEvent reads it */
static void event(unsigned char *r, int32_t time, int status, int key, int vol, int32_t dur, int vocal)
{
    put24(r, time);
    r[3] = 0;
    r[4] = (unsigned char)status;
    r[5] = (unsigned char)key;
    r[6] = (unsigned char)vol;
    put24(r + 7, dur);
    r[10] = (unsigned char)(vocal >> 8);
    r[11] = (unsigned char)vocal;
}

static void set_name(TrackInfoPtr t, const char *name)
{
    size_t n = strlen(name);
    if (n > 31)
        n = 31;
    t->trackName[0] = (char)n;
    memcpy(t->trackName + 1, name, n);
}

int vw_song_build(vw_song *s, vw_engine *e, const vw_note *notes, int count,
                  int bpm, int program)
{
    static const unsigned char terminator[4] = {0xff, 0xff, 0xff, 0xff};
    unsigned char *events, *lyrics;
    int32_t off, lastEnd = 0;
    int i, n = 0, err;
    (void)e;

    memset(s, 0, sizeof *s);
    s->reverbRoom = 40;
    s->reverbWet = 24;
    s->header.filetype = 3;
    s->header.version = 0x10000;
    s->header.seqData = SEQHEADER_SIZE;
    s->header.tempo = (int16_t)bpm;
    s->header.bpm = 4;                           /* the meter: 4/4 */
    s->header.beatVal = 2;
    s->header.ticks = 240;
    s->header.numOfTracks = 32;
    for (i = 0; i < 32; i++) {
        s->header.trackVol[i] = 100;
        s->header.trackPlay[i] = 1;
        s->trackLevels[i] = 100;
        s->trackPlay[i] = 1;
    }
    s->image_cap = SEQINFO_SIZE + (size_t)count * 64 + 4096;
    s->image = (unsigned char *)calloc(1, s->image_cap);
    if (s->image == NULL)
        return VW_ERR_MEMORY;
    s->image_len = SEQINFO_SIZE;
    for (i = 0; i < 32; i++)
        track(s, i)->trackNum = i;

    for (i = 0; i < count; i++)
        if (notes[i].start + notes[i].duration > lastEnd)
            lastEnd = notes[i].start + notes[i].duration;
    /* track 0: the tempo, and the end-of-song mark (status 9) the
       application writes after the last note's rest */
    {
        unsigned char rec[28];
        event(rec, 0, 4, bpm >> 8, bpm & 0xff, 0, 0);
        event(rec + 12, lastEnd + 240, 9, 0, 0, 0, 0);
        memcpy(rec + 24, terminator, 4);
        if ((err = append(s, rec, 28, &off)))
            return err;
        track(s, 0)->trackData = off;
        track(s, 0)->trackDataLen = 28;
        set_name(track(s, 0), "Tempo Track");
    }
    lastEnd = 0;
    /* track 1: the voice -- a program change, then the notes with a rest
       record (status 8, silent, carrying the coming pitch) in every gap */
    events = (unsigned char *)malloc((size_t)(2 * count + 3) * 12 + 4);
    lyrics = (unsigned char *)calloc((size_t)count + 1, 26);
    if (events == NULL || lyrics == NULL) {
        free(events);
        free(lyrics);
        return VW_ERR_MEMORY;
    }
    event(events + 12 * n++, 0, 3, 0, program, 0, 0);
    for (i = 0; i < count; i++) {
        const vw_note *nt = &notes[i];
        size_t tl = strlen(nt->lyric);
        if (lastEnd < nt->start)
            event(events + 12 * n++, lastEnd, 8, nt->key, 0, nt->start - lastEnd, 0);
        event(events + 12 * n++, nt->start, 6, nt->key, nt->velocity, nt->duration, i);
        lastEnd = nt->start + nt->duration;
        if (tl > 12)
            tl = 12;
        lyrics[26 * i] = (unsigned char)tl;
        memcpy(lyrics + 26 * i + 1, nt->lyric, tl);
        memcpy(lyrics + 26 * i + 13, nt->phonemes, 13);
    }
    /* the rest after the last note: the speech data ends with one (GetVocals),
       and the sequencer only finishes a vocal track once the voice has sung it */
    if (count > 0)
        event(events + 12 * n++, lastEnd, 8, notes[count - 1].key, 0, 240, 0);
    memcpy(events + 12 * n, terminator, 4);
    err = append(s, events, (size_t)n * 12 + 4, &off);
    free(events);
    if (err == 0) {
        TrackInfoPtr t = track(s, 1);
        t->trackData = off;
        t->trackDataLen = n * 12 + 4;
        t->flags = VW_TRACK_VOCAL;
        t->lyricWidth = 13;
        t->lyricEvents = count;
        set_name(t, "Voice");
        err = append(s, lyrics, (size_t)count * 26, &off);
        t->lyricData = off;
    }
    free(lyrics);
    if (err)
        return err;
    /* the other tracks: empty */
    for (i = 2; i < 32; i++) {
        if ((err = append(s, terminator, 4, &off)))
            return err;
        track(s, i)->trackData = off;
        track(s, i)->trackDataLen = 4;
    }
    return VW_OK;
}

/* -- rendering ------------------------------------------------------------- */

static int g_done;                               /* set by the sequencer's done callback */

static void on_seq_done(int32_t refCon)
{
    (void)refCon;
    g_done = 1;
}

/* What StartCurSeq does for a vocal track: build its speech data with the
   application's speech rate and hang it off the song. */
static int make_speech(vw_engine *e, vw_song *s, int i)
{
    TrackInfoPtr t = track(s, i);
    Handle h = NewHandle(0);
    int32_t len = 0, off;
    int16_t err;
    if (h == NULL)
        return VW_ERR_MEMORY;
    err = Synth_MakeSpeechData(e->svv, s->image + t->lyricData, s->image + t->trackData,
                               h, &len, SPEECH_RATE);
    if (err != 0) {
        DisposeHandle(h);
        e->last_error = err;
        return VW_ERR_ENGINE;
    }
    if (append(s, *h, (size_t)len, &off)) {
        DisposeHandle(h);
        return VW_ERR_MEMORY;
    }
    DisposeHandle(h);
    t = track(s, i);                             /* the image may have moved */
    t->speechData = off;
    t->speechDataLen = len;
    return VW_OK;
}

int vw_render(vw_engine *e, vw_song *s, const vw_render_options *opt,
              vw_sink sink, void *user)
{
    shellVarPtr svv = e->svv;
    PlayRec pr;
    int i, err;
    int16_t serr;
    double seconds = 0.0;
    int room = s->reverbRoom, wet = s->reverbWet;

    /* the speech channels first (the application gives a vocal track its
       channel when the song is opened): a track's level reaches the voice
       only once the track is a speech track */
    Synth_MakeAllTrackskNotSpeech(svv);
    for (i = 0; i < 32; i++) {
        if (track(s, i)->flags & VW_TRACK_VOCAL) {
            serr = Synth_MakeTrackSpeech(svv, (int16_t)i);
            if (serr != 0) {
                e->last_error = serr;
                return VW_ERR_ENGINE;
            }
        }
    }
    /* the tracks, as SetPlayTracks and StartCurSeq set them */
    Synth_SetKaraokeTrack(svv, -1);
    for (i = 0; i < 32; i++) {
        TrackInfoPtr t = track(s, i);
        if (t->flags & VW_TRACK_KARAOKE) {
            Synth_SetPlayTrack(svv, (int16_t)i, 0);
            Synth_SetKaraokeTrack(svv, (int16_t)i);
        } else {
            int play = s->trackPlay[i] != 0;
            if (!(t->flags & VW_TRACK_VOCAL) && !e->hasBank)
                play = 0;                        /* no instruments without the bank */
            Synth_SetPlayTrack(svv, (int16_t)i, (int16_t)play);
        }
        Synth_TrackToChan(svv, (int16_t)i, -1);
        Synth_SetTrackLevel(svv, (int16_t)i, s->trackLevels[i]);
    }
    for (i = 0; i < 32; i++) {
        if (track(s, i)->flags & VW_TRACK_VOCAL) {
            if ((err = make_speech(e, s, i)))
                return err;
        }
    }
    Synth_SetKbdFlags(svv, 0);

    /* the reverb, as SendReverbParams sends it */
    if (opt->reverbRoom >= 0)
        room = opt->reverbRoom;
    if (opt->reverbWet >= 0)
        wet = opt->reverbWet;
    if (opt->reverb && !s->reverbOff) {
        float delayGain = (float)room / 100.0f;
        float wetGain = (float)wet / 100.0f;
        Synth_SetReverb(svv, delayGain, wetGain, 1.0f - wetGain);
    } else {
        Synth_SetReverb(svv, 0.0f, 0.0f, 0.0f);
    }

    /* a fresh start of the oscillators and buffers, as at launch */
    Synth_StartMusic(svv);

    memset(&pr, 0, sizeof pr);
    pr.PbufStart = (Ptr)s->image;
    pr.theFlags = 1;                             /* play */
    pr.ticksPerBeat = 960 >> s->header.beatVal;
    pr.startBeat = opt->startBeat;
    pr.polyphony = POLYPHONY;
    g_done = 0;
    Synth_SetSeqDoneCB(svv, on_seq_done, 0);
    serr = Synth_SeqPlayer(svv, &pr);
    if (serr != 0) {
        e->last_error = serr;
        return VW_ERR_ENGINE;
    }
    err = VW_OK;
    while (!g_done) {
        Ptr buf;
        int32_t blen;
        Synth_GetNextBuffer(svv, &buf, &blen);
        if (sink(user, (const int16_t *)buf, (size_t)blen / 4))
            break;
        seconds += (double)(blen / 4) / 44100.0;
        if (opt->max_seconds > 0 && seconds >= opt->max_seconds)
            break;
    }
    pr.theFlags = 0;                             /* stop, as StopPlaying does */
    Synth_SeqPlayer(svv, &pr);
    Synth_KillAllNotes(svv);
    Synth_SetSeqDoneCB(svv, NULL, 0);
    return err;
}

/* -- WAV -------------------------------------------------------------------- */

typedef struct {
    FILE *fh;
    uint32_t bytes;
} wav_state;

static void put_le32(FILE *fh, uint32_t v)
{
    unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)};
    fwrite(b, 1, 4, fh);
}

static void put_le16(FILE *fh, uint32_t v)
{
    unsigned char b[2] = {(unsigned char)v, (unsigned char)(v >> 8)};
    fwrite(b, 1, 2, fh);
}

static void wav_header(FILE *fh, uint32_t bytes)
{
    fwrite("RIFF", 1, 4, fh);
    put_le32(fh, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, fh);
    put_le32(fh, 16);
    put_le16(fh, 1);                             /* PCM */
    put_le16(fh, 2);                             /* stereo */
    put_le32(fh, 44100);
    put_le32(fh, 44100 * 4);
    put_le16(fh, 4);
    put_le16(fh, 16);
    fwrite("data", 1, 4, fh);
    put_le32(fh, bytes);
}

static int wav_sink(void *user, const int16_t *samples, size_t frames)
{
    wav_state *w = (wav_state *)user;
    size_t i;
    for (i = 0; i < frames * 2; i++)
        put_le16(w->fh, (uint16_t)samples[i]);
    w->bytes += (uint32_t)(frames * 4);
    return 0;
}

int vw_render_wav(vw_engine *e, vw_song *s, const vw_render_options *opt, const char *path)
{
    wav_state w;
    int err;
    w.fh = fopen(path, "wb");
    w.bytes = 0;
    if (w.fh == NULL)
        return VW_ERR_FILE;
    wav_header(w.fh, 0);
    err = vw_render(e, s, opt, wav_sink, &w);
    fseek(w.fh, 0, SEEK_SET);
    wav_header(w.fh, w.bytes);
    fclose(w.fh);
    return err;
}

/* -- files ------------------------------------------------------------------ */

unsigned char *vw_read_file(const char *path, size_t *len)
{
    FILE *fh = fopen(path, "rb");
    unsigned char *buf;
    long n;
    if (fh == NULL)
        return NULL;
    fseek(fh, 0, SEEK_END);
    n = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    if (n < 0) {
        fclose(fh);
        return NULL;
    }
    buf = (unsigned char *)malloc((size_t)n + 1);
    if (buf == NULL || fread(buf, 1, (size_t)n, fh) != (size_t)n) {
        free(buf);
        fclose(fh);
        return NULL;
    }
    fclose(fh);
    buf[n] = 0;
    *len = (size_t)n;
    return buf;
}
