/* vocalwriter.h -- VocalWriter 2.0.1's synthesiser, recreated in C.
 *
 * The engine underneath is KAE Labs' own code, lifted from the PowerPC
 * binary function by function (see src/speech.c); this header is the
 * library's own face over it. Nothing here makes a sound without the
 * application's data files, which the caller supplies:
 *
 *   VocalWriter.rsrc   the engine's tables (resource `ttvi` id 2)
 *   GMSpeech.rsrc      the voices (`mvox` id 1)
 *   GMBank.rsrc        the instrument bank (`mwav` and `mdef` id 1), for
 *                      songs with instrument tracks; optional
 *   EnglishLex         the pronunciation dictionary, for typed lyrics;
 *                      optional
 *
 * Songs are the application's own .trk files, or are built here from
 * lyrics and notes the way the application builds them when they are
 * typed in. Rendering follows the application's "Play to Disk" export:
 * the sequencer fills sound buffers until the song is over, at 44100 Hz,
 * 16-bit stereo.
 */
#ifndef VOCALWRITER_H
#define VOCALWRITER_H

#include <stddef.h>
#include <stdint.h>
#include "vw_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    VW_OK = 0,
    VW_ERR_FILE = -1,          /* cannot read a file */
    VW_ERR_FORMAT = -2,        /* not the format expected */
    VW_ERR_MEMORY = -3,
    VW_ERR_ENGINE = -4,        /* the engine refused (its own error code follows) */
    VW_ERR_ARGUMENT = -5
};

/* -- data files -------------------------------------------------------- */

typedef struct vw_bank {
    unsigned char *mwav, *mdef;
    size_t mwav_len, mdef_len;
    unsigned char *waveTable;  /* the WaveDef records inside mwav */
    int waveCount;
    int16_t pcmType;
    InstDefPtr instDefs;       /* inside mdef */
    WaveListDefPtr waveLists;
    int instCount;
} vw_bank;

int vw_bank_load(vw_bank *bank, const unsigned char *mwav, size_t mwav_len,
                 const unsigned char *mdef, size_t mdef_len);
void vw_bank_free(vw_bank *bank);
int vw_bank_install(shellVarPtr svv, const vw_bank *bank, Ptr speechVoices);

/* -- the engine ---------------------------------------------------------- */

typedef struct vw_engine {
    shellVarPtr svv;           /* Synth_Startup's shell record */
    synthVarsPtr xx;           /* its channel globals */
    unsigned char *ttvi;
    Ptr voices;                /* the relocated voice bank (GMVoicePtr) */
    int voiceCount;
    vw_bank bank;
    int hasBank;
    unsigned char *lexicon;
    size_t lexicon_len;
    int16_t lexRef;            /* the dictionary's file reference, or 0 */
    int16_t last_error;        /* the engine's own last error code */
} vw_engine;

/* Bring the engine up the way the application does at launch: the shared
   tables, Synth_Startup with the application's polyphony (48), the voice
   bank, the instrument bank and the dictionary. gmbank and lexicon may be
   NULL. Returns VW_OK or an error. */
int vw_engine_open(vw_engine *e, const char *rsrc_path, const char *gmspeech_path,
                   const char *gmbank_path, const char *lexicon_path);
void vw_engine_close(vw_engine *e);

/* -- songs ---------------------------------------------------------------- */

typedef struct vw_song {
    SeqHeader header;          /* native byte order */
    unsigned char *image;      /* the SeqInfo and everything it points at */
    size_t image_len, image_cap;
    int16_t trackLevels[32];
    int16_t trackPlay[32];
    int reverbRoom, reverbWet, reverbOff;   /* the song's settings (SongRez) */
} vw_song;

/* A song from a .trk file (the data fork). The resource fork beside it
   (`<name>.trk.rsrc` or `<name>.trk/..namedfork/rsrc`), if given, supplies
   the reverb settings the application saved with the song. */
int vw_song_load(vw_song *s, const unsigned char *trk, size_t len,
                 const unsigned char *rsrc, size_t rsrc_len);
void vw_song_free(vw_song *s);

/* One sung note of a song built here. */
typedef struct vw_note {
    int32_t start;             /* ticks, 240 per quarter note */
    int32_t duration;          /* ticks */
    int16_t key;               /* MIDI note number */
    int16_t velocity;          /* 1-127 */
    const char *lyric;         /* the syllable's text, "-" joined to the next */
    unsigned char phonemes[13]; /* Pascal string of phoneme codes; [0] = 0 to
                                   look the text up with vw_word_syllables */
} vw_note;

/* A song of one vocal track, as the application builds one from typed
   lyrics: a tempo track with `bpm`, the notes with their syllables, rests
   in the gaps. `program` picks the voice (0-127). */
int vw_song_build(vw_song *s, vw_engine *e, const vw_note *notes, int count,
                  int bpm, int program);

/* -- words --------------------------------------------------------------- */

/* The application's own text-to-phoneme conversion of one word (Synth_ConvertWord):
   the syllables' phoneme strings, as Pascal strings of engine phoneme codes.
   Returns the syllable count, or an error. Needs the dictionary. */
int vw_word_syllables(vw_engine *e, const char *word, unsigned char syllables[10][9]);

/* Engine phoneme code -> its name in the application's phoneme table,
   or NULL. */
const char *vw_phoneme_name(const vw_engine *e, int code);
int vw_phoneme_code(const vw_engine *e, const char *name);

/* -- rendering ------------------------------------------------------------- */

typedef struct vw_render_options {
    int reverb;                /* 1: the reverb, with the song's room/wet */
    int reverbRoom, reverbWet; /* override when >= 0 (percent) */
    double max_seconds;        /* stop after this long (0: when the song ends) */
    int startBeat;
} vw_render_options;

/* Samples arrive interleaved stereo, 16-bit, 44100 Hz. Return nonzero to stop. */
typedef int (*vw_sink)(void *user, const int16_t *samples, size_t frames);

int vw_render(vw_engine *e, vw_song *s, const vw_render_options *opt,
              vw_sink sink, void *user);

/* Render straight to a WAV file. */
int vw_render_wav(vw_engine *e, vw_song *s, const vw_render_options *opt, const char *path);

/* -- files --------------------------------------------------------------- */

unsigned char *vw_read_file(const char *path, size_t *len);

#ifdef __cplusplus
}
#endif

#endif /* VOCALWRITER_H */
