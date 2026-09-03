/* vw_editor.h -- the engine as a note editor drives it.
 *
 * `vocalwriter.h` renders whole songs the way the application's Play to Disk
 * does: a sequencer, tracks, lyrics fitted to notes. An editor wants the
 * layer under that. It has already chosen the phonemes and how long each one
 * lasts, and it hands the synthesiser one voice's sequence and asks for
 * frames, so that it can put a note in at the moment the engine asks for one,
 * move a control between two frames, and stop when it likes.
 *
 * That is the path the VocalWriter repository's PowerPC interpreter drives,
 * and this is the same calls in the same order, so the two produce the same
 * samples. Nothing here is new engine behaviour: every function below is one
 * of the original's, or a field of its context read by name.
 *
 * One editor is one voice with its own context. Rendering several parts means
 * several editors, or one used again -- the application starts a fresh
 * context per phrase and so does the editor.
 */
#ifndef VW_EDITOR_H
#define VW_EDITOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vw_editor vw_editor;

/* The two resource forks: VocalWriter.rsrc (the engine's `ttvi` tables) and
   GMSpeech.rsrc (the `mvox` voice bank). Both are the whole fork; the
   resources are found inside. NULL if either is unusable. */
vw_editor *vw_ed_open(const unsigned char *rsrc, size_t rsrc_len,
                      const unsigned char *gmspeech, size_t gmspeech_len);
void vw_ed_close(vw_editor *e);

/* -- setting up a phrase -------------------------------------------------- */

/* The scale from a note's length in beats to the engine's own units. The
   application leaves it at 1/240; it is here because the driver sets it. */
void vw_ed_tempo_scale(vw_editor *e, float mul);
void vw_ed_tempo(vw_editor *e, int bpm);
void vw_ed_program(vw_editor *e, int program);

/* The packed phoneme block SetSeqAddr reads: count, then the phoneme codes,
   the control words, a spare word each and the durations, all big-endian, as
   the original's own data is. Copied, so the caller may free it.

   The program change picks the voice and SetSeqAddr would override it from
   the sequence's own voice word, so the voice is put back afterwards -- the
   application does the same. */
int vw_ed_sequence(vw_editor *e, const unsigned char *blob, size_t len);

/* InitSay, Init_ControlBlocks, Start_Speech, Sing_Speech. */
void vw_ed_start(vw_editor *e);

/* InitDefaultVoiceCntrls, and then the glide table if `glide` is nonzero.
   The order matters: the engine's default portamento is read out of that
   table, so wiring it first would put a glide on every note. See the
   VocalWriter repository's ppc/render.py, which explains it at length. */
void vw_ed_defaults(vw_editor *e, int glide);

/* Speech_Volume followed by SetTotalVolume, which is what makes the voiced
   branch audible at all. */
void vw_ed_volume(vw_editor *e, int32_t value);

/* The track level, as a factor rather than as the control's 0 to 100. The
   voices built on wavetables come out of the engine as much as fifty times
   over full scale, and 1 of 100 is not a fine enough division to bring them
   back; this is the same field Speech_TrackLevel writes, without the
   rounding, and the noise gain is recomputed with it as the engine does. */
void vw_ed_level(vw_editor *e, float level);

/* One of the engine's voice controls by its own name: Speech_Color,
   Speech_VibDepth, Speech_VibFreq, Speech_Chorus, Speech_Breath,
   Speech_Detune, Speech_Portamento, Speech_Noise, Speech_PitchBend,
   Speech_PBSens, Speech_TrackLevel. Returns 0, or -1 if the name is not one
   of them. */
int vw_ed_control(vw_editor *e, const char *name, int32_t value);

/* -- rendering ------------------------------------------------------------ */

/* Speech_Note: the pitch, the pitch of the note after it, the velocity, and
   the length in beats. */
void vw_ed_note(vw_editor *e, int key, int nextKey, int velocity, double beats);

/* e_Fill_Next_Frame then SayFrame, `count` times, stopping early when the
   engine finishes or asks for a note. Returns how many frames were run. */
int vw_ed_frames(vw_editor *e, int count);

int vw_ed_state(vw_editor *e);        /* speakState: 3 means it has finished */
int vw_ed_wants_note(vw_editor *e);   /* freezeFrame: it is waiting for one */

/* The samples written so far. They are halfwords in the engine's own
   arrangement -- four at a time as L0 L1 R0 R1, two per channel rather than
   two interleaved frames -- and `vw_ed_wave_index` counts them. */
int32_t vw_ed_wave_index(vw_editor *e);
const int16_t *vw_ed_wave(vw_editor *e);

/* -- the radiation shelf -------------------------------------------------- */

/* The two coefficients of the shelf that tilts the voice's high end, and the
   factor every voiced sample is scaled by. Tilting the shelf changes the
   loudness as well as the tone, so a caller that moves it compensates through
   the volume; these are the fields it needs. `hf_emph` is zero for a voice
   that has no shelf, and then there is nothing to tilt. */
int vw_ed_hf_emph(vw_editor *e);
float vw_ed_emph_a(vw_editor *e);
float vw_ed_emph_b(vw_editor *e);
void vw_ed_set_emph(vw_editor *e, float a, float b);
float vw_ed_speech_volume(vw_editor *e);
void vw_ed_set_speech_volume(vw_editor *e, float v);

/* -- the reverb ----------------------------------------------------------- */

/* The application's own reverb, set the way its own dialog sets it: `room`
   scales the four delay lines (its "room size") and `wet` is how much of the
   reverberated signal is heard, the dry part being 1 - wet. Both 0 to 1;
   VocalWriter's own defaults are 0.40 and 0.24. Wet at zero turns it off.

   Returns 0 when it is on, 1 when the settings leave it off, -1 if the
   reverb's memory could not be had. */
int vw_ed_reverb(vw_editor *e, float room, float wet);

/* Reverberate a block in place: `frames` stereo frames of interleaved 16-bit
   samples, which is the arrangement the engine's own output buffer is in.
   The block is processed 220 frames at a time, as the application processes
   its sound buffers, so `frames` should be a multiple of 220 -- anything left
   over is not touched. Returns 0, or -1 if the reverb is off.

   The delay lines carry over from one call to the next, so a song rendered in
   pieces reverberates as one piece: call it in order and do not interleave two
   songs through one editor. */
int vw_ed_reverberate(vw_editor *e, int16_t *samples, int32_t frames);

/* -- words ---------------------------------------------------------------- */

/* The `EnglishLex` dictionary, as the whole file. It stays the caller's to
   free, and is needed only for looking words up. */
int vw_ed_lexicon(vw_editor *e, const unsigned char *data, size_t len);

/* One word through the application's own text to phonemes: the dictionary
   search, the suffix morphology and the letter-to-sound rules. `out` is
   10 x 9 bytes, a Pascal string of phoneme codes per syllable. Returns the
   number of syllables, 0 for nothing usable, or -1 without a dictionary. */
int vw_ed_word(vw_editor *e, const char *text, unsigned char *out);

/* The application's own two-letter name for a phoneme code, or NULL. There
   are 57 of them, the last being silence. */
const char *vw_ed_phoneme_name(int code);

/* -- what the voice is ---------------------------------------------------- */

/* The name of the voice the current program selected, as the bank has it.
   NULL before a program change. */
const char *vw_ed_voice_name(vw_editor *e);

/* GMBank.rsrc, as the whole fork: the wavetables the voices with instrument
   names are built on -- "special synthetic models of musical instruments with
   dynamic vocal tracts", as the manual puts it, and they sing lyrics like any
   other voice. Without it only the voices that need no wavetable can be
   selected; with it, all of them. Returns 0 or an error. */
int vw_ed_bank(vw_editor *e, const unsigned char *fork, size_t len);

/* GMBank.rsrc, as the whole fork: the wavetables the voices with instrument
   names are built on -- "special synthetic models of musical instruments with
   dynamic vocal tracts", as the manual puts it, and they sing lyrics like any
   other voice. Without it only the voices that need no wavetable can be
   selected; with it, all of them. Returns 0 or an error. */
int vw_ed_bank(vw_editor *e, const unsigned char *fork, size_t len);

/* How many voices the bank holds, and what each is called.

   A program change reaches a voice through the bank's own 128-entry map, and
   that map does not name every voice it holds -- five of GMSpeech's sit in the
   bank with nothing pointing at them. These take the voice's own place in the
   bank instead, so all of them can be sung. */
int vw_ed_voice_count(vw_editor *e);

/* Which voice a program change would pick, for reading a song that was
   written down as program numbers. -1 if there is no such program. */
int vw_ed_program_voice(vw_editor *e, int program);
const char *vw_ed_voice_name_at(vw_editor *e, int index);

/* Sing with the voice at that place in the bank: what PgmChange_Speech does
   once it has looked the program up. Returns 0, or -1 if there is no such
   voice. */
int vw_ed_voice(vw_editor *e, int index);

#ifdef __cplusplus
}
#endif

#endif /* VW_EDITOR_H */
