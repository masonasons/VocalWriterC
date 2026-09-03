/* reverb.c -- VocalWriter's reverberator, from Macintosh.c and Music.c.
 *
 * Four all-pass delay modules per channel, with the delays scaled by a room
 * size and mixed wet against dry. The application runs it over each sound
 * buffer after the voices and instruments have been mixed into it
 * (FillSampBuf), so it is the last stage before the samples leave. Lifted
 * from the original's machine code like the rest; see src/speech.c.
 */
#include <stdlib.h>
#include "vw_engine.h"

/* the four delays (ms) and gains (dB) of each side, Macintosh.c's statics */
float g_Reverb_LeftDelay[4] = {193.25f, 131.61f, 84.96f, 52.26f};
float g_Reverb_LeftGain[4] = {-2.498f, -2.2533f, -2.7551f, -2.5828f};
float g_Reverb_RightDelay[4] = {199.92f, 124.61f, 85.73f, 51.5f};
float g_Reverb_RightGain[4] = {-2.4533f, -2.2104f, -2.7953f, -2.5305f};

/* Macintosh.c:2581  (0x78d58) */
float DecibelToInternalVol(float flDecibel)
{
    float fltIntVol;

    if (flDecibel >= -110.0f) {
        fltIntVol = (float)pow(10.0, flDecibel / 20.0);
        return fltIntVol;
    }
    fltIntVol = 0.0f;
    return fltIntVol;
}

/* Macintosh.c:2786  (0x79210) */
void Reverberator_Init(shellVarPtr svv, LPREVERBCONFIG pReverbConfig)
{
    int32_t dwDelay;
    int32_t i;
    float tempF;
    float fltSamplesPerMS;
    float vol;

    svv->ChannelGlobals->m_dwWetVol_h = pReverbConfig->m_fltDBWet;
    svv->ChannelGlobals->m_dwDryVol_h = pReverbConfig->m_fltDBDry;
    fltSamplesPerMS = 44.1f;
    for (i = 0; i <= 3; i++) {
        tempF = pReverbConfig->m_pfltLeftDelay[i] * fltSamplesPerMS;
        if (tempF > 8820.0f) {
            tempF = 8820.0f;
        }
        svv->ChannelGlobals->m_LEFT_Mods_h[i].m_dwDelayBufferSize = FTOI(tempF);
        vol = DecibelToInternalVol(pReverbConfig->m_pfltDBLeft[i]);
        svv->ChannelGlobals->m_LEFT_Mods_h[i].m_lGain = DecibelToInternalVol(pReverbConfig->m_pfltDBLeft[i]);
        tempF = pReverbConfig->m_pfltRightDelay[i] * fltSamplesPerMS;
        if (tempF > 8820.0f) {
            tempF = 8820.0f;
        }
        svv->ChannelGlobals->m_RIGHT_Mods_h[i].m_dwDelayBufferSize = FTOI(tempF);
        svv->ChannelGlobals->m_RIGHT_Mods_h[i].m_lGain = DecibelToInternalVol(pReverbConfig->m_pfltDBRight[i]);
    }
    svv->ChannelGlobals->newReverb_h = 1;
}

/* Macintosh.c:2896  (0x7974c) */
int16_t Synth_SetReverb(shellVarPtr svv, float delayGain, float wetGain, float dryGain)
{
    int16_t i;
    REVERBCONFIG reverbParam;
    int16_t result;

    result = 0;
    if (svv->reverbEnabled != 0) {
        if (!(wetGain != 0.0f || dryGain != 0.0f)) {
            svv->ChannelGlobals->reverbON = 0;
            return result;
        }
        for (i = 0; i <= 3; i++) {
            reverbParam.m_pfltLeftDelay[i] = g_Reverb_LeftDelay[i] * delayGain;
            reverbParam.m_pfltDBLeft[i] = g_Reverb_LeftGain[i];
            reverbParam.m_pfltRightDelay[i] = g_Reverb_RightDelay[i] * delayGain;
            reverbParam.m_pfltDBRight[i] = g_Reverb_RightGain[i];
        }
        reverbParam.m_fltDBWet = wetGain;
        reverbParam.m_fltDBDry = dryGain;
        svv->ChannelGlobals->reverbON = 1;
        svv->ChannelGlobals->newReverb_h = 1;
        Reverberator_Init(svv, &reverbParam);
        return result;
    }
    result = 1000;
    return result;
}

/* Macintosh.c:2687  (0x78e00) */
void ClearReverbModule(LP_REVERBMOD mod)
{
    int32_t i;
    float *dPtr;

    dPtr = mod->m_psDelayBuffer;
    for (i = 0; mod->m_dwDelayBufferSize > i; i++) {
        *dPtr = 0.0f;
        dPtr++;
    }
}

/* Macintosh.c:2704  (0x78e80) */
void ClearReverbHistory(synthVarsPtr xx)
{
    int32_t i;

    for (i = 0; i <= 3; i++) {
        if (xx->m_LEFT_Mods[i].m_psDelayBuffer != 0) {
            ClearReverbModule(&xx->m_LEFT_Mods[i]);
        }
        if (xx->m_RIGHT_Mods[i].m_psDelayBuffer != 0) {
            ClearReverbModule(&xx->m_RIGHT_Mods[i]);
        }
    }
}

/* Macintosh.c:2735  (0x78f7c) */
void DeleteReverbModules(shellVarPtr svv)
{
    int32_t i;

    for (i = 0; i <= 3; i++) {
        if (svv->ChannelGlobals->m_LEFT_Mods[i].m_psDelayBuffer != 0) {
            free(svv->ChannelGlobals->m_LEFT_Mods[i].m_psDelayBuffer);
        }
        svv->ChannelGlobals->m_LEFT_Mods[i].m_psDelayBuffer = NULL;
        svv->ChannelGlobals->m_LEFT_Mods[i].m_dwDelayBufferSize = 0;
        if (svv->ChannelGlobals->m_RIGHT_Mods[i].m_psDelayBuffer != 0) {
            free(svv->ChannelGlobals->m_RIGHT_Mods[i].m_psDelayBuffer);
        }
        svv->ChannelGlobals->m_RIGHT_Mods[i].m_psDelayBuffer = NULL;
        svv->ChannelGlobals->m_RIGHT_Mods[i].m_dwDelayBufferSize = 0;
    }
    if (svv->ChannelGlobals->m_psLeft != 0) {
        free(svv->ChannelGlobals->m_psLeft);
        svv->ChannelGlobals->m_psLeft = NULL;
    }
    if (svv->ChannelGlobals->m_psRight != 0) {
        free(svv->ChannelGlobals->m_psRight);
        svv->ChannelGlobals->m_psRight = NULL;
    }
    if (svv->ChannelGlobals->m_psDryLeft != 0) {
        free(svv->ChannelGlobals->m_psDryLeft);
        svv->ChannelGlobals->m_psDryLeft = NULL;
    }
    if (svv->ChannelGlobals->m_psDryRight == 0) {
        return;
    }
    free(svv->ChannelGlobals->m_psDryRight);
    svv->ChannelGlobals->m_psDryRight = NULL;
}

/* Macintosh.c:2832  (0x79490) */
int16_t GetReverbMemory(shellVarPtr svv)
{
    int16_t i;
    int32_t t_48;

    for (i = 0; i <= 3; i++) {
        svv->ChannelGlobals->m_LEFT_Mods[i].m_psDelayBuffer = (float *)calloc(1, 35280);
        if (svv->ChannelGlobals->m_LEFT_Mods[i].m_psDelayBuffer == 0) {
            goto L_796d0;
        }
        svv->ChannelGlobals->m_RIGHT_Mods[i].m_psDelayBuffer = (float *)calloc(1, 35280);
        if (svv->ChannelGlobals->m_RIGHT_Mods[i].m_psDelayBuffer == 0) {
            goto L_796d0;
        }
    }
    svv->ChannelGlobals->m_dwWorkBufferSize = 1024;
    svv->ChannelGlobals->m_psLeft = (float *)calloc(1, svv->ChannelGlobals->m_dwWorkBufferSize << 2);
    if (svv->ChannelGlobals->m_psLeft != 0) {
        svv->ChannelGlobals->m_psRight = (float *)calloc(1, svv->ChannelGlobals->m_dwWorkBufferSize << 2);
        if (svv->ChannelGlobals->m_psRight != 0) {
            svv->ChannelGlobals->m_psDryLeft = (float *)calloc(1, svv->ChannelGlobals->m_dwWorkBufferSize << 2);
            if (svv->ChannelGlobals->m_psDryLeft != 0) {
                svv->ChannelGlobals->m_psDryRight = (float *)calloc(1, svv->ChannelGlobals->m_dwWorkBufferSize << 2);
                if (svv->ChannelGlobals->m_psDryRight != 0) {
                    t_48 = 0;
                    return (int16_t)t_48;
                }
            }
        }
    }
L_796d0:
    DeleteReverbModules(svv);
    t_48 = -620;
    return (int16_t)t_48;
}

/* Music.c:1952  (0x848c8) */
void XferReverbHold(synthVarsPtr xx)
{
    int16_t i;

    if (xx->newReverb_h != 1) {
        return;
    }
    xx->newReverb_h = 0;
    for (i = 0; i <= 3; i++) {
        if (xx->m_LEFT_Mods[i].m_dwDelayBufferSize != xx->m_LEFT_Mods_h[i].m_dwDelayBufferSize) {
            xx->m_LEFT_Mods[i].m_dwDelayBufferSize = xx->m_LEFT_Mods_h[i].m_dwDelayBufferSize;
            ClearReverbModule(&xx->m_LEFT_Mods[i]);
        } else {
            xx->m_LEFT_Mods[i].m_dwDelayBufferSize = xx->m_LEFT_Mods_h[i].m_dwDelayBufferSize;
        }
        xx->m_LEFT_Mods[i].m_lGain = xx->m_LEFT_Mods_h[i].m_lGain;
        xx->m_LEFT_Mods[i].m_psDelayIn = xx->m_LEFT_Mods[i].m_psDelayBuffer;
        xx->m_LEFT_Mods[i].m_psDelayOut = xx->m_LEFT_Mods[i].m_psDelayBuffer;
        xx->m_LEFT_Mods[i].m_psDelayEnd = &xx->m_LEFT_Mods[i].m_psDelayBuffer[xx->m_LEFT_Mods[i].m_dwDelayBufferSize];
        if (xx->m_RIGHT_Mods[i].m_dwDelayBufferSize != xx->m_RIGHT_Mods_h[i].m_dwDelayBufferSize) {
            xx->m_RIGHT_Mods[i].m_dwDelayBufferSize = xx->m_RIGHT_Mods_h[i].m_dwDelayBufferSize;
            ClearReverbModule(&xx->m_RIGHT_Mods[i]);
        } else {
            xx->m_RIGHT_Mods[i].m_dwDelayBufferSize = xx->m_RIGHT_Mods_h[i].m_dwDelayBufferSize;
        }
        xx->m_RIGHT_Mods[i].m_lGain = xx->m_RIGHT_Mods_h[i].m_lGain;
        xx->m_RIGHT_Mods[i].m_psDelayIn = xx->m_RIGHT_Mods[i].m_psDelayBuffer;
        xx->m_RIGHT_Mods[i].m_psDelayOut = xx->m_RIGHT_Mods[i].m_psDelayBuffer;
        xx->m_RIGHT_Mods[i].m_psDelayEnd = &xx->m_RIGHT_Mods[i].m_psDelayBuffer[xx->m_RIGHT_Mods[i].m_dwDelayBufferSize];
    }
    xx->m_dwWetVol = xx->m_dwWetVol_h;
    xx->m_dwDryVol = xx->m_dwDryVol_h;
}

/* Music.c:2603  (0x85b90) */
void Reverb_Demux16(float *psLeft, float *psRight, int16_t *psSource, int32_t dwSamples)
{
    while (dwSamples != 0) {
        *psLeft = (float)*psSource;
        psLeft++;
        psSource++;
        *psRight = (float)*psSource;
        psRight++;
        psSource++;
        dwSamples--;
    }
}

/* Music.c:2623  (0x85c98) */
void Reverb_Copy16_16(float *psDest, float *psSource, int32_t dwSamples, float rVolume)
{
    if (rVolume <= 0.0f) {
        while (dwSamples != 0) {
            *psDest = 0.0f;
            psDest++;
            dwSamples--;
        }
        return;
    }
    goto L_85d28;
L_85d28:
    if (rVolume == 1.0f) {
        while (dwSamples != 0) {
            *psDest = *psSource;
            psDest++;
            psSource++;
            dwSamples--;
        }
        return;
    }
    while (dwSamples != 0) {
        *psDest = *psSource * rVolume;
        psDest++;
        psSource++;
        dwSamples--;
    }
}

/* Music.c:2670  (0x85dd8) */
void Reverb_Mix16_16(float *psDest, float *psSource, int32_t dwSamples, float rVolume)
{
    float lSample;

    if (rVolume <= 0.0f) {
        return;
    }
    if (rVolume == 1.0f) {
        while (dwSamples != 0) {
            lSample = *psSource + *psDest;
            psSource++;
            if (lSample < -32768.0f) {
                lSample = -32768.0f;
            } else if (lSample > 32767.0f) {
                lSample = 32767.0f;
            }
            *psDest = lSample;
            psDest++;
            dwSamples--;
        }
        return;
    }
    while (dwSamples != 0) {
        lSample = *psSource * rVolume + *psDest;
        psSource++;
        if (lSample < -32768.0f) {
            lSample = -32768.0f;
        } else if (lSample > 32767.0f) {
            lSample = 32767.0f;
        }
        *psDest = lSample;
        psDest++;
        dwSamples--;
    }
}

/* Music.c:2723  (0x85fac) */
static void Reverb_Mux16(int16_t *psDest, float *psLeft, float *psRight, int32_t dwSamples)
{
    while (dwSamples != 0) {
        *psDest = FTOI(*psLeft);
        psDest++;
        psLeft++;
        *psDest = FTOI(*psRight);
        psDest++;
        psRight++;
        dwSamples--;
    }
}

/* Music.c:2746  (0x86074) */
static void ProcessReverbModule(LP_REVERBMOD mod, int32_t dwDestSamples, float *pSource, float *pDestination)
{
    float sDelayOut;
    float sDelayIn;

    dwDestSamples++;
    for (;;) {
        dwDestSamples--;
        if (dwDestSamples == 0) break;
        sDelayOut = *mod->m_psDelayOut;
        sDelayIn = mod->m_lGain * sDelayOut + *pSource;
        if (sDelayIn > 0.0f) {
            if (sDelayIn < 0.001) {
                sDelayIn = 0.0f;
            }
        } else if (sDelayIn > -0.001) {
            sDelayIn = 0.0f;
        }
        *mod->m_psDelayIn = sDelayIn;
        mod->m_psDelayIn++;
        *pDestination = sDelayOut - mod->m_lGain * sDelayIn;
        if ((uint32_t)mod->m_psDelayIn >= (uint32_t)mod->m_psDelayEnd) {
            mod->m_psDelayIn = mod->m_psDelayBuffer;
        }
        mod->m_psDelayOut++;
        if ((uint32_t)mod->m_psDelayOut >= (uint32_t)mod->m_psDelayEnd) {
            mod->m_psDelayOut = mod->m_psDelayBuffer;
        }
        if ((uint32_t)mod->m_psDelayOut > (uint32_t)mod->m_psDelayEnd) {
            /* DebugStr(); -- cannot happen */
        }
        pSource++;
        pDestination++;
    }
}

/* Music.c:2820  (0x8626c) */
static void ProcessReverbBuffer(float *psSample, int32_t dwSamples, LP_REVERBMOD mods)
{
    int16_t i;

    for (i = 0; i <= 3; i++) {
        ProcessReverbModule(&mods[i], dwSamples, psSample, psSample);
    }
}

/* Music.c:2915  (0x864fc) */
int16_t Reverberator_Process(synthVarsPtr xx, int16_t onlyOneFrame)
{
    int32_t dwSamplesRemaining;
    int32_t dwSamplesToProcess;
    int16_t *sampleBuffer;

    if (onlyOneFrame != 0) {
        sampleBuffer = &xx->sampleBuffer[xx->waveIndex - 440];
        dwSamplesRemaining = 220;
    } else {
        sampleBuffer = xx->sampleBuffer;
        dwSamplesRemaining = xx->SoundBufferFrames * 220;
    }
    while (dwSamplesRemaining > 0) {
        if (xx->m_dwWorkBufferSize > dwSamplesRemaining) {
            dwSamplesToProcess = dwSamplesRemaining;
        } else {
            dwSamplesToProcess = xx->m_dwWorkBufferSize;
        }
        Reverb_Demux16(xx->m_psDryLeft, xx->m_psDryRight, sampleBuffer, dwSamplesToProcess);
        Reverb_Copy16_16(xx->m_psLeft, xx->m_psDryLeft, dwSamplesToProcess, xx->m_dwWetVol);
        Reverb_Copy16_16(xx->m_psRight, xx->m_psDryRight, dwSamplesToProcess, xx->m_dwWetVol);
        ProcessReverbBuffer(xx->m_psLeft, dwSamplesToProcess, &xx->m_LEFT_Mods[0]);
        ProcessReverbBuffer(xx->m_psRight, dwSamplesToProcess, &xx->m_RIGHT_Mods[0]);
        Reverb_Mix16_16(xx->m_psLeft, xx->m_psDryLeft, dwSamplesToProcess, xx->m_dwDryVol);
        Reverb_Mix16_16(xx->m_psRight, xx->m_psDryRight, dwSamplesToProcess, xx->m_dwDryVol);
        Reverb_Mux16(sampleBuffer, xx->m_psLeft, xx->m_psRight, dwSamplesToProcess);
        sampleBuffer = &sampleBuffer[dwSamplesToProcess * 2];
        dwSamplesRemaining -= dwSamplesToProcess;
    }
    return (int16_t)0;
}
