/* Speech.c -- VocalWriter 2.0.1's Resonant Articulatory Synthesis engine.
 *
 * Recreated in C from the shipped PowerPC binary (KAE Labs, 2005). The
 * function and variable names are the original's, from its debug records;
 * the bodies were lifted from its unoptimised machine code by tools/lift.py
 * and are verified statement by statement against an interpreter running
 * the original code. Comments of the form Speech.c:NNN give the line in the
 * original source each function started on.
 *
 * Arithmetic is kept in the width PowerPC used: float where the original
 * used fmuls/fadds/fdivs, double where it used the double forms, with the
 * conversions the compiler inserted. Compile with -ffp-contract=off and
 * -fwrapv so the C compiler does not re-associate or fuse it.
 */
#include "vw_engine.h"

/* One stack slot of the original, modelled. SayFrame reads its local
 * `cycleIndex` on the first sample of every frame before it has assigned it,
 * so what it gets is whatever the previous call at that stack depth left
 * there: the saved stack pointer stored by StartNewPhon,
 * Init_Ctrls_for_New_Phon or SaveFrame (all called just before it, all with
 * frames that land their back chain on that word), or its own final value
 * from the previous frame when none of them ran. See VW_STACK_ADDRESS. */
int32_t vw_stack_slot_c0 = VW_STACK_ADDRESS;

/* Speech.c:62  (0x9376c) */
int16_t e_HzToPitch(formantVarPtr zz, int16_t hz)
{
    int32_t ratio;
    int16_t fk;
    int16_t note;
    int16_t freq;

    note = 0;
    if (hz <= 0) {
        return note;
    }
    if (hz <= 99) {
        freq = hz << 3;
        fk = 0;
    } else if (hz <= 199) {
        freq = hz << 2;
        fk = 256;
    } else if (hz <= 399) {
        freq = hz << 1;
        fk = 512;
    } else if (hz <= 799) {
        freq = hz;
        fk = 768;
    } else if (hz <= 1599) {
        freq = hz >> 1;
        fk = 1024;
    } else if (hz <= 3199) {
        freq = hz >> 2;
        fk = 1280;
    } else if (hz <= 6399) {
        freq = hz >> 3;
        fk = 1536;
    } else {
        freq = hz >> 4;
        fk = 1792;
    }
    ratio = (freq * 2621 - 1048400) >> 11;
    note = zz->logOf2Tbl[ratio] + fk;
    return note;
}

/* Speech.c:129  (0x93950) */
int16_t e_MidiToPitch1(int16_t midiNote)
{
    int32_t pitch;

    pitch = (midiNote * 5461 + 0x8000) >> 16;
    return (int16_t)pitch;
}

/* Speech.c:142  (0x939ac) */
int16_t e_MidiToPitch(int16_t midiNote)
{
    int32_t pitch;
    int32_t midiFixed;

    midiFixed = midiNote << 8;
    if (midiNote <= 4952) {
        midiNote = 0;
    } else {
        midiNote -= 4953;
    }
    pitch = (midiNote * 5461 + 0x8000) >> 16;
    return (int16_t)pitch;
}

/* Speech.c:182  (0x93a40) */
int16_t PitchToHz(formantVarPtr zz, int16_t pitch)
{
    int16_t freq;

    freq = (zz->OctFreqTbl[(pitch & 3840) >> 8] * zz->ExpOf2Tbl[(uint8_t)pitch]) >> 15;
    return freq;
}

/* Speech.c:193  (0x93ae0) */
int16_t e_LogToLin(formantVarPtr zz, int16_t logVal)
{
    int16_t linVal;

    if (logVal > 63) {
        logVal = 63;
    }
    linVal = zz->logToLinPtr[logVal >> 1];
    return linVal;
}

/* Speech.c:206  (0x93b64) */
int16_t LogToLog(formantVarPtr zz, int16_t logVal)
{
    if (logVal <= 63) {
        return (int16_t)(logVal >> 1);
    }
    logVal = 63;
    return (int16_t)(logVal >> 1);
}

/* Speech.c:220  (0x93bc8) */
void Calc_Pole_Coefficients(formantVarPtr zz, rShort *Acoeff, rShort *Bcoeff, rShort *Ccoeff, int16_t pitch, int16_t bandWidth)
{
    int16_t bwIndex;
    rShort cosVal;

    if (bandWidth > 1225) {
        bandWidth = 1225;
    }
    if (bandWidth < zz->voiceMinBW) {
        bandWidth = zz->voiceMinBW;
    }
    if (pitch <= 0xff) {
        pitch = 256;
    }
    bwIndex = (bandWidth - 50) / 5;
    (*Ccoeff) = zz->CcoeffTblPtr[bwIndex];
    cosVal = zz->CosTblPtr[pitch - 256];
    (*Bcoeff) = zz->BcoeffTblPtr[bwIndex] * cosVal;
    (*Acoeff) = (float)(1.0 - *Bcoeff - *Ccoeff);
}

/* Speech.c:242  (0x93d48) */
void Calc_Zero_Coefficients(formantVarPtr zz, rShort *Acoeff, rShort *Bcoeff, rShort *Ccoeff, int16_t pitch, int16_t bandWidth)
{
    int16_t bwIndex;
    rShort cosVal;

    if (bandWidth > 1225) {
        bandWidth = 1225;
    }
    bwIndex = (bandWidth - 50) / 5;
    (*Ccoeff) = zz->CcoeffTblPtr[bwIndex];
    cosVal = zz->CosTblPtr[pitch - 256];
    (*Bcoeff) = zz->BcoeffTblPtr[bwIndex] * cosVal;
    (*Bcoeff) = 0.0f - *Bcoeff;
    (*Ccoeff) = 0.0f - *Ccoeff;
    (*Acoeff) = (float)(*Bcoeff + 1.0 + *Ccoeff);
}

/* Speech.c:263  (0x93ec0) */
void InitFixedFormants(formantVarPtr zz)
{
    Calc_Pole_Coefficients(zz, &zz->Acoeff4, &zz->Bcoeff4, &zz->Ccoeff4, zz->voice_F4_Freq, zz->voice_F4_BW);
    Calc_Pole_Coefficients(zz, &zz->Acoeff5x, &zz->Bcoeff5x, &zz->Ccoeff5x, zz->voice_F5_Freq, zz->voice_F5_BW);
    Calc_Pole_Coefficients(zz, &zz->Acoeff4p, &zz->Bcoeff4p, &zz->Ccoeff4p, zz->f4_Par, zz->bw4_Par);
    zz->Acoeff4p = (float)(zz->Acoeff4p * 0.4);
    Calc_Pole_Coefficients(zz, &zz->Acoeff5, &zz->Bcoeff5, &zz->Ccoeff5, zz->f5_Par, zz->bw5_Par);
    zz->Acoeff5 = (float)(zz->Acoeff5 * 0.4);
    Calc_Pole_Coefficients(zz, &zz->Acoeff6, &zz->Bcoeff6, &zz->Ccoeff6, zz->f6_Par, zz->bw6_Par);
    zz->Acoeff6 = (float)(zz->Acoeff6 * 0.4);
    Calc_Pole_Coefficients(zz, &zz->AcoeffNP, &zz->BcoeffNP, &zz->CcoeffNP, zz->fNP, zz->bNP);
}

/* Speech.c:287  (0x94148) */
void InitSay(formantVarPtr zz)
{
    int16_t i;

    zz->waveIndex = 0;
    zz->glotIndex = 0;
    zz->glotIndex1 = 0x7f0000;
    zz->noiseIndex = 0;
    zz->Na1 = 0.0f;
    zz->Nb1 = 0.0f;
    zz->Na2 = 0.0f;
    zz->Nb2 = 0.0f;
    zz->Na3 = 0.0f;
    zz->Nb3 = 0.0f;
    zz->Na4 = 0.0f;
    zz->Nb4 = 0.0f;
    zz->Na5 = 0.0f;
    zz->Nb5 = 0.0f;
    zz->Na6 = 0.0f;
    zz->Nb6 = 0.0f;
    zz->Na2a = 0.0f;
    zz->Nb2a = 0.0f;
    zz->Na3a = 0.0f;
    zz->Nb3a = 0.0f;
    zz->Na4a = 0.0f;
    zz->Nb4a = 0.0f;
    zz->NaNP = 0.0f;
    zz->NbNP = 0.0f;
    zz->NaNZ = 0.0f;
    zz->NbNZ = 0.0f;
    zz->Acoeff1 = 0.0f;
    zz->Nx5 = 0.0f;
    zz->Ny5 = 0.0f;
    zz->lastnSampL = 0.0f;
    zz->lastnSampR = 0.0f;
    zz->lastAmp = 0.0f;
    zz->lastSample = 0.0f;
    zz->lastAcoeff1 = 0.0f;
    zz->lastBcoeff1 = 0.0f;
    zz->lastCcoeff1 = 0.0f;
    zz->lastAcoeff2 = 0.0f;
    zz->lastBcoeff2 = 0.0f;
    zz->lastCcoeff2 = 0.0f;
    zz->lastAcoeff3 = 0.0f;
    zz->lastBcoeff3 = 0.0f;
    zz->lastCcoeff3 = 0.0f;
}

/* Speech.c:346  (0x94370) */
static void InitSampOsc(formantVarPtr zz)
{
    if (zz->glotType != 1) {
        return;
    }
    zz->sGlottState = 0;
    zz->sGlottInSwap = 0;
    zz->sGlottAccumulator = 0;
    zz->sGlottModeA = zz->s_oscConfigA;
    zz->sGlottDirA = 1;
    zz->sGlottWaveAddrA = zz->s_waveAddrA;
    zz->sGlottWaveLenA = zz->s_waveSizeA;
    zz->sGlottVolumeA = zz->s_waveVolA * zz->wavesampleGain;
    zz->sGlottModeB = zz->s_oscConfigB;
    zz->sGlottWaveAddrB = zz->s_waveAddrB;
    zz->sGlottWaveLenB = zz->s_waveSizeB;
    zz->sGlottVolumeB = zz->s_waveVolB * zz->wavesampleGain;
}

/* Speech.c:369  (0x94484) */
void Set_SndFreq(formantVarPtr zz, int32_t speechPitch)
{
    int32_t octFrac;
    synthVarsPtr xx;

    xx = (synthVarsPtr)zz->musicVars;
    speechPitch += 413;
    octFrac = (uint8_t)speechPitch;
    speechPitch = (speechPitch >> 8) * 384 + ((octFrac >> 1) + octFrac);
    if (zz->sGlottInSwap != 0) {
        zz->sGlottPhaseIncB = xx->Freq_Tbl[zz->s_pitchB + speechPitch];
        zz->sGlottPhaseIncA = zz->sGlottPhaseIncB;
        return;
    }
    zz->sGlottPhaseIncB = xx->Freq_Tbl[zz->s_pitchB + speechPitch];
    zz->sGlottPhaseIncA = xx->Freq_Tbl[zz->s_pitchA + speechPitch];
}

/* Speech.c:404  (0x945b4) */
void SayFrame(formantVarPtr zz)
{
    FramePtr framePtr;
    rShort vPulse;
    rShort vPulse1;
    rShort asperation;
    rShort breath;
    rShort totalBreathGain;
    rLong nGain;
    int32_t cycleIndex;
    int32_t sampleIndex;
    rShort sourceC;
    rShort sourceP;
    rShort SampV;
    rShort nPulse;
    rShort SampAB;
    rShort Samp2;
    rShort Samp3;
    rShort Samp4;
    rShort Samp5;
    rShort Samp6;
    rShort Samp;
    rShort nSamp;
    rShort nSampL;
    rShort nSampR;
    rUSShort wByte;
    int16_t sampCtr;
    rShort tSamp;
    int16_t curF0Pitch;
    int16_t tempP;
    int16_t noNasal;
    int16_t vowel;
    rShort Acoeff2q;
    rShort Acoeff3q;
    rShort Acoeff4q;
    rShort Acoeff5q;
    rShort Acoeff6q;
    int16_t ampCtr;
    int16_t ampBank;
    synthVarsPtr xx;
    int32_t maxSampleL;
    int32_t maxSampleR;
    int16_t *waveAddr;
    int32_t sample_L;
    int32_t local_cur_PhonFlags_CF;
    int16_t *local_sampleBuffer;
    mFloat breathEnv;
    int16_t t_150;

    cycleIndex = vw_stack_slot_c0;           /* what the stack held */
    xx = (synthVarsPtr)zz->musicVars;
    local_cur_PhonFlags_CF = zz->cur_PhonFlags_CF;
    local_sampleBuffer = xx->sampleBuffer;
    local_sampleBuffer += zz->waveIndex;
    maxSampleL = xx->maxSampleL;
    maxSampleR = xx->maxSampleR;
    if (zz->curFrameBuf == 0) {
        framePtr = &zz->frameBuf2;
    } else {
        framePtr = &zz->frameBuf1;
    }
    if (!(zz->curAmp != 0.0f || zz->Af != 0.0f)) {
        zz->glotIndex = 0;
        zz->glotIndex1 = 0x7f0000;
        zz->Na1 = 0.0f;
        zz->Nb1 = 0.0f;
        zz->Na2 = 0.0f;
        zz->Nb2 = 0.0f;
        zz->Na3 = 0.0f;
        zz->Nb3 = 0.0f;
        zz->Na4 = 0.0f;
        zz->Nb4 = 0.0f;
        zz->NaNP = 0.0f;
        zz->NbNP = 0.0f;
        zz->NaNZ = 0.0f;
        zz->NbNZ = 0.0f;
        zz->lastAmp = 0.0f;
        zz->Nx5 = 0.0f;
        zz->Ny5 = 0.0f;
    }
    Calc_Pole_Coefficients(zz, &zz->Acoeff1, &zz->Bcoeff1, &zz->Ccoeff1, framePtr->f1 + framePtr->f1Gain, framePtr->bw1);
    zz->Acoeff1Step = (zz->Acoeff1 - zz->lastAcoeff1) / 128.0f;
    zz->Bcoeff1Step = (zz->Bcoeff1 - zz->lastBcoeff1) / 128.0f;
    zz->Ccoeff1Step = (zz->Ccoeff1 - zz->lastCcoeff1) / 128.0f;
    zz->CoeffCntr = 0;
    Calc_Pole_Coefficients(zz, &zz->Acoeff2, &zz->Bcoeff2, &zz->Ccoeff2, framePtr->f2 + framePtr->f2Gain, framePtr->bw2);
    zz->Acoeff2Step = (zz->Acoeff2 - zz->lastAcoeff2) / 128.0f;
    zz->Bcoeff2Step = (zz->Bcoeff2 - zz->lastBcoeff2) / 128.0f;
    zz->Ccoeff2Step = (zz->Ccoeff2 - zz->lastCcoeff2) / 128.0f;
    Calc_Pole_Coefficients(zz, &zz->Acoeff3, &zz->Bcoeff3, &zz->Ccoeff3, framePtr->f3 + framePtr->f3Gain, framePtr->bw3);
    zz->Acoeff3Step = (zz->Acoeff3 - zz->lastAcoeff3) / 128.0f;
    zz->Bcoeff3Step = (zz->Bcoeff3 - zz->lastBcoeff3) / 128.0f;
    zz->Ccoeff3Step = (zz->Ccoeff3 - zz->lastCcoeff3) / 128.0f;
    if (framePtr->FNZ != zz->fNP) {
        noNasal = 0;
        Calc_Zero_Coefficients(zz, &zz->AcoeffNZ, &zz->BcoeffNZ, &zz->CcoeffNZ, framePtr->FNZ + zz->nasalAmt, zz->bNP);
        nGain = zz->AcoeffNP / zz->AcoeffNZ;
    } else {
        noNasal = 1;
        nGain = 0.0f;
    }
    ampBank = 0;
    zz->Av = (float)framePtr->Av / 32.0f * zz->speechVolume;
    zz->Af = (float)framePtr->Af / 8.0f * zz->speechVolume;
    zz->ab = (float)framePtr->AB / 32.0f * zz->speechVolume;
    if (!(!(zz->Af > 0.0f) && !(zz->ab > 0.0f))) {
        ampBank = 1;
    }
    if (framePtr->a2 != 0) {
        zz->amp2 = (float)framePtr->a2 / 32.0f;
        Acoeff2q = zz->Acoeff2 * zz->amp2;
        ampBank = 1;
    } else {
        zz->amp2 = 0.0f;
        zz->Nb2a = 0.0f;
        zz->Na2a = 0.0f;
    }
    if (framePtr->a3 != 0) {
        zz->amp3 = (float)framePtr->a3 / 32.0f;
        Acoeff3q = zz->Acoeff3 * zz->amp3;
        ampBank = 1;
    } else {
        zz->amp3 = 0.0f;
        zz->Nb3a = 0.0f;
        zz->Na3a = 0.0f;
    }
    if (framePtr->a4 != 0) {
        zz->amp4 = (float)framePtr->a4 / 32.0f;
        Acoeff4q = zz->Acoeff4p * zz->amp4;
        ampBank = 1;
    } else {
        zz->amp4 = 0.0f;
        zz->Nb4a = 0.0f;
        zz->Na4a = 0.0f;
    }
    if (framePtr->a5 != 0) {
        zz->amp5 = (float)framePtr->a5 / 32.0f;
        Acoeff5q = zz->Acoeff5 * zz->amp5;
        ampBank = 1;
    } else {
        zz->amp5 = 0.0f;
        zz->Nb5 = 0.0f;
        zz->Na5 = 0.0f;
    }
    if (framePtr->a6 != 0) {
        zz->amp6 = (float)framePtr->a6 / 32.0f;
        Acoeff6q = zz->Acoeff6 * zz->amp6;
        ampBank = 1;
    } else {
        zz->amp6 = 0.0f;
        zz->Nb6 = 0.0f;
        zz->Na6 = 0.0f;
    }
    SampAB = 0.0f;
    Samp2 = 0.0f;
    Samp3 = 0.0f;
    Samp4 = 0.0f;
    Samp5 = 0.0f;
    Samp6 = 0.0f;
    curF0Pitch = framePtr->f0;
    if (zz->glotType == 1) {
        Set_SndFreq(zz, curF0Pitch);
        zz->glotInc = zz->TOPtr[(uint8_t)curF0Pitch] >> (5 - (curF0Pitch >> 8));
    } else {
        zz->glotInc = zz->TOPtr[(uint8_t)curF0Pitch] >> (5 - (curF0Pitch >> 8));
        if (zz->voiceChorus != 0) {
            curF0Pitch = zz->voiceChorus + curF0Pitch;
            if (curF0Pitch < 0) {
                curF0Pitch = 0;
            }
            zz->glotInc1 = zz->TOPtr[(uint8_t)curF0Pitch] >> (5 - (curF0Pitch >> 8));
        }
    }
    totalBreathGain = zz->breathGain;
    vowel = 0;
    if (zz->MIDIMode != 0) {
        if (framePtr->phon_Edge != 0 && (local_cur_PhonFlags_CF & 1) != 0) {
            vowel = 1;
        }
    } else if (framePtr->phon_Edge != 0 && (zz->cur_PhonCtrl_CF & 1) != 0) {
        vowel = 1;
    }
    zz->Av *= framePtr->velocity;
    zz->ampStep = (zz->Av - zz->lastAmp) / 128.0f;
    zz->curAmp_Full = zz->lastAmp;
    zz->lastAmp = zz->Av;
    ampCtr = 0;
    if (zz->sync_On_Vowel != 0 && vowel != 0) {
        InitSampOsc(zz);
    }
    for (sampCtr = 109; sampCtr >= 0; sampCtr--) {
        if (ampCtr <= 0x7f) {
            zz->curAmp_Full += zz->ampStep;
            zz->curAmp = zz->curAmp_Full;
            ampCtr++;
        } else {
            zz->curAmp = zz->Av;
        }
        if (zz->CoeffCntr <= 126) {
            zz->lastAcoeff1 += zz->Acoeff1Step;
            zz->lastBcoeff1 += zz->Bcoeff1Step;
            zz->lastCcoeff1 += zz->Ccoeff1Step;
            zz->lastAcoeff2 += zz->Acoeff2Step;
            zz->lastBcoeff2 += zz->Bcoeff2Step;
            zz->lastCcoeff2 += zz->Ccoeff2Step;
            zz->lastAcoeff3 += zz->Acoeff3Step;
            zz->lastBcoeff3 += zz->Bcoeff3Step;
            zz->lastCcoeff3 += zz->Ccoeff3Step;
            zz->CoeffCntr++;
        } else if (zz->CoeffCntr == 0x7f) {
            zz->lastAcoeff1 = zz->Acoeff1;
            zz->lastBcoeff1 = zz->Bcoeff1;
            zz->lastCcoeff1 = zz->Ccoeff1;
            zz->lastAcoeff2 = zz->Acoeff2;
            zz->lastBcoeff2 = zz->Bcoeff2;
            zz->lastCcoeff2 = zz->Ccoeff2;
            zz->lastAcoeff3 = zz->Acoeff3;
            zz->lastBcoeff3 = zz->Bcoeff3;
            zz->lastCcoeff3 = zz->Ccoeff3;
        }
        if (!(zz->singEnabled == 0 || !(zz->curAmp > 0.0f || ampBank != 0) && zz->zeroCntr <= 0)) {
            zz->noiseIndex = (zz->noiseIndex + 78656) & 0xfffffff;
            if (zz->curAmp > 0.0f) {
                if (zz->glotType == 1 && zz->sGlottState == 0) {
                    if (zz->sGlottDirA != 0) {
                        waveAddr = &zz->sGlottWaveAddrA[zz->sGlottAccumulator >> 12];
                        sample_L = *waveAddr;
                        sample_L += ((zz->sGlottAccumulator & 4095) * (waveAddr[1] - sample_L)) >> 12;
                    } else {
                        waveAddr = &zz->sGlottWaveAddrA[(zz->sGlottWaveLenA - zz->sGlottAccumulator) >> 12];
                        sample_L = *waveAddr;
                        sample_L += ((zz->sGlottAccumulator & 4095) * (waveAddr[-1] - sample_L)) >> 12;
                    }
                    sourceC = (float)sample_L;
                    sourceC *= zz->sGlottVolumeA;
                    zz->sGlottAccumulator += zz->sGlottPhaseIncA;
                    if (zz->sGlottAccumulator >= (uint32_t)zz->sGlottWaveLenA) {
                        switch (zz->sGlottModeA) {
                        case 0:
                            zz->sGlottAccumulator -= zz->sGlottWaveLenA;
                            break;
                        case 1:
                            zz->sGlottAccumulator -= zz->sGlottWaveLenA;
                            zz->sGlottDirA ^= 1;
                            break;
                        case 2:
                            zz->sGlottState = 1;
                            break;
                        case 3:
                            zz->sGlottAccumulator = 0;
                            zz->sGlottPhaseIncA = zz->sGlottPhaseIncB;
                            zz->sGlottWaveLenA = zz->sGlottWaveLenB;
                            zz->sGlottWaveAddrA = zz->sGlottWaveAddrB;
                            zz->sGlottVolumeA = zz->sGlottVolumeB;
                            zz->sGlottModeA = zz->sGlottModeB;
                            zz->sGlottDirA = 1;
                            zz->sGlottInSwap = 1;
                            break;
                        }
                    }
                } else {
                    sourceC = 0.0f;
                }
                if (!(!(totalBreathGain > 0.0f) || zz->breathCycle > cycleIndex)) {
                    tSamp = zz->breathWave[zz->noiseIndex >> 16];
                    breath = tSamp;
                    zz->lastBreath = tSamp;
                    sourceC += breath * totalBreathGain;
                }
                zz->glotIndex = (zz->glotInc + zz->glotIndex) & 0xffffff;
                cycleIndex = zz->glotIndex >> 16;
                vPulse = zz->voiceWaveform[cycleIndex] * zz->waveAmp1 + zz->voiceWaveform1[cycleIndex] * zz->waveAmp2;
                if (zz->voiceChorus != 0 && zz->glotType != 1) {
                    zz->glotIndex1 = (zz->glotInc1 + zz->glotIndex1) & 0xffffff;
                    cycleIndex = zz->glotIndex1 >> 16;
                    vPulse1 = zz->voiceWaveform1[cycleIndex];
                    vPulse = (vPulse + vPulse1) / 2.0f;
                }
                sourceC = (sourceC + vPulse) * zz->curAmp;
            } else {
                sourceC = 0.0f;
                zz->lastnSampL = 0.0f;
                zz->lastnSampR = 0.0f;
                zz->glotIndex = 0;
                zz->glotIndex1 = 0x7f0000;
                zz->lastAmp = 0.0f;
                vPulse = 0.0f;
            }
            if (!(!(zz->curAmp > 0.0f) && !(zz->Af > 0.0f))) {
                asperation = zz->BandNoisePtr[zz->noiseIndex >> 16];
                sourceC += zz->Af * asperation;
                if (noNasal != 0) {
                    SampV = sourceC;
                } else {
                    SampV = zz->BcoeffNZ * zz->NaNZ + zz->CcoeffNZ * zz->NbNZ + sourceC;
                    zz->NbNZ = zz->NaNZ;
                    zz->NaNZ = sourceC;
                    SampV *= nGain;
                    SampV += zz->BcoeffNP * zz->NaNP + zz->CcoeffNP * zz->NbNP;
                    zz->NbNP = zz->NaNP;
                    zz->NaNP = SampV;
                }
                SampV = zz->lastAcoeff1 * SampV + zz->lastBcoeff1 * zz->Na1 + zz->lastCcoeff1 * zz->Nb1;
                zz->Nb1 = zz->Na1;
                zz->Na1 = SampV;
                SampV = zz->lastAcoeff2 * SampV + zz->lastBcoeff2 * zz->Na2 + zz->lastCcoeff2 * zz->Nb2;
                zz->Nb2 = zz->Na2;
                zz->Na2 = SampV;
                SampV = zz->lastAcoeff3 * SampV + zz->lastBcoeff3 * zz->Na3 + zz->lastCcoeff3 * zz->Nb3;
                zz->Nb3 = zz->Na3;
                zz->Na3 = SampV;
                SampV = zz->Acoeff4 * SampV + zz->Bcoeff4 * zz->Na4 + zz->Ccoeff4 * zz->Nb4;
                zz->Nb4 = zz->Na4;
                zz->Na4 = SampV;
                SampV = zz->Acoeff5x * SampV + zz->Bcoeff5x * zz->Nx5 + zz->Ccoeff5x * zz->Ny5;
                zz->Ny5 = zz->Nx5;
                zz->Nx5 = SampV;
            } else {
                SampV = 0.0f;
            }
            if (zz->ab > 0.0f) {
                nPulse = zz->NoiseWavePtr[zz->noiseIndex >> 16];
            } else {
                nPulse = zz->HPNoisePtr[zz->noiseIndex >> 16];
            }
            sourceP = zz->voiceNoiseGain * nPulse;
            if (zz->ab > 0.0f) {
                SampAB = zz->ab * sourceP;
            }
            if (zz->amp2 > 0.0f) {
                Samp2 = Acoeff2q * sourceP + zz->Bcoeff2 * zz->Na2a + zz->Ccoeff2 * zz->Nb2a;
                zz->Nb2a = zz->Na2a;
                zz->Na2a = Samp2;
            }
            if (zz->amp3 > 0.0f) {
                Samp3 = Acoeff3q * sourceP + zz->Bcoeff3 * zz->Na3a + zz->Ccoeff3 * zz->Nb3a;
                zz->Nb3a = zz->Na3a;
                zz->Na3a = Samp3;
            }
            if (zz->amp4 > 0.0f) {
                Samp4 = Acoeff4q * sourceP + zz->Bcoeff4p * zz->Na4a + zz->Ccoeff4p * zz->Nb4a;
                zz->Nb4a = zz->Na4a;
                zz->Na4a = Samp4;
            }
            if (zz->amp5 > 0.0f) {
                Samp5 = Acoeff5q * sourceP + zz->Bcoeff5 * zz->Na5 + zz->Ccoeff5 * zz->Nb5;
                zz->Nb5 = zz->Na5;
                zz->Na5 = Samp5;
            }
            if (zz->amp6 > 0.0f) {
                Samp6 = Acoeff6q * sourceP + zz->Bcoeff6 * zz->Na6 + zz->Ccoeff6 * zz->Nb6;
                zz->Nb6 = zz->Na6;
                zz->Na6 = Samp6;
            }
            Samp = SampAB - Samp3 + Samp4 - Samp5 + Samp6 - Samp2;
            nSamp = SampV + Samp;
            if (zz->hfEmph != 0) {
                tSamp = zz->emphA * nSamp - zz->lastSample * zz->emphB;
                zz->lastSample = nSamp;
                nSamp = (float)(tSamp + nSamp * 0.5);
            }
            nSampL = nSamp + nSamp;
            nSampR = nSampL;
            wByte = (float)(zz->lastnSampL + (nSampL - zz->lastnSampL) * 0.5);
            sampleIndex = *local_sampleBuffer + FTOI(wByte);
            if (sampleIndex > 32760) {
                sampleIndex = 32760;
            } else if (sampleIndex < -32760) {
                sampleIndex = -32760;
            }
            (*local_sampleBuffer) = sampleIndex;
            wByte = (float)(zz->lastnSampR + (nSampR - zz->lastnSampR) * 0.5);
            sampleIndex = local_sampleBuffer[1] + FTOI(wByte);
            if (sampleIndex > 32760) {
                sampleIndex = 32760;
            } else if (sampleIndex < -32760) {
                sampleIndex = -32760;
            }
            local_sampleBuffer[1] = sampleIndex;
            wByte = nSampL;
            sampleIndex = local_sampleBuffer[2] + FTOI(wByte);
            if (sampleIndex > maxSampleL) {
                maxSampleL = sampleIndex;
            }
            if (sampleIndex > 32760) {
                sampleIndex = 32760;
            } else if (sampleIndex < -32760) {
                sampleIndex = -32760;
            }
            local_sampleBuffer[2] = sampleIndex;
            wByte = nSampR;
            sampleIndex = local_sampleBuffer[3] + FTOI(wByte);
            if (sampleIndex > maxSampleR) {
                maxSampleR = sampleIndex;
            }
            if (sampleIndex > 32760) {
                sampleIndex = 32760;
            } else if (sampleIndex < -32760) {
                sampleIndex = -32760;
            }
            local_sampleBuffer[3] = sampleIndex;
            local_sampleBuffer += 4;
            zz->lastnSampL = nSampL;
            zz->lastnSampR = nSampR;
        } else {
            zz->lastnSampL = 0.0f;
            zz->lastnSampR = 0.0f;
            zz->glotIndex = 0;
            zz->glotIndex1 = 0x7f0000;
            zz->lastAmp = 0.0f;
            local_sampleBuffer += 4;
        }
    }
    zz->waveIndex += 440;
    xx->maxSampleL = maxSampleL;
    xx->maxSampleR = maxSampleR;
    vw_stack_slot_c0 = cycleIndex;
}

/* Speech.c:1179  (0x96378) */
void SaveFrame(formantVarPtr zz)
{
    FramePtr frameBuf;
    int16_t curF1;
    int16_t curF2;
    int16_t curF3;

    if (zz->curFrameBuf == 0) {
        frameBuf = &zz->frameBuf1;
    } else {
        frameBuf = &zz->frameBuf2;
    }
    vw_stack_slot_c0 = VW_STACK_ADDRESS;   /* see STACK_SLOT_WRITERS */
    curF1 = zz->controlData[0];
    curF2 = zz->controlData[1];
    curF3 = zz->controlData[2];
    while (curF2 - curF1 <= 199) {
        curF1 -= 10;
    }
    while (curF3 - curF2 <= 599) {
        curF3 += 10;
    }
    frameBuf->f1 = e_HzToPitch(zz, curF1);
    frameBuf->f2 = e_HzToPitch(zz, curF2);
    frameBuf->f3 = e_HzToPitch(zz, curF3);
    frameBuf->f1Gain = zz->voiceF1Gain;
    frameBuf->f2Gain = zz->voiceF2Gain;
    frameBuf->f3Gain = zz->voiceF3Gain;
    frameBuf->bw1 = zz->controlData[3];
    frameBuf->bw2 = zz->controlData[4];
    frameBuf->bw3 = zz->controlData[5];
    frameBuf->FNZ = e_HzToPitch(zz, zz->controlData[6]);
    if (zz->controlData[9] < 0) {
        zz->controlData[9] = 0;
    }
    if (zz->controlData[10] < 0) {
        zz->controlData[10] = 0;
    }
    if (zz->controlData[11] < 0) {
        zz->controlData[11] = 0;
    }
    if (zz->controlData[12] < 0) {
        zz->controlData[12] = 0;
    }
    if (zz->controlData[13] < 0) {
        zz->controlData[13] = 0;
    }
    if (zz->controlData[14] < 0) {
        zz->controlData[14] = 0;
    }
    if (zz->controlData[7] < 0) {
        zz->controlData[7] = 0;
    }
    if (zz->controlData[8] < 0) {
        zz->controlData[8] = 0;
    }
    frameBuf->Av = e_LogToLin(zz, zz->controlData[7]);
    frameBuf->Af = e_LogToLin(zz, zz->controlData[8]);
    frameBuf->a2 = e_LogToLin(zz, zz->controlData[9]);
    frameBuf->a3 = e_LogToLin(zz, zz->controlData[10]);
    frameBuf->a4 = e_LogToLin(zz, zz->controlData[11]);
    frameBuf->a5 = e_LogToLin(zz, zz->controlData[12]);
    frameBuf->a6 = e_LogToLin(zz, zz->controlData[13]);
    frameBuf->AB = e_LogToLin(zz, zz->controlData[14]);
    frameBuf->f0 = zz->controlF0;
    frameBuf->phon_Edge = zz->starting_New_Phon;
    frameBuf->marker = zz->frameMarker;
    frameBuf->velocity = zz->curVel;
}

/* Speech.c:1264  (0x967c8) */
int16_t e_GetPhon(formantVarPtr zz, int16_t index)
{
    int16_t ret;

    if (index >= 0 && index < zz->numOfPhons) {
        ret = zz->phon_Buf[index];
        return ret;
    }
    ret = 23;
    return ret;
}

/* Speech.c:1281  (0x96864) */
int16_t e_GetPhonCtrl(formantVarPtr zz, int16_t index)
{
    int16_t ret;

    if (index >= 0 && index < zz->numOfPhons) {
        ret = zz->phon_Ctrl_Buf[index];
        return ret;
    }
    ret = 0;
    return ret;
}

/* Speech.c:1306  (0x96900) */
void Init_ControlBlocks(formantVarPtr zz)
{
    ControlBlock *cb;
    int16_t i;

    zz->big_Bang = 1;
    for (i = 0; i <= 14; i++) {
        cb = &zz->controlBlockArray[i];
        cb->curP_START_Targ = 0;
        cb->curTarget_TIME = 0;
        cb->curTarget_STEP = 0;
        cb->curTarget_OFFS = 0;
        cb->HEAD_offs = 0;
        cb->HEAD_step = 0;
        cb->TAIL_offs = 0;
        cb->TAIL_step = 0;
        cb->TAIL_START_time = 0;
        cb->onset_END_TIME = 0;
        cb->onset_VAL = 0;
        cb->nextP_START_Targ = 0;
        cb->prevP_END_Targ = 0;
        cb->curP_END_Targ = 0;
        cb->ptrToTargetList = NULL;
        cb->lastVal = 0;
    }
}

/* Speech.c:1341  (0x96a48) */
void Insert_Burst(formantVarPtr zz)
{
    int16_t burstClosureDur;
    int16_t burstReleaseDur;
    int16_t burstDur;
    int16_t i;
    ControlBlock *cb;

    if ((((uint32_t)zz->cur_PhonFlags_CF >> 9) & 1) != 0) {
        burstDur = zz->BurstDurTbl[zz->cur_Phon_CF] / 5;
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 12) & 1) != 0 && (((uint32_t)zz->cur_PhonFlags_CF >> 2) & 1) == 0 && (zz->next_PhonFlags_CF & 4160) != 0) {
            if ((zz->next_PhonCtrl_CF & 5120) != 0) {
                burstDur = 0;
            } else {
                burstDur >>= 1;
            }
        }
        burstClosureDur = zz->cur_Phon_Dur_CF - burstDur;
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 24) & 1) != 0 && burstClosureDur > 16) {
            burstClosureDur = 16;
        }
        for (i = 9; i <= 14; i++) {
            cb = &zz->controlBlockArray[i];
            cb->onset_END_TIME = burstClosureDur;
            cb->onset_VAL = 0;
        }
    }
    burstReleaseDur = 0;
    if ((((uint32_t)zz->prev_PhonFlags_CF >> 12) & 1) != 0 && (((uint32_t)zz->prev_PhonFlags_CF >> 2) & 1) == 0 && (((uint32_t)zz->cur_PhonFlags_CF >> 5) & 1) != 0) {
        burstReleaseDur = 8;
        zz->controlBlockArray[7].onset_VAL = 0;
        if (zz->Rank_FWD_Tbl[zz->next_Phon_CF] == 0) {
            zz->controlBlockArray[8].onset_VAL = 48;
        } else {
            zz->controlBlockArray[8].onset_VAL = 54;
        }
        if ((zz->cur_PhonCtrl_CF & 1) == 0) {
            burstReleaseDur = 5;
            zz->controlBlockArray[8].onset_VAL -= 3;
        }
        if ((((uint32_t)(uint16_t)zz->cur_PhonCtrl_CF >> 7) & 1) != 0 || zz->cur_Phon_CF == 9) {
            zz->controlBlockArray[8].onset_VAL += 3;
        }
        if (zz->prev2_Phon_CF == 40) {
            if ((zz->prev2_PhonCtrl_CF & 12) == 0) {
                burstReleaseDur = 2;
            }
        } else if ((zz->cur_PhonCtrl_CF & 1) == 0) {
            burstReleaseDur += 4;
        }
        if (burstReleaseDur >= zz->cur_Phon_Dur_CF) {
            burstReleaseDur = zz->cur_Phon_Dur_CF - 1;
        }
        if (burstReleaseDur > zz->cur_Phon_Dur_CF >> 1 && (zz->cur_PhonFlags_CF & 1) != 0 && (zz->cur_PhonCtrl_CF & 5120) != 0) {
            burstReleaseDur = zz->cur_Phon_Dur_CF >> 1;
        }
        if ((((uint32_t)(uint16_t)zz->cur_PhonCtrl_CF >> 14) & 1) != 0) {
            burstReleaseDur = zz->cur_Phon_Dur_CF;
            zz->controlBlockArray[8].onset_VAL -= 3;
        }
        zz->controlBlockArray[7].onset_END_TIME = burstReleaseDur;
        zz->controlBlockArray[8].onset_END_TIME = burstReleaseDur;
        zz->controlBlockArray[3].onset_END_TIME = burstReleaseDur;
        zz->controlBlockArray[4].onset_END_TIME = burstReleaseDur;
        zz->controlBlockArray[3].onset_VAL = zz->controlBlockArray[3].curP_START_Targ + 250;
        zz->controlBlockArray[4].onset_VAL = zz->controlBlockArray[4].curP_START_Targ + 70;
    }
    if ((((uint32_t)zz->cur_PhonFlags_CF >> 12) & 1) == 0) {
        return;
    }
    if ((((uint32_t)zz->cur_PhonFlags_CF >> 2) & 1) == 0) {
        return;
    }
    if ((((uint32_t)zz->prev_PhonFlags_CF >> 2) & 1) == 0) {
        return;
    }
    if ((((uint32_t)zz->next_PhonFlags_CF >> 2) & 1) != 0) {
        return;
    }
    if (zz->cur_Phon_CF == 52) {
        return;
    }
    zz->controlBlockArray[7].onset_END_TIME = zz->cur_Phon_Dur_CF - 2;
    zz->controlBlockArray[3].onset_END_TIME = zz->cur_Phon_Dur_CF;
    zz->controlBlockArray[4].onset_END_TIME = zz->cur_Phon_Dur_CF;
    zz->controlBlockArray[5].onset_END_TIME = zz->cur_Phon_Dur_CF;
    zz->controlBlockArray[7].onset_VAL = 53;
    zz->controlBlockArray[3].onset_VAL = 1000;
    zz->controlBlockArray[4].onset_VAL = 1000;
    zz->controlBlockArray[5].onset_VAL = 1200;
}

/* Speech.c:1512  (0x96fec) */
int16_t Adjust_Colored_TargetX(formantVarPtr zz, int16_t index, int16_t entryCount)
{
    int16_t cur_Phon;
    int16_t next_Phon;
    int16_t prev_Phon;
    int32_t cur_Flags;
    int32_t next_Flags;
    int32_t prev_Flags;
    int16_t cur_PhonCtrl;
    int16_t adjust;

    cur_Phon = e_GetPhon(zz, index);
    next_Phon = e_GetPhon(zz, (int16_t)((uint16_t)index + 1));
    prev_Phon = e_GetPhon(zz, (int16_t)((uint16_t)index - 1));
    cur_Flags = zz->phonFlags2[cur_Phon];
    next_Flags = zz->phonFlags2[next_Phon];
    prev_Flags = zz->phonFlags2[prev_Phon];
    adjust = 0;
    if (zz->cur_ControlBlk_Index != 1) {
        return adjust;
    }
    if (cur_Phon != 15) {
        return adjust;
    }
    if ((((uint32_t)prev_Flags >> 13) & 1) == 0) {
        return adjust;
    }
    adjust = 200;
    return adjust;
}

/* Speech.c:1548  (0x9714c) */
int16_t Adjust_Colored_Target(formantVarPtr zz, int16_t index, int16_t entryCount)
{
    int16_t cur_Phon;
    int16_t next_Phon;
    int16_t prev_Phon;
    int32_t cur_Flags;
    int32_t next_Flags;
    int32_t prev_Flags;
    int16_t cur_PhonCtrl;
    int16_t adjust;

    cur_Phon = e_GetPhon(zz, index);
    next_Phon = e_GetPhon(zz, (int16_t)((uint16_t)index + 1));
    prev_Phon = e_GetPhon(zz, (int16_t)((uint16_t)index - 1));
    cur_Flags = zz->phonFlags2[cur_Phon];
    next_Flags = zz->phonFlags2[next_Phon];
    prev_Flags = zz->phonFlags2[prev_Phon];
    adjust = 0;
    if (zz->cur_ControlBlk_Index == 2) {
        if ((((uint32_t)cur_Flags >> 3) & 1) == 0) {
            return adjust;
        }
        if (cur_Phon == 9) {
            return adjust;
        }
        if ((((uint32_t)prev_Flags >> 25) & 1) == 0) {
            if ((((uint32_t)next_Flags >> 25) & 1) == 0) {
                return adjust;
            }
        }
        adjust = -150;
        return adjust;
    }
    if (zz->cur_ControlBlk_Index != 1) {
        return adjust;
    }
    cur_PhonCtrl = zz->phon_Ctrl_Buf[index];
    if (next_Phon == 25) {
        if ((((uint32_t)cur_Flags >> 21) & 1) != 0) {
            adjust = -150;
        } else if ((cur_Phon == 11 || cur_Phon == 12) && entryCount > 0) {
            adjust = -250;
        }
    }
    if ((prev_Phon == 25 || prev_Phon == 31 || prev_Phon == 28) && (((uint32_t)cur_Flags >> 21) & 1) != 0) {
        adjust = -150;
    }
    if (cur_Phon == 15 && (((uint32_t)prev_Flags >> 13) & 1) != 0) {
        adjust = 200;
    }
    if (entryCount > 0 && (cur_Phon == 15 || cur_Phon == 16) && (((uint32_t)next_Flags >> 13) & 1) != 0) {
        adjust += 200;
    }
    if ((cur_PhonCtrl & 7168) != 0) {
        adjust >>= 1;
    } else {
        adjust = (adjust >> 1) + adjust;
        if (entryCount > 0 && cur_Phon == 16) {
            adjust = 400;
        }
    }
    if (adjust > 400) {
        adjust = 400;
        return adjust;
    }
    if (adjust >= -400) {
        return adjust;
    }
    adjust = -400;
    return adjust;
}

/* Speech.c:1666  (0x974e0) */
void AdjustGain(formantVarPtr zz, int16_t index, int16_t *target_Val)
{
    int16_t cur_phon;
    int32_t cur_Flags;

    cur_phon = e_GetPhon(zz, index);
    cur_Flags = zz->phonFlags2[cur_phon];
    if ((cur_Flags & 1) == 0) {
        return;
    }
    if (zz->cur_ControlBlk_Index == 3) {
        (*target_Val) = (*target_Val * zz->voiceBWgain1) >> 16;
        return;
    }
    if (zz->cur_ControlBlk_Index == 4) {
        (*target_Val) = (*target_Val * zz->voiceBWgain2) >> 16;
        return;
    }
    if (zz->cur_ControlBlk_Index != 5) {
        return;
    }
    (*target_Val) = (*target_Val * zz->voiceBWgain3) >> 16;
}

/* Speech.c:1690  (0x97640) */
int16_t GetTarget(formantVarPtr zz, int16_t index)
{
    ControlBlock *cb;
    int16_t cur_ControlBlk_Type;
    int16_t cur_phon;
    int32_t cur_Flags;
    int16_t cur_PhonCtrl;
    int16_t next_phon;
    int32_t next_Flags;
    int16_t prev_phon;
    int32_t prev_Flags;
    int16_t target_Val;
    int16_t *targetPtr;
    int16_t rank;

    cb = &zz->controlBlockArray[zz->cur_ControlBlk_Index];
    cur_ControlBlk_Type = zz->CtrlBlockTypeTbl[zz->cur_ControlBlk_Index];
    cur_phon = e_GetPhon(zz, index);
    cur_Flags = zz->phonFlags2[cur_phon];
    cur_PhonCtrl = zz->phon_Ctrl_Buf[index];
    next_phon = e_GetPhon(zz, (int16_t)((uint16_t)index + 1));
    next_Flags = zz->phonFlags2[next_phon];
    prev_phon = e_GetPhon(zz, (int16_t)((uint16_t)index - 1));
    prev_Flags = zz->phonFlags2[prev_phon];
    target_Val = -1;
    switch (cur_ControlBlk_Type) {
    case 0:
    case 1:
        targetPtr = zz->voice_Formants[zz->cur_ControlBlk_Index];
        target_Val = targetPtr[cur_phon];
        if (target_Val < -1) {
            return target_Val;
        }
        if (target_Val < 0) {
            if (target_Val == -1) {
                target_Val = targetPtr[next_phon];
                if (target_Val == -1) {
                    target_Val = targetPtr[e_GetPhon(zz, (int16_t)((uint16_t)index + 2))];
                    if (target_Val == -1) {
                        target_Val = targetPtr[prev_phon];
                        if (target_Val < 0 && target_Val != -1) {
                            target_Val = zz->EnvelopeListTbl[(target_Val & 0x7fff) + 2];
                        }
                        if (target_Val == -1) {
                            target_Val = zz->DefaultTargTbl[zz->cur_ControlBlk_Index];
                        }
                    }
                }
            }
            if (target_Val < -1) {
                target_Val &= 0x7fff;
                target_Val = zz->EnvelopeListTbl[target_Val];
            }
        }
        if (zz->cur_ControlBlk_Index == 0 && (((uint32_t)cur_Flags >> 10) & 1) != 0 && (((uint32_t)cur_Flags >> 11) & 1) == 0 && (prev_Flags & 1) != 0) {
            target_Val += 40;
        }
        if (cur_phon != 34) {
            if (cur_phon != 27) {
                return target_Val;
            }
        }
        if (zz->cur_ControlBlk_Index != 4) {
            return target_Val;
        }
        if (zz->Rank_FWD_Tbl[next_phon] != 0) {
            target_Val += 60;
        }
        if (cur_phon != 34 && cur_phon != 27) {
            if (cur_phon != 35) {
                return target_Val;
            }
        }
        if (zz->cur_ControlBlk_Index != 5) {
            return target_Val;
        }
        if ((((uint32_t)next_Flags >> 18) & 1) == 0) {
            if ((((uint32_t)prev_Flags >> 19) & 1) == 0) {
                return target_Val;
            }
        }
        target_Val = 1225;
        return target_Val;
    case 2:
        if ((((uint32_t)cur_Flags >> 6) & 1) != 0) {
            target_Val = zz->nasalTargFreq;
            return target_Val;
        }
        target_Val = zz->nasalBaseFreq;
        return target_Val;
    case 3:
        if (zz->cur_ControlBlk_Index == 7) {
            target_Val = zz->voice_av_Tbl[cur_phon];
            if ((((uint32_t)(uint16_t)cur_PhonCtrl >> 14) & 1) != 0) {
                if ((((uint32_t)prev_Flags >> 6) & 1) != 0) {
                    target_Val -= 6;
                } else {
                    target_Val -= 20;
                }
            }
            if ((((uint32_t)cur_Flags >> 12) & 1) != 0 && (((uint32_t)prev_Flags >> 2) & 1) == 0) {
                target_Val = 0;
            }
            if (cur_phon != 32) {
                return target_Val;
            }
            if ((((uint32_t)prev_Flags >> 2) & 1) == 0) {
                return target_Val;
            }
            if ((cur_PhonCtrl & 5120) != 0) {
                return target_Val;
            }
            target_Val = 54;
            return target_Val;
        }
        if (cur_phon == 32) {
            if (zz->Rank_FWD_Tbl[next_phon] == 0) {
                target_Val = 58;
            } else {
                target_Val = 62;
            }
            if ((cur_PhonCtrl & 7168) != 0) {
                return target_Val;
            }
            target_Val--;
            return target_Val;
        }
        target_Val = 0;
        return target_Val;
    }
    if (cur_ControlBlk_Type != 4) {
        return target_Val;
    }
    target_Val = zz->NoiseIndexTbl[cur_phon];
    if (target_Val == -1) {
        target_Val = 0;
        return target_Val;
    }
    if (next_phon == 23) {
        rank = zz->Rank_BKWD_Tbl[prev_phon];
    } else {
        rank = zz->Rank_FWD_Tbl[next_phon];
    }
    if (rank == 4) {
        rank = 2;
    }
    target_Val = zz->cur_ControlBlk_Index + rank * 6 + target_Val - 9;
    target_Val = zz->voice_NoiseAmp_Tbl[target_Val];
    if ((((uint32_t)(uint16_t)zz->phon_Ctrl_Buf[index + 1] >> 14) & 1) == 0) {
        return target_Val;
    }
    if (target_Val <= 3) {
        return target_Val;
    }
    target_Val -= 4;
    return target_Val;
}

/* Speech.c:1898  (0x97e28) */
int16_t Get_FIRST_Target(formantVarPtr zz, int16_t index)
{
    int16_t targ;
    int16_t targType;

    targType = zz->CtrlBlockTypeTbl[zz->cur_ControlBlk_Index];
    targ = GetTarget(zz, index);
    if (targ < -1) {
        targ &= 0x7fff;
        targ = zz->EnvelopeListTbl[targ];
    }
    if (targType == 0) {
        targ = Adjust_Colored_Target(zz, index, 0) + targ;
        return targ;
    }
    if (targType != 1) {
        return targ;
    }
    AdjustGain(zz, index, &targ);
    return targ;
}

/* Speech.c:1930  (0x97f7c) */
int16_t Get_LAST_Target(formantVarPtr zz, int16_t index)
{
    int16_t targ;
    int16_t targType;

    targType = zz->CtrlBlockTypeTbl[zz->cur_ControlBlk_Index];
    targ = GetTarget(zz, index);
    if (targ < -1) {
        targ = zz->EnvelopeListTbl[(targ & 0x7fff) + 2];
    }
    if (targType == 0) {
        targ = Adjust_Colored_Target(zz, index, 1) + targ;
        return targ;
    }
    if (targType != 1) {
        return targ;
    }
    AdjustGain(zz, index, &targ);
    return targ;
}

/* Speech.c:1960  (0x980c8) */
void Get_Locus(formantVarPtr zz, int16_t i_Consonant, int16_t i_Vowel, int16_t bType)
{
    int32_t con_Flags;
    int32_t v1_Flags;
    int16_t consonant_Phon;
    int16_t vowel1_Phon;
    int16_t consonant_Rank;
    int16_t vowel_Rank;
    int16_t v1_Target;
    int16_t loci_Tbl_Index;
    int16_t locus_Freq;
    int16_t locus_Pcnt;
    int16_t target_Offset;
    int16_t f2_y_Colored;
    int16_t t_58;

    if (zz->cur_ControlBlk_Index < 0) {
        return;
    }
    if (zz->cur_ControlBlk_Index > 2) {
        return;
    }
    consonant_Phon = e_GetPhon(zz, i_Consonant);
    vowel1_Phon = e_GetPhon(zz, i_Vowel);
    if (bType == 0) {
        vowel_Rank = zz->Rank_FWD_Tbl[vowel1_Phon];
        consonant_Rank = zz->Rank_BKWD_Tbl[consonant_Phon];
    } else {
        vowel_Rank = zz->Rank_BKWD_Tbl[vowel1_Phon];
        consonant_Rank = zz->Rank_FWD_Tbl[consonant_Phon];
    }
    if (consonant_Rank != 3) {
        return;
    }
    if (vowel_Rank == 3) {
        return;
    }
    v1_Flags = zz->phonFlags2[vowel1_Phon];
    con_Flags = zz->phonFlags2[consonant_Phon];
    if ((((uint32_t)v1_Flags >> 18) & 1) != 0) {
        f2_y_Colored = 1;
    } else {
        f2_y_Colored = 0;
    }
    if (bType == 0) {
        v1_Target = Get_FIRST_Target(zz, i_Vowel);
    } else {
        v1_Target = Get_LAST_Target(zz, i_Vowel);
    }
    switch (vowel_Rank) {
    case 0:
        loci_Tbl_Index = zz->Front_Loci_Tbl[consonant_Phon];
        break;
    case 1:
        loci_Tbl_Index = zz->Mid_Loci_Tbl[consonant_Phon];
        break;
    case 2:
    case 4:
        loci_Tbl_Index = zz->Back_Loci_Tbl[consonant_Phon];
        break;
    }
    if (loci_Tbl_Index == -1) {
        return;
    }
    loci_Tbl_Index >>= 1;
    loci_Tbl_Index = zz->cur_ControlBlk_Index * 3 + loci_Tbl_Index;
    locus_Freq = zz->voice_Locus_Tbl[loci_Tbl_Index];
    loci_Tbl_Index++;
    locus_Pcnt = zz->voice_Locus_Tbl[loci_Tbl_Index];
    loci_Tbl_Index++;
    locus_Freq = zz->locusOffset + locus_Freq;
    if (locus_Freq <= 0) {
        locus_Freq = 1;
    }
    zz->trans_TIME = zz->voice_Locus_Tbl[loci_Tbl_Index] / 5;
    if ((((uint32_t)con_Flags >> 6) & 1) == 0 && f2_y_Colored == 0) {
        zz->trans_TIME -= zz->trans_TIME >> 2;
    }
    if (vowel_Rank == 4 && zz->cur_ControlBlk_Index != 0 && (con_Flags & 0x30000) != 0) {
        locus_Pcnt = (locus_Pcnt >> 1) + 50;
    }
    if (f2_y_Colored != 0 && zz->cur_ControlBlk_Index == 1) {
        locus_Pcnt = locus_Pcnt - (locus_Pcnt >> 2) + 25;
    }
    target_Offset = locus_Pcnt * (v1_Target - locus_Freq) / 100;
    zz->trans_LEVEL = locus_Freq + target_Offset;
}

/* Speech.c:2076  (0x985f8) */
void Head_Rules(formantVarPtr zz)
{
    ControlBlock *cb;
    int16_t cur_ControlBlk_Type;
    int16_t ampT;

    cb = &zz->controlBlockArray[zz->cur_ControlBlk_Index];
    cur_ControlBlk_Type = zz->CtrlBlockTypeTbl[zz->cur_ControlBlk_Index];
    switch (cur_ControlBlk_Type) {
    case 0:
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 5) & 1) != 0) {
            if ((((uint32_t)zz->cur_PhonFlags_CF >> 7) & 1) == 0) {
                zz->trans_TIME = 9;
                if ((((uint32_t)zz->prev_PhonFlags_CF >> 7) & 1) != 0) {
                    zz->trans_LEVEL = (cb->prevP_END_Targ + zz->trans_LEVEL) >> 1;
                    if (zz->prev_Phon_CF == 31 && zz->cur_ControlBlk_Index == 0) {
                        zz->trans_LEVEL += 80;
                    } else if (zz->prev_Phon_CF == 30 && zz->cur_ControlBlk_Index != 0) {
                        zz->trans_TIME = 14;
                    }
                } else if (zz->cur_Phon_CF == 32) {
                    zz->trans_LEVEL = (cb->prevP_END_Targ + zz->trans_LEVEL) >> 1;
                }
            } else {
                if ((((uint32_t)zz->prev_PhonFlags_CF >> 7) & 1) == 0) {
                    zz->trans_LEVEL = (cb->prevP_END_Targ + zz->trans_LEVEL) >> 1;
                } else {
                    zz->trans_LEVEL = (cb->prevP_END_Targ + zz->trans_LEVEL) >> 1;
                }
                zz->trans_TIME = 6;
            }
        }
        if (zz->cur_Phon_CF == 23) {
            zz->trans_LEVEL = cb->prevP_END_Targ;
            zz->trans_TIME = zz->cur_Phon_Dur_CF;
        } else {
            Get_Locus(zz, zz->cur_PhonBuf_Index_CF - 1, zz->cur_PhonBuf_Index_CF, 0);
            Get_Locus(zz, zz->cur_PhonBuf_Index_CF, zz->cur_PhonBuf_Index_CF - 1, 1);
            if ((((uint32_t)zz->prev_PhonFlags_CF >> 12) & 1) != 0 && (((uint32_t)zz->prev_PhonFlags_CF >> 2) & 1) == 0 && zz->cur_ControlBlk_Index == 0) {
                zz->trans_LEVEL += 100;
            }
            if ((((uint32_t)zz->cur_PhonFlags_CF >> 10) & 1) != 0) {
                if (zz->cur_ControlBlk_Index == 0) {
                    zz->trans_TIME = 4;
                } else {
                    zz->trans_TIME = 6;
                }
                if ((((uint32_t)zz->cur_PhonFlags_CF >> 12) & 1) != 0) {
                    zz->trans_TIME = zz->cur_Phon_Dur_CF;
                }
            }
            if ((((uint32_t)zz->cur_PhonFlags_CF >> 6) & 1) != 0) {
                if (zz->cur_ControlBlk_Index == 0) {
                    zz->trans_TIME = 0;
                } else {
                    zz->trans_TIME = zz->cur_Phon_Dur_CF;
                }
                if ((zz->cur_Phon_CF == 34 || zz->cur_Phon_CF == 27) && zz->Rank_BKWD_Tbl[zz->prev_Phon_CF] == 0) {
                    if (zz->cur_ControlBlk_Index == 1) {
                        if ((((uint32_t)zz->prev_PhonFlags_CF >> 19) & 1) != 0) {
                            zz->trans_LEVEL -= 200;
                        } else {
                            zz->trans_LEVEL -= 100;
                        }
                    } else if (zz->cur_ControlBlk_Index == 2) {
                        zz->trans_LEVEL -= 100;
                    }
                } else if (zz->cur_Phon_CF == 33 && zz->cur_ControlBlk_Index == 1 && (((uint32_t)zz->prev_PhonFlags_CF >> 19) & 1) != 0) {
                    zz->trans_LEVEL -= 150;
                }
            }
        }
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 10) & 1) == 0 && zz->Rank_BKWD_Tbl[zz->prev_Phon_CF] != 3 && zz->trans_TIME > 0) {
            zz->trans_TIME = ((zz->cur_Phon_PctOfMaxDur1_CF * zz->trans_TIME) >> 16) + 1;
        }
        break;
    case 2:
        if ((((uint32_t)zz->prev_PhonFlags_CF >> 6) & 1) != 0 && (((uint32_t)zz->cur_PhonFlags_CF >> 6) & 1) == 0) {
            zz->trans_LEVEL = zz->nasalBaseFreq + ((zz->nasalTargFreq - zz->nasalBaseFreq) >> 1);
            if ((((uint32_t)zz->cur_PhonFlags_CF >> 7) & 1) != 0) {
                zz->trans_TIME = 16;
            } else {
                zz->trans_TIME = 16;
            }
        }
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 6) & 1) != 0) {
            zz->trans_LEVEL = zz->nasalTargFreq;
        }
        break;
    case 1:
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 2) & 1) != 0) {
            if ((((uint32_t)zz->prev_PhonFlags_CF >> 2) & 1) == 0 && zz->cur_ControlBlk_Index == 3) {
                zz->trans_TIME = 10;
                zz->trans_LEVEL = (zz->controlBlockArray[0].curP_START_Targ >> 3) + cb->curP_START_Targ;
            } else {
                zz->trans_TIME = 8;
            }
        } else {
            zz->trans_TIME = 4;
        }
        if (zz->prev_Phon_CF == 23) {
            zz->trans_LEVEL = (5 - cur_ControlBlk_Type) * 50 + cb->curP_START_Targ;
            zz->trans_TIME = 10;
        } else if (zz->cur_Phon_CF == 23) {
            zz->trans_LEVEL = (5 - cur_ControlBlk_Type) * 50 + cb->prevP_END_Targ;
            if ((((uint32_t)zz->phonFlags2[zz->prev2_Phon_CF] >> 2) & 1) == 0 && (((uint32_t)(uint16_t)zz->prev_PhonCtrl_CF >> 14) & 1) != 0 && zz->cur_ControlBlk_Index == 3) {
                zz->trans_LEVEL = 250;
            }
            zz->trans_TIME = 10;
        }
        if ((((uint32_t)zz->prev_PhonFlags_CF >> 6) & 1) != 0) {
            zz->trans_LEVEL = cb->curP_START_Targ;
            if (zz->cur_ControlBlk_Index == 4) {
                if ((zz->prev_Phon_CF == 34 || zz->prev_Phon_CF == 27) && zz->Rank_FWD_Tbl[zz->cur_Phon_CF] != 0) {
                    zz->trans_LEVEL += 60;
                    zz->trans_TIME = 12;
                }
            } else if (zz->cur_ControlBlk_Index == 3) {
                zz->trans_LEVEL += 70;
                zz->trans_TIME = 20;
            }
        }
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 6) & 1) != 0) {
            zz->trans_TIME = 0;
        }
        break;
    case 3:
    case 4:
        ampT = cb->curP_START_Targ - 10;
        if (ampT > zz->trans_LEVEL || (((uint32_t)zz->prev_PhonFlags_CF >> 12) & 1) != 0 || zz->prev_Phon_CF == 51) {
            zz->trans_LEVEL = ampT;
            if ((((uint32_t)zz->cur_PhonFlags_CF >> 10) & 1) == 0) {
                zz->trans_TIME = 4;
            }
            if (zz->cur_ControlBlk_Index == 7) {
                if (zz->prev_Phon_CF == 23 && (((uint32_t)zz->cur_PhonFlags_CF >> 2) & 1) != 0) {
                    zz->trans_LEVEL -= 8;
                    zz->trans_TIME = 9;
                }
                if ((((uint32_t)zz->prev_PhonFlags_CF >> 10) & 1) != 0) {
                    zz->trans_LEVEL = ampT + 6;
                }
                if ((((uint32_t)zz->prev_PhonFlags_CF >> 12) & 1) != 0) {
                    zz->trans_LEVEL = cb->curP_START_Targ - 5;
                }
            }
        }
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 2) & 1) != 0 && (((uint32_t)zz->prev_PhonFlags_CF >> 6) & 1) != 0) {
            zz->trans_TIME = 0;
        }
        if ((((uint32_t)zz->prev_PhonFlags_CF >> 2) & 1) != 0 && (((uint32_t)zz->cur_PhonFlags_CF >> 6) & 1) != 0 && zz->cur_ControlBlk_Index == 7) {
            zz->trans_TIME = 0;
        }
        ampT = cb->prevP_END_Targ - 10;
        if (ampT > zz->trans_LEVEL) {
            zz->trans_LEVEL = ampT - 3;
            if (zz->cur_Phon_CF == 23) {
                zz->trans_TIME = 14;
            }
        }
        if (zz->cur_ControlBlk_Index == 10 && (((uint32_t)zz->cur_PhonFlags_CF >> 24) & 1) != 0) {
            zz->trans_TIME = zz->cur_Phon_Dur_CF - 2;
            zz->trans_LEVEL = cb->curP_START_Targ - 30;
        }
        if (zz->cur_ControlBlk_Index == 7 && (((uint32_t)zz->cur_PhonFlags_CF >> 9) & 1) != 0) {
            zz->trans_TIME = 2;
        }
        if (zz->cur_ControlBlk_Index == 8 && (zz->cur_Phon_CF == 23 || zz->cur_Phon_CF == 36 || zz->cur_Phon_CF == 38 || zz->cur_Phon_CF == 40 || zz->cur_Phon_CF == 42) && (((uint32_t)zz->prev_PhonFlags_CF >> 2) & 1) != 0 && (((uint32_t)zz->prev_PhonFlags_CF >> 10) & 1) == 0) {
            if (zz->cur_Phon_CF == 23) {
                zz->trans_TIME = 16;
                zz->trans_LEVEL = 52;
            } else {
                zz->trans_TIME = 9;
                zz->trans_LEVEL = 48;
            }
        }
        break;
    }
    if (zz->trans_TIME > zz->cur_Phon_Dur_CF) {
        zz->trans_TIME = zz->cur_Phon_Dur_CF;
    }
    if (zz->trans_TIME > 26) {
        zz->trans_TIME = 26;
    }
    if (zz->trans_TIME >= 0) {
        return;
    }
    zz->trans_TIME = 0;
}

/* Speech.c:2506  (0x99444) */
void Tail_Rules(formantVarPtr zz)
{
    ControlBlock *cb;
    int16_t cur_ControlBlk_Type;
    int16_t ampT;

    cb = &zz->controlBlockArray[zz->cur_ControlBlk_Index];
    cur_ControlBlk_Type = zz->CtrlBlockTypeTbl[zz->cur_ControlBlk_Index];
    switch (cur_ControlBlk_Type) {
    case 0:
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 5) & 1) != 0) {
            zz->trans_TIME = 9;
            if ((((uint32_t)zz->cur_PhonFlags_CF >> 7) & 1) == 0) {
                if ((((uint32_t)zz->next_PhonFlags_CF >> 7) & 1) != 0) {
                    if (zz->cur_ControlBlk_Index == 2) {
                        zz->trans_TIME = 12;
                    }
                    if (zz->next_Phon_CF == 31 && zz->cur_ControlBlk_Index == 0) {
                        zz->trans_LEVEL += 80;
                    }
                } else if (zz->next_Phon_CF == 32) {
                    zz->trans_LEVEL = (cb->curP_END_Targ + zz->trans_LEVEL) >> 1;
                }
            } else if ((((uint32_t)zz->next_PhonFlags_CF >> 7) & 1) == 0) {
                zz->trans_LEVEL = (cb->curP_END_Targ + zz->trans_LEVEL) >> 1;
                zz->trans_TIME = 4;
            } else {
                zz->trans_LEVEL = (cb->curP_END_Targ + zz->trans_LEVEL) >> 1;
                zz->trans_TIME = 8;
            }
        }
        if (zz->next_Phon_CF == 23) {
            zz->trans_TIME = 0;
        } else {
            Get_Locus(zz, zz->cur_PhonBuf_Index_CF + 1, zz->cur_PhonBuf_Index_CF, 1);
            Get_Locus(zz, zz->cur_PhonBuf_Index_CF, zz->cur_PhonBuf_Index_CF + 1, 0);
            if ((((uint32_t)zz->cur_PhonFlags_CF >> 10) & 1) != 0) {
                if (zz->cur_ControlBlk_Index == 0) {
                    zz->trans_TIME = 4;
                } else {
                    zz->trans_TIME = 6;
                }
                if ((((uint32_t)zz->cur_PhonFlags_CF >> 12) & 1) != 0) {
                    zz->trans_TIME = zz->cur_Phon_Dur_CF;
                    if ((((uint32_t)zz->cur_PhonFlags_CF >> 2) & 1) == 0 && zz->cur_ControlBlk_Index == 0) {
                        zz->trans_LEVEL += 100;
                    }
                }
            }
            if ((((uint32_t)zz->cur_PhonFlags_CF >> 6) & 1) != 0) {
                if (zz->cur_ControlBlk_Index == 0) {
                    zz->trans_TIME = 0;
                } else {
                    zz->trans_TIME = zz->cur_Phon_Dur_CF;
                }
                if ((zz->cur_Phon_CF == 34 || zz->cur_Phon_CF == 27) && zz->Rank_FWD_Tbl[zz->next_Phon_CF] == 0) {
                    if (zz->cur_ControlBlk_Index == 1) {
                        zz->trans_LEVEL -= 100;
                        if ((((uint32_t)zz->next_PhonFlags_CF >> 18) & 1) != 0) {
                            zz->trans_LEVEL -= 100;
                        }
                    } else if (zz->cur_ControlBlk_Index == 2) {
                        zz->trans_LEVEL -= 100;
                    }
                } else if (zz->cur_Phon_CF == 33 && zz->cur_ControlBlk_Index == 1 && (((uint32_t)zz->next_PhonFlags_CF >> 18) & 1) != 0) {
                    zz->trans_LEVEL -= 150;
                }
            }
        }
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 10) & 1) == 0 && zz->Rank_FWD_Tbl[zz->next_Phon_CF] != 3 && zz->trans_TIME > 0) {
            zz->trans_TIME = ((zz->cur_Phon_PctOfMaxDur2_CF * zz->trans_TIME) >> 16) + 1;
        }
        break;
    case 2:
        if ((((uint32_t)zz->next_PhonFlags_CF >> 6) & 1) != 0 && (((uint32_t)zz->cur_PhonFlags_CF >> 6) & 1) == 0) {
            zz->trans_LEVEL = zz->nasalTargFreq;
            zz->trans_TIME = 16;
        }
        break;
    case 1:
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 2) & 1) != 0) {
            zz->trans_TIME = 8;
            if ((((uint32_t)zz->next_PhonFlags_CF >> 2) & 1) == 0 && zz->cur_ControlBlk_Index == 3) {
                zz->trans_TIME = 10;
                zz->trans_LEVEL = (zz->controlBlockArray[0].curP_START_Targ >> 3) + cb->curP_END_Targ;
            }
        } else {
            zz->trans_TIME = 4;
        }
        if (zz->next_Phon_CF == 23) {
            zz->trans_LEVEL = (5 - cur_ControlBlk_Type) * 50 + cb->curP_END_Targ;
            zz->trans_TIME = 10;
        } else if (zz->cur_Phon_CF == 23) {
            zz->trans_LEVEL = (5 - cur_ControlBlk_Type) * 50 + cb->nextP_START_Targ;
            zz->trans_TIME = 10;
        }
        if ((((uint32_t)zz->next_PhonFlags_CF >> 6) & 1) != 0) {
            zz->trans_LEVEL = cb->curP_END_Targ;
            if (zz->cur_ControlBlk_Index == 4) {
                if ((zz->next_Phon_CF == 34 || zz->next_Phon_CF == 27) && zz->Rank_FWD_Tbl[zz->cur_Phon_CF] != 0) {
                    zz->trans_LEVEL += 60;
                    zz->trans_TIME = 12;
                }
            } else if (zz->cur_ControlBlk_Index == 3) {
                zz->trans_LEVEL += 100;
                zz->trans_TIME = 20;
            }
        }
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 6) & 1) != 0) {
            zz->trans_TIME = 0;
        }
        break;
    case 3:
    case 4:
        ampT = cb->nextP_START_Targ - 10;
        if (ampT > zz->trans_LEVEL) {
            zz->trans_LEVEL = ampT;
            if (zz->cur_Phon_CF == 23) {
                zz->trans_TIME = 14;
            }
        }
        if (zz->cur_ControlBlk_Index == 7 && zz->trans_LEVEL < cb->nextP_START_Targ && zz->cur_Phon_CF != 37 && zz->cur_Phon_CF != 39 && zz->cur_Phon_CF != 51 && zz->cur_Phon_CF != 43 && zz->cur_Phon_CF != 41) {
            zz->trans_TIME = 0;
            if ((zz->cur_PhonFlags_CF & 0x1001000) != 0) {
                if ((((uint32_t)zz->cur_PhonFlags_CF >> 2) & 1) != 0) {
                    zz->trans_LEVEL = cb->curP_END_Targ - 3;
                    zz->trans_TIME = 9;
                } else {
                    zz->trans_TIME = 0;
                }
                break;
            }
        }
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 2) & 1) != 0 && (((uint32_t)zz->next_PhonFlags_CF >> 6) & 1) != 0) {
            zz->trans_TIME = 0;
        }
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 6) & 1) != 0) {
            if (((((uint32_t)zz->next_PhonFlags_CF >> 2) & 1) ^ 1) == 0 && (((uint32_t)zz->cur_PhonFlags_CF >> 10) & 1) == 0 && (((uint32_t)(uint16_t)zz->next_PhonCtrl_CF >> 14) & 1) == 0) {
                zz->trans_TIME = 0;
            } else {
                zz->trans_TIME = 8;
            }
        }
        ampT = cb->curP_END_Targ - 10;
        if ((((uint32_t)zz->cur_PhonFlags_CF >> 9) & 1) != 0) {
            zz->trans_TIME = 3;
            if ((((uint32_t)zz->cur_PhonFlags_CF >> 12) & 1) != 0 || zz->cur_Phon_CF == 53 || zz->cur_Phon_CF == 54 || zz->cur_Phon_CF == 55) {
                ampT = cb->curP_END_Targ;
            }
        }
        if (ampT > zz->trans_LEVEL) {
            zz->trans_LEVEL = ampT - 3;
            zz->trans_TIME = 4;
        }
        if (zz->cur_ControlBlk_Index == 7 && (ampT > zz->trans_LEVEL || ampT > 0 && (((uint32_t)(uint16_t)zz->next_PhonCtrl_CF >> 14) & 1) != 0)) {
            zz->trans_LEVEL = ampT + 3;
            if (zz->next_Phon_CF == 23 || (((uint32_t)(uint16_t)zz->next_PhonCtrl_CF >> 14) & 1) != 0) {
                zz->trans_TIME = 15;
            }
        }
        if (zz->next_Phon_CF > 43 && ((((uint32_t)zz->cur_PhonFlags_CF >> 6) & 1) == 0 || zz->cur_ControlBlk_Index != 7)) {
            zz->trans_TIME = 0;
        }
        if (zz->cur_ControlBlk_Index == 8) {
            if ((zz->cur_Phon_CF == 36 || zz->cur_Phon_CF == 38 || zz->cur_Phon_CF == 40 || zz->cur_Phon_CF == 42) && (((uint32_t)zz->next_PhonFlags_CF >> 2) & 1) != 0 && (((uint32_t)zz->next_PhonFlags_CF >> 10) & 1) == 0) {
                zz->trans_TIME = 8;
                zz->trans_LEVEL = 52;
            }
            if ((zz->cur_PhonFlags_CF & 1) != 0 && zz->next_Phon_CF == 23) {
                zz->trans_TIME = 26;
                zz->trans_LEVEL = 52;
            }
        }
        break;
    }
    if (zz->trans_TIME > zz->cur_Phon_Dur_CF) {
        zz->trans_TIME = zz->cur_Phon_Dur_CF;
    }
    if (zz->trans_TIME > 26) {
        zz->trans_TIME = 26;
    }
    cb->TAIL_START_time = zz->cur_Phon_Dur_CF - zz->trans_TIME;
    if (zz->trans_TIME >= 0) {
        return;
    }
    zz->trans_TIME = 0;
}

/* Speech.c:2929  (0x9a270) */
int16_t Scale_Prcnt_to_PhonDur(formantVarPtr zz, int16_t percent)
{
    int32_t tempL;

    tempL = (percent * zz->cur_Phon_PctOfMaxDur_CF) >> 8;
    tempL = (zz->cur_Phon_MaxDur_CF * tempL / 100) >> 8;
    if (tempL > 0) {
        return (int16_t)tempL;
    }
    tempL = 1;
    return (int16_t)tempL;
}

/* Speech.c:2957  (0x9a31c) */
void Get_Diphthongs(formantVarPtr zz, int16_t index)
{
    ControlBlock *cb;
    int16_t cur_ControlBlk_Type;
    int16_t p1_Val;
    int16_t p2_Val;
    int16_t t1_Val;
    int16_t t2_Val;
    int32_t artic_Factor;
    int32_t tempL;
    int16_t rampTime;
    int16_t step_Size;

    cb = &zz->controlBlockArray[zz->cur_ControlBlk_Index];
    cur_ControlBlk_Type = zz->CtrlBlockTypeTbl[zz->cur_ControlBlk_Index];
    artic_Factor = 6550;
    cb->ptrToTargetList = zz->next_DiphEntry;
    p1_Val = zz->EnvelopeListTbl[index];
    index++;
    t1_Val = zz->EnvelopeListTbl[index];
    index++;
    p2_Val = zz->EnvelopeListTbl[index];
    index++;
    t2_Val = zz->EnvelopeListTbl[index];
    t1_Val = Scale_Prcnt_to_PhonDur(zz, t1_Val);
    t2_Val = Scale_Prcnt_to_PhonDur(zz, t2_Val);
    switch (cur_ControlBlk_Type) {
    case 0:
        if (cb->prevP_END_Targ > 0) {
            p1_Val = (((cb->prevP_END_Targ - p1_Val) * artic_Factor) >> 16) + p1_Val;
        }
        p1_Val = Adjust_Colored_Target(zz, zz->cur_PhonBuf_Index_CF, 0) + p1_Val;
        if (cb->nextP_START_Targ > 0) {
            p2_Val = (((cb->nextP_START_Targ - p2_Val) * artic_Factor) >> 16) + p2_Val;
        }
        p2_Val = Adjust_Colored_Target(zz, zz->cur_PhonBuf_Index_CF, 1) + p2_Val;
        break;
    case 1:
        AdjustGain(zz, zz->cur_PhonBuf_Index_CF, &p1_Val);
        AdjustGain(zz, zz->cur_PhonBuf_Index_CF, &p2_Val);
        break;
    }
    rampTime = t2_Val - t1_Val;
    tempL = (p2_Val - p1_Val) << 3;
    if (rampTime <= 99) {
        step_Size = (zz->One_Over_X_Tbl[rampTime] * tempL) >> 16;
    } else {
        step_Size = tempL / rampTime;
    }
    cb->curP_START_Targ = p1_Val;
    cb->curTarget_TIME = t1_Val;
    cb->curTarget_STEP = 0;
    (*zz->next_DiphEntry) = t2_Val;
    zz->next_DiphEntry++;
    (*zz->next_DiphEntry) = step_Size;
    zz->next_DiphEntry++;
    (*zz->next_DiphEntry) = zz->cur_Phon_Dur_CF;
    zz->next_DiphEntry++;
    (*zz->next_DiphEntry) = 0;
    zz->next_DiphEntry++;
    cb->curP_END_Targ = p2_Val;
}

/* Speech.c:3051  (0x9a77c) */
void Fill_Phon_Targets(formantVarPtr zz)
{
    ControlBlock *cb;
    int16_t i;

    if (zz->cur_PhonBuf_Index_CF == 0 && zz->big_Bang != 0) {
        zz->big_Bang = 0;
        for (zz->cur_ControlBlk_Index = 0; zz->cur_ControlBlk_Index <= 14; zz->cur_ControlBlk_Index++) {
            cb = &zz->controlBlockArray[zz->cur_ControlBlk_Index];
            cb->curP_END_Targ = Get_FIRST_Target(zz, zz->cur_PhonBuf_Index_CF);
        }
    }
    zz->cur_Phon_MaxDur_CF = zz->maxDurTbl[zz->cur_Phon_CF] / 5;
    if ((((uint32_t)zz->cur_PhonFlags_CF >> 10) & 1) == 0 && zz->cur_Phon_CF != 23) {
        zz->cur_Phon_PctOfMaxDur_CF = (zz->cur_Phon_Dur_CF << 16) / zz->cur_Phon_MaxDur_CF;
        zz->cur_Phon_PctOfMaxDur1_CF = (zz->cur_Phon_PctOfMaxDur_CF >> 1) + 0x8000;
        zz->cur_Phon_PctOfMaxDur2_CF = zz->cur_Phon_PctOfMaxDur1_CF - 6550;
    }
    zz->next_DiphEntry = &zz->diphEntryArray[0];
    for (i = 0; i <= 14; i++) {
        cb = &zz->controlBlockArray[i];
        cb->onset_END_TIME = 0;
    }
}

/* Speech.c:3098  (0x9a9b0) */
void Init_Ctrls_for_New_Phon(formantVarPtr zz)
{
    ControlBlock *cb;
    int16_t cur_ControlBlk_Type;
    int32_t tempL;
    int16_t tempS;

    vw_stack_slot_c0 = VW_STACK_ADDRESS;   /* see STACK_SLOT_WRITERS */
    Fill_Phon_Targets(zz);
    for (zz->cur_ControlBlk_Index = 0; zz->cur_ControlBlk_Index <= 14; zz->cur_ControlBlk_Index++) {
        cb = &zz->controlBlockArray[zz->cur_ControlBlk_Index];
        cur_ControlBlk_Type = zz->CtrlBlockTypeTbl[zz->cur_ControlBlk_Index];
        cb->prevP_END_Targ = cb->curP_END_Targ;
        cb->nextP_START_Targ = Get_FIRST_Target(zz, zz->cur_PhonBuf_Index_CF + 1);
        cb->curTarget_OFFS = 0;
        cb->curP_START_Targ = GetTarget(zz, zz->cur_PhonBuf_Index_CF);
        if (cb->curP_START_Targ < -1) {
            Get_Diphthongs(zz, (int16_t)(cb->curP_START_Targ & 0x7fff));
        } else {
            AdjustGain(zz, zz->cur_PhonBuf_Index_CF, (int16_t *)cb);
            cb->curTarget_STEP = 0;
            cb->curTarget_TIME = zz->cur_Phon_Dur_CF;
            if (cur_ControlBlk_Type == 0) {
                tempL = 6550;
                if ((zz->phon_Ctrl_Buf[zz->cur_PhonBuf_Index_CF] & 7168) != 0) {
                    if (zz->cur_ControlBlk_Index == 1) {
                        tempL = 16375;
                    } else {
                        tempL = 9825;
                    }
                }
                cb->curP_START_Targ += ((((cb->prevP_END_Targ + cb->nextP_START_Targ) >> 1) - cb->curP_START_Targ) * tempL) >> 16;
            }
            cb->curP_END_Targ = cb->curP_START_Targ;
        }
        if (cur_ControlBlk_Type == 0) {
            tempL = 6550;
            cb->nextP_START_Targ += ((cb->curP_END_Targ - cb->nextP_START_Targ) * tempL) >> 16;
        }
        zz->trans_LEVEL = (cb->prevP_END_Targ + cb->curP_START_Targ) >> 1;
        zz->trans_TIME = 6;
        Head_Rules(zz);
        cb->HEAD_offs = 0;
        if (zz->trans_TIME > 0) {
            cb->HEAD_offs = (zz->trans_LEVEL - cb->curP_START_Targ) << 3;
            if (cb->HEAD_offs != 0) {
                tempL = (zz->One_Over_X_Tbl[zz->trans_TIME] * cb->HEAD_offs) >> 16;
                cb->HEAD_step = tempL;
                cb->HEAD_offs = zz->trans_TIME * tempL;
            }
        }
        zz->trans_LEVEL = (cb->curP_END_Targ + cb->nextP_START_Targ) >> 1;
        zz->trans_TIME = 5;
        Tail_Rules(zz);
        cb->TAIL_offs = 0;
        cb->TAIL_step = 0;
        if (zz->trans_TIME > 0) {
            tempS = (zz->trans_LEVEL - cb->curP_END_Targ) << 3;
            if (tempS != 0) {
                cb->TAIL_step = (zz->One_Over_X_Tbl[zz->trans_TIME] * tempS) >> 16;
            }
        }
    }
    Insert_Burst(zz);
}

/* Speech.c:3237  (0x9aebc) */
void Interpolate_Formants(formantVarPtr zz)
{
    ControlBlock *cb;
    int16_t i;
    int16_t offset;
    int16_t val;

    for (i = 0; i <= 6; i++) {
        cb = &zz->controlBlockArray[i];
        if (zz->dur_Done_in_Phon_CF > cb->curTarget_TIME) {
            cb->curTarget_TIME = *cb->ptrToTargetList;
            cb->ptrToTargetList++;
            cb->curTarget_STEP = *cb->ptrToTargetList;
            cb->ptrToTargetList++;
            cb->curP_START_Targ += cb->curTarget_OFFS >> 3;
            cb->curTarget_OFFS = 0;
        }
        cb->curTarget_OFFS += cb->curTarget_STEP;
        offset = cb->curTarget_OFFS + cb->HEAD_offs;
        if (cb->HEAD_offs != 0) {
            cb->HEAD_offs -= cb->HEAD_step;
        }
        if (zz->dur_Done_in_Phon_CF >= cb->TAIL_START_time) {
            offset = cb->TAIL_offs + offset;
            cb->TAIL_offs += cb->TAIL_step;
        }
        val = cb->curP_START_Targ + (offset >> 3);
        zz->controlData[i] = val;
        if (cb->onset_END_TIME > 0 && zz->dur_Done_in_Phon_CF < cb->onset_END_TIME) {
            zz->controlData[i] = cb->onset_VAL;
        }
    }
    for (i = 7; i <= 14; i++) {
        cb = &zz->controlBlockArray[i];
        offset = cb->curP_START_Targ + (cb->HEAD_offs >> 3);
        if (cb->HEAD_offs != 0) {
            cb->HEAD_offs -= cb->HEAD_step;
        }
        if (zz->dur_Done_in_Phon_CF >= cb->TAIL_START_time) {
            offset = (cb->TAIL_offs >> 3) + offset;
            cb->TAIL_offs += cb->TAIL_step;
        }
        zz->controlData[i] = offset;
        if (cb->onset_END_TIME > 0) {
            if (zz->dur_Done_in_Phon_CF < cb->onset_END_TIME) {
                zz->controlData[i] = cb->onset_VAL;
            } else if (i > 8 && zz->dur_Done_in_Phon_CF == cb->onset_END_TIME + 1 && zz->controlData[i] > 10) {
                zz->controlData[i] -= 10;
            }
        }
    }
}

/* Speech.c:3380  (0x9b3a0) */
static void JumpToNoteTarget(formantVarPtr zz, int16_t note)
{
    zz->VP_baselinePitch = (float)e_MidiToPitch((int16_t)(note << 8));
    zz->portamentoAccum = zz->VP_baselinePitch;
    zz->portamentoStep = 0.0f;
    zz->newPortaTarget = 0;
}

/* Speech.c:3389  (0x9b460) */
void DoNote(formantVarPtr zz, int16_t note)
{
    mFloat disp;
    mFloat stepMult;
    mFloat numOfSteps;

    zz->curVel = (float)zz->noteVel;
    zz->curVel /= 64.0f;
    if (zz->VP_baselinePitch == 0.0f) {
        JumpToNoteTarget(zz, note);
        return;
    }
    zz->VP_baselinePitch = (float)e_MidiToPitch((int16_t)(note << 8));
    disp = zz->VP_baselinePitch - zz->portamentoAccum;
    if (disp >= 0.0f) {
        if (zz->portamento >= disp) {
            JumpToNoteTarget(zz, note);
            return;
        }
        numOfSteps = disp / zz->portamento;
        stepMult = (float)pow((double)disp, 1.0 / numOfSteps);
        zz->portamentoStep = (float)pow((double)stepMult, numOfSteps - 1.0);
        zz->portamentoScale = 1.0f / stepMult;
        zz->newPortaTarget = 1;
        return;
    }
    disp = 0.0f - disp;
    if (zz->portamento >= disp) {
        JumpToNoteTarget(zz, note);
        return;
    }
    numOfSteps = disp / zz->portamento;
    stepMult = (float)pow((double)disp, 1.0 / numOfSteps);
    zz->portamentoStep = (float)(0.0 - pow((double)stepMult, numOfSteps - 1.0));
    zz->portamentoScale = 1.0f / stepMult;
    zz->newPortaTarget = 1;
}

/* Speech.c:3451  (0x9b78c) */
void StartNewPhon(formantVarPtr zz)
{
    int16_t nextIndex;

    vw_stack_slot_c0 = VW_STACK_ADDRESS;   /* see STACK_SLOT_WRITERS */
    zz->dur_Done_in_Phon_CF = 0;
    zz->cur_Phon_Dur_CF = zz->dur_Buf[zz->cur_PhonBuf_Index_CF];
    if (zz->cur_PhonBuf_Index_CF == 0) {
        zz->prev_Phon_CF = 23;
        zz->prev_PhonCtrl_CF = 0;
        zz->prev2_Phon_CF = 23;
        zz->prev2_PhonCtrl_CF = 0;
    } else {
        zz->prev2_Phon_CF = zz->prev_Phon_CF;
        zz->prev2_PhonCtrl_CF = zz->prev_PhonCtrl_CF;
        zz->prev_Phon_CF = zz->cur_Phon_CF;
        zz->prev_PhonCtrl_CF = zz->cur_PhonCtrl_CF;
    }
    zz->prev_PhonFlags_CF = zz->phonFlags2[zz->prev_Phon_CF];
    zz->cur_Phon_CF = e_GetPhon(zz, zz->cur_PhonBuf_Index_CF);
    zz->cur_PhonCtrl_CF = e_GetPhonCtrl(zz, zz->cur_PhonBuf_Index_CF);
    zz->cur_PhonFlags_CF = zz->phonFlags2[zz->cur_Phon_CF];
    nextIndex = zz->cur_PhonBuf_Index_CF + 1;
    zz->next_Phon_CF = e_GetPhon(zz, nextIndex);
    zz->next_PhonCtrl_CF = e_GetPhonCtrl(zz, nextIndex);
    zz->next_PhonFlags_CF = zz->phonFlags2[zz->next_Phon_CF];
}

/* Speech.c:3506  (0x9b9b0) */
void Interpolate_Pitch(formantVarPtr zz)
{
    int16_t vibrato;
    int32_t notePitch;

    if (zz->newPortaTarget != 0) {
        zz->susTime = 0;
        if (zz->portamentoStep > 0.0f) {
            zz->portamentoAccum = zz->VP_baselinePitch - zz->portamentoStep;
            zz->portamentoStep *= zz->portamentoScale;
            if (zz->portamentoStep <= 1.0f) {
                zz->portamentoAccum = zz->VP_baselinePitch;
                zz->newPortaTarget = 0;
            }
        } else if (zz->portamentoStep < 0.0f) {
            zz->portamentoAccum = zz->VP_baselinePitch - zz->portamentoStep;
            zz->portamentoStep *= zz->portamentoScale;
            if (zz->portamentoStep >= -1.0f) {
                zz->portamentoAccum = zz->VP_baselinePitch;
                zz->newPortaTarget = 0;
            }
        } else {
            zz->portamentoAccum = zz->VP_baselinePitch;
            zz->newPortaTarget = 0;
        }
    } else {
        zz->susTime++;
    }
    notePitch = FTOI(zz->portamentoAccum);
    zz->controlF0 = zz->voiceDetune + notePitch + zz->pitchBend;
    zz->vibrato_Phase1 = (zz->vibratoFreq + zz->vibrato_Phase1) & 0xffffff;
    vibrato = zz->SineWavePtr[zz->vibrato_Phase1 >> 16] - 128;
    if (zz->susTime > 150) {
        zz->controlF0 += (vibrato * zz->vibratoDepth1) >> 16;
    } else {
        zz->controlF0 += (vibrato * zz->vibratoDepth2) >> 16;
    }
    if (zz->controlF0 >= 0) {
        return;
    }
    zz->controlF0 = 0;
}

/* Speech.c:3577  (0x9bcac) */
void Syllable_DurationX(formantVarPtr zz, int16_t cur_Index, int16_t total_Dur)
{
    synthVarsPtr xx;
    mFloat tempF;
    int16_t prev_Phon;
    int16_t cur_Phon;
    int16_t cur_PhonCtrl;
    int32_t cur_PhonFlags;
    int16_t i;
    int32_t note_Dur;
    int32_t dur_Adjust;
    int32_t cur_Dur;
    int32_t endP;
    int32_t new_Total;
    int32_t scale;
    int16_t calcDur;

    xx = (synthVarsPtr)zz->musicVars;
    prev_Phon = e_GetPhon(zz, (int16_t)((uint16_t)cur_Index - 1));
    cur_Phon = e_GetPhon(zz, cur_Index);
    cur_PhonCtrl = e_GetPhonCtrl(zz, cur_Index);
    calcDur = 0;
    if ((cur_PhonCtrl & 1) != 0) {
        calcDur = 1;
    } else if (prev_Phon == 23) {
        calcDur = 1;
        total_Dur = zz->dur_Buf[cur_Index];
        for (i = cur_Index + 1; i < zz->numOfPhons; i++) {
            cur_Phon = e_GetPhon(zz, i);
            cur_PhonCtrl = e_GetPhonCtrl(zz, i);
            if ((cur_PhonCtrl & 1) != 0) {
                if (cur_Phon != 23) {
                    cur_Index = i;
                } else {
                    cur_Phon = e_GetPhon(zz, cur_Index);
                }
                break;
            }
            total_Dur = zz->dur_Buf[i] + total_Dur;
        }
    }
    if (calcDur == 0) {
        return;
    }
    tempF = zz->noteDur * xx->timeWarp;
    note_Dur = FTOI(floor((double)tempF));
    if (cur_Phon == 23) {
        zz->dur_Buf[cur_Index] = note_Dur - total_Dur;
        return;
    }
    cur_Dur = zz->dur_Buf[cur_Index];
    total_Dur = cur_Dur + total_Dur;
    for (i = cur_Index + 1; i < zz->numOfPhons; i++) {
        cur_Phon = e_GetPhon(zz, i);
        cur_PhonCtrl = e_GetPhonCtrl(zz, i);
        if ((cur_PhonCtrl & 1) != 0) {
            endP = i;
            if (total_Dur <= note_Dur) {
                dur_Adjust = note_Dur - total_Dur;
                cur_Dur += dur_Adjust;
                zz->dur_Buf[cur_Index] = cur_Dur;
                return;
            }
            scale = (note_Dur << 16) / total_Dur;
            new_Total = 0;
            for (i = cur_Index; i < endP; i++) {
                cur_Dur = (zz->dur_Buf[i] * scale) >> 16;
                if (cur_Dur == 0) {
                    cur_Dur = 1;
                }
                new_Total += cur_Dur;
                zz->dur_Buf[i] = cur_Dur;
            }
            while (new_Total < note_Dur) {
                zz->dur_Buf[cur_Index]++;
                new_Total++;
            }
            return;
        }
        total_Dur = zz->dur_Buf[i] + total_Dur;
    }
}

/* Speech.c:3711  (0x9c1a4) */
void Syllable_Duration(formantVarPtr zz, int16_t cur_Index, int16_t total_Dur)
{
    synthVarsPtr xx;
    mFloat tempF;
    int16_t cur_Phon;
    int16_t cur_PhonCtrl;
    int32_t cur_PhonFlags;
    int16_t i;
    int32_t note_Dur;
    int32_t dur_Adjust;
    int32_t cur_Dur;
    int32_t endP;
    int32_t new_Total;
    int32_t scale;

    xx = (synthVarsPtr)zz->musicVars;
    cur_Phon = e_GetPhon(zz, cur_Index);
    cur_PhonCtrl = e_GetPhonCtrl(zz, cur_Index);
    if ((cur_PhonCtrl & 1) == 0) {
        return;
    }
    tempF = zz->noteDur * xx->timeWarp;
    note_Dur = FTOI(floor((double)tempF));
    cur_Dur = zz->dur_Buf[cur_Index];
    total_Dur = cur_Dur + total_Dur;
    for (i = cur_Index + 1; i < zz->numOfPhons; i++) {
        cur_Phon = e_GetPhon(zz, i);
        cur_PhonCtrl = e_GetPhonCtrl(zz, i);
        if ((cur_PhonCtrl & 1) != 0) {
            endP = i;
            if (total_Dur <= note_Dur) {
                dur_Adjust = note_Dur - total_Dur;
                cur_Dur += dur_Adjust;
                zz->dur_Buf[cur_Index] = cur_Dur;
                return;
            }
            scale = (note_Dur << 16) / total_Dur;
            new_Total = 0;
            for (i = cur_Index; i < endP; i++) {
                cur_Dur = (zz->dur_Buf[i] * scale) >> 16;
                if (cur_Dur == 0) {
                    cur_Dur = 1;
                }
                new_Total += cur_Dur;
                zz->dur_Buf[i] = cur_Dur;
            }
            while (new_Total < note_Dur) {
                zz->dur_Buf[cur_Index]++;
                new_Total++;
            }
            return;
        }
        total_Dur = zz->dur_Buf[i] + total_Dur;
    }
}

/* Speech.c:3792  (0x9c4f0) */
void e_Fill_Next_Frame(formantVarPtr zz)
{
    if (zz->newNote != 0) {
        for (;;) {
            if (zz->cur_PhonBuf_Index_CF < zz->numOfPhons - 1) {
                if (zz->firstNote != 0) {
                    zz->firstNote = 0;
                } else {
                    zz->cur_PhonBuf_Index_CF++;
                }
                if ((e_GetPhonCtrl(zz, zz->cur_PhonBuf_Index_CF) & 1) == 0) {
                    continue;
                }
                zz->newNote = 0;
                zz->speakState = 1;
                zz->freezeFrame = 0;
                DoNote(zz, zz->noteKey);
            } else {
                zz->speakState = 3;
            }
            break;
        }
    }
    if (zz->speakState == 1) {
        Syllable_Duration(zz, zz->cur_PhonBuf_Index_CF, 0);
        StartNewPhon(zz);
        Init_Ctrls_for_New_Phon(zz);
        if ((((uint32_t)(uint16_t)zz->cur_PhonCtrl_CF >> 1) & 1) != 0 && (zz->cur_PhonCtrl_CF & 1) == 0) {
            DoNote(zz, zz->nextNote);
        }
        zz->speakState = 2;
        zz->starting_New_Phon = 1;
    }
    if (zz->speakState != 2) {
        return;
    }
    if (zz->freezeFrame == 0) {
        Interpolate_Pitch(zz);
        Interpolate_Formants(zz);
        SaveFrame(zz);
        if (zz->curFrameBuf == 0) {
            zz->curFrameBuf = 1;
        } else {
            zz->curFrameBuf = 0;
        }
        zz->dur_Done_in_Phon_CF++;
        if (zz->dur_Done_in_Phon_CF >= zz->cur_Phon_Dur_CF) {
            if (zz->cur_PhonBuf_Index_CF < zz->numOfPhons - 1) {
                if ((zz->next_PhonCtrl_CF & 1) != 0) {
                    zz->freezeFrame = 1;
                } else {
                    zz->cur_PhonBuf_Index_CF++;
                    zz->speakState = 1;
                }
            } else {
                zz->speakState = 3;
            }
        }
    }
    zz->starting_New_Phon = 0;
}

/* Speech.c:3891  (0x9c81c) */
void e_Fill_Next_Frame_MIDI(formantVarPtr zz)
{
    if (zz->newNote != 0) {
        for (;;) {
            if (zz->cur_PhonBuf_Index_CF < zz->numOfPhons - 1) {
                if ((e_GetPhonCtrl(zz, zz->cur_PhonBuf_Index_CF) & 1) != 0) {
                    zz->newNote = 0;
                    zz->speakState = 1;
                    zz->freezeFrame = 0;
                    DoNote(zz, zz->noteKey);
                    break;
                }
                zz->cur_PhonBuf_Index_CF++;
                continue;
            }
            zz->speakState = 3;
            zz->speechIsActive = 0;
            break;
        }
    }
    if (zz->speakState == 1) {
        StartNewPhon(zz);
        Init_Ctrls_for_New_Phon(zz);
        zz->speakState = 2;
        zz->starting_New_Phon = 1;
    }
    if (zz->speakState != 2) {
        return;
    }
    Interpolate_Pitch(zz);
    Interpolate_Formants(zz);
    SaveFrame(zz);
    if (zz->curFrameBuf == 0) {
        zz->curFrameBuf = 1;
    } else {
        zz->curFrameBuf = 0;
    }
    zz->dur_Done_in_Phon_CF++;
    if (zz->dur_Done_in_Phon_CF >= zz->cur_Phon_Dur_CF) {
        if (zz->cur_PhonBuf_Index_CF < zz->numOfPhons - 1) {
            zz->cur_PhonBuf_Index_CF++;
            zz->speakState = 1;
        } else {
            zz->speakState = 3;
            zz->speechIsActive = 0;
        }
    }
    zz->starting_New_Phon = 0;
}

/* Speech.c:3968  (0x9ca90) */
static void SetTotalVolume(formantVarPtr zz)
{
    zz->speechVolume = zz->trackLevel * zz->volumeCmd;
    zz->voiceNoiseGain = zz->setNoiseGain * zz->speechVolume * zz->noiseScale;
}

/* Speech.c:3982  (0x9cb04) */
static float *MakePulse(formantVarPtr zz, float max)
{
    float *shape;
    float tp;
    float tn;
    float t;
    int16_t totalOpen;
    float tempF1;
    float tempF2;
    float ratio;
    int16_t i;

    max += max;
    for (i = 0; i <= 0xff; i++) {
        zz->voiceWaveform1[i] = 0.0f;
        zz->voiceWaveform[i] = zz->voiceWaveform1[i];
    }
    tp = 51.2f;
    tn = 20.48f;
    totalOpen = FTOI(tp + tn);
    if (totalOpen > 256) {
        totalOpen = 256;
    }
    for (i = 0; i < totalOpen; i++) {
        t = (float)i;
        if (t <= tp) {
            ratio = t / tp;
            tempF2 = ratio * ratio * ratio;
            tempF2 += tempF2;
            tempF1 = ratio * ratio;
            tempF1 *= 3.0f;
            tempF1 = tempF1 - tempF2 - 0.5f;
            zz->voiceWaveform[i] = tempF1 * max;
            zz->voiceWaveform1[i] = zz->voiceWaveform[i];
        } else {
            tempF1 = (t - tp) / tn;
            tempF1 *= tempF1;
            tempF1 = 1.0f - tempF1 - 0.5f;
            zz->voiceWaveform[i] = tempF1 * max;
            zz->voiceWaveform1[i] = zz->voiceWaveform[i];
        }
    }
    return zz->voiceWaveform;
}

/* Speech.c:4030  (0x9ce08) */
void InvDFT(formantVarPtr zz, voiceDataPtr vd)
{
    int16_t i;
    int16_t j;
    int16_t sIndex;
    rShort amp;
    rShort amp1;
    int16_t sIndex1;
    int16_t *hPtr;
    int16_t *hPtr1;
    rLong max;
    rLong max1;
    rLong max2;
    rLong hold;
    rLong voiceWaveGain;

    voiceWaveGain = (float)vd->vGain;
    voiceWaveGain /= 200.0f;
    for (j = 0; j <= 0xff; j++) {
        zz->voiceWaveform[j] = 0.0f;
        zz->voiceWaveform1[j] = 0.0f;
    }
    hPtr = &vd->vWave[0];
    hPtr1 = &vd->vWave1[0];
    for (i = 0; i <= 47; i++) {
        amp = (float)hPtr[i] * voiceWaveGain;
        amp1 = (float)hPtr1[i] * voiceWaveGain;
        sIndex = 0;
        sIndex1 = 0;
        for (j = 0; j <= 0xff; j++) {
            zz->voiceWaveform[j] += zz->SineWave15Ptr[sIndex] * amp;
            zz->voiceWaveform1[j] += zz->SineWave15Ptr[sIndex] * amp1;
            sIndex += i;
            if (sIndex > 0xff) {
                sIndex -= 256;
            }
            sIndex1 += i;
            if (sIndex1 > 0xff) {
                sIndex1 -= 256;
            }
        }
    }
    max = 0.0f;
    max1 = 0.0f;
    for (j = 0; j <= 0xff; j++) {
        hold = zz->voiceWaveform[j];
        if (hold < 0.0f) {
            hold = 0.0f - hold;
        }
        if (hold > max) {
            max = hold;
        }
        hold = zz->voiceWaveform1[j];
        if (hold < 0.0f) {
            hold = 0.0f - hold;
        }
        if (hold > max1) {
            max1 = hold;
        }
    }
    if (!(max1 > 0.0f)) {
        return;
    }
    max2 = max / max1;
    for (j = 0; j <= 0xff; j++) {
        zz->voiceWaveform1[j] *= max2;
    }
}

/* Speech.c:4141  (0x9d2c4) */
static void InitSampleGlott(formantVarPtr zz, voiceDataPtr vd)
{
    int16_t tempS;
    int32_t pitchH;
    synthVarsPtr xx;

    xx = (synthVarsPtr)zz->musicVars;
    tempS = vd->waveRefA;
    zz->s_waveAddrA = (int16_t *)&xx->Wave_Data[tempS].waveName[xx->Wave_Data[tempS].waveOffset];
    zz->s_waveSizeA = xx->Wave_Data[tempS].waveLen << 12;
    tempS = vd->waveRefB;
    zz->s_waveAddrB = (int16_t *)&xx->Wave_Data[tempS].waveName[xx->Wave_Data[tempS].waveOffset];
    zz->s_waveSizeB = xx->Wave_Data[tempS].waveLen << 12;
    tempS = vd->oscMode;
    if (tempS > 5 || tempS < 0) {
        tempS = 0;
    }
    tempS <<= 1;
    zz->s_oscConfigA = xx->OscModeTbl[tempS];
    tempS++;
    zz->s_oscConfigB = xx->OscModeTbl[tempS];
    tempS = vd->octTuneA;
    if (tempS > 6) {
        tempS = 6;
    } else if (tempS < 0) {
        tempS = 0;
    }
    pitchH = xx->Oct_Tbl[tempS] + 384;
    tempS = vd->semiTuneA;
    if (tempS > 11 || tempS < 0) {
        tempS = 0;
    }
    pitchH += tempS << 5;
    tempS = vd->fineTuneA;
    if (tempS > 63 || tempS < 0) {
        tempS = 0;
    }
    zz->s_pitchA = (tempS >> 1) + pitchH;
    tempS = vd->octTuneB;
    if (tempS > 6) {
        tempS = 6;
    } else if (tempS < 0) {
        tempS = 0;
    }
    pitchH = xx->Oct_Tbl[tempS] + 384;
    tempS = vd->semiTuneB;
    if (tempS > 11 || tempS < 0) {
        tempS = 0;
    }
    pitchH += tempS << 5;
    tempS = vd->fineTuneB;
    if (tempS > 63 || tempS < 0) {
        tempS = 0;
    }
    zz->s_pitchB = (tempS >> 1) + pitchH;
    zz->s_waveVolA = (float)vd->oscVolA;
    zz->s_waveVolA /= 100.0f;
    zz->s_waveVolB = (float)vd->oscVolB;
    zz->s_waveVolB /= 100.0f;
}

/* Speech.c:4233  (0x9d79c) */
void InitVoice(formantVarPtr zz, voiceDataPtr vd)
{
    int16_t temp_Pitch;
    rLong tempLong;

    zz->glotType = vd->waveType;
    if (vd->waveType == 1) {
        tempLong = (float)vd->sGain;
        zz->wavesampleGain = (float)(tempLong / 100.0 / 21.0);
        zz->sync_On_Vowel = vd->sync;
        InitSampleGlott(zz, vd);
        InitSampOsc(zz);
    } else {
        zz->sync_On_Vowel = 0;
    }
    zz->voice_Num = vd->voice;
    zz->breathCycle = vd->aCycle;
    if (!(zz->breathGainCtrl != -1.0f && zz->breathCycle > 16)) {
        tempLong = (float)vd->aGain;
        tempLong /= 100.0f;
        zz->breathGain = (float)(tempLong * 50.0f * 0.4);
    } else {
        zz->breathGain = zz->breathGainCtrl;
    }
    zz->voice_F4_Freq = e_HzToPitch(zz, vd->f4_Freq);
    zz->voice_F4_BW = vd->f4_BW;
    zz->voice_F5_Freq = e_HzToPitch(zz, vd->f5_Freq);
    zz->voice_F5_BW = vd->f5_BW;
    zz->f4_Par = e_HzToPitch(zz, vd->f4p_Freq);
    zz->bw4_Par = vd->f4p_BW;
    zz->f5_Par = e_HzToPitch(zz, vd->f5p_Freq);
    zz->bw5_Par = vd->f5p_BW;
    zz->f6_Par = e_HzToPitch(zz, vd->f6p_Freq);
    zz->bw6_Par = vd->f6p_BW;
    zz->nasalBaseFreq = vd->nasal_Base;
    zz->nasalTargFreq = vd->nasal_targ;
    zz->fNP = e_HzToPitch(zz, zz->nasalBaseFreq);
    zz->bNP = vd->nasal_BW;
    InitFixedFormants(zz);
    zz->voiceBWgain1 = vd->bwGain1;
    zz->voiceBWgain1 = (zz->voiceBWgain1 << 16) / 100;
    zz->voiceBWgain2 = vd->bwGain2;
    zz->voiceBWgain2 = (zz->voiceBWgain2 << 16) / 100;
    zz->voiceBWgain3 = vd->bwGain3;
    zz->voiceBWgain3 = (zz->voiceBWgain3 << 16) / 100;
    zz->voiceF1Gain = vd->f1_Offset;
    zz->voiceF2Gain = vd->f2_Offset;
    zz->voiceF3Gain = vd->f3_Offset;
    tempLong = (float)vd->nGain;
    zz->voiceNoiseGain = tempLong / 100.0f;
    zz->chorusBase = vd->chorus;
    zz->voiceChorus = zz->chorusBase + zz->chorusOffs;
    zz->voiceNoiseGain *= 1.5f;
    zz->setNoiseGain = zz->voiceNoiseGain;
    SetTotalVolume(zz);
    if (vd->customForm != 0) {
        zz->voice_Formants[0] = zz->a_f1FreqTblM;
        zz->voice_Formants[1] = zz->a_f2FreqTblM;
        zz->voice_Formants[2] = zz->a_f3FreqTblM;
        zz->voice_Formants[3] = zz->a_b1FreqTblM;
        zz->voice_Formants[4] = zz->a_b2FreqTblM;
        zz->voice_Formants[5] = zz->a_b3FreqTblM;
        zz->voice_av_Tbl = zz->a_avVolTblM;
        zz->EnvelopeListTbl = zz->a_EnvelopeListTbl;
    } else {
        zz->EnvelopeListTbl = zz->IntEnvelopeListTbl;
        if (zz->voice_Num == 0) {
            zz->voice_Formants[0] = zz->f1FreqTblM;
            zz->voice_Formants[1] = zz->f2FreqTblM;
            zz->voice_Formants[2] = zz->f3FreqTblM;
            zz->voice_Formants[3] = zz->b1FreqTblM;
            zz->voice_Formants[4] = zz->b2FreqTblM;
            zz->voice_Formants[5] = zz->b3FreqTblM;
            zz->voice_av_Tbl = zz->avVolTblM;
        } else {
            zz->voice_Formants[0] = zz->f1FreqTblF;
            zz->voice_Formants[1] = zz->f2FreqTblF;
            zz->voice_Formants[2] = zz->f3FreqTblF;
            zz->voice_Formants[3] = zz->b1FreqTblF;
            zz->voice_Formants[4] = zz->b2FreqTblF;
            zz->voice_Formants[5] = zz->b3FreqTblF;
            zz->voice_av_Tbl = zz->avVolTblF;
        }
    }
    if (zz->voice_Num == 0) {
        zz->voice_NoiseAmp_Tbl = zz->Male_NoiseAmpTbl;
        zz->voice_Locus_Tbl = zz->Male_Loci_Tbl;
        zz->voiceMinBW = 50;
    } else {
        zz->voice_NoiseAmp_Tbl = zz->Female_NoiseAmpTbl;
        zz->voice_Locus_Tbl = zz->Female_Loci_Tbl;
        zz->voiceMinBW = 50;
    }
    if (vd->AsperW == 0) {
        zz->breathWave = zz->BandNoisePtr;
    } else if (vd->AsperW == 1) {
        zz->breathWave = zz->NoiseWavePtr;
    } else {
        zz->breathWave = zz->HPNoisePtr;
    }
    zz->locusOffset = vd->locus;
    zz->nasalAmt = vd->nasalAmt;
    zz->VP_baselinePitch = 0.0f;
    zz->portamentoAccum = 0.0f;
    zz->portamentoStep = 0.0f;
    zz->newPortaTarget = 0;
    if (vd->emphVoice > 0) {
        zz->hfEmph = 1;
        zz->emphB = (float)vd->emphVoice;
        zz->emphB /= 100.0f;
        zz->emphA = 2.0f - zz->emphB;
    } else {
        zz->hfEmph = 0;
    }
    zz->lastBreath = 0.0f;
}

/* Speech.c:4398  (0x9e034) */
void ResetVoice(formantVarPtr zz)
{
    InitVoice(zz, &zz->vd);
    InvDFT(zz, &zz->vd);
}

/* Speech.c:4431  (0x9e09c) */
int16_t CopyVoice(formantVarPtr zz, Ptr voice)
{
    int16_t i;
    int16_t *theNotes;
    int16_t error;
    voiceDataPtr vDat;

    vDat = (voiceDataPtr)voice;
    error = 0;
    zz->vd.voice = vDat->voice;
    zz->vd.vGain = vDat->vGain;
    zz->vd.aGain = vDat->aGain;
    zz->vd.aCycle = vDat->aCycle;
    zz->vd.f4_Freq = vDat->f4_Freq;
    zz->vd.f5_Freq = vDat->f5_Freq;
    zz->vd.f4_BW = vDat->f4_BW;
    zz->vd.f4p_Freq = vDat->f4p_Freq;
    zz->vd.f4p_BW = vDat->f4p_BW;
    zz->vd.f5p_Freq = vDat->f5p_Freq;
    zz->vd.f5p_BW = vDat->f5p_BW;
    zz->vd.f6p_Freq = vDat->f6p_Freq;
    zz->vd.f6p_BW = vDat->f6p_BW;
    zz->vd.nasal_Base = vDat->nasal_Base;
    zz->vd.nasal_targ = vDat->nasal_targ;
    zz->vd.nasal_BW = vDat->nasal_BW;
    zz->vd.locus = vDat->locus;
    zz->vd.bwGain1 = vDat->bwGain1;
    zz->vd.bwGain2 = vDat->bwGain2;
    zz->vd.bwGain3 = vDat->bwGain3;
    zz->vd.f1_Offset = vDat->f1_Offset;
    zz->vd.f2_Offset = vDat->f2_Offset;
    zz->vd.f3_Offset = vDat->f3_Offset;
    zz->vd.chorus = vDat->chorus;
    zz->vd.nGain = vDat->nGain;
    zz->vd.sPitch = vDat->sPitch;
    zz->vd.sGain = vDat->sGain;
    zz->vd.AsperW = vDat->AsperW;
    zz->vd.voiceVers = vDat->voiceVers;
    zz->vd.waveType = vDat->waveType;
    for (i = 0; i <= 47; i++) {
        zz->vd.vWave[i] = vDat->vWave[i];
        zz->vd.vWave1[i] = vDat->vWave1[i];
    }
    zz->vd.customForm = vDat->customForm;
    zz->vd.nasalAmt = vDat->nasalAmt;
    zz->vd.emphVoice = vDat->emphVoice;
    zz->vd.rvbDelay = vDat->rvbDelay;
    zz->vd.rvbDepth = vDat->rvbDepth;
    zz->vd.waveRefA = vDat->waveRefA;
    zz->vd.octTuneA = vDat->octTuneA;
    zz->vd.semiTuneA = vDat->semiTuneA;
    zz->vd.fineTuneA = vDat->fineTuneA;
    zz->vd.oscVolA = vDat->oscVolA;
    zz->vd.waveRefB = vDat->waveRefB;
    zz->vd.octTuneB = vDat->octTuneB;
    zz->vd.semiTuneB = vDat->semiTuneB;
    zz->vd.fineTuneB = vDat->fineTuneB;
    zz->vd.oscVolB = vDat->oscVolB;
    zz->vd.oscMode = vDat->oscMode;
    zz->vd.sync = vDat->sync;
    InitFixedFormants(zz);
    return error;
}

/* Speech.c:4535  (0x9e550) */
int16_t NewVoice(formantVarPtr zz, void *vDat)
{
    int16_t error;

    error = CopyVoice(zz, (Ptr)vDat);
    if (error != 0) {
        return error;
    }
    ResetVoice(zz);
    return error;
}

/* Speech.c:4548  (0x9e5d0) */
void PgmChange_Speech(synthVarsPtr xx, int16_t track, int16_t vNum)
{
    formantVarPtr zz;
    int16_t error;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->voiceRef = zz->GMVoiceMap[vNum];
    error = NewVoice(zz, (void *)zz->GMVoiceData[zz->voiceRef]);
}

/* Speech.c:4562  (0x9e69c) */
void DiffNoiseWave(formantVarPtr zz)
{
    int16_t i;
    mFloat tempD;
    mFloat first;

    first = *zz->BandNoisePtr;
    for (i = 0; i <= 4094; i++) {
        tempD = zz->BandNoisePtr[i];
        tempD -= zz->BandNoisePtr[i + 1];
        zz->BandNoisePtr[i] = tempD;
    }
    tempD = zz->BandNoisePtr[4095];
    tempD -= first;
    zz->BandNoisePtr[4095] = tempD;
}

/* Speech.c:4583  (0x9e7a8) */
static Ptr GetThePtr(Ptr basePtr, int32_t *tblPtr)
{
    Ptr dataPtr;
    int32_t offset;

    offset = *tblPtr;
    dataPtr = &basePtr[offset];
    return dataPtr;
}

/* Speech.c:4596  (0x9e804) */
static void SetSpeechTblAddr(formantVarPtr zz, int32_t *SpeechTbls)
{
    int32_t *tblPtr;
    Ptr basePtr;

    tblPtr = SpeechTbls;
    basePtr = (Ptr)SpeechTbls;
    zz->One_Over_X_Tbl = (int32_t *)GetThePtr(basePtr, tblPtr++);
    zz->TOPtr = (Fixed *)GetThePtr(basePtr, tblPtr++);
    zz->CtrlBlockTypeTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->Male_Loci_Tbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->Female_Loci_Tbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->Front_Loci_Tbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->Mid_Loci_Tbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->Back_Loci_Tbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->NoiseIndexTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->Male_NoiseAmpTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->Female_NoiseAmpTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->Rank_FWD_Tbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->Rank_BKWD_Tbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->DefaultTargTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->BurstDurTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->IntEnvelopeListTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->f1FreqTblM = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->f2FreqTblM = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->f3FreqTblM = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->b1FreqTblM = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->b2FreqTblM = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->b3FreqTblM = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->avVolTblM = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->f1FreqTblF = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->f2FreqTblF = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->f3FreqTblF = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->b1FreqTblF = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->b2FreqTblF = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->b3FreqTblF = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->avVolTblF = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->IntervalVoices = (voiceData *)GetThePtr(basePtr, tblPtr++);
    zz->CosTblPtr = (rShort *)GetThePtr(basePtr, tblPtr++);
    zz->BcoeffTblPtr = (rShort *)GetThePtr(basePtr, tblPtr++);
    zz->CcoeffTblPtr = (rShort *)GetThePtr(basePtr, tblPtr++);
    zz->SineWave15Ptr = (rShort *)GetThePtr(basePtr, tblPtr++);
    zz->NoiseWavePtr = (rUSC *)GetThePtr(basePtr, tblPtr++);
    zz->BandNoisePtr = (rUSC *)GetThePtr(basePtr, tblPtr++);
    zz->HPNoisePtr = (rUSC *)GetThePtr(basePtr, tblPtr++);
    zz->logToLinPtr = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->logOf2Tbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->SineWavePtr = GetThePtr(basePtr, tblPtr++);
    zz->phonFlags2 = (int32_t *)GetThePtr(basePtr, tblPtr++);
    zz->ExpOf2Tbl = (uint16_t *)GetThePtr(basePtr, tblPtr++);
    zz->OctFreqTbl = (uint16_t *)GetThePtr(basePtr, tblPtr++);
    zz->maxDurTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
    zz->minDurTbl = (int16_t *)GetThePtr(basePtr, tblPtr++);
}

/* Speech.c:4667  (0x9ef88) */
static void SetSeqAddr(formantVarPtr zz, int16_t *speechData)
{
    int32_t i;

    zz->voiceRef = zz->GMVoiceMap[*speechData];
    speechData++;
    zz->numOfPhons = *speechData;
    speechData++;
    zz->phon_Buf = speechData;
    zz->phon_Ctrl_Buf = &zz->phon_Buf[zz->numOfPhons];
    zz->dur_Buf1 = &zz->phon_Ctrl_Buf[zz->numOfPhons];
    zz->dur_Buf = &zz->dur_Buf1[zz->numOfPhons];
}

/* Speech.c:4695  (0x9f08c) */
int16_t NewSong_Speech(synthVarsPtr xx, int16_t track, int16_t *trackData)
{
    formantVarPtr zz;
    int16_t error;

    zz = (formantVarPtr)xx->speechVars[track];
    SetSeqAddr(zz, trackData);
    zz->noiseScale = 1.0f;
    error = NewVoice(zz, (void *)zz->GMVoiceData[zz->voiceRef]);
    return error;
}

/* Speech.c:4710  (0x9f150) */
int16_t InitGlobals_Speech(synthVarsPtr xx, int16_t track)
{
    formantVarPtr zz;
    int16_t error;

    error = 0;
    zz = (formantVarPtr)xx->speechVars[track];
    zz->musicGlobals = (void *)xx;
    SetSpeechTblAddr(zz, xx->SpeechTbls);
    zz->GMVoiceMap = (int16_t *)xx->GMVoicePtr;
    zz->GMVoiceData = (voiceDataPtr *)&zz->GMVoiceMap[256];
    zz->musicVars = (void *)xx;
    zz->starting_New_Phon = 0;
    zz->FormTables = NULL;
    zz->speechIsActive = 0;
    zz->freezeFrame = 0;
    zz->Time_Tbl = xx->Time_Tbl;
    zz->trackLevel = 1.0f;
    zz->MIDIMode = 0;
    zz->singEnabled = 0;
    return error;
}

/* Speech.c:4742  (0x9f280) */
void Sing_Speech(synthVarsPtr xx, int16_t track, int16_t singState)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->singEnabled = singState;
}

/* Speech.c:4753  (0x9f2ec) */
void Update_Speech(synthVarsPtr xx, int16_t track)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    if (zz->speechIsActive == 0) {
        return;
    }
    if (zz->MIDIMode != 0) {
        e_Fill_Next_Frame_MIDI(zz);
    } else {
        e_Fill_Next_Frame(zz);
    }
    if (zz->speakState == 3) {
        zz->waveIndex += 440;
        return;
    }
    SayFrame(zz);
}

/* Speech.c:4777  (0x9f3c4) */
void New_Update_Speech(synthVarsPtr xx, int16_t track)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->waveIndex = 0;
}

/* Speech.c:4787  (0x9f428) */
void SingMIDI_Speech(synthVarsPtr xx, int16_t track, int16_t note, int16_t vel)
{
    formantVarPtr zz;

}

/* Speech.c:4795  (0x9f470) */
void StartSILPhon(formantVarPtr zz)
{
    int16_t nextIndex;

    zz->dur_Done_in_Phon_CF = 0;
    zz->cur_Phon_Dur_CF = 0x7fff;
    zz->prev_Phon_CF = 23;
    zz->prev_PhonCtrl_CF = 0;
    zz->prev2_Phon_CF = 23;
    zz->prev2_PhonCtrl_CF = 0;
    zz->prev_PhonFlags_CF = zz->phonFlags2[23];
    zz->cur_Phon_CF = 23;
    zz->cur_PhonCtrl_CF = 0;
    zz->cur_PhonFlags_CF = zz->phonFlags2[23];
    nextIndex = zz->cur_PhonBuf_Index_CF + 1;
    zz->next_Phon_CF = e_GetPhon(zz, nextIndex);
    zz->next_PhonCtrl_CF = e_GetPhonCtrl(zz, nextIndex);
    zz->next_PhonFlags_CF = zz->phonFlags2[zz->next_Phon_CF];
}

/* Speech.c:4830  (0x9f5bc) */
void FindStartNucleus(formantVarPtr zz, int32_t nucleusNum)
{
    int32_t count;
    int32_t i;

    count = 0;
    for (zz->cur_PhonBuf_Index_CF = 0; zz->cur_PhonBuf_Index_CF < zz->numOfPhons; zz->cur_PhonBuf_Index_CF++) {
        if ((e_GetPhonCtrl(zz, zz->cur_PhonBuf_Index_CF) & 1) != 0) {
            count++;
            if (count >= nucleusNum) {
                break;
            }
        }
    }
    zz->cur_PhonBuf_Index_CF++;
}

/* Speech.c:4852  (0x9f6b8) */
static void InitDefaultVoiceCntrls(formantVarPtr zz)
{
    zz->vibratoDepth1 = 31;
    zz->vibratoDepth1 = (zz->vibratoDepth1 << 16) / 1000;
    zz->vibratoDepth2 = 16;
    zz->vibratoDepth2 = (zz->vibratoDepth2 << 16) / 1000;
    zz->vibratoFreq = 47;
    zz->vibratoFreq = (zz->vibratoFreq << 16) / 10;
    zz->vibratoFreq = (zz->vibratoFreq << 8) / 200;
    zz->portamento = (float)zz->Time_Tbl[20];
    zz->portamento /= 64.0f;
    zz->chorusOffs = 0;
    zz->chorusBase = zz->chorusOffs;
    zz->voiceChorus = zz->chorusBase;
    zz->voiceDetune = 0;
    zz->volumeCmd = 1.0f;
    zz->waveAmp1 = 0.75f;
    zz->waveAmp2 = 1.0f - zz->waveAmp1;
    zz->noiseScale = 1.0f;
    SetTotalVolume(zz);
}

/* Speech.c:4885  (0x9f8b8) */
void Start_Speech(synthVarsPtr xx, int16_t track)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    InitDefaultVoiceCntrls(zz);
    zz->speechIsActive = 0;
    zz->freezeFrame = 0;
    if (track == 0) {
        zz->vibrato_Phase1 = 0;
        zz->vibrato_Phase2 = 0;
    } else {
        zz->vibrato_Phase1 = 128;
        zz->vibrato_Phase2 = 128;
        zz->vibrato_Phase1 <<= 16;
        zz->vibrato_Phase2 <<= 16;
    }
    zz->speakState = 3;
    zz->newNote = 0;
    zz->firstNote = 1;
    zz->dur_Done_in_Phon_CF = 0;
    zz->cur_Phon_Dur_CF = 0;
    zz->curFrameBuf = 0;
    zz->VP_baselinePitch = 0.0f;
    zz->portamentoAccum = 0.0f;
    zz->portamentoStep = 0.0f;
    zz->newPortaTarget = 0;
    zz->breathGainCtrl = -1.0f;
    zz->noiseScale = 1.0f;
    zz->susTime = 0;
    zz->PB_Range = e_MidiToPitch1(512);
    zz->pitchBend = 0;
    Init_ControlBlocks(zz);
    InitSay(zz);
    zz->cur_PhonBuf_Index_CF = 0;
    NewVoice(zz, (void *)zz->GMVoiceData[zz->voiceRef]);
}

/* Speech.c:4955  (0x9fab0) */
void StartPoint_Speech(synthVarsPtr xx, int16_t track)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    if (xx->startNoteNum[track] == 0) {
        zz->cur_PhonBuf_Index_CF = 0;
    } else {
        FindStartNucleus(zz, xx->startNoteNum[track]);
        StartSILPhon(zz);
        Init_Ctrls_for_New_Phon(zz);
    }
    zz->speechIsActive = 1;
}

/* Speech.c:4983  (0x9fb90) */
void Speech_Note(synthVarsPtr xx, int16_t track, int16_t note, int16_t nextNote, int16_t vel, mFloat dur)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->noteKey = note;
    zz->noteVel = vel;
    zz->noteDur = dur;
    zz->nextNote = nextNote;
    zz->newNote = 1;
}

/* Speech.c:4996  (0x9fc40) */
void Speech_PitchBend(synthVarsPtr xx, int16_t track, int32_t amt)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->pitchBend = (zz->PB_Range * amt) >> 16;
}

/* Speech.c:5006  (0x9fcb8) */
void Speech_Detune(synthVarsPtr xx, int16_t track, int32_t amt)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->voiceDetune = (amt * 21) >> 16;
}

/* Speech.c:5017  (0x9fd3c) */
void Speech_PBSens(synthVarsPtr xx, int16_t track, int32_t amt)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->PB_Range = e_MidiToPitch1((int16_t)(amt << 8));
}

/* Speech.c:5026  (0x9fdc8) */
void Speech_Color(synthVarsPtr xx, int16_t track, int32_t amt)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->waveAmp1 = (float)amt;
    if (zz->waveAmp1 < 0.0f) {
        zz->waveAmp1 = 0.0f;
    } else if (zz->waveAmp1 > 127.0f) {
        zz->waveAmp1 = 1.0f;
    } else {
        zz->waveAmp1 /= 127.0f;
    }
    zz->waveAmp2 = 1.0f - zz->waveAmp1;
}

/* Speech.c:5043  (0x9fef4) */
void Speech_VibDepth(synthVarsPtr xx, int16_t track, int32_t amt)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->vibratoDepth2 = (amt << 16) / 1000;
    zz->vibratoDepth1 = zz->vibratoDepth2;
}

/* Speech.c:5053  (0x9ff88) */
void Speech_VibFreq(synthVarsPtr xx, int16_t track, int32_t amt)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->vibratoFreq = (amt << 16) / 10;
    zz->vibratoFreq = (zz->vibratoFreq << 8) / 200;
}

/* Speech.c:5064  (0xa0038) */
void Speech_Chorus(synthVarsPtr xx, int16_t track, int32_t amt)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->chorusOffs = amt;
    zz->voiceChorus = zz->chorusBase + zz->chorusOffs;
}

/* Speech.c:5076  (0xa00cc) */
void Speech_TrackLevel(synthVarsPtr xx, int16_t track, int32_t amt)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->trackLevel = (float)amt;
    zz->trackLevel /= 100.0f;
    SetTotalVolume(zz);
}

/* Speech.c:5089  (0xa0194) */
void Speech_Volume(synthVarsPtr xx, int16_t track, int32_t amt)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->volumeCmd = (float)amt;
    zz->volumeCmd /= 127.0f;
    SetTotalVolume(zz);
}

/* Speech.c:5103  (0xa025c) */
void Speech_Portamento(synthVarsPtr xx, int16_t track, int32_t amt)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->portamento = (float)zz->Time_Tbl[amt >> 1];
    zz->portamento /= 64.0f;
}

/* Speech.c:5114  (0xa0330) */
void Speech_Breath(synthVarsPtr xx, int16_t track, int32_t amt)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->breathGainCtrl = (float)(amt >> 1);
    zz->breathGain = zz->breathGainCtrl;
}

/* Speech.c:5131  (0xa03e0) */
void Stop_Speech(synthVarsPtr xx, int16_t track)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->speechIsActive = 0;
    zz->speakState = 3;
}

/* Speech.c:5141  (0xa0450) */
int16_t State_Speech(synthVarsPtr xx, int16_t track)
{
    formantVarPtr zz;
    int32_t t_28;

    zz = (formantVarPtr)xx->speechVars[track];
    if (zz->speakState == 3) {
        t_28 = 0;
        return (int16_t)t_28;
    }
    t_28 = 1;
    return (int16_t)t_28;
}

/* Speech.c:5160  (0xa04d8) */
void NewTempo_Speech(synthVarsPtr xx, int16_t track)
{
}

/* Speech.c:5165  (0xa0510) */
void NewTempo_SpeechXX(synthVarsPtr xx, int16_t track)
{
    formantVarPtr zz;
    mFloat tempF;
    mFloat scale;
    int32_t i;

    zz = (formantVarPtr)xx->speechVars[track];
    if ((e_GetPhonCtrl(zz, zz->cur_PhonBuf_Index_CF) & 1) == 0) {
        return;
    }
    scale = xx->timeWarp_P / xx->timeWarp;
    tempF = (float)zz->dur_Done_in_Phon_CF;
    tempF = (float)floor((double)(tempF * scale));
    zz->dur_Done_in_Phon_CF = FTOI(tempF);
}

/* Speech.c:5219  (0xa0634) */
void StartMIDIMode(synthVarsPtr xx, int16_t track)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->MIDIMode = 1;
}

/* Speech.c:5229  (0xa0698) */
void StopMIDIMode(synthVarsPtr xx, int16_t track)
{
    formantVarPtr zz;

    zz = (formantVarPtr)xx->speechVars[track];
    zz->MIDIMode = 0;
}

/* Speech.c:5239  (0xa06fc) */
void Speech_Noise(synthVarsPtr xx, int16_t track, int32_t amt)
{
    formantVarPtr zz;
    float temoF;

    zz = (formantVarPtr)xx->speechVars[track];
    temoF = (float)amt;
    if (temoF < 0.0f) {
        zz->noiseScale = 0.0f;
    } else if (temoF > 127.0f) {
        zz->noiseScale = 2.0f;
    } else {
        zz->noiseScale = temoF / 64.0f;
    }
    SetTotalVolume(zz);
}

/* -- entry points for the driver: the original kept these file-static ------- */

void vw_InitDefaultVoiceCntrls(formantVarPtr zz)
{
    InitDefaultVoiceCntrls(zz);
}

void vw_SetTotalVolume(formantVarPtr zz)
{
    SetTotalVolume(zz);
}

void vw_SetSeqAddr(formantVarPtr zz, int16_t *speechData)
{
    SetSeqAddr(zz, speechData);
}
