/* VocalWriter 2.0.1 synthesiser, recreated in C.
 *
 * Function names, parameters and struct layouts are the original's, read out
 * of the debug records in the shipped PowerPC binary. The bodies were lifted
 * from its unoptimised machine code and are checked against an interpreter
 * running that code; see test/difftest.py.
 */
#ifndef VW_ENGINE_H
#define VW_ENGINE_H

#include <math.h>
#include <stdint.h>
#include <string.h>
#include "vw_types.h"

/* -- PowerPC arithmetic the C has to reproduce exactly ---------------------- */

/* fctiwz: float -> int, truncating, saturating; NaN gives INT_MIN. */
static inline int32_t vw_ftoi(double x)
{
    if (x != x)
        return (int32_t)0x80000000;
    if (x >= 2147483647.0)
        return 0x7FFFFFFF;
    if (x <= -2147483648.0)
        return (int32_t)0x80000000;
    return (int32_t)x;
}
#define FTOI(x) vw_ftoi(x)

/* mulhw: the high word of a signed 32x32 product. */
static inline int32_t vw_mulhw(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * (int64_t)b) >> 32);
}
#define MULHW(a, b) vw_mulhw((a), (b))

/* a float stored through an integer register, and back */
static inline float vw_bits_f(int32_t v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}
static inline int32_t vw_bits_i(float f)
{
    int32_t v;
    memcpy(&v, &f, 4);
    return v;
}
#define BITS_F(v) vw_bits_f((int32_t)(v))
#define BITS_I(f) vw_bits_i(f)

/* The value SayFrame finds in its uninitialised `cycleIndex` on the first
 * sample of a frame: the stack pointer a callee of e_Fill_Next_Frame saved
 * at that address. On Mac OS X the main thread's stack sits just under
 * 0xC0000000, so as a signed 32-bit word it is negative, and the breath
 * gate `breathCycle > cycleIndex` passes on that first sample. Define it as a
 * positive value to model a thread stack in low memory (the Sound Manager's
 * callback thread), or the interpreter's. */
#ifndef VW_STACK_ADDRESS
#define VW_STACK_ADDRESS ((int32_t)0xBFFFEFB0)
#endif
extern int32_t vw_stack_slot_c0;

/* -- Speech.c ------------------------------------------------------------- */

int16_t e_HzToPitch(formantVarPtr zz, int16_t hz);
int16_t e_MidiToPitch1(int16_t midiNote);
int16_t e_MidiToPitch(int16_t midiNote);
int16_t PitchToHz(formantVarPtr zz, int16_t pitch);
int16_t e_LogToLin(formantVarPtr zz, int16_t logVal);
int16_t LogToLog(formantVarPtr zz, int16_t logVal);
void Calc_Pole_Coefficients(formantVarPtr zz, rShort *Acoeff, rShort *Bcoeff, rShort *Ccoeff, int16_t pitch, int16_t bandWidth);
void Calc_Zero_Coefficients(formantVarPtr zz, rShort *Acoeff, rShort *Bcoeff, rShort *Ccoeff, int16_t pitch, int16_t bandWidth);
void InitFixedFormants(formantVarPtr zz);
void InitSay(formantVarPtr zz);
void Set_SndFreq(formantVarPtr zz, int32_t speechPitch);
void SayFrame(formantVarPtr zz);
void SaveFrame(formantVarPtr zz);
int16_t e_GetPhon(formantVarPtr zz, int16_t index);
int16_t e_GetPhonCtrl(formantVarPtr zz, int16_t index);
void Init_ControlBlocks(formantVarPtr zz);
void Insert_Burst(formantVarPtr zz);
int16_t Adjust_Colored_TargetX(formantVarPtr zz, int16_t index, int16_t entryCount);
int16_t Adjust_Colored_Target(formantVarPtr zz, int16_t index, int16_t entryCount);
void AdjustGain(formantVarPtr zz, int16_t index, int16_t *target_Val);
int16_t GetTarget(formantVarPtr zz, int16_t index);
int16_t Get_FIRST_Target(formantVarPtr zz, int16_t index);
int16_t Get_LAST_Target(formantVarPtr zz, int16_t index);
void Get_Locus(formantVarPtr zz, int16_t i_Consonant, int16_t i_Vowel, int16_t bType);
void Head_Rules(formantVarPtr zz);
void Tail_Rules(formantVarPtr zz);
int16_t Scale_Prcnt_to_PhonDur(formantVarPtr zz, int16_t percent);
void Get_Diphthongs(formantVarPtr zz, int16_t index);
void Fill_Phon_Targets(formantVarPtr zz);
void Init_Ctrls_for_New_Phon(formantVarPtr zz);
void Interpolate_Formants(formantVarPtr zz);
void DoNote(formantVarPtr zz, int16_t note);
void StartNewPhon(formantVarPtr zz);
void Interpolate_Pitch(formantVarPtr zz);
void Syllable_DurationX(formantVarPtr zz, int16_t cur_Index, int16_t total_Dur);
void Syllable_Duration(formantVarPtr zz, int16_t cur_Index, int16_t total_Dur);
void e_Fill_Next_Frame(formantVarPtr zz);
void e_Fill_Next_Frame_MIDI(formantVarPtr zz);
void InvDFT(formantVarPtr zz, voiceDataPtr vd);
void InitVoice(formantVarPtr zz, voiceDataPtr vd);
void ResetVoice(formantVarPtr zz);
int16_t CopyVoice(formantVarPtr zz, Ptr voice);
int16_t NewVoice(formantVarPtr zz, void *vDat);
void PgmChange_Speech(synthVarsPtr xx, int16_t track, int16_t vNum);
void DiffNoiseWave(formantVarPtr zz);
int16_t NewSong_Speech(synthVarsPtr xx, int16_t track, int16_t *trackData);
int16_t InitGlobals_Speech(synthVarsPtr xx, int16_t track);
void Sing_Speech(synthVarsPtr xx, int16_t track, int16_t singState);
void Update_Speech(synthVarsPtr xx, int16_t track);
void New_Update_Speech(synthVarsPtr xx, int16_t track);
void SingMIDI_Speech(synthVarsPtr xx, int16_t track, int16_t note, int16_t vel);
void StartSILPhon(formantVarPtr zz);
void FindStartNucleus(formantVarPtr zz, int32_t nucleusNum);
void Start_Speech(synthVarsPtr xx, int16_t track);
void StartPoint_Speech(synthVarsPtr xx, int16_t track);
void Speech_Note(synthVarsPtr xx, int16_t track, int16_t note, int16_t nextNote, int16_t vel, mFloat dur);
void Speech_PitchBend(synthVarsPtr xx, int16_t track, int32_t amt);
void Speech_Detune(synthVarsPtr xx, int16_t track, int32_t amt);
void Speech_PBSens(synthVarsPtr xx, int16_t track, int32_t amt);
void Speech_Color(synthVarsPtr xx, int16_t track, int32_t amt);
void Speech_VibDepth(synthVarsPtr xx, int16_t track, int32_t amt);
void Speech_VibFreq(synthVarsPtr xx, int16_t track, int32_t amt);
void Speech_Chorus(synthVarsPtr xx, int16_t track, int32_t amt);
void Speech_TrackLevel(synthVarsPtr xx, int16_t track, int32_t amt);
void Speech_Volume(synthVarsPtr xx, int16_t track, int32_t amt);
void Speech_Portamento(synthVarsPtr xx, int16_t track, int32_t amt);
void Speech_Breath(synthVarsPtr xx, int16_t track, int32_t amt);
void Stop_Speech(synthVarsPtr xx, int16_t track);
int16_t State_Speech(synthVarsPtr xx, int16_t track);
void NewTempo_Speech(synthVarsPtr xx, int16_t track);
void NewTempo_SpeechXX(synthVarsPtr xx, int16_t track);
void StartMIDIMode(synthVarsPtr xx, int16_t track);
void StopMIDIMode(synthVarsPtr xx, int16_t track);
void Speech_Noise(synthVarsPtr xx, int16_t track, int32_t amt);

/* the two file-static entry points the driver has to reach */
void vw_InitDefaultVoiceCntrls(formantVarPtr zz);
void vw_SetTotalVolume(formantVarPtr zz);
void vw_SetSeqAddr(formantVarPtr zz, int16_t *speechData);

/* -- Music.c ---------------------------------------------------------------- */

void SetTempo(synthVarsPtr xx, int16_t tempoVal);

/* -- the reverberator (Macintosh.c / Music.c) -------------------------------- */

typedef REVERBCONFIG *LPREVERBCONFIG;
typedef REVERBMOD *LP_REVERBMOD;
extern float g_Reverb_LeftDelay[4], g_Reverb_LeftGain[4];
extern float g_Reverb_RightDelay[4], g_Reverb_RightGain[4];

float DecibelToInternalVol(float flDecibel);
/* allocate the delay lines and work buffers; 0 on success */
int16_t GetReverbMemory(shellVarPtr svv);
void DeleteReverbModules(shellVarPtr svv);
/* delayGain scales the delays (the room), wetGain/dryGain are linear; both
   zero switches the reverb off. The application passes RoomAmt/100,
   WetAmt/100 and 1 - WetAmt/100. Takes effect at the next XferReverbHold. */
int16_t Synth_SetReverb(shellVarPtr svv, float delayGain, float wetGain, float dryGain);
void Reverberator_Init(shellVarPtr svv, LPREVERBCONFIG pReverbConfig);
void XferReverbHold(synthVarsPtr xx);
void ClearReverbModule(LP_REVERBMOD mod);
void ClearReverbHistory(synthVarsPtr xx);
/* reverberate xx->sampleBuffer in place: SoundBufferFrames frames of 220
   stereo pairs, or with onlyOneFrame the last frame written */
int16_t Reverberator_Process(synthVarsPtr xx, int16_t onlyOneFrame);
void Reverb_Demux16(float *psLeft, float *psRight, int16_t *psSource, int32_t dwSamples);
void Reverb_Copy16_16(float *psDest, float *psSource, int32_t dwSamples, float rVolume);
void Reverb_Mix16_16(float *psDest, float *psSource, int32_t dwSamples, float rVolume);

/* -- Macintosh.c: the shared tables ---------------------------------------- */

extern Handle g_DataHandle;
extern int32_t *g_TopOctave;
extern int32_t *g_Freq_Tbl;
extern int16_t *g_Note_Tbl_Def;
extern int16_t *g_NoteDecayTbl;
extern int16_t *g_Time_Tbl;
extern int16_t *g_VG_Scale;
extern int16_t *g_VG_Add;
extern int16_t *g_OscModeTbl;
extern int16_t *g_InitialOscState;
extern int16_t *g_Oct_Tbl;
extern unsigned char *g_SineWavePtr;
extern int16_t *g_velToLinPtr;
extern int16_t *g_GM_Map;
extern int16_t *g_GM_DrumMap;
extern int16_t *g_MidiLengths;
extern char *g_metaNameStr;
extern char *g_trackNameStr;
extern int32_t *g_phonFlags2;
extern int16_t *g_maxDurTbl;
extern int16_t *g_minDurTbl;
extern uint16_t *g_Opcode_To_ASCII;
extern int16_t *g_phonTypeTbl;
extern Ptr g_RulesData;
extern int16_t *g_hash;
extern unsigned char *g_rule;
extern unsigned char *g_kind;
extern unsigned char *g_dashruletab;
extern unsigned char *g_atruletab;
extern unsigned char *g_lruletab;
extern unsigned char *g_mruletab;
extern unsigned char *g_zruletab;
extern unsigned char *g_percentruletab;
extern unsigned char *g_bruletab;
extern unsigned char *g_SuffixTab;
extern int16_t *g_SuffixType;
extern mFloat *g_CosTbl;
extern mFloat *g_BcoeffTbl;
extern mFloat *g_CcoeffTbl;
extern int32_t *g_SpeechTbls;

void InitSharedTables(void);
void Make_F_Table(void);

/* -- loading VocalWriter's data (not in the original: it used the Mac
      Resource Manager) ------------------------------------------------------- */

/* Byte-swap a `ttvi` id 2 blob into native order, in place, and install it
   as the shared data (g_DataHandle). Returns 0 on success. */
int vw_load_ttvi(unsigned char *blob, size_t len);

/* Parse a raw Macintosh resource fork; returns a malloc'd copy of the
   resource with the given type and id, or NULL. */
unsigned char *vw_resource(const unsigned char *fork, size_t len,
                           const char type[4], int id, size_t *out_len);

/* Build the voice bank the engine indexes through GMVoiceMap/GMVoiceData
   from an `mvox` id 1 resource. Returns the block to store in
   synthVars.GMVoicePtr, or NULL. */
Ptr vw_load_voices(const unsigned char *mvox, size_t len, int *count);

/* -- big-endian accesses through byte pointers ----------------------------
   Where the original read a halfword or word through a `char *` (track
   records, the lexicon's packed strings) the port reads the bytes in the
   order the PowerPC did, so the data formats stay what the files hold. */
#define VW_LD16BE(p) ((int16_t)(uint16_t)(((uint16_t)((const uint8_t *)(p))[0] << 8) |                                           ((const uint8_t *)(p))[1]))
#define VW_LD32BE(p) ((int32_t)(((uint32_t)((const uint8_t *)(p))[0] << 24) |                                 ((uint32_t)((const uint8_t *)(p))[1] << 16) |                                 ((uint32_t)((const uint8_t *)(p))[2] << 8) |                                 (uint32_t)((const uint8_t *)(p))[3]))
#define VW_ST16BE(p, v) do { uint16_t vw_v_ = (uint16_t)(v); uint8_t *vw_p_ = (uint8_t *)(p);                              vw_p_[0] = (uint8_t)(vw_v_ >> 8); vw_p_[1] = (uint8_t)vw_v_; } while (0)
#define VW_ST32BE(p, v) do { uint32_t vw_v_ = (uint32_t)(v); uint8_t *vw_p_ = (uint8_t *)(p);                              vw_p_[0] = (uint8_t)(vw_v_ >> 24); vw_p_[1] = (uint8_t)(vw_v_ >> 16);                              vw_p_[2] = (uint8_t)(vw_v_ >> 8); vw_p_[3] = (uint8_t)vw_v_; } while (0)
/* a big-endian word held in a native variable (file headers) */
#define VW_BE32(x) VW_LD32BE(&(x))

/* -- the Mac OS calls the front end makes (src/macshim.c) ----------------- */
Ptr NewPtr(int32_t size);
Ptr NewPtrClear(int32_t size);
void DisposePtr(void *p);            /* Ptr in the original; any block here */
Handle NewHandle(int32_t size);
void SetHandleSize(Handle h, int32_t size);
void DisposeHandle(Handle h);
void HLock(Handle h);
void HUnlock(Handle h);
int16_t MemError(void);
void DebugStr(const char *s);
int16_t SetFPos(int16_t refNum, int16_t posMode, int32_t posOff);
int16_t FSRead(int16_t refNum, int32_t *count, void *buf);
/* the File Manager works over files loaded into memory: returns a refNum */
int16_t vw_fs_open(const unsigned char *data, size_t len);
void vw_fs_close(int16_t refNum);
extern int vw_shim_defer_free;          /* tests: keep freed blocks readable */
extern int vw_shim_fill;                /* tests: NewPtr fill byte, or -1 */
void vw_shim_flush_deferred(void);
extern int vw_shim_overruns;            /* blocks found overrun when freed */

#include "vw_frontend.h"

#endif /* VW_ENGINE_H */
