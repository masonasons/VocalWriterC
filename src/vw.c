/* vw.c -- the command line: VocalWriter's synthesiser, offline.
 *
 *   vw render SONG.trk OUT.wav [options]      play a VocalWriter song file
 *   vw sing OUT.wav --lyrics "..." --notes "..." [options]
 *                                              sing typed lyrics to notes
 *   vw phonemes WORD...                        the pronunciation of words
 *   vw info SONG.trk                           what a song file holds
 *
 * Options:
 *   --data DIR        where VocalWriter's files are (default: $VW_DATA, or
 *                     ../VocalWriter/assets): VocalWriter.rsrc (or the
 *                     application bundle), GMSpeech.rsrc, GMBank.rsrc,
 *                     EnglishLex
 *   --no-reverb       leave the reverb out
 *   --room N --wet N  the reverb amounts, percent (the song's own otherwise)
 *   --max-seconds S   stop after S seconds
 *   --solo 1,4        play only these tracks (numbers as `vw info` lists them)
 *   --mute 2,3        silence these tracks
 *   --bpm N           tempo for `sing` (default 120)
 *   --voice N         voice for `sing`: the program number (default 0)
 *   --velocity N      note velocity for `sing` (default 114)
 *
 * Lyrics are syllables separated by spaces, one per note; a trailing "-"
 * joins a syllable to the next one of the same word ("Dai- sy"). Notes are
 * "PITCH:BEATS" like "A4:1.5" or "60:0.5"; a rest is "r:BEATS".
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vw_engine.h"
#include "vocalwriter.h"

static const char *g_data;

static int exists(const char *path)
{
    FILE *fh = fopen(path, "rb");
    if (fh == NULL)
        return 0;
    fclose(fh);
    return 1;
}

static char *data_file(const char *name)
{
    static char path[2048];
    const char *dirs[] = {g_data, getenv("VW_DATA"), "../VocalWriter/assets", "assets", "."};
    const char *subs[] = {"", "VocalWriter.app/Contents/Resources/"};
    size_t i, j;
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        if (dirs[i] == NULL)
            continue;
        for (j = 0; j < 2; j++) {
            snprintf(path, sizeof path, "%s/%s%s", dirs[i], subs[j], name);
            if (exists(path))
                return path;
        }
    }
    return NULL;
}

static int open_engine(vw_engine *e, int need_bank, int need_lexicon)
{
    char rsrc[2048], gm[2048], bank[2048], lex[2048];
    char *p;
    int rc;
    p = data_file("VocalWriter.rsrc");
    if (p == NULL) {
        fprintf(stderr, "vw: cannot find VocalWriter.rsrc (use --data DIR)\n");
        return 1;
    }
    strcpy(rsrc, p);
    p = data_file("GMSpeech.rsrc");
    if (p == NULL) {
        fprintf(stderr, "vw: cannot find GMSpeech.rsrc\n");
        return 1;
    }
    strcpy(gm, p);
    p = data_file("GMBank.rsrc");
    strcpy(bank, p ? p : "");
    if (need_bank && !p)
        fprintf(stderr, "vw: GMBank.rsrc not found; instrument tracks will be silent\n");
    p = data_file("EnglishLex");
    strcpy(lex, p ? p : "");
    if (need_lexicon && !p) {
        fprintf(stderr, "vw: cannot find EnglishLex (the dictionary)\n");
        return 1;
    }
    rc = vw_engine_open(e, rsrc, gm, bank[0] ? bank : NULL, lex[0] ? lex : NULL);
    if (rc != VW_OK) {
        fprintf(stderr, "vw: the engine did not start (%d, engine error %d)\n", rc, e->last_error);
        return 1;
    }
    return 0;
}

static int arg_int(int argc, char **argv, const char *name, int dflt)
{
    int i;
    for (i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], name) == 0)
            return atoi(argv[i + 1]);
    return dflt;
}

static const char *arg_str(int argc, char **argv, const char *name)
{
    int i;
    for (i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], name) == 0)
            return argv[i + 1];
    return NULL;
}

static int arg_flag(int argc, char **argv, const char *name)
{
    int i;
    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], name) == 0)
            return 1;
    return 0;
}

static void options(int argc, char **argv, vw_render_options *opt)
{
    memset(opt, 0, sizeof *opt);
    opt->reverb = !arg_flag(argc, argv, "--no-reverb");
    opt->reverbRoom = arg_int(argc, argv, "--room", -1);
    opt->reverbWet = arg_int(argc, argv, "--wet", -1);
    opt->max_seconds = atof(arg_str(argc, argv, "--max-seconds") ? arg_str(argc, argv, "--max-seconds") : "0");
}

static int load_song(vw_song *s, const char *path)
{
    size_t len, rlen = 0;
    unsigned char *trk = vw_read_file(path, &len), *rsrc = NULL;
    char rpath[2048];
    int rc;
    if (trk == NULL) {
        fprintf(stderr, "vw: cannot read %s\n", path);
        return 1;
    }
    snprintf(rpath, sizeof rpath, "%s.rsrc", path);
    rsrc = vw_read_file(rpath, &rlen);
    if (rsrc == NULL) {
        snprintf(rpath, sizeof rpath, "%s/..namedfork/rsrc", path);
        rsrc = vw_read_file(rpath, &rlen);
    }
    rc = vw_song_load(s, trk, len, rsrc, rlen);
    free(trk);
    free(rsrc);
    if (rc != VW_OK) {
        fprintf(stderr, "vw: %s is not a VocalWriter song (%d)\n", path, rc);
        return 1;
    }
    return 0;
}

/* --solo 1,4 plays only those tracks; --mute 2 silences those */
static void apply_track_choice(vw_song *s, const char *solo, const char *mute)
{
    int i;
    if (solo != NULL) {
        for (i = 0; i < 32; i++)
            s->trackPlay[i] = 0;
        while (*solo) {
            int n = atoi(solo);
            if (n >= 0 && n < 32)
                s->trackPlay[n] = 1;
            while (*solo && *solo != ',')
                solo++;
            if (*solo == ',')
                solo++;
        }
    }
    if (mute != NULL) {
        while (*mute) {
            int n = atoi(mute);
            if (n >= 0 && n < 32)
                s->trackPlay[n] = 0;
            while (*mute && *mute != ',')
                mute++;
            if (*mute == ',')
                mute++;
        }
    }
}

static int cmd_render(int argc, char **argv)
{
    vw_engine e;
    vw_song s;
    vw_render_options opt;
    int rc;
    if (argc < 4) {
        fprintf(stderr, "usage: vw render SONG.trk OUT.wav [options]\n");
        return 2;
    }
    if (load_song(&s, argv[2]))
        return 1;
    apply_track_choice(&s, arg_str(argc, argv, "--solo"), arg_str(argc, argv, "--mute"));
    if (open_engine(&e, 1, 0))
        return 1;
    options(argc, argv, &opt);
    rc = vw_render_wav(&e, &s, &opt, argv[3]);
    if (rc != VW_OK)
        fprintf(stderr, "vw: rendering failed (%d, engine error %d)\n", rc, e.last_error);
    else
        printf("wrote %s\n", argv[3]);
    vw_song_free(&s);
    vw_engine_close(&e);
    return rc != VW_OK;
}

/* "A4", "C#3", "Bb2" or a MIDI number -> MIDI note, or -1 for a rest */
static int parse_pitch(const char *s)
{
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7};   /* A B C D E F G */
    int n, oct;
    if (isdigit((unsigned char)s[0]))
        return atoi(s);
    if (s[0] == 'r' || s[0] == 'R' || s[0] == '-')
        return -1;
    if (!isalpha((unsigned char)s[0]))
        return -2;
    n = base[(toupper((unsigned char)s[0]) - 'A') % 7];
    s++;
    if (*s == '#') {
        n++;
        s++;
    } else if (*s == 'b') {
        n--;
        s++;
    }
    oct = atoi(s);
    return (oct + 1) * 12 + n;
}

static int cmd_sing(int argc, char **argv)
{
    vw_engine e;
    vw_song s;
    vw_render_options opt;
    const char *lyrics = arg_str(argc, argv, "--lyrics");
    const char *notes = arg_str(argc, argv, "--notes");
    int bpm = arg_int(argc, argv, "--bpm", 120);
    int voice = arg_int(argc, argv, "--voice", 0);
    int velocity = arg_int(argc, argv, "--velocity", 114);
    char *lyr, *nts, *tok, *save;
    char *syll[512];
    vw_note list[512];
    int nsyll = 0, nnotes = 0, i, rc, tick = 0;

    if (argc < 3 || lyrics == NULL || notes == NULL) {
        fprintf(stderr, "usage: vw sing OUT.wav --lyrics \"...\" --notes \"...\" [options]\n");
        return 2;
    }
    lyr = strdup(lyrics);
    nts = strdup(notes);
    for (tok = strtok_r(lyr, " \t\n", &save); tok && nsyll < 512; tok = strtok_r(NULL, " \t\n", &save))
        syll[nsyll++] = tok;
    /* the notes: rests advance time, pitches take the next syllable */
    for (tok = strtok_r(nts, " \t\n", &save); tok && nnotes < 512; tok = strtok_r(NULL, " \t\n", &save)) {
        char *colon = strchr(tok, ':');
        double beats = 1.0;
        int pitch;
        if (colon) {
            *colon = 0;
            beats = atof(colon + 1);
        }
        pitch = parse_pitch(tok);
        if (pitch == -2) {
            fprintf(stderr, "vw: bad note %s\n", tok);
            return 2;
        }
        if (pitch >= 0) {
            list[nnotes].start = tick;
            list[nnotes].duration = (int32_t)floor(beats * 240 + 0.5);
            list[nnotes].key = (int16_t)pitch;
            list[nnotes].velocity = (int16_t)velocity;
            list[nnotes].lyric = nnotes < nsyll ? syll[nnotes] : "-";
            list[nnotes].phonemes[0] = 0;
            nnotes++;
        }
        tick += (int)floor(beats * 240 + 0.5);
    }
    if (nnotes == 0) {
        fprintf(stderr, "vw: no notes\n");
        return 2;
    }
    if (open_engine(&e, 0, 1))
        return 1;
    /* words to phonemes: a word is a run of syllables joined by "-" */
    for (i = 0; i < nnotes;) {
        char word[64] = "";
        unsigned char sy[10][9];
        int j = i, k, n;
        do {
            const char *t = list[j].lyric;
            size_t tl = strlen(t);
            int cont = tl > 0 && t[tl - 1] == '-';
            strncat(word, t, sizeof word - strlen(word) - 1);
            j++;
            if (!cont)
                break;
        } while (j < nnotes);
        n = vw_word_syllables(&e, word, sy);
        if (n < 0) {
            fprintf(stderr, "vw: cannot convert '%s' (%d)\n", word, n);
            n = 0;
        }
        for (k = i; k < j; k++) {
            int si = k - i;
            if (si < n) {
                memcpy(list[k].phonemes, sy[si], 9);
                /* extra syllables the word has beyond its notes go on the last note */
                if (k == j - 1) {
                    int m;
                    for (m = si + 1; m < n; m++) {
                        int room = 12 - list[k].phonemes[0];
                        int take = sy[m][0] < room ? sy[m][0] : room;
                        memcpy(list[k].phonemes + 1 + list[k].phonemes[0], sy[m] + 1, (size_t)take);
                        list[k].phonemes[0] = (unsigned char)(list[k].phonemes[0] + take);
                    }
                }
            } else if (n > 0) {
                /* more notes than syllables: hold the last vowel, as "=" does */
                list[k].phonemes[0] = 1;
                list[k].phonemes[1] = sy[n - 1][sy[n - 1][0]];
            } else {
                list[k].phonemes[0] = 1;
                list[k].phonemes[1] = 23;        /* a rest */
            }
        }
        i = j;
    }
    rc = vw_song_build(&s, &e, list, nnotes, bpm, voice);
    if (rc == VW_OK) {
        options(argc, argv, &opt);
        rc = vw_render_wav(&e, &s, &opt, argv[2]);
    }
    if (rc != VW_OK)
        fprintf(stderr, "vw: rendering failed (%d, engine error %d)\n", rc, e.last_error);
    else
        printf("wrote %s\n", argv[2]);
    vw_song_free(&s);
    vw_engine_close(&e);
    free(lyr);
    free(nts);
    return rc != VW_OK;
}

static int cmd_phonemes(int argc, char **argv)
{
    vw_engine e;
    int i;
    if (open_engine(&e, 0, 1))
        return 1;
    for (i = 2; i < argc; i++) {
        unsigned char sy[10][9];
        int n, k, p;
        if (argv[i][0] == '-')
            continue;
        n = vw_word_syllables(&e, argv[i], sy);
        printf("%-16s", argv[i]);
        for (k = 0; k < n; k++) {
            printf(k ? " . " : " ");
            for (p = 1; p <= sy[k][0]; p++)
                printf("%s%s", p > 1 ? " " : "", vw_phoneme_name(&e, sy[k][p]));
        }
        printf("\n");
    }
    vw_engine_close(&e);
    return 0;
}

static int cmd_info(int argc, char **argv)
{
    vw_song s;
    int i;
    if (argc < 3 || load_song(&s, argv[2]))
        return 1;
    printf("tempo %d, meter %d/%d, %d ticks per beat, reverb room %d wet %d%s\n",
           s.header.tempo, s.header.bpm, 1 << s.header.beatVal, s.header.ticks,
           s.reverbRoom, s.reverbWet, s.reverbOff ? " (off)" : "");
    for (i = 0; i < 32; i++) {
        TrackInfoPtr t = (TrackInfoPtr)(s.image + i * 88);
        if (t->trackDataLen > 4)
            printf("  track %2d  %-8s %5d bytes  level %3d  %s%.*s\n", i,
                   t->flags & 1 ? "vocal" : t->flags & 0x10 ? "karaoke" : "instr",
                   t->trackDataLen, s.trackLevels[i], s.trackPlay[i] ? "" : "(muted) ",
                   t->trackName[0], t->trackName + 1);
    }
    vw_song_free(&s);
    return 0;
}

int main(int argc, char **argv)
{
    g_data = arg_str(argc, argv, "--data");
    if (argc < 2) {
        fprintf(stderr, "usage: vw render|sing|phonemes|info ... (see the source header)\n");
        return 2;
    }
    if (strcmp(argv[1], "render") == 0)
        return cmd_render(argc, argv);
    if (strcmp(argv[1], "sing") == 0)
        return cmd_sing(argc, argv);
    if (strcmp(argv[1], "phonemes") == 0)
        return cmd_phonemes(argc, argv);
    if (strcmp(argv[1], "info") == 0)
        return cmd_info(argc, argv);
    fprintf(stderr, "vw: unknown command %s\n", argv[1]);
    return 2;
}
