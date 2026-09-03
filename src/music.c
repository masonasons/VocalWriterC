/* Music.c -- VocalWriter's sequencer and instrument synthesiser.
 *
 * Plays the tracks of a song: the instrument notes through 96 wavetable
 * oscillators (PlayFrame16) with envelopes and vibrato (Compute_Envelope),
 * the vocal tracks through the speech engine (Speech_Note), 220 sample
 * frames at a time (FillSampBuf), and mixes both into the sound buffer the
 * reverb then runs over. Lifted from the original's machine code like the
 * rest; see src/speech.c.
 */
#include <math.h>
#include "vw_engine.h"

void UpdateTimer(synthVarsPtr xx);
static int16_t FindNextNote(synthVarsPtr xx, unsigned char *curTrack);
static mFloat ScaleWithTempo(synthVarsPtr xx, unsigned char *curTrack, mFloat duration_D);
static int16_t Buffer_MIDI_Out(synthVarsPtr xx, MIDI_ItemPtr itemPtr);

/* Music.c:95  (0x7f500) */
int16_t GetNextTrackEvent(MIDI_EventPtr me, int16_t trackNum)
{
    uint32_t seqItemTime;
    int16_t midiStatus;
    MIDI_ItemPtr curItem;
    mFloat scaledTime;
    int32_t i;
    int32_t len;
    int16_t status;

    status = 1;
    seqItemTime = (uint32_t)VW_LD32BE(me->targetTrack) >> 8;
    if (seqItemTime == 0xffffff) {
        status = 0;
        return status;
    }
    me->target_time = seqItemTime;
    if (me->target_time >= me->target_endTime) {
        status = 0;
        return status;
    }
    me->targetTrack += 3;
    me->target_chan = trackNum;
    me->targetTrack++;
    midiStatus = *me->targetTrack;
    me->targetTrack++;
    me->target_cmd = midiStatus;
    me->target_key = *me->targetTrack;
    me->targetTrack++;
    me->target_vol = *me->targetTrack;
    me->targetTrack++;
    if (midiStatus == 1 || midiStatus == 6 || midiStatus == 8) {
        seqItemTime = (uint32_t)VW_LD32BE(me->targetTrack) >> 8;
        me->targetTrack += 3;
        me->target_dur = seqItemTime;
        me->target_vocals = VW_LD16BE(me->targetTrack);
        me->targetTrack += 2;
        return status;
    }
    me->targetTrack += 5;
    return status;
}

/* Music.c:157  (0x7f714) */
void NewUpdateAllSpeech(synthVarsPtr xx)
{
    int16_t i;

    for (i = 0; i <= 19; i++) {
        if (xx->speechVars[i] != 0) {
            New_Update_Speech(xx, i);
        }
    }
}

/* Music.c:172  (0x7f7b4) */
void UpdateAllSpeech(synthVarsPtr xx)
{
    int16_t i;

    for (i = 0; i <= 19; i++) {
        if (xx->speechVars[i] != 0) {
            Update_Speech(xx, i);
        }
    }
}

/* Music.c:186  (0x7f854) */
void StartAllSpeech(synthVarsPtr xx)
{
    int16_t i;

    for (i = 0; i <= 19; i++) {
        if (xx->speechVars[i] != 0) {
            Start_Speech(xx, i);
        }
    }
}

/* Music.c:201  (0x7f8f4) */
void StartPointSpeech(synthVarsPtr xx)
{
    int16_t i;

    for (i = 0; i <= 19; i++) {
        if (xx->speechVars[i] != 0) {
            StartPoint_Speech(xx, i);
        }
    }
}

/* Music.c:217  (0x7f994) */
void StopAllSpeech(synthVarsPtr xx)
{
    int16_t i;

    for (i = 0; i <= 19; i++) {
        if (xx->speechVars[i] != 0) {
            Stop_Speech(xx, i);
        }
    }
}

/* Music.c:233  (0x7fa34) */
int16_t StateAllSpeech(synthVarsPtr xx)
{
    int16_t i;
    int16_t sum;

    sum = 0;
    if (xx->songHasSpeech == 0) {
        return sum;
    }
    for (i = 0; i <= 19; i++) {
        if (xx->speechVars[i] != 0) {
            sum = State_Speech(xx, i) | sum;
        }
    }
    return sum;
}

/* Music.c:254  (0x7fb10) */
int16_t SetSpeechTempo(synthVarsPtr xx)
{
    int16_t i;

    if (xx->songHasSpeech == 0) {
        return 0;   /* r3, undefined */
    }
    for (i = 0; i <= 19; i++) {
        if (xx->speechVars[i] != 0) {
            NewTempo_Speech(xx, i);
        }
    }
    return 0;   /* the original returned r3, undefined */
}

/* Music.c:273  (0x7fbc4) */
uint16_t GetStep(synthVarsPtr xx, int16_t rate)
{
    if (rate > 63) {
        rate = 63;
    } else if (rate < 0) {
        rate = 0;
    }
    rate = 63 - rate;
    return (uint16_t)xx->Time_Tbl[rate];
}

/* Music.c:288  (0x7fc64) */
void ClearCh(synthVarsPtr xx, int16_t chan)
{
    int16_t noteNum;
    int16_t *chanOffs;

    chanOffs = &xx->noteList[chan * 128];
    for (noteNum = 0; noteNum <= 0x7f; noteNum++) {
        (*chanOffs) = -1;
        chanOffs++;
    }
}

/* Music.c:307  (0x7fcf8) */
void ClearNtbl(synthVarsPtr xx)
{
    int16_t chanNum;

    for (chanNum = 0; chanNum <= 33; chanNum++) {
        ClearCh(xx, chanNum);
    }
}

/* Music.c:322  (0x7fd74) */
int16_t InitChannels(synthVarsPtr xx)
{
    int16_t i;

    for (i = 0; i <= 33; i++) {
        xx->chDetune[i] = 0;
        xx->chPBTbl[i] = 0;
        xx->chPBRange[i] = 2;
        xx->chSusTbl[i] = 0;
        xx->chVolTbl[i] = 256;
    }
    return 0;   /* the original returned r3, undefined */
}

/* Music.c:337  (0x7fe6c) */
void HaltVoice(synthVarsPtr xx, int16_t voiceSlot)
{
    DOC_RegsPtr oReg;
    int16_t i;

    oReg = &xx->doc[voiceSlot * 2];
    for (i = 0; i <= 1; i++) {
        oReg[i].state = 1;
    }
}

/* Music.c:351  (0x7ff18) */
void HaltAllVoices(synthVarsPtr xx)
{
    int16_t i;

    for (i = 0; i <= 47; i++) {
        HaltVoice(xx, i);
        xx->VCB_Array[i].envState = 2;
        xx->VCB_Array[i].priority = 0;
    }
}

/* Music.c:371  (0x7ffd4) */
void SetTempo(synthVarsPtr xx, int16_t tempoVal)
{
    mFloat temp;

    if (tempoVal > 250) {
        tempoVal = 250;
    } else if (tempoVal <= 9) {
        tempoVal = 10;
    }
    xx->tempoBPM = tempoVal;
    temp = (float)tempoVal;
    temp = (float)(12027.2727273 / (xx->tempoMul * temp) / 240.0);
    if ((xx->seqFlags & 3) != 0 && (((uint32_t)xx->seqFlags >> 4) & 1) == 0) {
        xx->timeWarp_P = temp;
        return;
    }
    xx->timeWarp_P = 0.0f;
    xx->timeWarp = temp;
    xx->timeWarp_Inv = 1.0f / xx->timeWarp;
}

/* Music.c:402  (0x8013c) */
void NewTempoScale(synthVarsPtr xx, int32_t scale)
{
    xx->tempoMul = (float)scale;
    xx->tempoMul /= 65536.0f;
    SetTempo(xx, xx->tempoBPM);
}

/* Music.c:414  (0x801ec) */
int16_t StartOscillators(synthVarsPtr xx)
{
    xx->nextSampBuf = 0;
    xx->sampleBuffer = xx->sampleBuffer1;
    xx->lastnSamp_L = 0;
    xx->lastnSamp_R = 0;
    xx->queueIN = 0;
    xx->queueOUT = xx->queueIN;
    xx->minSamp = 0;
    xx->maxSamp = xx->minSamp;
    ClearNtbl(xx);
    InitChannels(xx);
    HaltAllVoices(xx);
    Start_The_Music(xx);
    return (int16_t)0;
}

/* Music.c:436  (0x802c4) */
void TurnTrackOn(synthVarsPtr xx, int16_t trackNum)
{
    int16_t chan;
    int16_t note;
    int16_t offs;
    int16_t i;
    int16_t speechTrack;

    speechTrack = -1;
    for (i = 0; i <= 19; i++) {
        if (xx->speechVars[i] != 0 && trackNum == xx->speechChanToTrack[i]) {
            speechTrack = i;
            break;
        }
    }
    if (speechTrack < 0) {
        return;
    }
    Sing_Speech(xx, speechTrack, 1);
}

/* Music.c:469  (0x803c4) */
void TurnTrackOff(synthVarsPtr xx, int16_t trackNum)
{
    int16_t chan;
    int16_t note;
    int16_t offs;
    int16_t i;
    int16_t speechTrack;

    speechTrack = -1;
    for (i = 0; i <= 19; i++) {
        if (xx->speechVars[i] != 0 && trackNum == xx->speechChanToTrack[i]) {
            speechTrack = i;
            break;
        }
    }
    if (speechTrack >= 0) {
        Sing_Speech(xx, speechTrack, 0);
        return;
    }
    for (chan = 0; chan <= 33; chan++) {
        for (note = 0; note <= 0x7f; note++) {
            offs = chan * 34 + note;
            if (trackNum == xx->noteList[offs]) {
                xx->foreQueue[xx->foreQueueIN].cmd = 2;
                xx->foreQueue[xx->foreQueueIN].chan = chan;
                xx->foreQueue[xx->foreQueueIN].key = note;
                xx->foreQueue[xx->foreQueueIN].vol = 0;
                xx->foreQueueIN = (xx->foreQueueIN + 1) & 31;
            }
        }
    }
}

/* Music.c:521  (0x80640) */
void MarkOn(synthVarsPtr xx)
{
    int16_t offs;

    offs = xx->Vchan * 34 + xx->Vkey;
    xx->noteList[offs] = xx->Vtrack;
}

/* Music.c:530  (0x806c8) */
void MarkOff(synthVarsPtr xx)
{
    int16_t offs;

    offs = xx->Vchan * 34 + xx->Vkey;
    xx->noteList[offs] = -1;
}

/* Music.c:540  (0x80748) */
void Set_Freq(synthVarsPtr xx, int16_t voiceSlot)
{
    DOC_RegsPtr oReg;
    VoiceCtrlBlockPtr curVCB;
    int32_t pbMod;
    int16_t oscOffs;
    VCB_GenPtr curGen;
    int16_t genNum;

    curVCB = &xx->VCB_Array[voiceSlot];
    pbMod = ((xx->chPBTbl[curVCB->chan] * xx->chPBRange[curVCB->chan]) >> 16) + xx->chDetune[curVCB->chan];
    oscOffs = 0;
    for (genNum = 0; genNum <= 1; genNum++) {
        oReg = &xx->doc[voiceSlot * 2 + oscOffs];
        curGen = &curVCB->genParams[genNum];
        if (oReg->inSwap != 0) {
            oReg->phaseIncB = xx->Freq_Tbl[curGen->vfreqB + pbMod + curGen->vibrato] - curGen->Detune;
            oReg->phaseIncA = oReg->phaseIncB;
        } else {
            oReg->phaseIncB = xx->Freq_Tbl[curGen->vfreqB + pbMod + curGen->vibrato] - curGen->Detune;
            oReg->phaseIncA = xx->Freq_Tbl[curGen->vfreqA + pbMod + curGen->vibrato] + curGen->Detune;
        }
        oscOffs++;
    }
}

/* Music.c:584  (0x809c4) */
void Calc_Log_Acceleration(VoiceCtrlBlockPtr curVCB, int32_t accelGain)
{
    int32_t numOfSteps;

    if (curVCB->segState == 0 && curVCB->atkSlope == 0) {
        curVCB->stepInitial = curVCB->env_inc;
        curVCB->stepAccel = 0;
        return;
    }
    if (curVCB->segState == 0) {
        numOfSteps = (curVCB->env_lim - curVCB->env_cur) / curVCB->env_inc;
    } else {
        numOfSteps = (curVCB->env_cur - curVCB->env_lim) / curVCB->env_inc;
    }
    if (numOfSteps <= 0) {
        numOfSteps = 1;
    }
    curVCB->stepInitial = curVCB->env_inc * accelGain;
    if (curVCB->stepInitial > 0x1fffc000) {
        curVCB->stepInitial = 0x1fffc000;
    } else if (curVCB->stepInitial <= 0x1bfff) {
        curVCB->stepInitial = 0x1c000;
    }
    curVCB->stepAccel = (curVCB->stepInitial - 0x1c000) / numOfSteps;
    if (curVCB->segState != 0) {
        return;
    }
    if (curVCB->atkSlope != 1) {
        return;
    }
    curVCB->stepInitial = curVCB->stepAccel;
}

/* Music.c:623  (0x80b78) */
void Exp_Ramp(VoiceCtrlBlockPtr curVCB)
{
    curVCB->stepInitial += curVCB->stepAccel;
    if (curVCB->stepInitial > 0x1fffc000) {
        curVCB->stepInitial = 0x1fffc000;
    }
    curVCB->env_inc = curVCB->stepInitial >> 14;
}

/* Music.c:634  (0x80c00) */
void Log_Ramp(VoiceCtrlBlockPtr curVCB)
{
    curVCB->stepInitial -= curVCB->stepAccel;
    if (curVCB->stepInitial < 0) {
        curVCB->stepInitial = 0x1fffc000;
    } else if (curVCB->stepInitial <= 0x1bfff) {
        curVCB->stepInitial = 0x1c000;
    }
    curVCB->env_inc = curVCB->stepInitial >> 14;
}

/* Music.c:647  (0x80cac) */
void GetNextSeg(VoiceCtrlBlockPtr curVCB)
{
    int16_t t_38;

    curVCB->env_cur = curVCB->env_lim;
    if (curVCB->env_cur == 0) {
        return;
    }
    t_38 = curVCB->segState;
    if (t_38 != 1) {
        if (t_38 <= 1) {
            if (t_38 == 0) {
                goto L_80d40;
            }
            return;
        }
        if (t_38 > 3) {
            return;
        }
        goto L_80e10;
L_80d40:
        curVCB->segState = 1;
        curVCB->env_dir = 2;
        curVCB->env_lim = curVCB->sustainLevel << 8;
        curVCB->env_inc = curVCB->decayStep + curVCB->addDky;
        Calc_Log_Acceleration(curVCB, curVCB->dkyAccelGain);
        return;
    }
    curVCB->segState = 2;
    curVCB->env_dir = 0;
    if ((uint32_t)curVCB->sustainLevel <= 3) {
        curVCB->sustainLevel = 4;
    }
    curVCB->env_lim = curVCB->sustainLevel << 8;
    curVCB->env_inc = 0;
    return;
L_80e10:
    curVCB->segState = 3;
    curVCB->env_dir = 2;
    curVCB->env_lim = 1024;
    curVCB->env_inc = curVCB->releaseStep + curVCB->addDky;
    Calc_Log_Acceleration(curVCB, curVCB->dkyAccelGain);
}

/* Music.c:694  (0x80e84) */
void StartRelease(VoiceCtrlBlockPtr curVCB)
{
    int16_t genNum;
    VCB_GenPtr curGen;

    curVCB->segState = 2;
    curVCB->env_dir = 2;
    curVCB->env_lim = curVCB->env_cur;
    curVCB->env_inc = 1;
    curVCB->envState = 1;
    curVCB->priority = (curVCB->priority >> 1) + 1;
    curVCB->sus_flag = 0;
}

/* Music.c:717  (0x80f24) */
int16_t VibratoEnvelope(VCB_GenPtr curGen, int16_t vibrato)
{
    if (curGen->vibrato_State > 1) {
        return vibrato;
    }
    if (curGen->vibrato_State != 0) {
        if (curGen->vibrato_State == 1) {
            goto L_80fe4;
        }
        return vibrato;
    }
    curGen->vibrato_Accum += curGen->vibratoDelay;
    if (curGen->vibrato_Accum > 0x7fff) {
        curGen->vibrato_Accum = 0;
        curGen->vibrato_State = 1;
    }
    vibrato = 0;
    return vibrato;
L_80fe4:
    curGen->vibrato_Accum += curGen->vibratoRamp;
    if (curGen->vibrato_Accum > 0x7fff) {
        curGen->vibrato_State = 2;
        return vibrato;
    }
    vibrato = (vibrato * curGen->vibrato_Accum) >> 15;
    return vibrato;
}

/* Music.c:751  (0x81058) */
int32_t Compute_Envelope(synthVarsPtr xx, int16_t voiceSlot)
{
    DOC_RegsPtr oReg;
    VoiceCtrlBlockPtr curVCB;
    VCB_GenPtr curGen;
    int16_t genNum;
    int32_t levelT;
    int16_t chVol;
    int16_t trackVol;
    int16_t vibrato;

    curVCB = &xx->VCB_Array[voiceSlot];
    chVol = xx->chVolTbl[curVCB->vcbCH];
    trackVol = xx->TrackLevel[curVCB->vcbCH];
    if (curVCB->sus_flag != 0 && xx->chSusTbl[curVCB->vcbCH] == 0 && curVCB->sus_hold != 0) {
        StartRelease(curVCB);
    }
    oReg = &xx->doc[voiceSlot * 2];
    curGen = &curVCB->genParams[0];
    if (curVCB->env_dir == 2) {
        Log_Ramp(curVCB);
        levelT = curVCB->env_cur - curVCB->env_inc;
        if (levelT < 0 || curVCB->env_lim > levelT) {
            GetNextSeg(curVCB);
        } else {
            curVCB->env_cur = levelT;
        }
        if (curVCB->env_cur <= 1024) {
            curVCB->env_cur = 0;
        }
    } else if (curVCB->env_dir == 1) {
        if (curVCB->atkSlope == 1) {
            Exp_Ramp(curVCB);
        } else if (curVCB->atkSlope == 2) {
            Log_Ramp(curVCB);
        }
        levelT = curVCB->env_cur + curVCB->env_inc;
        if (curVCB->env_lim < levelT) {
            GetNextSeg(curVCB);
        } else {
            curVCB->env_cur = levelT;
        }
    }
    levelT = ((curVCB->env_cur >> 8) * chVol) >> 7;
    levelT = (trackVol * levelT) >> 8;
    oReg->volumeB = ((curGen->volB * levelT) >> 7) << 1;
    if (oReg->inSwap != 0) {
        oReg->volumeA = oReg->volumeB;
    } else {
        oReg->volumeA = ((curGen->volA * levelT) >> 7) << 1;
    }
    if (curGen->vibratoDepth != 0) {
        curGen->vibrato_Phase = (curGen->vibratoFreq + curGen->vibrato_Phase) & 0xffffff;
        vibrato = xx->SineWavePtr[curGen->vibrato_Phase >> 16] - 128;
        vibrato = VibratoEnvelope(curGen, vibrato);
        curGen->vibrato = (vibrato * curGen->vibratoDepth) >> 16;
    } else {
        curGen->vibrato = 0;
    }
    oReg++;
    curGen = &curVCB->genParams[1];
    oReg->volumeB = ((curGen->volB * levelT) >> 7) << 1;
    if (oReg->inSwap != 0) {
        oReg->volumeA = oReg->volumeB;
    } else {
        oReg->volumeA = ((curGen->volA * levelT) >> 7) << 1;
    }
    if (curGen->vibratoDepth != 0) {
        curGen->vibrato_Phase = (curGen->vibratoFreq + curGen->vibrato_Phase) & 0xffffff;
        vibrato = xx->SineWavePtr[curGen->vibrato_Phase >> 16] - 128;
        curGen->vibrato = (vibrato * curGen->vibratoDepth) >> 16;
    } else {
        curGen->vibrato = 0;
    }
    Set_Freq(xx, voiceSlot);
    return curVCB->env_cur;
}

/* Music.c:881  (0x81560) */
void UpdateTimer(synthVarsPtr xx)
{
    mFloat beats;
    int32_t totalBeats;

    if ((xx->seqFlags & 3) == 0) {
        return;
    }
    if ((((uint32_t)xx->seqFlags >> 4) & 1) != 0) {
        return;
    }
    if (xx->timeWarp_P != 0.0f) {
        SetSpeechTempo(xx);
        xx->timer = (float)floor((double)(xx->timer * (xx->timeWarp_P / xx->timeWarp)));
        xx->timeWarp = xx->timeWarp_P;
        xx->timeWarp_P = 0.0f;
        xx->timeWarp_Inv = 1.0f / xx->timeWarp;
    }
    if ((((uint32_t)xx->seqFlags >> 5) & 1) != 0) {
        return;
    }
    xx->timer++;
    beats = (float)floor((double)(xx->timer * xx->timeWarp_Inv / xx->ticksPerBeat));
    if (!(xx->beatCount < beats)) {
        return;
    }
    xx->beatCount = beats;
    totalBeats = FTOI(beats);
    _i_At_Beat(xx, totalBeats);
    if (xx->playMerto == 0) {
        return;
    }
    xx->metro = 1;
}

/* Music.c:929  (0x81748) */
int16_t AllocateVoice(synthVarsPtr xx)
{
    int16_t min_s;
    int16_t voice_s;
    int16_t min_r;
    int16_t voice_r;
    int16_t i;
    VoiceCtrlBlockPtr curVCB;
    int16_t priorityNum;
    int16_t freeSlot;

    min_r = 65;
    min_s = min_r;
    freeSlot = 0;
    for (i = 0; i < xx->polyphony; i++) {
        curVCB = &xx->VCB_Array[i];
        priorityNum = curVCB->priority;
        if (priorityNum == 0) {
            freeSlot = i;
            return freeSlot;
        }
        if (curVCB->envState == 1) {
            priorityNum--;
            if (priorityNum <= 0) {
                priorityNum = 1;
            }
            if (priorityNum < min_r) {
                min_r = priorityNum;
                voice_r = i;
            }
        } else {
            priorityNum--;
            if (priorityNum <= 0) {
                priorityNum = 1;
            }
            if (priorityNum < min_s) {
                min_s = priorityNum;
                voice_s = i;
            }
        }
        curVCB->priority = priorityNum;
    }
    if (min_r <= 64) {
        freeSlot = voice_r;
    } else if (min_s <= 64) {
        freeSlot = voice_s;
    } else {
        DebugStr((char *)/* pic 0x81774 */ 0 + 0x28500);
    }
    HaltVoice(xx, freeSlot);
    return freeSlot;
}

/* Music.c:1002  (0x81940) */
void VCB_Release(synthVarsPtr xx, VoiceCtrlBlockPtr curVCB)
{
    if (xx->chSusTbl[curVCB->vcbCH] == 0) {
        StartRelease(curVCB);
        return;
    }
    if (curVCB->sus_hold != 0) {
        return;
    }
    curVCB->sus_hold = 1;
}

/* Music.c:1016  (0x819dc) */
void VoiceRelease(synthVarsPtr xx)
{
    VoiceCtrlBlockPtr curVCB;
    int16_t i;

    for (i = 0; i < xx->polyphony; i++) {
        curVCB = &xx->VCB_Array[i];
        if (curVCB->priority > 0 && curVCB->note == xx->Vkey && curVCB->chan == xx->Vchan) {
            curVCB->noteDur = 0.0f;
            if (curVCB->envState <= 0) {
                VCB_Release(xx, curVCB);
            }
        }
    }
}

/* Music.c:1042  (0x81aec) */
static int16_t FindNextNote(synthVarsPtr xx, unsigned char *curTrack)
{
    uint32_t seqItemTime;
    int16_t noteStatus;
    int16_t noteKey;
    int16_t noteVol;

    do {
        seqItemTime = (uint32_t)VW_LD32BE(curTrack) >> 8;
        if (seqItemTime == 0xffffff) {
            noteKey = -1;
            return noteKey;
        }
        curTrack += 3;
        curTrack++;
        noteStatus = *curTrack;
        curTrack++;
        noteKey = *curTrack;
        curTrack++;
        noteVol = *curTrack;
        curTrack++;
        curTrack += 5;
        if (noteStatus == 6) {
            return noteKey;
        }
    } while (noteStatus != 8);
    return noteKey;
}

/* Music.c:1084  (0x81bf4) */
int16_t PlaySeqItems(synthVarsPtr xx)
{
    uint32_t seqItemTime;
    int16_t midiStatus;
    MIDI_ItemPtr curItem;
    mFloat scaledTime;
    unsigned char *curTrack;
    int32_t i;
    int32_t x;
    int32_t index;
    int16_t stillMoreData;
    uint16_t vocalIndex;
    float t_88;

    if (xx->metro != 0) {
        xx->metro = 0;
        xx->itemQueue[xx->queueIN].track = 33;
        xx->itemQueue[xx->queueIN].chan = 33;
        xx->itemQueue[xx->queueIN].cmd = 1;
        xx->itemQueue[xx->queueIN].key = 60;
        xx->itemQueue[xx->queueIN].vol = 100;
        xx->itemQueue[xx->queueIN].dur = 30.0f;
        xx->queueIN = (xx->queueIN + 1) & 0x7f;
    }
    while (xx->foreQueueOUT != xx->foreQueueIN) {
        curItem = &xx->foreQueue[xx->foreQueueOUT];
        xx->itemQueue[xx->queueIN].track = curItem->track;
        xx->itemQueue[xx->queueIN].chan = curItem->chan;
        xx->itemQueue[xx->queueIN].cmd = curItem->cmd;
        xx->itemQueue[xx->queueIN].key = curItem->key;
        xx->itemQueue[xx->queueIN].vol = curItem->vol;
        if (curItem->cmd == 1) {
            xx->itemQueue[xx->queueIN].dur = 2.0e+07f;
        } else if (curItem->cmd == 10) {
            xx->itemQueue[xx->queueIN].dur = 30.0f;
            xx->itemQueue[xx->queueIN].cmd = 1;
        }
        xx->queueIN = (xx->queueIN + 1) & 0x7f;
        xx->foreQueueOUT = (xx->foreQueueOUT + 1) & 31;
        if ((((uint32_t)xx->seqFlags >> 1) & 1) != 0 && &xx->rec_BufPtr[8] < (uint32_t)xx->rec_BufPtr_End) {
            t_88 = xx->timer * xx->timeWarp_Inv;
            if (!(t_88 >= 2147483648.0)) {
                seqItemTime = FTOI(t_88);
            } else {
                seqItemTime = FTOI(t_88 - 2147483648.0);
                seqItemTime ^= -0x80000000;
            }
            seqItemTime = seqItemTime;
            VW_ST32BE(xx->rec_BufPtr, seqItemTime);
            xx->rec_BufPtr += 4;
            (*xx->rec_BufPtr) = curItem->track;
            xx->rec_BufPtr++;
            (*xx->rec_BufPtr) = curItem->cmd;
            xx->rec_BufPtr++;
            (*xx->rec_BufPtr) = curItem->key;
            xx->rec_BufPtr++;
            (*xx->rec_BufPtr) = curItem->vol;
            xx->rec_BufPtr++;
        }
    }
    stillMoreData = 0;
    if ((xx->seqFlags & 1) == 0) {
        return stillMoreData;
    }
    if ((((uint32_t)xx->seqFlags >> 4) & 1) != 0) {
        return stillMoreData;
    }
    if ((((uint32_t)xx->seqFlags >> 5) & 1) != 0) {
        return stillMoreData;
    }
    for (i = 0; i <= 31; i++) {
        if ((xx->seqStatus & (1 << i)) != 0) {
            curTrack = xx->seqPlayBuf[i];
L_82244:
            index = (xx->queueIN + 1) & 0x7f;
            if (xx->queueOUT == index) {
                stillMoreData = 1;
                return stillMoreData;
            }
            seqItemTime = (uint32_t)VW_LD32BE(curTrack) >> 8;
            if (seqItemTime == 0xffffff) {
                if (xx->SpeechMap[i] >= 0 && xx->speechVars[xx->SpeechMap[i]] != 0) {
                    if (State_Speech(xx, (int16_t)xx->SpeechMap[i]) == 0) {
                        xx->seqStatus &= -1 - (1 << i);
                        Stop_Speech(xx, (int16_t)xx->SpeechMap[i]);
                    } else {
                        xx->seqPlayBuf[i] = curTrack;
                    }
                } else {
                    xx->seqStatus &= -1 - (1 << i);
                }
                if (xx->seqStatus == 0) {
                    xx->seqFlags &= 65534;
                    xx->doPostSeq = 1;
                }
            } else {
                xx->seqPlayBuf[i] = curTrack;
                scaledTime = (float)seqItemTime;
                scaledTime *= xx->timeWarp;
                if (xx->timer >= scaledTime) {
                    curTrack += 3;
                    xx->itemQueue[xx->queueIN].track = i;
                    if (xx->SpeechMap[i] >= 0) {
                        xx->itemQueue[xx->queueIN].chan = i + 256;
                        curTrack++;
                    } else {
                        xx->itemQueue[xx->queueIN].chan = i;
                        curTrack++;
                    }
                    midiStatus = *curTrack;
                    curTrack++;
                    xx->itemQueue[xx->queueIN].cmd = midiStatus;
                    xx->itemQueue[xx->queueIN].key = *curTrack;
                    curTrack++;
                    xx->itemQueue[xx->queueIN].vol = *curTrack;
                    curTrack++;
                    if (midiStatus == 1 || midiStatus == 6 || midiStatus == 8) {
                        seqItemTime = (uint32_t)VW_LD32BE(curTrack) >> 8;
                        curTrack += 3;
                        xx->itemQueue[xx->queueIN].dur = (float)seqItemTime;
                        vocalIndex = VW_LD16BE(curTrack);
                        xx->itemQueue[xx->queueIN].vocalIndex = vocalIndex;
                        curTrack += 2;
                        if (xx->karaokeTrack == i && midiStatus == 6) {
                            _i_At_Kara(xx, vocalIndex);
                        }
                        if (midiStatus == 6) {
                            midiStatus = 1;
                            xx->itemQueue[xx->queueIN].cmd = midiStatus;
                        }
                    } else {
                        curTrack += 5;
                    }
                    if (xx->SpeechMap[i] >= 0 && (midiStatus == 1 || midiStatus == 8)) {
                        xx->itemQueue[xx->queueIN].nextKey = FindNextNote(xx, curTrack);
                        if (xx->itemQueue[xx->queueIN].nextKey < 0) {
                            xx->itemQueue[xx->queueIN].nextKey = xx->itemQueue[xx->queueIN].key;
                        }
                    }
                    if (xx->PlayTrackMap[i] != 0 || xx->SpeechMap[i] >= 0 || midiStatus != 1) {
                        xx->queueIN = index;
                    }
                    xx->seqPlayBuf[i] = curTrack;
                    goto L_82244;
                }
            }
        }
    }
    return stillMoreData;
}

/* Music.c:1310  (0x8298c) */
static mFloat ScaleWithTempo(synthVarsPtr xx, unsigned char *curTrack, mFloat duration_D)
{
    uint32_t eventTime;
    uint32_t endTime;
    mFloat curTime_D;
    mFloat tempo_D;
    mFloat curTempo_D;
    mFloat tempoTime_D;
    mFloat durAccum_D;
    mFloat scale_D;
    mFloat endTime_D;
    float t_70;

    curTime_D = xx->timer * xx->timeWarp_Inv;
    endTime_D = curTime_D + duration_D;
    t_70 = endTime_D;
    if (!(t_70 >= 2147483648.0)) {
        endTime = FTOI(t_70);
    } else {
        endTime = FTOI(t_70 - 2147483648.0);
        endTime ^= -0x80000000;
    }
    endTime = endTime;
    tempoTime_D = curTime_D;
    curTempo_D = (float)xx->tempoBPM;
    durAccum_D = 0.0f;
    scale_D = 1.0f;
L_82aa8:
    eventTime = (uint32_t)VW_LD32BE(curTrack) >> 8;
    if (eventTime != 0xffffff && eventTime < endTime) {
        if (curTrack[4] == 4) {
            tempoTime_D = (float)eventTime;
            tempo_D = (float)((curTrack[5] << 8) + curTrack[6]);
            durAccum_D += (tempoTime_D - curTime_D) * scale_D;
            scale_D = curTempo_D / tempo_D;
            curTime_D = tempoTime_D;
        }
        curTrack += 12;
        goto L_82aa8;
    }
    durAccum_D += (endTime_D - curTime_D) * scale_D;
    return durAccum_D;
}

/* Music.c:1353  (0x82bdc) */
static int16_t Buffer_MIDI_Out(synthVarsPtr xx, MIDI_ItemPtr itemPtr)
{
    int32_t i;
    int32_t slot;
    int32_t channel;
    int32_t vel;
    int16_t gotIt;

    if (xx->trackToMIDI[itemPtr->track] >= 0) {
        channel = xx->trackToMIDI[itemPtr->track] & 15;
        if ((uint32_t)itemPtr->cmd <= 38) {
            switch (itemPtr->cmd) {
            case 1:
                slot = -1;
                for (i = 0; i <= 63; i++) {
                    if (xx->MIDI_Dur_List[i].note < 0) {
                        slot = i;
                        break;
                    }
                }
                if (slot >= 0) {
                    vel = (itemPtr->vol * xx->TrackLevel[itemPtr->track]) >> 8;
                    if (vel > 0x7f) {
                        vel = 0x7f;
                    }
                    xx->MIDI_Dur_List[slot].duration = itemPtr->dur;
                    xx->MIDI_Dur_List[slot].channel = channel;
                    xx->MIDI_Dur_List[slot].note = itemPtr->key;
                    _i_MIDI_Buffer(xx, 3, (int16_t)channel + 144, itemPtr->key, (int16_t)vel);
                }
                break;
            case 2:
                _i_MIDI_Buffer(xx, 3, (int16_t)channel + 128, itemPtr->key, 0);
                break;
            case 3:
                _i_MIDI_Buffer(xx, 2, (int16_t)channel + 192, itemPtr->vol, 0);
                break;
            case 33:
                _i_MIDI_Buffer(xx, 3, (int16_t)channel + 176, 7, itemPtr->vol);
                break;
            case 34:
                _i_MIDI_Buffer(xx, 3, (int16_t)channel + 176, 64, itemPtr->vol);
                break;
            case 32:
                _i_MIDI_Buffer(xx, 3, (int16_t)channel + 224, itemPtr->key, itemPtr->vol);
                break;
            case 38:
                _i_MIDI_Buffer(xx, 3, (int16_t)channel + 176, 101, 0);
                _i_MIDI_Buffer(xx, 3, (int16_t)channel + 176, 100, 0);
                _i_MIDI_Buffer(xx, 3, (int16_t)channel + 176, 6, itemPtr->vol);
                break;
            case 0:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
            case 15:
            case 16:
            case 17:
            case 18:
            case 19:
            case 20:
            case 21:
            case 22:
            case 23:
            case 24:
            case 25:
            case 26:
            case 27:
            case 28:
            case 29:
            case 30:
            case 31:
            case 35:
            case 36:
            case 37:
                break;
            }
        }
        gotIt = 1;
        return gotIt;
    }
    gotIt = 0;
    return gotIt;
}

/* Music.c:1436  (0x83090) */
void CheckDrum(synthVarsPtr xx, MIDI_ItemPtr itemPtr)
{
    int16_t i;

    if ((((uint32_t)xx->TrackFlagBits[itemPtr->track] >> 1) & 1) == 0) {
        return;
    }
    if (itemPtr->key <= 34) {
        i = 0;
    } else if (itemPtr->key > 81) {
        i = 46;
    } else {
        i = itemPtr->key - 35;
    }
    i <<= 1;
    xx->Vocb = xx->GM_DrumMap[i] & 0x7fff;
    itemPtr->key = xx->GM_DrumMap[i + 1];
}

/* Music.c:1460  (0x831b4) */
void Update(synthVarsPtr xx)
{
    int16_t cmd;
    int16_t i;
    int16_t activeVoices;
    MIDI_ItemPtr itemPtr;
    int32_t tempL;
    VoiceCtrlBlockPtr curVCB;
    int16_t outIndex;
    int16_t stillMoreData;
    int16_t speechChan;

    do {
        stillMoreData = PlaySeqItems(xx);
        outIndex = xx->queueOUT;
        while (outIndex != xx->queueIN) {
            itemPtr = &xx->itemQueue[outIndex];
            if (itemPtr->chan > 0xff) {
                speechChan = xx->SpeechMap[itemPtr->chan - 256];
                if ((uint32_t)(itemPtr->cmd - 3) <= 68) {
                    switch (itemPtr->cmd) {
                    case 39:
                        Speech_Detune(xx, speechChan, ((itemPtr->vol << 7) + itemPtr->key - 8192) << 3);
                        break;
                    case 32:
                        Speech_PitchBend(xx, speechChan, ((itemPtr->vol << 7) + itemPtr->key - 8192) << 3);
                        break;
                    case 38:
                        if (itemPtr->vol > 12) {
                            itemPtr->vol = 12;
                        }
                        Speech_PBSens(xx, speechChan, itemPtr->vol);
                        break;
                    case 64:
                        Speech_Color(xx, speechChan, itemPtr->vol);
                        break;
                    case 71:
                        Speech_Noise(xx, speechChan, itemPtr->vol);
                        break;
                    case 67:
                        Speech_VibDepth(xx, speechChan, itemPtr->vol);
                        break;
                    case 68:
                        Speech_VibFreq(xx, speechChan, itemPtr->vol);
                        break;
                    case 69:
                        Speech_Portamento(xx, speechChan, itemPtr->vol);
                        break;
                    case 66:
                        Speech_Chorus(xx, speechChan, (itemPtr->vol << 7) + itemPtr->key - 8192);
                        break;
                    case 33:
                        Speech_Volume(xx, speechChan, itemPtr->vol);
                        break;
                    case 70:
                        Speech_Breath(xx, speechChan, itemPtr->vol);
                        break;
                    case 3:
                        PgmChange_Speech(xx, speechChan, itemPtr->vol);
                        break;
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                    case 9:
                    case 10:
                    case 11:
                    case 12:
                    case 13:
                    case 14:
                    case 15:
                    case 16:
                    case 17:
                    case 18:
                    case 19:
                    case 20:
                    case 21:
                    case 22:
                    case 23:
                    case 24:
                    case 25:
                    case 26:
                    case 27:
                    case 28:
                    case 29:
                    case 30:
                    case 31:
                    case 34:
                    case 35:
                    case 36:
                    case 37:
                    case 40:
                    case 41:
                    case 42:
                    case 43:
                    case 44:
                    case 45:
                    case 46:
                    case 47:
                    case 48:
                    case 49:
                    case 50:
                    case 51:
                    case 52:
                    case 53:
                    case 54:
                    case 55:
                    case 56:
                    case 57:
                    case 58:
                    case 59:
                    case 60:
                    case 61:
                    case 62:
                    case 63:
                    case 65:
                        break;
                    }
                }
            } else if ((uint32_t)(itemPtr->cmd - 3) <= 36) {
                switch (itemPtr->cmd) {
                case 39:
                    tempL = ((itemPtr->vol << 7) + itemPtr->key - 8192) << 3;
                    if ((((uint32_t)xx->TrackFlagBits[itemPtr->track] >> 1) & 1) != 0) {
                        xx->chDetune[itemPtr->chan] = (tempL << 5) >> 14;
                    } else {
                        xx->chDetune[itemPtr->chan] = (tempL << 5) >> 16;
                    }
                    break;
                case 32:
                    if (Buffer_MIDI_Out(xx, itemPtr) == 0) {
                        xx->chPBTbl[itemPtr->chan] = ((itemPtr->vol << 7) + itemPtr->key - 8192) << 8;
                    }
                    break;
                case 38:
                    if (Buffer_MIDI_Out(xx, itemPtr) == 0) {
                        if (itemPtr->vol > 12) {
                            itemPtr->vol = 12;
                        }
                        xx->chPBRange[itemPtr->chan] = itemPtr->vol;
                    }
                    break;
                case 33:
                    if (Buffer_MIDI_Out(xx, itemPtr) == 0) {
                        xx->chVolTbl[itemPtr->chan] = itemPtr->vol << 1;
                    }
                    break;
                case 34:
                    if (Buffer_MIDI_Out(xx, itemPtr) == 0) {
                        xx->chSusTbl[itemPtr->chan] = itemPtr->vol >> 6;
                    }
                    break;
                case 3:
                    if (Buffer_MIDI_Out(xx, itemPtr) == 0 && (((uint32_t)xx->TrackFlagBits[itemPtr->track] >> 1) & 1) == 0) {
                        xx->chanToOCB[itemPtr->chan] = xx->GM_Map[itemPtr->vol * 2] & 0x7fff;
                        xx->chanPitch[itemPtr->chan] = xx->GM_Map[itemPtr->vol * 2 + 1];
                    }
                    break;
                case 4:
                    tempL = (itemPtr->key << 8) + itemPtr->vol;
                    SetTempo(xx, (int16_t)tempL);
                    _i_At_Tempo(xx, tempL);
                    break;
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                case 10:
                case 11:
                case 12:
                case 13:
                case 14:
                case 15:
                case 16:
                case 17:
                case 18:
                case 19:
                case 20:
                case 21:
                case 22:
                case 23:
                case 24:
                case 25:
                case 26:
                case 27:
                case 28:
                case 29:
                case 30:
                case 31:
                case 35:
                case 36:
                case 37:
                    break;
                }
            }
            outIndex = (outIndex + 1) & 0x7f;
        }
        outIndex = xx->queueOUT;
        while (outIndex != xx->queueIN) {
            itemPtr = &xx->itemQueue[outIndex];
            if (itemPtr->chan > 0xff) {
                if (itemPtr->cmd == 1 || itemPtr->cmd == 8) {
                    speechChan = itemPtr->chan - 256;
                    itemPtr->dur = ScaleWithTempo(xx, xx->seqPlayBuf[0], itemPtr->dur);
                    Speech_Note(xx, (int16_t)xx->SpeechMap[speechChan], itemPtr->key, itemPtr->nextKey, itemPtr->vol, itemPtr->dur);
                }
            } else {
                if (itemPtr->cmd != 1) {
                    if (itemPtr->cmd == 2) {
                        goto L_83d4c;
                    }
                } else {
                    if (itemPtr->vol == 0) {
                        goto L_83d40;
                    }
                    if (xx->chanToOCB[itemPtr->chan] <= 0x7f && Buffer_MIDI_Out(xx, itemPtr) == 0) {
                        CheckDrum(xx, itemPtr);
                        xx->Vchan = itemPtr->chan;
                        if (xx->chanPitch[itemPtr->chan] != 0) {
                            xx->Vkey = xx->chanPitch[itemPtr->chan];
                        } else {
                            xx->Vkey = itemPtr->key;
                        }
                        xx->Vtrack = itemPtr->track;
                        xx->Vvol = itemPtr->vol;
                        xx->Vdur = itemPtr->dur;
                        VoiceInit(xx);
                        MarkOn(xx);
                    }
                }
                goto L_83e2c;
L_83d40:
                itemPtr->cmd = 2;
L_83d4c:
                if (Buffer_MIDI_Out(xx, itemPtr) == 0) {
                    CheckDrum(xx, itemPtr);
                    xx->Vchan = itemPtr->chan;
                    if (xx->chanPitch[itemPtr->chan] != 0) {
                        xx->Vkey = xx->chanPitch[itemPtr->chan];
                    } else {
                        xx->Vkey = itemPtr->key;
                    }
                    xx->Vtrack = itemPtr->track;
                    xx->Vvol = itemPtr->vol;
                    VoiceRelease(xx);
                    MarkOff(xx);
                }
            }
L_83e2c:
            outIndex = (outIndex + 1) & 0x7f;
        }
        xx->queueOUT = xx->queueIN;
    } while (stillMoreData != 0);
    activeVoices = 0;
    for (i = 0; i < xx->polyphony; i++) {
        curVCB = &xx->VCB_Array[i];
        if (curVCB->priority > 0) {
            activeVoices++;
            if (curVCB->envState <= 0) {
                if (curVCB->noteDur <= 0.0f) {
                    VCB_Release(xx, curVCB);
                } else {
                    curVCB->noteDur -= xx->timeWarp_Inv;
                }
            }
            if (Compute_Envelope(xx, i) == 0) {
                activeVoices--;
                HaltVoice(xx, i);
                curVCB->priority = 0;
            }
        }
    }
    for (i = 0; i <= 63; i++) {
        if (xx->MIDI_Dur_List[i].note >= 0) {
            if (xx->MIDI_Dur_List[i].duration <= 0.0f) {
                _i_MIDI_Buffer(xx, 3, xx->MIDI_Dur_List[i].channel | 128, xx->MIDI_Dur_List[i].note, 0);
                xx->MIDI_Dur_List[i].note = -1;
            } else {
                xx->MIDI_Dur_List[i].duration -= xx->timeWarp_Inv;
                activeVoices++;
            }
        }
    }
    if (activeVoices == 0 && xx->doPostSeq != 0) {
        xx->doPostSeq = 0;
        HaltAllVoices(xx);
        InitChannels(xx);
        _i_MakeEndCB(xx);
    }
    if (xx->clockSource == 0) {
        UpdateTimer(xx);
    }
    xx->freeRunTimer++;
}

/* Music.c:1788  (0x84190) */
void VoiceInit(synthVarsPtr xx)
{
    int16_t voiceSlot;
    DOC_RegsPtr oReg;
    VoiceCtrlBlockPtr curVCB;
    OCBPtr curOCB;
    WaveListPtr curWL;
    VCB_GenPtr curGen;
    int16_t OCB_Number;
    int16_t wlNum;
    int16_t baseIndex;
    int16_t temp;
    int16_t oscOffs;
    int16_t genNum;
    int32_t tempS;

    if ((((uint32_t)xx->TrackFlagBits[xx->Vchan] >> 1) & 1) != 0) {
        OCB_Number = xx->Vocb;
    } else {
        OCB_Number = xx->chanToOCB[xx->Vchan];
    }
    if (OCB_Number < 0) {
        return;
    }
    curOCB = &xx->OCB_Array[OCB_Number];
    for (wlNum = 0; wlNum <= 15; wlNum++) {
        curWL = &curOCB->waveList_OCB[0][wlNum];
        if (xx->Vkey < curWL->topKey) {
            break;
        }
    }
    if (wlNum == 16) {
        return;
    }
    voiceSlot = AllocateVoice(xx);
    curVCB = &xx->VCB_Array[voiceSlot];
    curVCB->vcbCH = xx->Vchan;
    curVCB->genNum = voiceSlot;
    curVCB->priority = 64;
    curVCB->note = xx->Vkey;
    curVCB->chan = xx->Vchan;
    curVCB->envState = 0;
    curVCB->noteDur = xx->Vdur;
    curVCB->segState = 0;
    curVCB->env_dir = 1;
    curVCB->env_lim = 32512;
    tempS = ((63 - (xx->Vvol >> 1)) * curOCB->atkVS) >> 16;
    tempS = curOCB->atkRate - tempS;
    if (tempS < 0) {
        tempS = 0;
    }
    curVCB->env_inc = GetStep(xx, (int16_t)tempS);
    curVCB->env_cur = 1024;
    curVCB->decayStep = curOCB->dkyStep;
    curVCB->sustainLevel = curOCB->susBkpt;
    curVCB->releaseStep = curOCB->relStep;
    curVCB->atkAccelGain = curOCB->atkAccelGain;
    curVCB->dkyAccelGain = curOCB->dkyAccelGain;
    curVCB->atkSlope = curOCB->atkSlope;
    Calc_Log_Acceleration(curVCB, curVCB->atkAccelGain);
    temp = xx->Vkey;
    if (temp > 96) {
        temp = 60;
    } else if (temp <= 35) {
        temp = 0;
    } else {
        temp -= 36;
    }
    curVCB->addDky = (xx->NoteDecayTbl[temp] * curOCB->decayKbd_OCB) >> 16;
    curVCB->sus_flag = 1;
    curVCB->sus_hold = 0;
    curVCB->PBgain = curOCB->pitchBend_OCB;
    oscOffs = 0;
    for (genNum = 0; genNum <= 1; genNum++) {
        curWL = &curOCB->waveList_OCB[genNum][wlNum];
        curGen = &curVCB->genParams[genNum];
        baseIndex = xx->Note_Tbl_Def[xx->Vkey];
        curGen->vfreqA = curWL->pitchA + baseIndex;
        curGen->vfreqB = curWL->pitchB + baseIndex;
        curGen->Detune = curWL->detune;
        temp = ((xx->velToLinPtr[xx->Vvol] * curOCB->VGainScale_OCB) >> 8) + curOCB->VGainAdd_OCB;
        curGen->volA = (temp * curWL->volA) >> 8;
        curGen->volB = (temp * curWL->volB) >> 8;
        curGen->vibratoFreq = curOCB->vibratoFreq[genNum];
        curGen->vibratoDepth = curOCB->vibratoDepth[genNum];
        curGen->vibratoDelay = curOCB->vibratoDelay[genNum];
        curGen->vibratoRamp = curOCB->vibratoRamp[genNum];
        curGen->vibrato_Phase = 0;
        curGen->vibrato_Accum = 0;
        curGen->vibrato_State = 0;
        oReg = &xx->doc[voiceSlot * 2 + oscOffs];
        oReg->state = 0;
        oReg->inSwap = 0;
        oReg->modeA = curWL->oscConfigA;
        oReg->dirA = 1;
        oReg->waveAddrA = curWL->waveAddrA;
        oReg->waveLenA = curWL->waveSizeA;
        oReg->modeB = curWL->oscConfigB;
        oReg->dirB = 1;
        oReg->waveAddrB = curWL->waveAddrB;
        oReg->waveLenB = curWL->waveSizeB;
        oReg->accumulator = curWL->delay;
        oscOffs++;
    }
    Set_Freq(xx, voiceSlot);
}

/* Music.c:2016  (0x84e50) */
void PlayFrame8(synthVarsPtr xx)
{
    int16_t sampCtr;
    DOC_RegsPtr oReg;
    signed char *wavrPtr;
    int32_t sample_L;
    int32_t sample_R;
    uint32_t *curMixer;
    int32_t i;
    int16_t *sampleBuffer;
    int32_t accumulator;
    int32_t phaseInc;
    int32_t waveLen;
    signed char *waveAddr;
    int16_t volume;
    int16_t mode;
    int16_t delay;
    int32_t last_L;
    int32_t last_R;
    int32_t maxSampleL;
    int32_t maxSampleR;

    oReg = &xx->doc[0];
    sampleBuffer = &xx->sampleBuffer[xx->waveIndex];
    curMixer = &xx->sampleMixer[0];
    maxSampleL = xx->maxSampleL;
    maxSampleR = xx->maxSampleR;
    curMixer = &xx->sampleMixer[0];
    for (sampCtr = 0; sampCtr <= 219; sampCtr++) {
        (*curMixer) = 0;
        curMixer++;
    }
    for (i = 0; i <= 95; i++) {
        if (oReg->state == 0) {
            accumulator = oReg->accumulator;
            phaseInc = oReg->phaseIncA;
            waveLen = oReg->waveLenA;
            waveAddr = oReg->waveAddrA;
            volume = oReg->volumeA;
            mode = oReg->modeA;
            if ((i & 1) != 0) {
                curMixer = &xx->sampleMixer[0];
            } else {
                curMixer = &xx->sampleMixer[1];
            }
            for (sampCtr = 0; sampCtr <= 109; sampCtr++) {
                if (accumulator < 0) {
                    curMixer += 2;
                } else {
                    wavrPtr = &waveAddr[accumulator >> 12];
                    sample_L = *wavrPtr;
                    (*curMixer) += ((((accumulator & 4095) * (wavrPtr[1] - sample_L)) >> 12) + sample_L) * volume;
                    curMixer += 2;
                }
                accumulator += phaseInc;
                if (accumulator >= waveLen) {
                    if (mode <= 1) {
                        accumulator -= waveLen;
                    } else if (mode > 2) {
                        accumulator = 0;
                        oReg->phaseIncA = oReg->phaseIncB;
                        phaseInc = oReg->phaseIncA;
                        oReg->waveLenA = oReg->waveLenB;
                        waveLen = oReg->waveLenA;
                        oReg->waveAddrA = oReg->waveAddrB;
                        waveAddr = oReg->waveAddrA;
                        oReg->volumeA = oReg->volumeB;
                        volume = oReg->volumeA;
                        oReg->modeA = oReg->modeB;
                        mode = oReg->modeA;
                        oReg->inSwap = 1;
                    } else {
                        oReg->state = 1;
                        break;
                    }
                }
            }
            oReg->accumulator = accumulator;
        }
        oReg++;
    }
    curMixer = &xx->sampleMixer[0];
    last_L = xx->lastnSamp_L;
    last_R = xx->lastnSamp_R;
    sample_L = *curMixer;
    curMixer++;
    for (sampCtr = 0; sampCtr <= 109; sampCtr++) {
        sample_L >>= 3;
        if (sample_L > maxSampleL) {
            maxSampleL = sample_L;
        }
        sample_R = *curMixer;
        curMixer++;
        if (sample_L > 32760) {
            sample_L = 32760;
        } else if (sample_L < -32760) {
            sample_L = -32760;
        }
        (*sampleBuffer) = sample_L - last_L + last_L;
        sample_R >>= 3;
        sampleBuffer[2] = sample_L;
        if (sample_R > maxSampleR) {
            maxSampleR = sample_R;
        }
        if (sample_R > 32760) {
            sample_R = 32760;
        } else if (sample_R < -32760) {
            sample_R = -32760;
        }
        sampleBuffer[1] = sample_R - last_R + last_R;
        last_L = sample_L;
        sampleBuffer[3] = sample_R;
        last_R = sample_R;
        sampleBuffer += 4;
        sample_L = *curMixer;
        curMixer++;
    }
    xx->lastnSamp_L = last_L;
    xx->lastnSamp_R = last_R;
    xx->waveIndex += 440;
    xx->maxSampleL = maxSampleL;
    xx->maxSampleR = maxSampleR;
}

/* Music.c:2307  (0x853e4) */
void PlayFrame16(synthVarsPtr xx)
{
    DOC_RegsPtr oReg;
    int16_t *wavrPtr;
    int32_t sample_L;
    int32_t sample_R;
    uint32_t *curMixer;
    int32_t i;
    int16_t *sampleBuffer;
    int32_t accumulator;
    int32_t phaseInc;
    int32_t waveLen;
    int16_t *waveAddr;
    int16_t volume;
    int16_t mode;
    int16_t delay;
    int32_t last_L;
    int32_t last_R;
    int32_t maxSampleL;
    int32_t maxSampleR;
    int16_t fwd;
    int32_t sampCtr;
    int16_t t_68;

    oReg = &xx->doc[0];
    sampleBuffer = &xx->sampleBuffer[xx->waveIndex];
    curMixer = &xx->sampleMixer[0];
    maxSampleL = xx->maxSampleL;
    maxSampleR = xx->maxSampleR;
    for (sampCtr = 0; sampCtr <= 219; sampCtr++) {
        (*curMixer) = 0;
        curMixer++;
    }
    for (i = 0; i <= 95; i++) {
        if (oReg->state == 0) {
            accumulator = oReg->accumulator;
            phaseInc = oReg->phaseIncA;
            waveLen = oReg->waveLenA;
            waveAddr = (int16_t *)oReg->waveAddrA;
            volume = oReg->volumeA;
            mode = oReg->modeA;
            fwd = oReg->dirA;
            if ((i & 1) != 0) {
                curMixer = &xx->sampleMixer[0];
            } else {
                curMixer = &xx->sampleMixer[1];
            }
            for (sampCtr = 0; sampCtr <= 109; sampCtr++) {
                if (accumulator < 0) {
                    curMixer += 2;
                } else {
                    if (fwd != 0) {
                        wavrPtr = &waveAddr[accumulator >> 12];
                        sample_L = *wavrPtr;
                        (*curMixer) += (((((accumulator & 4095) * (wavrPtr[1] - sample_L)) >> 12) + sample_L) * volume) >> 8;
                    } else {
                        wavrPtr = &waveAddr[(waveLen - accumulator) >> 12];
                        sample_L = *wavrPtr;
                        (*curMixer) += (((((accumulator & 4095) * (wavrPtr[-1] - sample_L)) >> 12) + sample_L) * volume) >> 8;
                    }
                    curMixer += 2;
                }
                accumulator += phaseInc;
                if (accumulator >= waveLen) {
                    switch (mode) {
                    case 0:
                        accumulator -= waveLen;
                        break;
                    case 1:
                        accumulator -= waveLen;
                        fwd ^= 1;
                        break;
                    case 2:
                        oReg->state = 1;
                        goto L_85818;
                    case 3:
                        accumulator = 0;
                        oReg->phaseIncA = oReg->phaseIncB;
                        phaseInc = oReg->phaseIncA;
                        oReg->waveLenA = oReg->waveLenB;
                        waveLen = oReg->waveLenA;
                        oReg->waveAddrA = oReg->waveAddrB;
                        waveAddr = (int16_t *)oReg->waveAddrA;
                        oReg->volumeA = oReg->volumeB;
                        volume = oReg->volumeA;
                        oReg->modeA = oReg->modeB;
                        mode = oReg->modeA;
                        oReg->dirA = 1;
                        fwd = oReg->dirA;
                        oReg->inSwap = 1;
                        break;
                    }
                }
            }
L_85818:
            oReg->accumulator = accumulator;
            oReg->dirA = fwd;
        }
        oReg++;
    }
    curMixer = &xx->sampleMixer[0];
    last_L = xx->lastnSamp_L;
    last_R = xx->lastnSamp_R;
    sample_L = *curMixer;
    curMixer++;
    for (sampCtr = 0; sampCtr <= 109; sampCtr++) {
        sample_L >>= 3;
        if (sample_L > maxSampleL) {
            maxSampleL = sample_L;
        }
        sample_R = *curMixer;
        curMixer++;
        if (sample_L > 32760) {
            sample_L = 32760;
        } else if (sample_L < -32760) {
            sample_L = -32760;
        }
        (*sampleBuffer) = sample_L - last_L + last_L;
        sample_R >>= 3;
        sampleBuffer[2] = sample_L;
        if (sample_R > maxSampleR) {
            maxSampleR = sample_R;
        }
        if (sample_R > 32760) {
            sample_R = 32760;
        } else if (sample_R < -32760) {
            sample_R = -32760;
        }
        sampleBuffer[1] = sample_R - last_R + last_R;
        last_L = sample_L;
        sampleBuffer[3] = sample_R;
        last_R = sample_R;
        sampleBuffer += 4;
        sample_L = *curMixer;
        curMixer++;
    }
    xx->lastnSamp_L = last_L;
    xx->lastnSamp_R = last_R;
    xx->waveIndex += 440;
    xx->maxSampleL = maxSampleL;
    xx->maxSampleR = maxSampleR;
}

/* Music.c:2579  (0x85a84) */
void Reverb_Demux16_S(float *psLeft, float *psRight, int16_t *psSource, int32_t dwSamples)
{
    while (dwSamples != 0) {
        (*psLeft) = (float)*psSource;
        psLeft++;
        psSource++;
        (*psRight) = -(float)*psSource;
        psRight++;
        psSource++;
        dwSamples--;
    }
}

/* Music.c:2836  (0x86310) */
int16_t Reverberator_ProcessXX(synthVarsPtr xx, int16_t onlyOneFrame)
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
        if (xx->stereo_Synth_ON != 0) {
            Reverb_Demux16_S(xx->m_psDryLeft, xx->m_psDryRight, sampleBuffer, dwSamplesToProcess);
        } else {
            Reverb_Demux16(xx->m_psDryLeft, xx->m_psDryRight, sampleBuffer, dwSamplesToProcess);
        }
        Reverb_Copy16_16(xx->m_psLeft, xx->m_psDryLeft, dwSamplesToProcess, xx->m_dwWetVol);
        Reverb_Copy16_16(xx->m_psRight, xx->m_psDryRight, dwSamplesToProcess, xx->m_dwWetVol);
        Reverb_Mux16(sampleBuffer, xx->m_psLeft, xx->m_psRight, dwSamplesToProcess);
        sampleBuffer = &sampleBuffer[dwSamplesToProcess * 2];
        dwSamplesRemaining -= dwSamplesToProcess;
    }
    return (int16_t)0;
}

/* Music.c:3000  (0x86744) */
void FillSampBuf(synthVarsPtr xx)
{
    xx->waveIndex = 0;
    if (xx->songHasSpeech != 0) {
        NewUpdateAllSpeech(xx);
    }
    if (xx->newReverb_h != 0) {
        XferReverbHold(xx);
    }
    xx->maxSampleL = 0;
    xx->maxSampleR = 0;
    for (xx->curSndFrame = 0; xx->curSndFrame < xx->SoundBufferFrames; xx->curSndFrame++) {
        PlayFrame16(xx);
        if (xx->songHasSpeech != 0) {
            UpdateAllSpeech(xx);
        }
        Update(xx);
    }
    if (xx->reverbON == 0) {
        return;
    }
    Reverberator_Process(xx, 0);
}

/* Music.c:3040  (0x8687c) */
void SetNextBuffer(synthVarsPtr xx)
{
    if (xx->nextSampBuf == 1) {
        xx->sampleBuffer = xx->sampleBuffer1;
        xx->nextSampBuf = 0;
        return;
    }
    xx->sampleBuffer = xx->sampleBuffer2;
    xx->nextSampBuf = 1;
}

/* Music.c:3058  (0x868fc) */
void Start_The_Music(synthVarsPtr xx)
{
    FillSampBuf(xx);
    _i_First_Sample_Buffer(xx, xx->sampleBuffer, xx->waveIndex);
    SetNextBuffer(xx);
    FillSampBuf(xx);
    _i_Cur_Sample_Buffer(xx, xx->sampleBuffer, xx->waveIndex);
    SetNextBuffer(xx);
}

/* Music.c:3086  (0x8699c) */
void ChaseControlers(synthVarsPtr xx, int32_t endTime)
{
    int16_t trackNum;
    MIDI_Event me;
    int32_t tempL;
    int16_t lastTempo;
    int16_t lastVocalIndex;
    int16_t lastSpeechPgm;
    int16_t speechChan;
    MIDI_Item PGM_item;
    MIDI_Item Vol_item;
    MIDI_Item Sus_item;
    MIDI_Item PB_item;
    MIDI_Item PBS_item;

    lastVocalIndex = -1;
    PGM_item.cmd = 3;
    Vol_item.cmd = 33;
    Sus_item.cmd = 34;
    PB_item.cmd = 32;
    PBS_item.cmd = 38;
    for (trackNum = 0; trackNum <= 31; trackNum++) {
        PGM_item.nextKey = 0;
        Vol_item.nextKey = 0;
        Sus_item.nextKey = 0;
        PB_item.nextKey = 0;
        PBS_item.nextKey = 0;
        if (xx->trackToMIDI[trackNum] >= 0) {
            PGM_item.track = trackNum;
            Vol_item.track = trackNum;
            Sus_item.track = trackNum;
            PB_item.track = trackNum;
            PBS_item.track = trackNum;
        } else if (xx->songHasSpeech != 0 && xx->SpeechMap[trackNum] >= 0) {
            speechChan = xx->SpeechMap[trackNum];
        }
        lastTempo = 0;
        lastSpeechPgm = -1;
        if ((xx->seqStatus & (1 << trackNum)) != 0) {
            me.targetTrack = xx->seqPlayBuf[trackNum];
            me.target_time = 0;
            me.target_endTime = endTime;
L_86b4c:
            if (GetNextTrackEvent(&me, trackNum) != 0) {
                if (xx->songHasSpeech != 0 && xx->SpeechMap[trackNum] >= 0) {
                    if ((uint32_t)(me.target_cmd - 3) > 68) {
                        goto L_86b4c;
                    }
                    switch (me.target_cmd) {
                    case 6:
                    case 8:
                        xx->startNoteNum[speechChan]++;
                        goto L_86b4c;
                    case 3:
                        lastSpeechPgm = me.target_vol;
                        goto L_86b4c;
                    case 39:
                        Speech_Detune(xx, speechChan, ((me.target_vol << 7) + me.target_key - 8192) << 3);
                        goto L_86b4c;
                    case 32:
                        Speech_PitchBend(xx, speechChan, ((me.target_vol << 7) + me.target_key - 8192) << 3);
                        goto L_86b4c;
                    case 38:
                        if ((uint32_t)me.target_vol > 12) {
                            me.target_vol = 12;
                        }
                        Speech_PBSens(xx, speechChan, me.target_vol);
                        goto L_86b4c;
                    case 64:
                        Speech_Color(xx, speechChan, me.target_vol);
                        goto L_86b4c;
                    case 71:
                        Speech_Noise(xx, speechChan, me.target_vol);
                        goto L_86b4c;
                    case 67:
                        Speech_VibDepth(xx, speechChan, me.target_vol);
                        goto L_86b4c;
                    case 68:
                        Speech_VibFreq(xx, speechChan, me.target_vol);
                        goto L_86b4c;
                    case 69:
                        Speech_Portamento(xx, speechChan, me.target_vol);
                        goto L_86b4c;
                    case 66:
                        Speech_Chorus(xx, speechChan, (me.target_vol << 7) + me.target_key - 8192);
                        goto L_86b4c;
                    case 33:
                        Speech_Volume(xx, speechChan, me.target_vol);
                        goto L_86b4c;
                    case 70:
                        Speech_Breath(xx, speechChan, me.target_vol);
                        goto L_86b4c;
                    }
                }
                if (xx->trackToMIDI[trackNum] >= 0) {
                    if ((uint32_t)(me.target_cmd - 3) > 35) {
                        goto L_86b4c;
                    }
                    switch (me.target_cmd) {
                    case 32:
                        PB_item.key = me.target_key;
                        PB_item.vol = me.target_vol;
                        PB_item.nextKey = 1;
                        goto L_86b4c;
                    case 38:
                        PBS_item.vol = me.target_vol;
                        PBS_item.nextKey = 1;
                        goto L_86b4c;
                    case 33:
                        Vol_item.vol = me.target_vol;
                        Vol_item.nextKey = 1;
                        goto L_86b4c;
                    case 34:
                        Sus_item.vol = me.target_vol;
                        Sus_item.nextKey = 1;
                        goto L_86b4c;
                    case 3:
                        PGM_item.vol = me.target_vol;
                        PGM_item.nextKey = 1;
                        goto L_86b4c;
                    }
                }
                if ((uint32_t)(me.target_cmd - 3) > 36) {
                    goto L_86b4c;
                }
                switch (me.target_cmd) {
                case 6:
                    if (trackNum != xx->karaokeTrack) {
                        goto L_86b4c;
                    }
                    lastVocalIndex = me.target_vocals;
                    goto L_86b4c;
                case 32:
                    xx->chPBTbl[me.target_chan] = ((me.target_vol << 7) + me.target_key - 8192) << 8;
                    goto L_86b4c;
                case 39:
                    tempL = ((me.target_vol << 7) + me.target_key - 8192) << 3;
                    if ((((uint32_t)xx->TrackFlagBits[trackNum] >> 1) & 1) != 0) {
                        xx->chDetune[me.target_chan] = (tempL << 5) >> 14;
                        goto L_86b4c;
                    }
                    xx->chDetune[me.target_chan] = (tempL << 5) >> 16;
                    goto L_86b4c;
                case 38:
                    if ((uint32_t)me.target_vol > 12) {
                        me.target_vol = 12;
                    }
                    xx->chPBRange[me.target_chan] = me.target_vol;
                    goto L_86b4c;
                case 33:
                    xx->chVolTbl[me.target_chan] = me.target_vol << 1;
                    goto L_86b4c;
                case 34:
                    xx->chSusTbl[me.target_chan] = (uint32_t)me.target_vol >> 6;
                    goto L_86b4c;
                case 3:
                    if ((((uint32_t)xx->TrackFlagBits[trackNum] >> 1) & 1) != 0) {
                        goto L_86b4c;
                    }
                    xx->chanToOCB[me.target_chan] = xx->GM_Map[me.target_vol * 2] & 0x7fff;
                    xx->chanPitch[me.target_chan] = xx->GM_Map[me.target_vol * 2 + 1];
                    goto L_86b4c;
                case 4:
                    tempL = (me.target_key << 8) + me.target_vol;
                    SetTempo(xx, (int16_t)tempL);
                    lastTempo = tempL;
                    goto L_86b4c;
                }
            }
            xx->seqPlayBuf[trackNum] = me.targetTrack;
            if (lastTempo != 0) {
                _i_At_Tempo_Imm(xx, lastTempo);
            }
            if (lastSpeechPgm >= 0) {
                PgmChange_Speech(xx, (int16_t)xx->SpeechMap[trackNum], lastSpeechPgm);
            }
        }
        if (PGM_item.nextKey != 0) {
            Buffer_MIDI_Out(xx, &PGM_item);
        }
        if (Vol_item.nextKey != 0) {
            Buffer_MIDI_Out(xx, &Vol_item);
        }
        if (Sus_item.nextKey != 0) {
            Buffer_MIDI_Out(xx, &Sus_item);
        }
        if (PB_item.nextKey != 0) {
            Buffer_MIDI_Out(xx, &PB_item);
        }
        if (PBS_item.nextKey != 0) {
            Buffer_MIDI_Out(xx, &PBS_item);
        }
    }
    if (lastVocalIndex < 0) {
        return;
    }
    _i_At_Kara_Imm(xx, lastVocalIndex);
}

/* Music.c:3331  (0x87620) */
int16_t StartSeq(synthVarsPtr xx, PlayRecPtr sp)
{
    int16_t i;
    SeqInfoPtr newSeq;
    TrackInfoPtr curTrack;
    unsigned char *curTrackData;
    int16_t *curSpeechData;
    int32_t startTime;
    int16_t error;
    int16_t speechCount;
    int16_t t_68;

    error = 0;
    if ((sp->theFlags & 3) != 0) {
        xx->meter = 0;
        xx->timer = 0.0f;
        xx->timeWarp_P = 0.0f;
        xx->countDown = 0;
        for (i = 0; i <= 31; i++) {
            xx->seqPlayBuf[i] = NULL;
        }
        for (i = 0; i <= 31; i++) {
            xx->chanToOCB[i] = 0;
            xx->chanPitch[i] = 0;
        }
        xx->songHasSpeech = 0;
        newSeq = (SeqInfoPtr)sp->PbufStart;
        xx->seqStatus = 0;
        xx->beatCount = -1.0f;
        xx->metro = 0;
        speechCount = 0;
        for (i = 0; i <= 31; i++) {
            curTrack = &newSeq->tracks[i];
            curTrackData = (unsigned char *)((char *)newSeq + curTrack->trackData);
            xx->TrackFlagBits[i] = curTrack->flags;
            if ((curTrack->flags & 1) != 0) {
                if (xx->SpeechMap[i] >= 0 && i == xx->speechChanToTrack[xx->SpeechMap[i]] && xx->speechVars[xx->SpeechMap[i]] != 0) {
                    curSpeechData = (int16_t *)((char *)newSeq + curTrack->speechData);
                    NewSong_Speech(xx, (int16_t)xx->SpeechMap[i], curSpeechData);
                    xx->songHasSpeech = 1;
                    speechCount++;
                } else {
                    error = 1005;
                    goto L_87bf0;
                }
            }
            xx->seqPlayBuf[i] = curTrackData;
            xx->seqStatus |= 1 << curTrack->trackNum;
        }
        xx->seqInfo = newSeq;
        InitChannels(xx);
        xx->doPostSeq = 0;
        for (i = 0; i <= 19; i++) {
            xx->startNoteNum[i] = 0;
        }
        xx->polyphony = sp->polyphony;
        if (xx->songHasSpeech != 0) {
            StartAllSpeech(xx);
        }
        if (sp->startBeat > 0) {
            startTime = FTOI(xx->ticksPerBeat);
            startTime *= sp->startBeat;
            ChaseControlers(xx, startTime);
            xx->timer = (float)startTime * xx->timeWarp;
        }
        if (xx->songHasSpeech != 0) {
            StartPointSpeech(xx);
        }
        if (((sp->theFlags >> 3) & 1) != 0) {
            xx->playMerto = 1;
        } else {
            xx->playMerto = 0;
        }
        xx->seqFlags |= 1;
        if (((sp->theFlags >> 1) & 1) != 0) {
            xx->seqFlags |= 2;
            xx->rec_BufPtr = sp->RbufStart;
            xx->rec_BufPtr_End = &sp->RbufStart[sp->RbufLen];
        }
    } else {
        sp->RbufLen = (int32_t)(xx->rec_BufPtr - (unsigned char *)sp->RbufStart);
        xx->seqFlags = 0;
        xx->playMerto = 0;
        xx->doPostSeq = 1;
        if (xx->songHasSpeech != 0) {
            StopAllSpeech(xx);
        }
    }
    t_68 = 0;
    return (int16_t)t_68;
L_87bf0:
    t_68 = error;
    return t_68;
}

/* Music.c:3474  (0x87c18) */
int16_t SingMIDINote(synthVarsPtr xx, int16_t trackNum, int16_t *curSpeechData, int16_t message, int16_t note, int16_t vel)
{
    int16_t i;
    int16_t error;
    int16_t speechChan;

    error = 0;
    if (xx->SpeechMap[trackNum] >= 0 && trackNum == xx->speechChanToTrack[xx->SpeechMap[trackNum]] && xx->speechVars[xx->SpeechMap[trackNum]] != 0) {
        speechChan = xx->SpeechMap[trackNum];
        switch (message) {
        case 5000:
            SetTempo(xx, 120);
            NewSong_Speech(xx, speechChan, curSpeechData);
            Sing_Speech(xx, speechChan, 1);
            Start_Speech(xx, speechChan);
            xx->startNoteNum[speechChan] = 0;
            StartPoint_Speech(xx, speechChan);
            StartMIDIMode(xx, speechChan);
            xx->songHasSpeech = 1;
            return error;
        case 5001:
            Stop_Speech(xx, speechChan);
            xx->songHasSpeech = 0;
            StopMIDIMode(xx, speechChan);
            return error;
        case 2:
            Speech_Note(xx, speechChan, note, note, vel, 240.0f);
            return error;
        }
        NewSong_Speech(xx, speechChan, curSpeechData);
        StartPoint_Speech(xx, speechChan);
        Speech_Note(xx, speechChan, note, note, vel, 240.0f);
        return error;
    }
    error = 1005;
    return error;
}

/* Music.c:3525  (0x87f24) */
void FillBufWithZero(synthVarsPtr xx)
{
    int32_t *bufPtr;
    int32_t index;
    int32_t len;

    xx->waveIndex = xx->SoundBufferLen;
}

/* Music.c:3544  (0x87f68) */
int16_t FillNextSampBuffer(synthVarsPtr xx, int16_t fillZeros)
{
    if (fillZeros != 0) {
        FillBufWithZero(xx);
    } else {
        FillSampBuf(xx);
    }
    _i_Cur_Sample_Buffer(xx, xx->sampleBuffer, xx->waveIndex);
    SetNextBuffer(xx);
    return (int16_t)0;
}

/* Music.c:3565  (0x88004) */
int16_t GetNewWL(synthVarsPtr xx, WaveListPtr curWL, WaveListDef *inst, int32_t volScale)
{
    int16_t error;
    int16_t i;
    int16_t j;
    int16_t tempH;
    int16_t config;
    int16_t osc;
    int16_t pitchH;
    int32_t index;
    int32_t tempL;
    int16_t negF;
    int32_t waveALen;
    int32_t t_38;

    error = 0;
    tempH = inst->U_TopKey;
    if (tempH <= 0x7f && tempH >= 0) {
        curWL->topKey = tempH;
        tempH = inst->U_Delay;
        if (tempH <= 0xff && tempH >= 0) {
            curWL->delay = tempH;
            tempH = inst->U_Detune;
            if (tempH <= 63 && tempH >= 0) {
                curWL->detune = tempH << 4;
                tempH = inst->U_WaveRefA;
                curWL->waveAddrA = (signed char *)&xx->Wave_Data[tempH].waveName[xx->Wave_Data[tempH].waveOffset];
                waveALen = xx->Wave_Data[tempH].waveLen;
                curWL->waveSizeA = waveALen << 12;
                tempH = inst->U_WaveRefB;
                curWL->waveAddrB = (signed char *)&xx->Wave_Data[tempH].waveName[xx->Wave_Data[tempH].waveOffset];
                curWL->waveSizeB = xx->Wave_Data[tempH].waveLen << 12;
                config = inst->U_Alg;
                if (config <= 5 && config >= 0) {
                    config <<= 1;
                    curWL->oscConfigA = xx->OscModeTbl[config];
                    config++;
                    curWL->oscConfigB = xx->OscModeTbl[config];
                    if (curWL->delay != 0) {
                        if (curWL->oscConfigA == 0 || curWL->oscConfigA == 1) {
                            curWL->delay = (curWL->delay << 12) / 256 * waveALen;
                        } else {
                            curWL->delay = 1 - (curWL->delay << 14);
                        }
                    }
                    tempH = inst->U_OctA;
                    if (tempH > 6) {
                        tempH = 6;
                    }
                    if (tempH >= 0) {
                        pitchH = xx->Oct_Tbl[tempH] + 384;
                        tempH = inst->U_SemiA;
                        if (tempH <= 11 && tempH >= 0) {
                            pitchH = (tempH << 5) + pitchH;
                            tempH = inst->U_FineA;
                            if (tempH <= 63 && tempH >= 0) {
                                curWL->pitchA = (tempH >> 1) + pitchH;
                                tempH = inst->U_OctB;
                                if (tempH > 6) {
                                    tempH = 6;
                                }
                                if (tempH >= 0) {
                                    pitchH = xx->Oct_Tbl[tempH] + 384;
                                    tempH = inst->U_SemiB;
                                    if (tempH <= 11 && tempH >= 0) {
                                        pitchH = (tempH << 5) + pitchH;
                                        tempH = inst->U_FineB;
                                        if (tempH <= 63 && tempH >= 0) {
                                            curWL->pitchB = (tempH >> 1) + pitchH;
                                            if ((uint32_t)inst->U_VolA > 100) {
                                                tempL = 100;
                                            } else {
                                                tempL = inst->U_VolA;
                                            }
                                            tempL = (((tempL << 16) / 100) << 7) >> 16;
                                            tempL = ((tempL << 1) * xx->sysVolume) >> 8;
                                            tempL = (tempL * volScale) >> 16;
                                            curWL->volA = tempL;
                                            if ((uint32_t)inst->U_VolB > 100) {
                                                tempL = 100;
                                            } else {
                                                tempL = inst->U_VolB;
                                            }
                                            tempL = (((tempL << 16) / 100) << 7) >> 16;
                                            tempL = ((tempL << 1) * xx->sysVolume) >> 8;
                                            tempL = (tempL * volScale) >> 16;
                                            curWL->volB = tempL;
                                            t_38 = error;
                                            return (int16_t)t_38;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    t_38 = 1001;
    return (int16_t)t_38;
}

/* Music.c:3720  (0x886a8) */
int16_t Copy_Inst(synthVarsPtr xx, InstDefPtr instPtr, WaveListDefPtr wlData, int16_t instNum)
{
    OCBPtr curOCB;
    WaveListPtr curWL;
    WaveListDefPtr waveDefPtr;
    int16_t numberWL;
    int16_t tempH;
    int16_t error;
    int16_t i;
    int32_t tempL;
    int32_t volScale;

    error = 0;
    if (instNum == 119) {
        error = 0;
    }
    curOCB = &xx->OCB_Array[instNum];
    numberWL = instPtr->U_WLs;
    if (numberWL > 16 || numberWL <= 0) {
        error = 1001;
        return error;
    }
    curOCB->WLNum_OCB = numberWL;
    tempL = instPtr->U_Vsens;
    if (tempL > 100 || tempL < 0) {
        error = 1001;
        return error;
    }
    tempL = (((tempL << 16) / 100) << 7) >> 16;
    curOCB->VGainAdd_OCB = 128 - tempL;
    curOCB->VGainScale_OCB = tempL << 1;
    curOCB->pitchBend_OCB = 2;
    if ((uint32_t)instPtr->U_Atk > 63) {
        curOCB->atkRate = 63;
    } else {
        curOCB->atkRate = instPtr->U_Atk;
    }
    if ((uint32_t)instPtr->U_AtkVS > 100) {
        tempL = 100;
    } else {
        tempL = instPtr->U_AtkVS;
    }
    curOCB->atkVS = (tempL << 16) / 100 + 1;
    curOCB->dkyStep = GetStep(xx, (int16_t)instPtr->U_Dky);
    curOCB->relStep = GetStep(xx, (int16_t)instPtr->U_Rel);
    if ((uint32_t)instPtr->U_AtkSlope > 2) {
        curOCB->atkSlope = 2;
    } else {
        curOCB->atkSlope = instPtr->U_AtkSlope;
    }
    tempL = 20;
    curOCB->dkyAccelGain = (tempL << 14) / 10;
    tempL = 20;
    curOCB->atkAccelGain = (tempL << 14) / 10;
    if ((uint32_t)instPtr->U_Sus > 100) {
        tempL = 100;
    } else {
        tempL = instPtr->U_Sus;
    }
    curOCB->susBkpt = (((tempL << 16) / 100) << 7) >> 16;
    if ((uint32_t)instPtr->U_EnvGain > 200) {
        volScale = 200;
    } else {
        volScale = instPtr->U_EnvGain;
    }
    volScale = (volScale << 16) / 100;
    tempL = instPtr->U_DecayKbd;
    if (tempL > 100 || tempL < 0) {
        error = 1001;
        return error;
    }
    curOCB->decayKbd_OCB = (tempL << 16) / 20;
    tempL = instPtr->U_vibFreq1;
    tempL = (tempL << 16) / 10;
    curOCB->vibratoFreq[0] = (tempL << 8) / 200;
    tempL = instPtr->U_vibDepth1;
    curOCB->vibratoDepth[0] = (tempL << 16) / 1000;
    curOCB->vibratoDelay[0] = GetStep(xx, (int16_t)instPtr->U_vibDelay1);
    curOCB->vibratoRamp[0] = GetStep(xx, (int16_t)instPtr->U_vibRamp1);
    tempL = instPtr->U_vibFreq2;
    tempL = (tempL << 16) / 10;
    curOCB->vibratoFreq[1] = (tempL << 8) / 200;
    tempL = instPtr->U_vibDepth2;
    curOCB->vibratoDepth[1] = (tempL << 16) / 1000;
    curOCB->vibratoDelay[1] = GetStep(xx, (int16_t)instPtr->U_vibDelay2);
    curOCB->vibratoRamp[1] = GetStep(xx, (int16_t)instPtr->U_vibRamp2);
    for (i = 0; i < numberWL; i++) {
        waveDefPtr = (WaveListDefPtr)((char *)wlData - 40 + instPtr->U_WLRef[i] * 40);
        curWL = &curOCB->waveList_OCB[0][i];
        error = GetNewWL(xx, curWL, waveDefPtr, volScale);
        if (error != 0) {
            return error;
        }
        waveDefPtr++;
        curWL = &curOCB->waveList_OCB[1][i];
        error = GetNewWL(xx, curWL, waveDefPtr, volScale);
        if (error != 0) {
            return error;
        }
    }
    return error;
}

/* Music.c:3913  (0x88d80) */
int16_t Copy_Bank(synthVarsPtr xx, InstDefPtr instPtr, WaveListDefPtr wlData, int16_t numOfInst)
{
    int16_t i;

    for (i = 0; i < numOfInst; i++) {
        Copy_Inst(xx, &instPtr[i], wlData, i);
    }
    return 0;   /* the original returned r3, undefined */
}

/* Music.c:3922  (0x88e3c) */
int16_t SetWaveBank(synthVarsPtr xx, Ptr waveData, int16_t PCMType)
{
    xx->Wave_Data = (WaveDefPtr)waveData;
    xx->PCMType = PCMType;
    return (int16_t)0;
}

/* Music.c:3930  (0x88e98) */
void SetPlayTrack(synthVarsPtr xx, int16_t trackNum, int16_t trackState)
{
    xx->PlayTrackMap[trackNum] = trackState;
    if (trackState == 0) {
        TurnTrackOff(xx, trackNum);
        return;
    }
    TurnTrackOn(xx, trackNum);
}

/* Music.c:3941  (0x88f44) */
void SetChanOCB(synthVarsPtr xx, int16_t chanNum, int16_t ocbNum)
{
    xx->chanToOCB[chanNum] = ocbNum;
}

/* Music.c:3948  (0x88fa4) */
void AllOff(synthVarsPtr xx)
{
    int16_t i;
    VoiceCtrlBlockPtr curVCB;
    int16_t channel;

    for (i = 0; i <= 33; i++) {
        xx->chSusTbl[i] = 0;
    }
    for (i = 0; i < xx->polyphony; i++) {
        curVCB = &xx->VCB_Array[i];
        if (curVCB->priority != 0 && curVCB->envState <= 0) {
            xx->foreQueue[xx->foreQueueIN].cmd = 2;
            xx->foreQueue[xx->foreQueueIN].chan = curVCB->chan;
            xx->foreQueue[xx->foreQueueIN].key = curVCB->note;
            xx->foreQueue[xx->foreQueueIN].vol = 0;
            xx->foreQueue[xx->foreQueueIN].track = curVCB->chan;
            xx->foreQueueIN = (xx->foreQueueIN + 1) & 31;
        }
    }
    for (i = 0; i <= 63; i++) {
        if (xx->MIDI_Dur_List[i].note >= 0) {
            _i_MIDI_Buffer(xx, 3, xx->MIDI_Dur_List[i].channel | 128, xx->MIDI_Dur_List[i].note, 0);
            xx->MIDI_Dur_List[i].note = -1;
        }
    }
    for (i = 0; i <= 31; i++) {
        channel = xx->trackToMIDI[i];
        if (channel >= 0) {
            _i_MIDI_Buffer(xx, 3, (int16_t)((uint16_t)channel + 176), 64, 0);
            _i_MIDI_Buffer(xx, 3, (int16_t)((uint16_t)channel + 176), 7, 0x7f);
            _i_MIDI_Buffer(xx, 3, (int16_t)((uint16_t)channel + 224), 0, 64);
            _i_MIDI_Buffer(xx, 3, (int16_t)((uint16_t)channel + 176), 101, 0);
            _i_MIDI_Buffer(xx, 3, (int16_t)((uint16_t)channel + 176), 100, 0);
            _i_MIDI_Buffer(xx, 3, (int16_t)((uint16_t)channel + 176), 6, 2);
            _i_MIDI_Buffer(xx, 3, (int16_t)((uint16_t)channel + 176), 121, 0);
        }
    }
}

/* Music.c:4018  (0x893f8) */
int16_t QueueMIDIMsg(synthVarsPtr xx, int16_t dest, int16_t byteNum, int16_t message, int16_t channel, int16_t data1, int16_t data2)
{
    if (dest == 8) {
        if (xx->songHasSpeech == 0) {
            return 0;   /* r3, undefined */
        }
        SingMIDI_Speech(xx, 0, data1, data2);
        return 0;   /* r3, undefined */
    }
    if (dest != 4) {
        return 0;   /* the original returned r3, undefined */
    }
    xx->foreQueue[xx->foreQueueIN].cmd = message;
    xx->foreQueue[xx->foreQueueIN].chan = 32;
    xx->foreQueue[xx->foreQueueIN].key = data1;
    xx->foreQueue[xx->foreQueueIN].vol = data2;
    xx->foreQueue[xx->foreQueueIN].track = 32;
    xx->foreQueueIN = (xx->foreQueueIN + 1) & 31;
    return 0;   /* the original returned r3, undefined */
}

/* Music.c:4044  (0x895bc) */
int16_t InitMusicChannel(synthVarsPtr xx)
{
    int16_t i;
    DOC_RegsPtr oReg;
    int16_t error;

    error = 0;
    HaltAllVoices(xx);
    xx->seqFlags = 0;
    SetTempo(xx, 120);
    xx->beatTicks = 240;
    xx->clockSource = 0;
    xx->sysVolume = 240;
    xx->doPostSeq = 0;
    xx->karaokeTrack = -1;
    xx->tempoMul = 1.0f;
    xx->queueIN = 0;
    xx->queueOUT = xx->queueIN;
    xx->minSamp = 0;
    xx->maxSamp = xx->minSamp;
    for (i = 0; i <= 33; i++) {
        xx->chanToOCB[i] = 0;
        xx->chanPitch[i] = 0;
    }
    for (i = 0; i <= 31; i++) {
        xx->seqPlayBuf[i] = NULL;
        xx->PlayTrackMap[i] = 0;
        xx->TrackLevel[i] = 256;
        xx->trackToMIDI[i] = -1;
    }
    xx->TrackFlagBits[32] = 0;
    xx->TrackLevel[32] = 256;
    xx->trackToMIDI[32] = -1;
    xx->TrackFlagBits[33] = 0;
    xx->TrackLevel[33] = 256;
    xx->trackToMIDI[33] = -1;
    xx->chanToOCB[33] = 71;
    xx->delay1L = 0.0f;
    xx->delay1R = 0.0f;
    xx->delay2L = 0.0f;
    xx->delay2R = 0.0f;
    xx->filterPitch = 50;
    xx->filterBW = 972;
    xx->songHasSpeech = 0;
    XferReverbHold(xx);
    for (i = 0; i <= 63; i++) {
        xx->MIDI_Dur_List[i].note = -1;
    }
    return error;
}

/* Music.c:4127  (0x898bc) */
int16_t InitNewSpeechChan(synthVarsPtr xx, int16_t trackNum)
{
    int16_t error;
    int16_t newSpeechChan;

    if (xx->NumOfSpeechChans > 19) {
        error = 1006;
        return error;
    }
    for (newSpeechChan = 0; newSpeechChan <= 19; newSpeechChan++) {
        if (xx->speechVars[newSpeechChan] == 0) {
            break;
        }
    }
    if (newSpeechChan > 19) {
        error = 1006;
        return error;
    }
    error = _i_Get_Speech(xx, newSpeechChan);
    if (error != 0) {
        return error;
    }
    InitGlobals_Speech(xx, newSpeechChan);
    xx->speechChanToTrack[newSpeechChan] = trackNum;
    xx->SpeechMap[trackNum] = newSpeechChan;
    xx->NumOfSpeechChans++;
    return error;
}

/* Music.c:4165  (0x89a38) */
int16_t DisposeSpeechChan(synthVarsPtr xx, int16_t trackNum)
{
    int16_t error;

    error = 0;
    if (trackNum == xx->speechChanToTrack[xx->SpeechMap[trackNum]]) {
        _i_Dispose_Speech(xx, (int16_t)xx->SpeechMap[trackNum]);
        return error;
    }
    error = 1005;
    return error;
}

/* Music.c:4180  (0x89b10) */
void SetTheTrackLevel(synthVarsPtr xx, int16_t trackNum, int16_t level)
{
    int32_t tempL;

    tempL = level;
    xx->TrackLevel[trackNum] = ((tempL << 16) / 100) >> 8;
    if (xx->SpeechMap[trackNum] < 0) {
        return;
    }
    Speech_TrackLevel(xx, (int16_t)xx->SpeechMap[trackNum], level);
}

/* Music.c:4193  (0x89c0c) */
void SetKbdFlags(synthVarsPtr xx, int32_t kbdFlags)
{
    xx->TrackFlagBits[32] = kbdFlags;
}
