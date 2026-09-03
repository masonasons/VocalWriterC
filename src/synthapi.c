/* synthapi.c -- the library's public face: Macintosh.c's Synth_* calls.
 *
 * Lifted from the original's machine code; see src/speech.c. What talked
 * to Mac OS itself -- the sound channel, the Time Manager, the Resource
 * Manager, the deferred task that filled the next buffer -- lives in
 * src/synthglue.c instead, written for rendering offline.
 */
#include "vw_engine.h"

void CalibrateSndBuffer(shellVarPtr svv, int16_t latency);
static void GetTheTimers(shellVarPtr svv);
static void RemoveTheTimers(shellVarPtr svv);
static void FreeTheTimers(shellVarPtr svv);
static void Tempo_TimerTask(TMTaskPtr tmTaskPtr);
static void Kara_TimerTask(TMTaskPtr tmTaskPtr);
static void Beat_TimerTask(TMTaskPtr tmTaskPtr);
static void SeqItem_TimerTask(TMTaskPtr tmTaskPtr);
static void SeqMark_TimerTask(TMTaskPtr tmTaskPtr);
static void MaybeDisposeSharedGlobals(void);
static void CloseMusicChannel(shellVarPtr svv);
static void SendBufCmd(shellVarPtr svv, int16_t nullBuf);
static void SendMIDIBuffer(shellVarPtr svv);
static void SM_CallBack(SndChannelPtr chan, SndCommand *cmd);
static int16_t InitSound16(shellVarPtr svv);

/* Macintosh.c:331  (0x73974) */
void TerminateSynth(void)
{
}

/* Macintosh.c:337  (0x739a0) */
void CalibrateSndBuffer(shellVarPtr svv, int16_t latency)
{
    double tempD;
    int32_t numOfFrames;

    if (latency <= 9) {
        latency = 10;
    } else if (latency > 100) {
        latency = 100;
    }
    numOfFrames = (int16_t)(latency / 5);
    svv->SoundBufferFrames = numOfFrames;
    svv->ChannelGlobals->SoundBufferFrames = svv->SoundBufferFrames;
    tempD = (double)numOfFrames * 4988.662131508;
    svv->SoundBufferTime = FTOI(tempD);
    svv->SoundBufferLen = numOfFrames * 220;
    svv->ChannelGlobals->SoundBufferLen = svv->SoundBufferLen;
}

/* Macintosh.c:365  (0x73af4) */
int16_t Synth_Startup(shellVarPtr *svvPtr, int32_t polyphony, int32_t reverbAllowed)
{
    int16_t error;
    shellVarPtr svv;
    int16_t err;
    int16_t refNum;
    int16_t i;
    int16_t t_48;

    svv = (shellVarPtr)NewPtrClear(668);
    if (svv == 0) {
        error = -620;
    } else {
        svv->ret = 825373492;
        svv->SndChan = NULL;
        svv->nullBufPtr = NULL;
        svv->soundBufPtr = NULL;
        svv->ChannelGlobals = NULL;
        svv->seqErrorCallBackProc = NULL;
        svv->seqItemCallBackProc = NULL;
        svv->seqMarkCallBackProc = NULL;
        svv->seqDoneCallBackProc = NULL;
        svv->OverloadCallBackProc = NULL;
        svv->beatCallBackProc = NULL;
        svv->tempoCallBackProc = NULL;
        svv->karaCallBackProc = NULL;
        svv->meterCallBackProc = NULL;
        svv->OMSOutCallBackProc = NULL;
        svv->MusicDTProc = NULL;
        svv->SoundCBProc = NULL;
        svv->beat_CBTimerProc = NULL;
        svv->tempo_CBTimerProc = NULL;
        svv->kara_CBTimerProc = NULL;
        svv->seqItem_CBTimerProc = NULL;
        svv->seqMark_CBTimerProc = NULL;
        svv->MIDI_Buf1 = NULL;
        svv->MIDI_Buf2 = NULL;
        svv->MemBufStart = NULL;
        svv->MemBufEnd = NULL;
        svv->freeTask = NULL;
        svv->backTime = 0;
        svv->backTime2 = svv->backTime;
        svv->backTime1 = svv->backTime2;
        svv->frameTime = 0;
        svv->frameTime2 = svv->frameTime;
        svv->frameTime1 = svv->frameTime2;
        svv->latencyMS = 20;
        svv->MusicDTProc = (DeferredTaskUPP)NewDeferredTaskUPP(&Synth_MusicDT);
        if (svv->MusicDTProc == 0) {
            error = -620;
        } else {
            svv->SoundCBProc = (SndCallBackUPP)NewSndCallBackUPP(&SM_CallBack);
            if (svv->SoundCBProc == 0) {
                error = -620;
            } else {
                svv->beat_CBTimerProc = (TimerUPP)NewTimerUPP(&Beat_TimerTask);
                if (svv->beat_CBTimerProc == 0) {
                    error = -620;
                } else {
                    svv->tempo_CBTimerProc = (TimerUPP)NewTimerUPP(&Tempo_TimerTask);
                    if (svv->tempo_CBTimerProc == 0) {
                        error = -620;
                    } else {
                        svv->kara_CBTimerProc = (TimerUPP)NewTimerUPP(&Kara_TimerTask);
                        if (svv->kara_CBTimerProc == 0) {
                            error = -620;
                        } else {
                            svv->seqItem_CBTimerProc = (TimerUPP)NewTimerUPP(&SeqItem_TimerTask);
                            if (svv->seqItem_CBTimerProc == 0) {
                                error = -620;
                            } else {
                                svv->seqMark_CBTimerProc = (TimerUPP)NewTimerUPP(&SeqMark_TimerTask);
                                if (svv->seqMark_CBTimerProc == 0) {
                                    error = -620;
                                } else {
                                    svv->ChannelGlobals = (synthVarsPtr)NewPtrClear(200836);
                                    if (svv->ChannelGlobals == 0) {
                                        error = -620;
                                    } else {
                                        svv->ChannelGlobals->ret = 825373492;
                                        svv->ChannelGlobals->shellV = (Ptr)svv;
                                        svv->ChannelGlobals->Freq_Tbl = g_Freq_Tbl;
                                        svv->ChannelGlobals->Note_Tbl_Def = g_Note_Tbl_Def;
                                        svv->ChannelGlobals->NoteDecayTbl = g_NoteDecayTbl;
                                        svv->ChannelGlobals->Time_Tbl = g_Time_Tbl;
                                        svv->ChannelGlobals->VG_Scale = g_VG_Scale;
                                        svv->ChannelGlobals->VG_Add = g_VG_Add;
                                        svv->ChannelGlobals->OscModeTbl = g_OscModeTbl;
                                        svv->ChannelGlobals->InitialOscState = g_InitialOscState;
                                        svv->ChannelGlobals->Oct_Tbl = g_Oct_Tbl;
                                        svv->ChannelGlobals->SineWavePtr = g_SineWavePtr;
                                        svv->ChannelGlobals->velToLinPtr = g_velToLinPtr;
                                        svv->ChannelGlobals->GM_Map = g_GM_Map;
                                        svv->ChannelGlobals->GM_DrumMap = g_GM_DrumMap;
                                        svv->ChannelGlobals->MidiLengths = g_MidiLengths;
                                        svv->ChannelGlobals->metaNameStr = g_metaNameStr;
                                        svv->ChannelGlobals->trackNameStr = g_trackNameStr;
                                        svv->ChannelGlobals->phonFlags2 = g_phonFlags2;
                                        svv->ChannelGlobals->maxDurTbl = g_maxDurTbl;
                                        svv->ChannelGlobals->minDurTbl = g_minDurTbl;
                                        svv->ChannelGlobals->Opcode_To_ASCII = g_Opcode_To_ASCII;
                                        svv->ChannelGlobals->phonTypeTbl = g_phonTypeTbl;
                                        svv->ChannelGlobals->hash = g_hash;
                                        svv->ChannelGlobals->rule = g_rule;
                                        svv->ChannelGlobals->kind = g_kind;
                                        svv->ChannelGlobals->dashruletab = g_dashruletab;
                                        svv->ChannelGlobals->atruletab = g_atruletab;
                                        svv->ChannelGlobals->lruletab = g_lruletab;
                                        svv->ChannelGlobals->mruletab = g_mruletab;
                                        svv->ChannelGlobals->zruletab = g_zruletab;
                                        svv->ChannelGlobals->percentruletab = g_percentruletab;
                                        svv->ChannelGlobals->bruletab = g_bruletab;
                                        svv->ChannelGlobals->SuffixTab = g_SuffixTab;
                                        svv->ChannelGlobals->SuffixType = g_SuffixType;
                                        svv->ChannelGlobals->CosTbl = g_CosTbl;
                                        svv->ChannelGlobals->BcoeffTbl = g_BcoeffTbl;
                                        svv->ChannelGlobals->CcoeffTbl = g_CcoeffTbl;
                                        svv->ChannelGlobals->SpeechTbls = g_SpeechTbls;
                                        svv->ChannelGlobals->m_psLeft = NULL;
                                        svv->ChannelGlobals->m_psRight = NULL;
                                        svv->ChannelGlobals->m_psDryLeft = NULL;
                                        svv->ChannelGlobals->m_psDryRight = NULL;
                                        for (i = 0; i <= 3; i++) {
                                            svv->ChannelGlobals->m_LEFT_Mods[i].m_psDelayBuffer = NULL;
                                            svv->ChannelGlobals->m_LEFT_Mods[i].m_dwDelayBufferSize = 0;
                                            svv->ChannelGlobals->m_RIGHT_Mods[i].m_psDelayBuffer = NULL;
                                            svv->ChannelGlobals->m_RIGHT_Mods[i].m_dwDelayBufferSize = 0;
                                        }
                                        if (polyphony == 0) {
                                            svv->ChannelGlobals->polyphony = 24;
                                        } else if (polyphony > 48) {
                                            svv->ChannelGlobals->polyphony = 48;
                                        } else if (polyphony <= 7) {
                                            svv->ChannelGlobals->polyphony = 8;
                                        } else {
                                            svv->ChannelGlobals->polyphony = polyphony;
                                        }
                                        for (i = 0; i <= 19; i++) {
                                            svv->ChannelGlobals->speechVars[i] = NULL;
                                            svv->ChannelGlobals->speechChanToTrack[i] = -1;
                                        }
                                        for (i = 0; i <= 31; i++) {
                                            svv->ChannelGlobals->SpeechMap[i] = -1;
                                        }
                                        svv->ChannelGlobals->NumOfSpeechChans = 0;
                                        CalibrateSndBuffer(svv, (int16_t)svv->latencyMS);
                                        error = InitSound16(svv);
                                        if (error == 0) {
                                            svv->ChannelGlobals->bit16_Sound = 1;
                                            svv->ChannelGlobals->sampleBuffer1 = (int16_t *)svv->soundBufPtr;
                                            svv->ChannelGlobals->sampleBuffer2 = (int16_t *)&svv->soundBufPtr[17600];
                                            svv->MIDI_Buf1 = (Ptr)NewPtrClear(4096);
                                            svv->MIDI_Buf2 = (Ptr)NewPtrClear(4096);
                                            svv->MIDI_Index1 = 0;
                                            svv->MIDI_Index2 = 0;
                                            svv->MIDI_BufSel = 0;
                                            svv->MIDI_Out_Buffer = svv->MIDI_Buf1;
                                            svv->MIDI_Out_Index = svv->MIDI_Index1;
                                            svv->reverbEnabled = 0;
                                            if (GetReverbMemory(svv) != 0) {
                                                goto L_745ac;
                                            }
                                            goto L_745cc;
                                        }
                                        error = -200;
                                        goto L_74670;
L_745ac:
                                        svv->reverbEnabled = 0;
                                        svv->ChannelGlobals->reverbON = 0;
                                        goto L_74604;
L_745cc:
                                        svv->reverbEnabled = 1;
                                        Synth_SetStereoSynth(svv, 1);
                                        Synth_SetReverb(svv, 1.0f, 0.5f, 0.5f);
L_74604:
                                        GetTheTimers(svv);
                                        error = InitMusicChannel(svv->ChannelGlobals);
                                        if (error == 0) {
                                            g_instanceCount++;
                                            (*svvPtr) = svv;
                                            t_48 = 0;
                                            return (int16_t)t_48;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
L_74670:
        CloseMusicChannel(svv);
    }
    MaybeDisposeSharedGlobals();
    t_48 = error;
    return t_48;
}

/* Macintosh.c:651  (0x746a4) */
int16_t Synth_ShutDown(shellVarPtr svv)
{
    if (svv == 0) {
        return (int16_t)0;
    }
    CloseMusicChannel(svv);
    g_instanceCount--;
    MaybeDisposeSharedGlobals();
    return (int16_t)0;
}

/* Macintosh.c:679  (0x74730) */
static void GetTheTimers(shellVarPtr svv)
{
    int16_t i;

    for (i = 0; i <= 7; i++) {
        svv->timeTasks[i].TimeMgrTask.qLink = NULL;
        svv->timeTasks[i].TimeMgrTask.qType = 0;
        svv->timeTasks[i].TimeMgrTask.tmCount = 0;
        svv->timeTasks[i].inUse = 0;
        svv->timeTasks[i].TimeMgrTask.tmWakeUp = 0;
        svv->timeTasks[i].TimeMgrTask.tmReserved = 0;
        InsTime(&svv->timeTasks[i]);
    }
    svv->nextTask = 0;
    svv->freeTask = &svv->timeTasks[0];
}

/* Macintosh.c:702  (0x748cc) */
static void RemoveTheTimers(shellVarPtr svv)
{
    int16_t i;

    for (i = 0; i <= 7; i++) {
        RmvTime(&svv->timeTasks[i]);
    }
}

/* Macintosh.c:713  (0x7495c) */
static void FreeTheTimers(shellVarPtr svv)
{
    int16_t i;

    for (i = 0; i <= 7; i++) {
        svv->timeTasks[i].inUse = 0;
    }
}

/* Macintosh.c:728  (0x749dc) */
void KillSndChannel(shellVarPtr svv)
{
    OSErr ignore;
    SndCommand sCMD;

    if (svv->SndChan == 0) {
        return;
    }
    sCMD.cmd = 4;
    sCMD.param1 = 0;
    sCMD.param2 = 0;
    SndDoImmediate(svv->SndChan, &sCMD);
    ignore = SndDisposeChannel(svv->SndChan, 1);
    svv->SndChan = NULL;
}

/* Macintosh.c:747  (0x74a84) */
OSErr GetSndChannel(shellVarPtr svv, int32_t initParam, int16_t synth)
{
    svv->SndChan = NULL;
    SndNewChannel(&svv->SndChan, synth, initParam, svv->SoundCBProc);
    return SndNewChannel(&svv->SndChan, synth, initParam, svv->SoundCBProc);
}

/* Macintosh.c:756  (0x74b10) */
int16_t Synth_StartMusic(shellVarPtr svv)
{
    svv->inMusicDT = 0;
    svv->doAnotherBuffer = 0;
    svv->lastSndBuffer = 0;
    svv->firstBuffer = 1;
    svv->doneWithMusic = 0;
    svv->stopMusic = 0;
    svv->soundCB_Count = 0;
    svv->cb_Count = 0;
    svv->timerPending = 0;
    svv->nextMaxL = 0;
    svv->nextMaxR = 0;
    ClearReverbHistory(svv->ChannelGlobals);
    StartOscillators(svv->ChannelGlobals);
    SendBufCmd(svv, 0);
    return (int16_t)0;
}

/* Macintosh.c:778  (0x74c08) */
int16_t Synth_StopMusic(shellVarPtr svv)
{
    AllOff(svv->ChannelGlobals);
    svv->stopMusic = 1;
    do {
    } while (svv->soundCB_Count > 0);
    RemoveTheTimers(svv);
    SendMIDIBuffer(svv);
    SendMIDIBuffer(svv);
    return (int16_t)0;
}

/* Macintosh.c:795  (0x74c98) */
int16_t Synth_SetTempoCB(shellVarPtr svv, TempoUPP userTempoCB, int32_t userTempoRefCon)
{
    svv->tempoRefCon = userTempoRefCon;
    svv->tempoCallBackProc = userTempoCB;
    return (int16_t)0;
}

/* Macintosh.c:803  (0x74cf0) */
int16_t Synth_SetKaraCB(shellVarPtr svv, KaraUPP userKaraCB, int32_t userKaraRefCon)
{
    svv->karaRefCon = userKaraRefCon;
    svv->karaCallBackProc = userKaraCB;
    return (int16_t)0;
}

/* Macintosh.c:812  (0x74d48) */
int16_t Synth_SetBeatCB(shellVarPtr svv, BeatUPP userBeatCB, int32_t userBeatRefCon)
{
    svv->beatRefCon = userBeatRefCon;
    svv->beatCallBackProc = userBeatCB;
    return (int16_t)0;
}

/* Macintosh.c:821  (0x74da0) */
int16_t Synth_SetSeqDoneCB(shellVarPtr svv, SeqDoneUPP userDoneCB, int32_t userDoneRefCon)
{
    svv->seqDoneRefCon = userDoneRefCon;
    svv->seqDoneCallBackProc = userDoneCB;
    return (int16_t)0;
}

/* Macintosh.c:828  (0x74df8) */
int16_t Synth_SetOverloadCB(shellVarPtr svv, OverloadUPP userOverloadCB, int32_t userOverloadRefCon)
{
    svv->OverloadRefCon = userOverloadRefCon;
    svv->OverloadCallBackProc = userOverloadCB;
    return (int16_t)0;
}

/* Macintosh.c:838  (0x74e50) */
int16_t Synth_SetMeterCB(shellVarPtr svv, MeterUPP userMeterCB, int32_t userMeterRefCon)
{
    svv->meterRefCon = userMeterRefCon;
    svv->meterCallBackProc = userMeterCB;
    return (int16_t)0;
}

/* Macintosh.c:848  (0x74ea8) */
int16_t Synth_SeqPlayer(shellVarPtr svv, PlayRecPtr playRec)
{
    int16_t error;

    if ((playRec->theFlags & 3) != 0) {
        if ((svv->ChannelGlobals->seqFlags & 3) != 0) {
            error = -242;
            return error;
        }
        svv->cb_Count = 0;
        svv->timeAccum = 0;
        svv->timerPending = 0;
        FreeTheTimers(svv);
        svv->ChannelGlobals->ticksPerBeat = (float)playRec->ticksPerBeat;
        error = StartSeq(svv->ChannelGlobals, playRec);
        if (error == 0) {
            Microseconds(&svv->timeZero);
            return error;
        }
        if (error != 1003) {
            return error;
        }
        svv->ChannelGlobals->Busy = 0;
        _i_MakeEndCB(svv->ChannelGlobals);
        error = 0;
        return error;
    }
    error = StartSeq(svv->ChannelGlobals, playRec);
    return error;
}

/* Macintosh.c:898  (0x75038) */
int16_t Synth_SetTrackLevel(shellVarPtr svv, int16_t trackNum, int16_t level)
{
    int16_t error;

    if (trackNum <= 32 && level <= 200 && level >= 0) {
        SetTheTrackLevel(svv->ChannelGlobals, trackNum, level);
        error = 0;
        return error;
    }
    error = 1001;
    return error;
}

/* Macintosh.c:915  (0x75100) */
int16_t Synth_GetTrackLevel(shellVarPtr svv, int16_t trackNum, int16_t *level)
{
    int16_t error;
    int32_t tempL;

    if (trackNum <= 31) {
        tempL = svv->ChannelGlobals->TrackLevel[trackNum];
        (*level) = (tempL * 100) >> 8;
        error = 0;
        return error;
    }
    error = 1001;
    return error;
}

/* Macintosh.c:933  (0x751bc) */
int16_t Synth_SetInstrument(shellVarPtr svv, InstDefPtr instData, WaveListDefPtr wlData, int16_t instNum)
{
    int16_t error;

    error = Copy_Inst(svv->ChannelGlobals, instData, wlData, instNum);
    return error;
}

/* Macintosh.c:944  (0x75244) */
int16_t Synth_SetWaveBank(shellVarPtr svv, Ptr waveData, int16_t PCMType, Ptr speechVoices)
{
    int16_t error;

    svv->ChannelGlobals->GMVoicePtr = speechVoices;
    error = SetWaveBank(svv->ChannelGlobals, waveData, PCMType);
    return error;
}

/* Macintosh.c:954  (0x752d8) */
int16_t Synth_PlayNote(shellVarPtr svv, int16_t chan, int16_t note, int16_t velocity)
{
    return (int16_t)0;
}

/* Macintosh.c:962  (0x75328) */
int16_t Synth_StopNote(shellVarPtr svv, int16_t chan, int16_t note)
{
    return (int16_t)0;
}

/* Macintosh.c:970  (0x75370) */
int16_t Synth_MIDIMessage(shellVarPtr svv, int16_t dest, int16_t byteNum, int16_t message, int16_t channel, int16_t data1, int16_t data2)
{
    int16_t sendIt;
    int16_t t_48;

    sendIt = 0;
    switch (message) {
    case 144:
        message = 1;
        sendIt = 1;
        break;
    case 1:
        message = 10;
        sendIt = 1;
        break;
    case 128:
        message = 2;
        sendIt = 1;
        break;
    case 176:
        switch (data1) {
        case 7:
            message = 33;
            sendIt = 1;
            break;
        case 64:
            message = 34;
            sendIt = 1;
            if (data2 <= 63) {
                data2 = 0;
            } else {
                data2 = 0x7f;
            }
            break;
        }
        break;
    case 224:
        message = 32;
        sendIt = 1;
        break;
    case 192:
        message = 3;
        sendIt = 1;
        break;
    case 38:
        sendIt = 1;
        break;
    }
    if (sendIt == 0) {
        return (int16_t)0;
    }
    QueueMIDIMsg(svv->ChannelGlobals, dest, byteNum, message, channel, data1, data2);
    return (int16_t)0;
}

/* Macintosh.c:1034  (0x7559c) */
int16_t Synth_MIDISing(shellVarPtr svv, int16_t trackNum, int16_t *curSpeechData, int16_t message, int16_t note, int16_t vel)
{
    int16_t sendIt;
    int16_t error;
    int16_t t_48;

    sendIt = 0;
    error = 0;
    switch (message) {
    case 144:
        message = 1;
        sendIt = 1;
        break;
    case 128:
        message = 2;
        sendIt = 1;
        break;
    case 5000:
        sendIt = 1;
        break;
    case 5001:
        sendIt = 1;
        break;
    }
    if (sendIt == 0) {
        return error;
    }
    error = SingMIDINote(svv->ChannelGlobals, trackNum, curSpeechData, message, note, vel);
    return error;
}

/* Macintosh.c:1072  (0x75704) */
int16_t Synth_KillAllNotes(shellVarPtr svv)
{
    AllOff(svv->ChannelGlobals);
    return (int16_t)0;
}

/* Macintosh.c:1080  (0x7575c) */
int16_t Synth_GetTuningTable(shellVarPtr svv, Ptr tablePtr)
{
    return (int16_t)0;
}

/* Macintosh.c:1088  (0x75798) */
int16_t Synth_SetTuningTable(shellVarPtr svv, Ptr tablePtr)
{
    return (int16_t)0;
}

/* Macintosh.c:1096  (0x757d4) */
int16_t Synth_SetTempo(shellVarPtr svv, int16_t tempo)
{
    SetTempo(svv->ChannelGlobals, tempo);
    return (int16_t)0;
}

/* Macintosh.c:1104  (0x75840) */
int16_t Synth_SetBeat(shellVarPtr svv, int16_t beat)
{
    return (int16_t)0;
}

/* Macintosh.c:1112  (0x75880) */
int16_t Synth_SetRecordTrack(shellVarPtr svv, int16_t trackNum)
{
    return (int16_t)0;
}

/* Macintosh.c:1120  (0x758c0) */
int16_t Synth_SetPlayTrack(shellVarPtr svv, int16_t trackNum, int16_t trackState)
{
    SetPlayTrack(svv->ChannelGlobals, trackNum, trackState);
    return (int16_t)0;
}

/* Macintosh.c:1128  (0x75940) */
int16_t Synth_SetKaraokeTrack(shellVarPtr svv, int16_t trackNum)
{
    svv->ChannelGlobals->karaokeTrack = trackNum;
    return (int16_t)0;
}

/* Macintosh.c:1137  (0x75990) */
int16_t Synth_SetUpdate(shellVarPtr svv, int16_t latency)
{
    int16_t wasRunning;

    if (svv->stopMusic != 0) {
        wasRunning = 0;
    } else {
        Synth_StopMusic(svv);
        wasRunning = 1;
    }
    if (latency <= 9) {
        latency = 10;
    } else if (latency > 100) {
        latency = 100;
    }
    svv->latencyMS = latency;
    CalibrateSndBuffer(svv, (int16_t)svv->latencyMS);
    if (wasRunning == 0) {
        return (int16_t)0;
    }
    Synth_StartMusic(svv);
    return (int16_t)0;
}

/* Macintosh.c:1177  (0x75a84) */
int16_t Synth_GetFrameTime(shellVarPtr svv, int32_t *result)
{
    (*result) = svv->SoundBufferTime;
    return (int16_t)0;
}

/* Macintosh.c:1187  (0x75ad0) */
int16_t Synth_SetTempoScale(shellVarPtr svv, int32_t scale)
{
    NewTempoScale(svv->ChannelGlobals, scale);
    return (int16_t)0;
}

/* Macintosh.c:1195  (0x75b30) */
int16_t Synth_TrackToChan(shellVarPtr svv, int16_t trackNum, int16_t chanNum)
{
    svv->ChannelGlobals->trackToMIDI[trackNum] = chanNum;
    return (int16_t)0;
}

/* Macintosh.c:1203  (0x75b9c) */
int16_t Synth_ChanToOCB(shellVarPtr svv, int16_t chanNum, int16_t ocbNum)
{
    SetChanOCB(svv->ChannelGlobals, chanNum, ocbNum);
    return (int16_t)0;
}

/* Macintosh.c:1237  (0x75c1c) */
int16_t Synth_MusicStatus(shellVarPtr svv, int16_t *status)
{
    return (int16_t)0;
}

/* Macintosh.c:1246  (0x75c58) */
int16_t Synth_NewSong(shellVarPtr svv, int16_t trackNum, int32_t *trackData)
{
    int16_t error;

    error = NewSong_Speech(svv->ChannelGlobals, trackNum, (int16_t *)trackData);
    return error;
}

/* Macintosh.c:1256  (0x75cdc) */
int16_t Synth_ConvertSMF(shellVarPtr svv, Ptr src, int32_t srcLen, Ptr dest, int32_t *destLen, _i_CvtSMFProg_Ptr infoCB, int32_t refCon)
{
    ConvertSMF(svv->ChannelGlobals, src, srcLen, dest, destLen, (_i_CvtSMFProg_Ptr)infoCB, refCon);
    return ConvertSMF(svv->ChannelGlobals, src, srcLen, dest, destLen, (_i_CvtSMFProg_Ptr)infoCB, refCon);
}

/* Macintosh.c:1262  (0x75d64) */
int16_t Synth_ExpandTracks_1(shellVarPtr svv, Expand_SMF_RecPtr esr)
{
    ExpandTracks_1(esr);
    return ExpandTracks_1(esr);
}

/* Macintosh.c:1268  (0x75db8) */
int16_t Synth_ExpandTracks_2(shellVarPtr svv, Expand_SMF_RecPtr esr)
{
    ExpandTracks_2(esr);
    return ExpandTracks_2(esr);
}

/* Macintosh.c:1275  (0x75e0c) */
int16_t Synth_PerfTime(shellVarPtr svv, int16_t *cpuLoad, int16_t *polyphony)
{
    double tempD;
    synthVarsPtr xx;
    int16_t i;
    int16_t vCount;
    VoiceCtrlBlockPtr curVCB;

    if (svv->frameTime != 0) {
        tempD = (double)svv->backTime / (double)svv->frameTime * 10000.0;
    } else {
        tempD = 0.0;
    }
    (*cpuLoad) = FTOI(tempD);
    xx = svv->ChannelGlobals;
    vCount = 0;
    for (i = 0; i < xx->polyphony; i++) {
        curVCB = &xx->VCB_Array[i];
        if (curVCB->priority > 0) {
            vCount++;
        }
    }
    (*polyphony) = vCount;
    return (int16_t)0;
}

/* Macintosh.c:1315  (0x75f8c) */
int16_t Synth_MIDIRefNum(shellVarPtr svv, OMSOutUPP userOMSOutCB)
{
    svv->OMSOutCallBackProc = userOMSOutCB;
    return (int16_t)0;
}

/* Macintosh.c:1328  (0x75fd4) */
int16_t Synth_ConvertWord(shellVarPtr svv, ConvertTextRecPtr tRec, int16_t dictFile)
{
    int16_t error;

    error = 0;
    error = OrthToPhon(svv->ChannelGlobals, tRec, dictFile);
    return error;
}

/* Macintosh.c:1344  (0x7605c) */
int16_t Synth_MakeSpeechData(shellVarPtr svv, unsigned char *targetVocals, unsigned char *targetTrack, Handle speechData, int32_t *speechDataLen, int32_t rate)
{
    int16_t error;

    error = 0;
    error = MakeSpeechData(svv->ChannelGlobals, targetVocals, targetTrack, speechData, speechDataLen, rate);
    return error;
}

/* Macintosh.c:1359  (0x760f0) */
int16_t Synth_SpeechRecode(shellVarPtr svv, unsigned char *targetVocals, unsigned char *targetTrack, int32_t startTime, int32_t endTime, int32_t flags)
{
    int16_t error;

    error = 0;
    error = AdjustBoundryPhons(svv->ChannelGlobals, targetVocals, targetTrack, startTime, endTime, flags);
    return error;
}

/* Macintosh.c:1373  (0x76184) */
int16_t Synth_MakeTrackSpeech(shellVarPtr svv, int16_t trackNum)
{
    int16_t error;

    error = InitNewSpeechChan(svv->ChannelGlobals, trackNum);
    return error;
}

/* Macintosh.c:1381  (0x761fc) */
int16_t Synth_MakeTrackNotSpeech(shellVarPtr svv, int16_t trackNum)
{
    int16_t error;

    error = DisposeSpeechChan(svv->ChannelGlobals, trackNum);
    return error;
}

/* Macintosh.c:1389  (0x76274) */
void Synth_MakeAllTrackskNotSpeech(shellVarPtr svv)
{
    _i_Dispose_AllSpeech(svv->ChannelGlobals);
}

/* Macintosh.c:1395  (0x762c4) */
int16_t Synth_SetKbdFlags(shellVarPtr svv, int32_t kbdFlags)
{
    int16_t error;

    error = 0;
    SetKbdFlags(svv->ChannelGlobals, kbdFlags);
    return error;
}

/* Macintosh.c:1407  (0x76330) */
int16_t Synth_GetNextBuffer(shellVarPtr svv, Ptr *bufPtr, int32_t *buflen)
{
    FillNextSampBuffer(svv->ChannelGlobals, 0);
    (*bufPtr) = (Ptr)svv->curSampleBuffer;
    (*buflen) = svv->curSampleLen << 1;
    svv->MIDI_Index1 = 0;
    svv->MIDI_Index2 = 0;
    svv->MIDI_Out_Index = 0;
    return (int16_t)0;
}

/* Macintosh.c:1439  (0x763dc) */
void DispatchTimeCB(shellVarPtr svv, int16_t skew)
{
    TMinfoPtr theTask;

    theTask = svv->freeTask;
    theTask->inUse = 1;
    theTask->TimeMgrTask.qLink = NULL;
    theTask->TimeMgrTask.qType = 0;
    theTask->TimeMgrTask.tmCount = 0;
    theTask->TimeMgrTask.tmWakeUp = 0;
    theTask->TimeMgrTask.tmReserved = 0;
    theTask->count = svv->ChannelGlobals->curSndFrame * 4989;
    if (svv->nextTask > 6) {
        svv->nextTask = 0;
        svv->freeTask = &svv->timeTasks[0];
    } else {
        svv->nextTask++;
        svv->freeTask = (TMinfoPtr)((char *)theTask + 36);
    }
    svv->timerPending = 1;
}

/* Macintosh.c:1473  (0x764f0) */
void _i_Last_Snd_Buffer(synthVarsPtr xx)
{
    shellVarPtr svv;

    svv = (shellVarPtr)xx->shellV;
    svv->lastSndBuffer = 1;
}

/* Macintosh.c:1483  (0x76538) */
void _i_Cur_Sample_Buffer(synthVarsPtr xx, int16_t *sampleBuffer, int32_t sampleLen)
{
    shellVarPtr svv;
    uint32_t oldA5;

    svv = (shellVarPtr)xx->shellV;
    svv->curSampleBuffer = sampleBuffer;
    svv->curSampleLen = sampleLen;
    svv->timeAccum += svv->SoundBufferTime;
    svv->curMaxL = svv->nextMaxL;
    svv->curMaxR = svv->nextMaxR;
    svv->nextMaxL = xx->maxSampleL;
    svv->nextMaxR = xx->maxSampleR;
    if (svv->meterCallBackProc == 0) {
        return;
    }
    if (svv->userA5 != 0) {
        oldA5 = SetA5(svv->userA5);
    }
    svv->meterCallBackProc(svv->curMaxL, svv->curMaxR, svv->meterRefCon);
    if (svv->userA5 == 0) {
        return;
    }
    oldA5 = SetA5(oldA5);
}

/* Macintosh.c:1511  (0x76694) */
void _i_First_Sample_Buffer(synthVarsPtr xx, int16_t *sampleBuffer, int32_t sampleLen)
{
    shellVarPtr svv;

    svv = (shellVarPtr)xx->shellV;
    svv->firstSampleBuffer = sampleBuffer;
    svv->firstSampleLen = sampleLen;
    svv->timeAccum += svv->SoundBufferTime;
}

/* Macintosh.c:1524  (0x7670c) */
void _i_Dispose_Speech(synthVarsPtr xx, int16_t speechChan)
{
    int16_t i;
    formantVarPtr zz;

    if (xx->speechVars[speechChan] == 0) {
        return;
    }
    zz = (formantVarPtr)xx->speechVars[speechChan];
    DisposePtr(xx->speechVars[speechChan]);
    xx->speechVars[speechChan] = NULL;
    xx->speechChanToTrack[speechChan] = -1;
    xx->NumOfSpeechChans--;
    for (i = 0; i <= 31; i++) {
        if (xx->SpeechMap[i] == speechChan) {
            xx->SpeechMap[i] = -1;
            return;
        }
    }
}

/* Macintosh.c:1549  (0x76890) */
void _i_Dispose_AllSpeech(synthVarsPtr xx)
{
    int16_t i;

    for (i = 0; i <= 19; i++) {
        _i_Dispose_Speech(xx, i);
    }
    xx->NumOfSpeechChans = 0;
    for (i = 0; i <= 31; i++) {
        xx->SpeechMap[i] = -1;
    }
}

/* Macintosh.c:1568  (0x76960) */
int16_t _i_Get_Speech(synthVarsPtr xx, int16_t speechChan)
{
    int16_t error;

    error = 0;
    xx->speechVars[speechChan] = (void *)NewPtrClear(4396);
    if (xx->speechVars[speechChan] != 0) {
        return error;
    }
    error = 1000;
    return error;
}

/* Macintosh.c:1590  (0x76a10) */
void _i_EngineError(synthVarsPtr xx, int16_t errorCode, uint32_t where)
{
    uint32_t oldA5;
    shellVarPtr svv;

    svv = (shellVarPtr)xx->shellV;
    if (svv->seqErrorCallBackProc == 0) {
        return;
    }
    if (svv->userA5 != 0) {
        oldA5 = SetA5(svv->userA5);
    }
    svv->seqErrorCallBackProc(svv->userRefCon, errorCode, where);
    if (svv->userA5 == 0) {
        return;
    }
    oldA5 = SetA5(oldA5);
}

/* Macintosh.c:1611  (0x76af8) */
void _i_MIDI_Buffer(synthVarsPtr xx, int16_t byteNum, int16_t status, int16_t data1, int16_t data2)
{
    shellVarPtr svv;

    svv = (shellVarPtr)xx->shellV;
    if (svv->OMSOutCallBackProc == 0) {
        return;
    }
    if (svv->MIDI_Out_Index + 4 > 4095) {
        return;
    }
    svv->MIDI_Out_Buffer[svv->MIDI_Out_Index] = byteNum;
    svv->MIDI_Out_Index++;
    svv->MIDI_Out_Buffer[svv->MIDI_Out_Index] = status;
    svv->MIDI_Out_Index++;
    svv->MIDI_Out_Buffer[svv->MIDI_Out_Index] = data1;
    svv->MIDI_Out_Index++;
    svv->MIDI_Out_Buffer[svv->MIDI_Out_Index] = data2;
    svv->MIDI_Out_Index++;
}

/* Macintosh.c:1630  (0x76c38) */
static void Tempo_TimerTask(TMTaskPtr tmTaskPtr)
{
    uint32_t oldA5;
    shellVarPtr svv;
    TMinfoPtr theTask;

    theTask = (TMinfoPtr)tmTaskPtr;
    theTask->inUse = 0;
    svv = (shellVarPtr)theTask->refCon;
    if (svv->tempoCallBackProc == 0) {
        return;
    }
    if (svv->userA5 != 0) {
        oldA5 = SetA5(svv->userA5);
    }
    svv->tempoCallBackProc(theTask->data, svv->tempoRefCon);
    if (svv->userA5 == 0) {
        return;
    }
    oldA5 = SetA5(oldA5);
}

/* Macintosh.c:1656  (0x76d24) */
void _i_At_Tempo_Imm(synthVarsPtr xx, int32_t tempoVal)
{
    shellVarPtr svv;
    uint32_t oldA5;

    svv = (shellVarPtr)xx->shellV;
    if (svv->tempoCallBackProc == 0) {
        return;
    }
    if (svv->userA5 != 0) {
        oldA5 = SetA5(svv->userA5);
    }
    svv->tempoCallBackProc(tempoVal, svv->tempoRefCon);
    if (svv->userA5 == 0) {
        return;
    }
    oldA5 = SetA5(oldA5);
}

/* Macintosh.c:1675  (0x76df4) */
void _i_At_Tempo(synthVarsPtr xx, int32_t tempoVal)
{
    shellVarPtr svv;
    TMinfoPtr theTask;

    svv = (shellVarPtr)xx->shellV;
    if (svv->stopMusic != 0) {
        if (svv->tempoCallBackProc == 0) {
            return;
        }
        svv->tempoCallBackProc(tempoVal, svv->tempoRefCon);
        return;
    }
    if (svv->tempoCallBackProc == 0) {
        return;
    }
    if (svv->freeTask->inUse != 0) {
        return;
    }
    theTask = svv->freeTask;
    theTask->TimeMgrTask.tmAddr = svv->tempo_CBTimerProc;
    theTask->refCon = (Ptr)svv;
    theTask->data = tempoVal;
    DispatchTimeCB(svv, 0);
}

/* Macintosh.c:1704  (0x76ef8) */
static void Kara_TimerTask(TMTaskPtr tmTaskPtr)
{
    uint32_t oldA5;
    shellVarPtr svv;
    TMinfoPtr theTask;

    theTask = (TMinfoPtr)tmTaskPtr;
    theTask->inUse = 0;
    svv = (shellVarPtr)theTask->refCon;
    if (svv->karaCallBackProc == 0) {
        return;
    }
    if (svv->userA5 != 0) {
        oldA5 = SetA5(svv->userA5);
    }
    svv->karaCallBackProc(theTask->data, svv->karaRefCon);
    if (svv->userA5 == 0) {
        return;
    }
    oldA5 = SetA5(oldA5);
}

/* Macintosh.c:1729  (0x76fe4) */
void _i_At_Kara_Imm(synthVarsPtr xx, int32_t index)
{
    shellVarPtr svv;
    uint32_t oldA5;

    svv = (shellVarPtr)xx->shellV;
    if (svv->karaCallBackProc == 0) {
        return;
    }
    if (svv->userA5 != 0) {
        oldA5 = SetA5(svv->userA5);
    }
    svv->karaCallBackProc(index, svv->karaRefCon);
    if (svv->userA5 == 0) {
        return;
    }
    oldA5 = SetA5(oldA5);
}

/* Macintosh.c:1749  (0x770b4) */
void _i_At_Kara(synthVarsPtr xx, int32_t index)
{
    shellVarPtr svv;
    TMinfoPtr theTask;

    svv = (shellVarPtr)xx->shellV;
    if (svv->stopMusic != 0) {
        if (svv->karaCallBackProc == 0) {
            return;
        }
        svv->karaCallBackProc(index, svv->karaRefCon);
        return;
    }
    if (svv->karaCallBackProc == 0) {
        return;
    }
    if (svv->freeTask->inUse != 0) {
        return;
    }
    theTask = svv->freeTask;
    theTask->TimeMgrTask.tmAddr = svv->kara_CBTimerProc;
    theTask->refCon = (Ptr)svv;
    theTask->data = index;
    DispatchTimeCB(svv, 0);
}

/* Macintosh.c:1780  (0x771b8) */
static void Beat_TimerTask(TMTaskPtr tmTaskPtr)
{
    uint32_t oldA5;
    shellVarPtr svv;
    TMinfoPtr theTask;

    theTask = (TMinfoPtr)tmTaskPtr;
    theTask->inUse = 0;
    svv = (shellVarPtr)theTask->refCon;
    if (svv->beatCallBackProc == 0) {
        return;
    }
    if (svv->userA5 != 0) {
        oldA5 = SetA5(svv->userA5);
    }
    svv->beatCallBackProc(theTask->data, svv->beatRefCon);
    if (svv->userA5 == 0) {
        return;
    }
    oldA5 = SetA5(oldA5);
}

/* Macintosh.c:1806  (0x772a4) */
void _i_At_Beat(synthVarsPtr xx, int32_t clockVal)
{
    shellVarPtr svv;
    TMinfoPtr theTask;

    svv = (shellVarPtr)xx->shellV;
    if (svv->stopMusic != 0) {
        if (svv->beatCallBackProc == 0) {
            return;
        }
        svv->beatCallBackProc(clockVal, svv->beatRefCon);
        return;
    }
    if (svv->beatCallBackProc == 0) {
        return;
    }
    if (svv->freeTask->inUse != 0) {
        return;
    }
    theTask = svv->freeTask;
    theTask->TimeMgrTask.tmAddr = svv->beat_CBTimerProc;
    theTask->refCon = (Ptr)svv;
    theTask->data = clockVal;
    DispatchTimeCB(svv, 0);
}

/* Macintosh.c:1837  (0x773a8) */
static void SeqItem_TimerTask(TMTaskPtr tmTaskPtr)
{
    uint32_t oldA5;
    shellVarPtr svv;
    TMinfoPtr theTask;

    theTask = (TMinfoPtr)tmTaskPtr;
    theTask->inUse = 0;
    svv = (shellVarPtr)theTask->refCon;
    if (svv->seqItemCallBackProc == 0) {
        return;
    }
    if (svv->userA5 != 0) {
        oldA5 = SetA5(svv->userA5);
    }
    svv->seqItemCallBackProc(svv->userRefCon, theTask->data);
    if (svv->userA5 == 0) {
        return;
    }
    oldA5 = SetA5(oldA5);
}

/* Macintosh.c:1861  (0x77494) */
void _i_At_SeqItem(synthVarsPtr xx, uint32_t where)
{
    shellVarPtr svv;
    TMinfoPtr theTask;

    svv = (shellVarPtr)xx->shellV;
    if (svv->stopMusic != 0) {
        if (svv->seqItemCallBackProc == 0) {
            return;
        }
        svv->seqItemCallBackProc(svv->userRefCon, where);
        return;
    }
    if (svv->seqItemCallBackProc == 0) {
        return;
    }
    if (svv->freeTask->inUse != 0) {
        return;
    }
    theTask = svv->freeTask;
    theTask->TimeMgrTask.tmAddr = svv->seqItem_CBTimerProc;
    theTask->refCon = (Ptr)svv;
    theTask->data = where;
    DispatchTimeCB(svv, 0);
}

/* Macintosh.c:1894  (0x77598) */
static void SeqMark_TimerTask(TMTaskPtr tmTaskPtr)
{
    uint32_t oldA5;
    shellVarPtr svv;
    TMinfoPtr theTask;

    theTask = (TMinfoPtr)tmTaskPtr;
    theTask->inUse = 0;
    svv = (shellVarPtr)theTask->refCon;
    if (svv->seqMarkCallBackProc == 0) {
        return;
    }
    if (svv->userA5 != 0) {
        oldA5 = SetA5(svv->userA5);
    }
    svv->seqMarkCallBackProc(svv->userRefCon, theTask->data);
    if (svv->userA5 == 0) {
        return;
    }
    oldA5 = SetA5(oldA5);
}

/* Macintosh.c:1918  (0x77684) */
void _i_At_Phoneme(synthVarsPtr xx, uint32_t where)
{
    shellVarPtr svv;
    TMinfoPtr theTask;

    svv = (shellVarPtr)xx->shellV;
    if (svv->stopMusic != 0) {
        if (svv->seqMarkCallBackProc == 0) {
            return;
        }
        svv->seqMarkCallBackProc(svv->userRefCon, where);
        return;
    }
    if (svv->seqMarkCallBackProc == 0) {
        return;
    }
    if (svv->freeTask->inUse != 0) {
        return;
    }
    theTask = svv->freeTask;
    theTask->TimeMgrTask.tmAddr = svv->seqMark_CBTimerProc;
    theTask->refCon = (Ptr)svv;
    theTask->data = where;
    DispatchTimeCB(svv, 0);
}

/* Macintosh.c:1971  (0x77788) */
void MakeOverloadCB(shellVarPtr svv)
{
    uint32_t oldA5;

    if (svv->OverloadCallBackProc == 0) {
        return;
    }
    if (svv->userA5 != 0) {
        oldA5 = SetA5(svv->userA5);
    }
    svv->OverloadCallBackProc(svv->OverloadRefCon);
    if (svv->userA5 == 0) {
        return;
    }
    oldA5 = SetA5(oldA5);
}

/* Macintosh.c:1991  (0x77844) */
void _i_MakeEndCB(synthVarsPtr xx)
{
    shellVarPtr svv;
    uint32_t oldA5;

    svv = (shellVarPtr)xx->shellV;
    if (svv->seqDoneCallBackProc == 0) {
        return;
    }
    if (svv->userA5 != 0) {
        oldA5 = SetA5(svv->userA5);
    }
    svv->seqDoneCallBackProc(svv->seqDoneRefCon);
    if (svv->userA5 == 0) {
        return;
    }
    oldA5 = SetA5(oldA5);
}

/* Macintosh.c:2015  (0x7790c) */
static void MaybeDisposeSharedGlobals(void)
{
    if (g_instanceCount != 0) {
        return;
    }
    if (g_DataHandle != 0) {
        DisposeHandle(g_DataHandle);
    }
    if (g_Freq_Tbl == 0) {
        return;
    }
    DisposePtr(g_Freq_Tbl);
}

/* Macintosh.c:2037  (0x779b8) */
static void CloseMusicChannel(shellVarPtr svv)
{
    int16_t i;

    if (svv->stopMusic == 0) {
        Synth_StopMusic(svv);
    }
    KillSndChannel(svv);
    if (svv->nullBufPtr != 0) {
        DisposePtr(svv->nullBufPtr);
    }
    if (svv->soundBufPtr != 0) {
        DisposePtr(svv->soundBufPtr);
    }
    if (svv->MIDI_Buf1 != 0) {
        DisposePtr(svv->MIDI_Buf1);
    }
    if (svv->MIDI_Buf2 != 0) {
        DisposePtr(svv->MIDI_Buf2);
    }
    DeleteReverbModules(svv);
    if (svv->freeTask != 0) {
        for (i = 0; i <= 7; i++) {
            RmvTime(&svv->timeTasks[i]);
        }
    }
    _i_Dispose_AllSpeech(svv->ChannelGlobals);
    if (svv->ChannelGlobals != 0) {
        DisposePtr(svv->ChannelGlobals);
    }
    DisposePtr(svv);
}

/* Macintosh.c:2105  (0x77b40) */
static void SendBufCmd(shellVarPtr svv, int16_t nullBuf)
{
    int32_t now;
    int32_t negTime;
    TMinfoPtr theTask;
    UnsignedWide micro64;
    int16_t i;

    svv->soundCB_Count++;
    if (svv->timerPending == 1) {
        for (i = 0; i <= 7; i++) {
            theTask = &svv->timeTasks[i];
            if (theTask->inUse == 1) {
                PrimeTime(theTask, -theTask->count);
            }
        }
        svv->timerPending = 0;
    }
    svv->SCmd.cmd = 81;
    svv->SCmd.param1 = 0;
    svv->SCmd.param2 = (int32_t)(intptr_t)&svv->SH;
    if (svv->firstBuffer != 0) {
        svv->SH.samplePtr = (Ptr)svv->firstSampleBuffer;
        svv->SH.numFrames = svv->firstSampleLen;
        svv->firstBuffer = 0;
    } else if (nullBuf != 0) {
        svv->SH.samplePtr = svv->nullBufPtr;
        svv->SH.numFrames = svv->SoundBufferLen;
    } else {
        svv->SH.samplePtr = (Ptr)svv->curSampleBuffer;
        svv->SH.numFrames = svv->curSampleLen >> 1;
    }
    svv->SH.loopEnd = 0;
    SndDoImmediate(svv->SndChan, &svv->SCmd);
    svv->SCmd.cmd = 13;
    svv->SCmd.param1 = 0;
    svv->SCmd.param2 = (int32_t)(intptr_t)svv;
    SndDoCommand(svv->SndChan, &svv->SCmd, 1);
}

/* Macintosh.c:2180  (0x77d70) */
static void SendMIDIBuffer(shellVarPtr svv)
{
    if (svv->OMSOutCallBackProc == 0) {
        return;
    }
    if (svv->MIDI_BufSel != 0) {
        if (svv->MIDI_Index1 > 0) {
            svv->OMSOutCallBackProc(svv->MIDI_Buf1, svv->MIDI_Index1);
            svv->MIDI_Index1 = 0;
        }
        svv->MIDI_BufSel = 0;
        svv->MIDI_Out_Buffer = svv->MIDI_Buf1;
        svv->MIDI_Index2 = svv->MIDI_Out_Index;
        svv->MIDI_Out_Index = svv->MIDI_Index1;
        return;
    }
    if (svv->MIDI_Index2 > 0) {
        svv->OMSOutCallBackProc(svv->MIDI_Buf2, svv->MIDI_Index2);
        svv->MIDI_Index2 = 0;
    }
    svv->MIDI_BufSel = 1;
    svv->MIDI_Out_Buffer = svv->MIDI_Buf2;
    svv->MIDI_Index1 = svv->MIDI_Out_Index;
    svv->MIDI_Out_Index = svv->MIDI_Index2;
}

/* Macintosh.c:2220  (0x77ee4) */
void Synth_MusicDT(int32_t dtParam)
{
    shellVarPtr svv;
    UnsignedWide uwTemp;

    svv = (shellVarPtr)dtParam;
    svv->inMusicDT = 1;
    Microseconds(&uwTemp);
    svv->backTime1 = uwTemp.lo;
    svv->frameTime2 = svv->backTime1;
    if (svv->frameTime1 != 0 && svv->frameTime2 > svv->frameTime1) {
        svv->frameTime = svv->frameTime2 - svv->frameTime1;
    }
    svv->frameTime1 = svv->frameTime2;
    if (svv->lastSndBuffer == 0) {
        FillNextSampBuffer(svv->ChannelGlobals, 0);
    } else if (svv->doneWithMusic == 0) {
        svv->doneWithMusic = 1;
    } else {
        svv->ChannelGlobals->Busy = 0;
    }
    Microseconds(&uwTemp);
    svv->backTime2 = uwTemp.lo;
    if (svv->backTime2 > svv->backTime1) {
        svv->backTime = svv->backTime2 - svv->backTime1;
    }
    svv->inMusicDT = 0;
}

/* Macintosh.c:2289  (0x7806c) */
static void SM_CallBack(SndChannelPtr chan, SndCommand *cmd)
{
    shellVarPtr svv;

    svv = (shellVarPtr)cmd->param2;
    svv->soundCB_Count--;
    if (svv->stopMusic != 0) {
        return;
    }
    if (svv->inMusicDT != 0) {
        if (svv->doneWithMusic == 0) {
            SendBufCmd(svv, 1);
        }
        MakeOverloadCB(svv);
    } else {
        if (svv->doneWithMusic == 0) {
            SendBufCmd(svv, 0);
        }
        svv->inMusicDT = 1;
        svv->DT.qLink = NULL;
        svv->DT.qType = 7;
        svv->DT.dtFlags = 0;
        svv->DT.dtAddr = svv->MusicDTProc;
        svv->DT.dtParam = (int32_t)(intptr_t)svv;
        svv->DT.dtReserved = 0;
        DTInstall(&svv->DT);
    }
    SendMIDIBuffer(svv);
}

/* Macintosh.c:2371  (0x7826c) */
static int16_t InitSound16(shellVarPtr svv)
{
    int16_t error;
    int32_t result;
    int32_t i;
    int16_t t_48;

    error = 0;
    if (Gestalt(1936614432, &result) == 0) {
        if ((((uint32_t)result >> 7) & 1) != 0) {
            svv->nullBufPtr = (Ptr)NewPtrClear(17600);
            if (svv->nullBufPtr == 0) {
                error = -620;
                goto L_78414;
            }
            svv->soundBufPtr = (Ptr)NewPtrClear(35200);
            if (svv->soundBufPtr == 0) {
                error = -620;
                goto L_78404;
            }
            svv->SndChan = NULL;
            if (GetSndChannel(svv, 196, 5) != 0) {
                error = -200;
                goto L_783f4;
            }
            svv->SH.numChannels = 2;
            svv->SH.sampleRate = -0x53bc0000;
            svv->SH.loopStart = 0;
            svv->SH.encode = -1;
            svv->SH.baseFrequency = 60;
            svv->SH.markerChunk = NULL;
            svv->SH.instrumentChunks = NULL;
            svv->SH.AESRecording = NULL;
            svv->SH.sampleSize = 16;
        } else {
            error = -200;
        }
    }
    t_48 = error;
    return (int16_t)t_48;
L_783f4:
    DisposePtr(svv->soundBufPtr);
L_78404:
    DisposePtr(svv->nullBufPtr);
L_78414:
    t_48 = error;
    return t_48;
}

/* Macintosh.c:2888  (0x796fc) */
int16_t Synth_SetStereoSynth(shellVarPtr svv, int16_t state)
{
    svv->ChannelGlobals->stereo_Synth_ON = state;
    return (int16_t)0;
}
