/* ConvertSMF.c -- importing a Standard MIDI File as VocalWriter tracks.
 *
 * Lifted from the original's machine code; see src/speech.c.
 */
#include "vw_engine.h"



/* ConvertSMF.c:65  (0x89c4c) */
int32_t GetScaledTime(synthVarsPtr xx, int32_t deltaTime)
{
    mFloat timeF;
    int32_t correction;

    if (deltaTime <= 0) {
        return deltaTime;
    }
    timeF = (float)deltaTime;
    xx->accumTime += timeF;
    timeF *= xx->timeScale;
    deltaTime = FTOI(timeF);
    xx->accumTime_I += deltaTime;
    correction = FTOI(xx->accumTime * xx->timeScale);
    correction -= xx->accumTime_I;
    deltaTime += correction;
    xx->accumTime_I += correction;
    return deltaTime;
}

/* ConvertSMF.c:95  (0x89d88) */
void CopyPStrSFM(unsigned char *src, unsigned char *dest)
{
    int16_t srcLen;

    for (srcLen = *src; srcLen >= 0; srcLen--) {
        dest[srcLen] = src[srcLen];
    }
}

/* ConvertSMF.c:105  (0x89e20) */
void ConcatPStrSFM(unsigned char *st1, unsigned char *st2)
{
    unsigned char *ptr1;
    unsigned char *ptr2;
    int16_t i;

    ptr1 = &st1[*st1 + 1];
    ptr2 = &st2[1];
    for (i = 0; i < *st2; i++) {
        ptr1[i] = ptr2[i];
    }
    (*st1) += *st2;
}

/* ConvertSMF.c:121  (0x89f10) */
void GenByte(unsigned char *dest, int32_t *index, int16_t data, int32_t limit)
{
    if (*index < limit) {
        dest[*index] = data;
    }
    (*index)++;
}

/* ConvertSMF.c:179  (0x89f98) */
void GenLong(unsigned char *dest, int32_t *index, int32_t data, int32_t limit)
{
    GenByte(dest, index, (int16_t)(uint8_t)(data >> 24), limit);
    GenByte(dest, index, (int16_t)(uint8_t)(data >> 16), limit);
    GenByte(dest, index, (int16_t)(uint8_t)(data >> 8), limit);
    GenByte(dest, index, (int16_t)(uint8_t)data, limit);
}

/* ConvertSMF.c:189  (0x8a080) */
void GenWord(unsigned char *dest, int32_t *index, int16_t data, int32_t limit)
{
    GenByte(dest, index, (int16_t)(uint8_t)(data >> 8), limit);
    GenByte(dest, index, data, limit);
}

/* ConvertSMF.c:196  (0x8a11c) */
void Gen24(unsigned char *dest, int32_t *index, int32_t data, int32_t limit)
{
    GenByte(dest, index, (int16_t)(uint8_t)(data >> 16), limit);
    GenByte(dest, index, (int16_t)(uint8_t)(data >> 8), limit);
    GenByte(dest, index, (int16_t)(uint8_t)data, limit);
}

/* ConvertSMF.c:204  (0x8a1dc) */
int32_t PrintTime(synthVarsPtr xx, int32_t deltaTime)
{
    int32_t absTime;

    deltaTime = GetScaledTime(xx, deltaTime);
    absTime = xx->accumTime_I;
    if (absTime > 0xffffff) {
        absTime = 0xffffff;
    }
    Gen24(xx->outSeq, &xx->outSeqIndex, absTime, xx->destLen);
    return 0;
}

/* ConvertSMF.c:225  (0x8a294) */
void GenString(unsigned char *dest, int32_t *index, char *src, int16_t fieldSize, int32_t limit)
{
    int16_t nameLen;
    int16_t k;

    nameLen = (int8_t)*src;
    if (nameLen > fieldSize - 1) {
        nameLen = fieldSize - 1;
    }
    GenByte(dest, index, nameLen, limit);
    for (k = 1; k < fieldSize; k++) {
        if (k <= nameLen) {
            GenByte(dest, index, (int16_t)(int8_t)src[k], limit);
        } else {
            GenByte(dest, index, 0, limit);
        }
    }
}

/* ConvertSMF.c:249  (0x8a3cc) */
int16_t egetc(synthVarsPtr xx, int32_t *seqIndexPtr, int32_t *toBeReadPtr)
{
    int16_t c;

    c = xx->inSeq[*seqIndexPtr];
    (*seqIndexPtr)++;
    (*toBeReadPtr)--;
    return c;
}

/* ConvertSMF.c:261  (0x8a458) */
int32_t To_32_Bit(int16_t c1, int16_t c2, int16_t c3, int16_t c4)
{
    int32_t value;

    value = 0;
    value = 0;
    value = (uint8_t)c1;
    value = (value << 8) + (uint8_t)c2;
    value = (value << 8) + (uint8_t)c3;
    value = (value << 8) + (uint8_t)c4;
    return value;
}

/* ConvertSMF.c:273  (0x8a520) */
int16_t To_16_Bit(int16_t c1, int16_t c2)
{
    return (int16_t)((uint8_t)c1 << 8) + (uint8_t)c2;
}

/* ConvertSMF.c:281  (0x8a588) */
int32_t Read_32_Bit(synthVarsPtr xx, int32_t *seqIndexPtr, int32_t *toBeReadPtr)
{
    int16_t c1;
    int16_t c2;
    int16_t c3;
    int16_t c4;

    c1 = egetc(xx, seqIndexPtr, toBeReadPtr);
    c2 = egetc(xx, seqIndexPtr, toBeReadPtr);
    c3 = egetc(xx, seqIndexPtr, toBeReadPtr);
    c4 = egetc(xx, seqIndexPtr, toBeReadPtr);
    To_32_Bit(c1, c2, c3, c4);
    return To_32_Bit(c1, c2, c3, c4);
}

/* ConvertSMF.c:292  (0x8a66c) */
int16_t Read_16_Bit(synthVarsPtr xx, int32_t *seqIndexPtr, int32_t *toBeReadPtr)
{
    int16_t c1;
    int16_t c2;

    c1 = egetc(xx, seqIndexPtr, toBeReadPtr);
    c2 = egetc(xx, seqIndexPtr, toBeReadPtr);
    To_16_Bit(c1, c2);
    return To_16_Bit(c1, c2);
}

/* ConvertSMF.c:306  (0x8a708) */
int32_t ReadVariNum(synthVarsPtr xx, int32_t *seqIndexPtr, int32_t *toBeReadPtr)
{
    int32_t value;
    int16_t c;

    c = egetc(xx, seqIndexPtr, toBeReadPtr);
    value = c;
    if ((((uint32_t)(uint16_t)c >> 7) & 1) == 0) {
        return value;
    }
    value &= 0x7f;
    do {
        c = egetc(xx, seqIndexPtr, toBeReadPtr);
        value = (value << 7) + (c & 0x7f);
    } while ((((uint32_t)(uint16_t)c >> 7) & 1) != 0);
    return value;
}

/* ConvertSMF.c:331  (0x8a7ec) */
int16_t ReadMT(synthVarsPtr xx, char *mType, int32_t *seqIndexPtr, int32_t srcLen)
{
    int16_t nRead;
    char b[4];
    int16_t c;
    int16_t error;

    error = 0;
    nRead = 0;
    for (;;) {
        while (nRead <= 3) {
            c = xx->inSeq[*seqIndexPtr];
            (*seqIndexPtr)++;
            if (*seqIndexPtr > srcLen) {
                error = -1;
                return error;
            }
            b[nRead] = c;
            nRead++;
        }
        if ((int8_t)*mType == (int8_t)b[0] && (int8_t)mType[1] == (int8_t)b[1] && (int8_t)mType[2] == (int8_t)b[2] && (int8_t)mType[3] == (int8_t)b[3]) {
            error = 0;
            return error;
        }
        b[0] = b[1];
        b[1] = b[2];
        b[2] = b[3];
        nRead = 3;
        continue;
    }
    return error;
}

/* ConvertSMF.c:385  (0x8a98c) */
void ChanMessageParse(synthVarsPtr xx, int16_t status, int16_t c1, int16_t c2)
{
    int16_t i;
    int16_t cmd;

    cmd = status & 240;
    if (xx->trackStatus[xx->trackNum] == 0) {
        xx->trackStatus[xx->trackNum] = 1;
        xx->trackType[xx->trackNum] = 0;
        xx->usedTracks++;
    }
    if (cmd == 192) {
        xx->trackPgmChange[xx->trackNum] = c1 + 1;
        xx->usedPgms[c1] = 1;
    }
    if (cmd == 144) {
        xx->trackFlags[xx->trackNum] |= 4;
        i = status & 15;
        xx->noteChanBits |= 1 << i;
    }
    i = (status & 15) + 1;
    xx->usedChans[xx->trackNum] |= 1 << i;
}

/* ConvertSMF.c:415  (0x8abcc) */
int32_t ScaleTime(synthVarsPtr xx, int32_t time)
{
    mFloat timeF;

    timeF = (float)time;
    timeF *= xx->timeScale;
    time = FTOI(timeF);
    return time;
}

/* ConvertSMF.c:431  (0x8ac6c) */
int32_t ChanMessage(synthVarsPtr xx, int16_t status, int16_t c1, int16_t c2, int32_t len, int32_t deltaTime)
{
    int16_t chan;
    int32_t temp;
    int32_t t_68;

    chan = status & 15;
    if (xx->chanFilter >= 0) {
        if (chan != xx->chanFilter - 1) {
            return deltaTime;
        }
    }
    if (((1 << chan) & xx->noteChanBits) == 0) {
        return deltaTime;
    }
    t_68 = status & 240;
    if (t_68 != 176) {
        if (t_68 <= 176) {
            if (t_68 == 144) {
                goto L_8ad94;
            }
            if (t_68 == 160) {
                goto L_8b084;
            }
            return deltaTime;
        }
        if (t_68 != 208) {
            if (t_68 == 224) {
                goto L_8af40;
            }
            if (t_68 == 192) {
                goto L_8b094;
            }
            return deltaTime;
        }
        goto L_8b1d0;
L_8ad94:
        deltaTime = PrintTime(xx, deltaTime);
        GenByte(xx->outSeq, &xx->outSeqIndex, chan, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 1, xx->destLen);
        while (c1 > 96) {
            c1 -= 12;
        }
        while (c1 <= 23) {
            c1 += 12;
        }
        GenByte(xx->outSeq, &xx->outSeqIndex, c1, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, c2, xx->destLen);
        len = ScaleTime(xx, len);
        if (len <= 9) {
            len = 10;
        }
        Gen24(xx->outSeq, &xx->outSeqIndex, len, xx->destLen);
        GenWord(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        xx->regParam = -1;
        return deltaTime;
L_8af40:
        deltaTime = PrintTime(xx, deltaTime);
        GenByte(xx->outSeq, &xx->outSeqIndex, chan, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 32, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, c1, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, c2, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        xx->regParam = -1;
        return deltaTime;
L_8b084:
        xx->regParam = -1;
        return deltaTime;
L_8b094:
        deltaTime = PrintTime(xx, deltaTime);
        GenByte(xx->outSeq, &xx->outSeqIndex, chan, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 3, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, c1, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        xx->regParam = -1;
        return deltaTime;
L_8b1d0:
        xx->regParam = -1;
        return deltaTime;
    }
    switch (c1) {
    case 7:
        xx->lastMIDI_Vol = (float)c2;
        xx->lastMIDI_Vol /= 127.0f;
        temp = FTOI(xx->lastMIDI_Vol * xx->lastMIDI_Express * 127.0);
        deltaTime = PrintTime(xx, deltaTime);
        GenByte(xx->outSeq, &xx->outSeqIndex, chan, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 33, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, (int16_t)temp, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        xx->regParam = -1;
        return deltaTime;
    case 11:
        xx->lastMIDI_Express = (float)c2;
        xx->lastMIDI_Express /= 127.0f;
        temp = FTOI(xx->lastMIDI_Vol * xx->lastMIDI_Express * 127.0);
        deltaTime = PrintTime(xx, deltaTime);
        GenByte(xx->outSeq, &xx->outSeqIndex, chan, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 33, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, (int16_t)temp, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        xx->regParam = -1;
        return deltaTime;
    case 64:
        deltaTime = PrintTime(xx, deltaTime);
        GenByte(xx->outSeq, &xx->outSeqIndex, chan, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 34, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        if (c2 <= 63) {
            c2 = 0;
        } else {
            c2 = 0x7f;
        }
        GenByte(xx->outSeq, &xx->outSeqIndex, c2, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        xx->regParam = -1;
        return deltaTime;
    case 6:
        if (xx->regParam != 0) {
            return deltaTime;
        }
        deltaTime = PrintTime(xx, deltaTime);
        GenByte(xx->outSeq, &xx->outSeqIndex, chan, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 38, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, c2, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        return deltaTime;
    case 100:
        if (c2 == 0) {
            xx->regParam = c2;
            return deltaTime;
        }
        xx->regParam = -1;
        return deltaTime;
    case 5:
        deltaTime = PrintTime(xx, deltaTime);
        GenByte(xx->outSeq, &xx->outSeqIndex, chan, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 36, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, c2, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        xx->regParam = -1;
        return deltaTime;
    case 84:
        deltaTime = PrintTime(xx, deltaTime);
        GenByte(xx->outSeq, &xx->outSeqIndex, chan, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 37, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, c2, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        xx->regParam = -1;
        return deltaTime;
    }
    if (c1 != 121) {
        return deltaTime;
    }
    deltaTime = PrintTime(xx, deltaTime);
    GenByte(xx->outSeq, &xx->outSeqIndex, chan, xx->destLen);
    GenByte(xx->outSeq, &xx->outSeqIndex, 35, xx->destLen);
    GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    xx->regParam = -1;
    return deltaTime;
}

/* ConvertSMF.c:613  (0x8bc80) */
int32_t MetaEvent(synthVarsPtr xx, int16_t type, int32_t deltaTime, int32_t metaMsg)
{
    int32_t newTempo;
    mFloat temp_F;
    int16_t denom;
    int16_t nn;
    int16_t dd;
    int16_t cc;
    int16_t bb;
    int32_t i;
    int32_t textLen;
    int32_t chanNum;
    int16_t t_68;

    if (xx->chanFilter >= 0) {
        if (xx->chanFilter != 0) {
            return deltaTime;
        }
    }
    t_68 = type;
    if (t_68 != 81) {
        if (t_68 <= 81) {
            if (t_68 == 1) {
                goto L_8bd3c;
            }
            return deltaTime;
        }
        if (t_68 == 88) {
            goto L_8bf84;
        }
        if (t_68 == 89) {
            goto L_8c11c;
        }
        return deltaTime;
L_8bd3c:
        if (xx->lyricsAllowed != 0) {
            return deltaTime;
        }
        if ((int8_t)xx->MsgBuff[0] != 36) {
            return deltaTime;
        }
        if ((int8_t)xx->MsgBuff[1] != 69) {
            return deltaTime;
        }
        if ((int8_t)xx->MsgBuff[2] != 78) {
            return deltaTime;
        }
        if ((int8_t)xx->MsgBuff[3] != 71) {
            return deltaTime;
        }
        if ((int8_t)xx->MsgBuff[4] != 76) {
            return deltaTime;
        }
        xx->lyricsAllowed = 1;
        return deltaTime;
    }
    if (xx->noMoreTempo != 0) {
        if (xx->gotTempo != 0) {
            return deltaTime;
        }
    }
    newTempo = To_32_Bit(0, (int16_t)(int8_t)xx->MsgBuff[0], (int16_t)(int8_t)xx->MsgBuff[1], (int16_t)(int8_t)xx->MsgBuff[2]);
    newTempo = 0x3938700 / newTempo;
    temp_F = (float)newTempo;
    temp_F *= xx->tempoScale;
    newTempo = FTOI(temp_F);
    deltaTime = PrintTime(xx, deltaTime);
    GenWord(xx->outSeq, &xx->outSeqIndex, 4, xx->destLen);
    GenWord(xx->outSeq, &xx->outSeqIndex, (int16_t)newTempo, xx->destLen);
    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    xx->trackhasMeta = 1;
    xx->gotTempo = 1;
    return deltaTime;
L_8bf84:
    if (xx->noMoreSig != 0) {
        return deltaTime;
    }
    denom = 1;
    nn = (int8_t)xx->MsgBuff[0];
    dd = (int8_t)xx->MsgBuff[1];
    cc = (int8_t)xx->MsgBuff[2];
    bb = (int8_t)xx->MsgBuff[3];
    deltaTime = PrintTime(xx, deltaTime);
    GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    GenByte(xx->outSeq, &xx->outSeqIndex, 5, xx->destLen);
    GenByte(xx->outSeq, &xx->outSeqIndex, nn, xx->destLen);
    GenByte(xx->outSeq, &xx->outSeqIndex, dd, xx->destLen);
    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    xx->trackhasMeta = 1;
    return deltaTime;
L_8c11c:
    deltaTime = PrintTime(xx, deltaTime);
    GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    GenByte(xx->outSeq, &xx->outSeqIndex, 7, xx->destLen);
    GenByte(xx->outSeq, &xx->outSeqIndex, (int16_t)(int8_t)xx->MsgBuff[0], xx->destLen);
    GenByte(xx->outSeq, &xx->outSeqIndex, (int16_t)(int8_t)xx->MsgBuff[1], xx->destLen);
    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    xx->trackhasMeta = 1;
    return deltaTime;
}

/* ConvertSMF.c:741  (0x8c278) */
int32_t MetaEventParse(synthVarsPtr xx, int16_t type, int32_t deltaTime, int32_t metaMsg)
{
    int16_t nn;
    int16_t dd;
    int16_t cc;
    int16_t bb;
    int32_t tempTempo;
    int16_t t_48;

    t_48 = type;
    if (t_48 != 3) {
        if (t_48 <= 3) {
            if (t_48 == 1) {
                goto L_8c310;
            }
            if (t_48 == 2) {
                goto L_8c460;
            }
            return deltaTime;
        }
        if (t_48 == 81) {
            goto L_8c498;
        }
        if (t_48 == 88) {
            goto L_8c698;
        }
        return deltaTime;
L_8c310:
        if ((int8_t)xx->MsgBuff[0] == 64 && (int8_t)xx->MsgBuff[1] == 76 && (int8_t)xx->MsgBuff[2] == 69 && (int8_t)xx->MsgBuff[3] == 78 && (int8_t)xx->MsgBuff[4] == 71 && (int8_t)xx->MsgBuff[5] == 76) {
            xx->hasKaraText = 1;
            return deltaTime;
        }
        if (xx->hasKaraText == 0) {
            return deltaTime;
        }
        if ((int8_t)xx->MsgBuff[0] == 64) {
            return deltaTime;
        }
        deltaTime = GetScaledTime(xx, deltaTime);
        if (xx->makeTextCB != 0 && xx->convert_InfoCB != 0) {
            xx->convert_InfoCB(2, (intptr_t)&xx->inSeq[metaMsg], xx->accumTime_I, xx->convert_RefCon);
        }
        xx->karaTextCount++;
        deltaTime = 0;
        return deltaTime;
L_8c460:
        xx->SMFCopyright = metaMsg;
        return deltaTime;
    }
    xx->trackName[xx->trackNum] = metaMsg;
    return deltaTime;
L_8c498:
    if (xx->gotTempo == 0) {
        xx->midi_tempo = To_32_Bit(0, (int16_t)(int8_t)xx->MsgBuff[0], (int16_t)(int8_t)xx->MsgBuff[1], (int16_t)(int8_t)xx->MsgBuff[2]);
        xx->midi_tempo = 0x3938700 / xx->midi_tempo;
        xx->gotTempo = 1;
        xx->maxTempo = xx->midi_tempo;
        xx->minTempo = xx->midi_tempo;
    } else {
        tempTempo = To_32_Bit(0, (int16_t)(int8_t)xx->MsgBuff[0], (int16_t)(int8_t)xx->MsgBuff[1], (int16_t)(int8_t)xx->MsgBuff[2]);
        tempTempo = 0x3938700 / tempTempo;
        if (xx->maxTempo < tempTempo) {
            xx->maxTempo = tempTempo;
        } else if (xx->minTempo > tempTempo) {
            xx->minTempo = tempTempo;
        }
    }
    if (xx->trackStatus[xx->trackNum] != 0) {
        return deltaTime;
    }
    xx->trackStatus[xx->trackNum] = 1;
    xx->trackType[xx->trackNum] = 1;
    xx->usedTracks++;
    return deltaTime;
L_8c698:
    if (xx->gotTimeSig == 0) {
        nn = (int8_t)xx->MsgBuff[0];
        dd = (int8_t)xx->MsgBuff[1];
        cc = (int8_t)xx->MsgBuff[2];
        bb = (int8_t)xx->MsgBuff[3];
        xx->beatsPerMeasure = nn;
        if (xx->beatsPerMeasure <= 1) {
            xx->beatsPerMeasure = 2;
        } else if (xx->beatsPerMeasure > 16) {
            xx->beatsPerMeasure = 16;
        }
        xx->beatTo = dd;
        if (xx->beatTo < 0) {
            xx->beatTo = 0;
        } else if (xx->beatTo > 5) {
            xx->beatTo = 5;
        }
        xx->gotTimeSig = 1;
    }
    if (xx->trackStatus[xx->trackNum] != 0) {
        return deltaTime;
    }
    xx->trackStatus[xx->trackNum] = 1;
    xx->trackType[xx->trackNum] = 1;
    xx->usedTracks++;
    return deltaTime;
}

/* ConvertSMF.c:851  (0x8c844) */
void GetNextMsg(synthVarsPtr xx, int32_t *seqIndexPtr, int32_t *toBeReadPtr, MsgRec *curMsgPtr)
{
    int32_t dataLen;
    int16_t nextByte;
    int16_t running;
    int16_t needed;
    int16_t i;

    running = 0;
    nextByte = egetc(xx, seqIndexPtr, toBeReadPtr);
    if (((((uint32_t)(uint16_t)nextByte >> 7) ^ 1) & 1) != 0) {
        if (curMsgPtr->status == 0) {
            curMsgPtr->type = -8;
            return;
        }
        running = 1;
    } else {
        curMsgPtr->status = nextByte;
        running = 0;
    }
    needed = xx->MidiLengths[(curMsgPtr->status >> 4) & 15];
    if (needed != 0) {
        curMsgPtr->type = 0;
        if (running != 0) {
            curMsgPtr->c1 = nextByte;
        } else {
            curMsgPtr->c1 = egetc(xx, seqIndexPtr, toBeReadPtr);
        }
        if (needed == 2) {
            curMsgPtr->c2 = egetc(xx, seqIndexPtr, toBeReadPtr);
            return;
        }
        curMsgPtr->c2 = 0;
        return;
    }
    switch (nextByte) {
    case 255:
        curMsgPtr->type = 1;
        curMsgPtr->metaType = egetc(xx, seqIndexPtr, toBeReadPtr);
        curMsgPtr->metaMsg = *seqIndexPtr;
        dataLen = ReadVariNum(xx, seqIndexPtr, toBeReadPtr);
        if (dataLen < 0) {
            curMsgPtr->type = -7;
            return;
        }
        if (dataLen <= 0xff) {
            for (i = 0; i < dataLen; i++) {
                xx->MsgBuff[i] = egetc(xx, seqIndexPtr, toBeReadPtr);
            }
            return;
        }
        curMsgPtr->type = 2;
        for (i = 0; i < dataLen; i++) {
            egetc(xx, seqIndexPtr, toBeReadPtr);
        }
        return;
    case 240:
    case 247:
        curMsgPtr->type = 2;
        dataLen = ReadVariNum(xx, seqIndexPtr, toBeReadPtr);
        for (i = 0; i < dataLen; i++) {
            egetc(xx, seqIndexPtr, toBeReadPtr);
        }
        return;
    }
    egetc(xx, seqIndexPtr, toBeReadPtr);
    curMsgPtr->type = 2;
}

/* ConvertSMF.c:954  (0x8cbb0) */
int16_t ParseTrack(synthVarsPtr xx, int32_t *seqIndexPtr, int16_t onlyParse)
{
    int16_t error;
    int32_t toBeRead;
    int32_t deltaTime;
    MsgRec curMsg;
    int32_t seqIndex_T;
    int32_t toBeRead_T;
    MsgRec curMsg_T;
    int16_t targ1_T;
    int16_t targ2_T;
    int16_t gotNote_T;
    int32_t noteLen_T;
    int32_t lastNoteDur;
    int16_t lastStatus;
    int16_t lastC1;
    int16_t lastC2;
    int32_t diff;

    error = 0;
    if (ReadMT(xx, (char *)/* pic 0x8cbdc */ 0 + 118972, seqIndexPtr, xx->srcLen) != 0) {
        error = -1;
        return error;
    }
    toBeRead = Read_32_Bit(xx, seqIndexPtr, &toBeRead);
    deltaTime = 0;
    curMsg.status = 0;
    xx->accumTime = 0.0f;
    xx->accumTime_I = 0;
    if (xx->trackhasMeta != 0) {
        xx->noMoreTempo = 1;
        xx->noMoreSig = 1;
    }
    while (toBeRead > 0) {
        deltaTime += ReadVariNum(xx, seqIndexPtr, &toBeRead);
        GetNextMsg(xx, seqIndexPtr, &toBeRead, &curMsg);
        if ((uint32_t)(curMsg.type + 8) <= 10) {
            switch (curMsg.type) {
            case 0:
                if (onlyParse != 0) {
                    ChanMessageParse(xx, curMsg.status, curMsg.c1, curMsg.c2);
                } else if ((curMsg.status & 240) == 144) {
                    if (curMsg.c2 > 0) {
                        targ1_T = (curMsg.status & 15) + 128;
                        targ2_T = curMsg.status;
                        seqIndex_T = *seqIndexPtr;
                        toBeRead_T = toBeRead;
                        noteLen_T = 0;
                        curMsg_T.status = curMsg.status;
                        gotNote_T = 0;
                        while (toBeRead_T > 0) {
                            noteLen_T += ReadVariNum(xx, &seqIndex_T, &toBeRead_T);
                            GetNextMsg(xx, &seqIndex_T, &toBeRead_T, &curMsg_T);
                            if (curMsg_T.type < 0) {
                                error = curMsg_T.type;
                                return error;
                            }
                            if (curMsg_T.type == 0 && targ1_T == curMsg_T.status && curMsg_T.c1 == curMsg.c1 || curMsg_T.type == 0 && targ2_T == curMsg_T.status && curMsg_T.c1 == curMsg.c1 && curMsg_T.c2 == 0) {
                                gotNote_T = 1;
                                break;
                            }
                            if (curMsg_T.type == 1 && curMsg.metaType == 47) {
                                break;
                            }
                        }
                        if (gotNote_T == 0) {
                            error = -6;
                            return error;
                        }
                        deltaTime = ChanMessage(xx, curMsg.status, curMsg.c1, curMsg.c2, noteLen_T, deltaTime);
                    }
                } else {
                    deltaTime = ChanMessage(xx, curMsg.status, curMsg.c1, curMsg.c2, 0, deltaTime);
                }
                break;
            case 1:
                if (curMsg.metaType == 47) {
                    toBeRead = 0;
                } else if (onlyParse != 0) {
                    deltaTime = MetaEventParse(xx, curMsg.metaType, deltaTime, curMsg.metaMsg);
                } else {
                    deltaTime = MetaEvent(xx, curMsg.metaType, deltaTime, curMsg.metaMsg);
                }
                break;
            case -7:
                error = -7;
                return error;
            case -8:
                error = -8;
                return error;
            case -6:
            case -5:
            case -4:
            case -3:
            case -2:
            case -1:
            case 2:
                break;
            }
        }
    }
    if (onlyParse != 0) {
        return error;
    }
    GenWord(xx->outSeq, &xx->outSeqIndex, -1, xx->destLen);
    GenWord(xx->outSeq, &xx->outSeqIndex, -1, xx->destLen);
    return error;
}

/* ConvertSMF.c:1107  (0x8d110) */
int16_t ReadHeader(synthVarsPtr xx, int32_t *seqIndexPtr)
{
    int16_t error;
    int32_t toBeRead;

    toBeRead = 0;
    error = 0;
    if (ReadMT(xx, (char *)/* pic 0x8d13c */ 0 + 117604, seqIndexPtr, xx->srcLen) != 0) {
        error = 1;
        return error;
    }
    toBeRead = Read_32_Bit(xx, seqIndexPtr, &toBeRead);
    xx->format = Read_16_Bit(xx, seqIndexPtr, &toBeRead);
    xx->ntrks = Read_16_Bit(xx, seqIndexPtr, &toBeRead);
    xx->division = Read_16_Bit(xx, seqIndexPtr, &toBeRead);
    while (toBeRead > 0) {
        egetc(xx, seqIndexPtr, &toBeRead);
    }
    return error;
}

/* ConvertSMF.c:1136  (0x8d250) */
int16_t ConvertSMF(synthVarsPtr xx, Ptr src, int32_t srcLen, Ptr dest, int32_t *destLen, _i_CvtSMFProg_Ptr infoCB, int32_t refCon)
{
    int16_t error;
    int32_t inSeqIndex;
    mFloat temp_F;
    int16_t i;
    int32_t seqPtrLoc;
    int32_t infoLoc;
    int16_t totalTracks;
    int16_t curTrack;
    int16_t chanCount;
    char mName[13];
    char nName[2];
    char crNotice[16];
    int16_t t1;
    int16_t t2;
    int32_t progStep;
    int32_t progAccum;
    int32_t progVal;
    int32_t l_UsedChans[32];
    int32_t dataStart;
    int16_t allowKara;
    int32_t t_130;
    int16_t t_134;

    VW_ST32BE(&mName[0], 0xb54656d /* '\x0bTem' */);
    VW_ST32BE(&mName[4], 0x706f2054 /* 'po T' */);
    VW_ST32BE(&mName[8], 0x7261636b /* 'rack' */);
    mName[12] = 0x0 /* '\x00' */;
    VW_ST16BE(&nName[0], 0);
    VW_ST32BE(&crNotice[0], 0xe3c456d /* '\x0e<Em' */);
    VW_ST32BE(&crNotice[4], 0x70747920 /* 'pty ' */);
    VW_ST32BE(&crNotice[8], 0x4e6f7469 /* 'Noti' */);
    VW_ST32BE(&crNotice[12], 0x63653e00 /* 'ce>\x00' */);
    error = 0;
    xx->inSeq = src;
    xx->srcLen = srcLen;
    xx->outSeq = dest;
    xx->destLen = *destLen;
    xx->convert_InfoCB = infoCB;
    xx->convert_RefCon = refCon;
    for (i = 0; i <= 31; i++) {
        xx->trackStatus[i] = 0;
        xx->trackPgmChange[i] = 0;
        xx->trackName[i] = 0;
        xx->trackFlags[i] = 0;
        xx->trackType[i] = -1;
        xx->usedChans[i] = 0;
    }
    xx->noteChanBits = 0;
    xx->SMFCopyright = 0;
    xx->beatTo = 2;
    xx->beatsPerMeasure = 4;
    for (i = 0; i <= 0x7f; i++) {
        xx->usedPgms[i] = 0;
    }
    inSeqIndex = 0;
    if (ReadHeader(xx, &inSeqIndex) != 0) {
        error = -1;
        return error;
    }
    if (xx->format > 1) {
        error = -2;
        return error;
    }
    if (xx->division <= 23) {
        error = -4;
        return error;
    }
    xx->timeScale = (float)xx->division;
    xx->timeScale = 240.0f / xx->timeScale;
    xx->tempoScale = 1.0f;
    xx->outSeqIndex = 0;
    xx->trackhasMeta = 0;
    xx->noMoreTempo = 0;
    xx->noMoreSig = 0;
    xx->trackNum = 0;
    xx->usedTracks = 0;
    xx->gotTempo = 0;
    xx->gotTimeSig = 0;
    xx->chanFilter = -1;
    xx->midi_tempo = 120;
    xx->maxTempo = xx->midi_tempo;
    xx->minTempo = xx->midi_tempo;
    xx->hasKaraText = 0;
    xx->karaTextCount = 0;
    xx->makeTextCB = 0;
    if (xx->destLen > 0) {
        allowKara = 1;
    } else {
        allowKara = 0;
    }
    goto L_8d7a4;
L_8d66c:
    xx->trackIndex[xx->trackNum] = inSeqIndex;
    error = ParseTrack(xx, &inSeqIndex, 1);
    if (error != 0) {
        return error;
    }
    if (allowKara != 0 && xx->hasKaraText != 0) {
        if (xx->convert_InfoCB != 0) {
            xx->convert_InfoCB(1, xx->karaTextCount, 0, xx->convert_RefCon);
            xx->makeTextCB = 1;
            xx->karaTextCount = 0;
            inSeqIndex = xx->trackIndex[xx->trackNum];
            ParseTrack(xx, &inSeqIndex, 1);
            xx->makeTextCB = 0;
        }
        allowKara = 0;
    }
    xx->trackNum++;
L_8d7a4:
    t_134 = xx->ntrks;
    t_130 = 1;
    if (t_134 <= 0) {
        t_130 = 0;
    }
    xx->ntrks = t_134 - 1;
    if (t_130 != 0) {
        goto L_8d66c;
    }
    totalTracks = 0;
    for (xx->trackNum = 0; xx->trackNum <= 31; xx->trackNum++) {
        if (xx->trackStatus[xx->trackNum] != 0) {
            l_UsedChans[xx->trackNum] = xx->usedChans[xx->trackNum] & (xx->noteChanBits << 1);
            if (totalTracks == 0) {
                l_UsedChans[xx->trackNum] |= 1;
                totalTracks++;
            }
            if (l_UsedChans[xx->trackNum] == 0) {
                xx->trackStatus[xx->trackNum] = 0;
            } else {
                totalTracks++;
                chanCount = 0;
                for (i = 1; i <= 16; i++) {
                    if ((l_UsedChans[xx->trackNum] & (1 << i)) != 0) {
                        chanCount++;
                    }
                }
                l_UsedChans[xx->trackNum] |= 2;
                if (chanCount > 1) {
                    xx->trackFlags[xx->trackNum] |= 8;
                }
            }
        }
    }
    if (totalTracks > 32) {
        error = -5;
        return error;
    }
    if (totalTracks == 0) {
        error = -11;
        return error;
    }
    progStep = 0x640000 / totalTracks;
    progAccum = 0;
    temp_F = (float)xx->midi_tempo;
    temp_F *= xx->tempoScale;
    xx->midi_tempo = FTOI(temp_F);
    if (xx->maxTempo - xx->minTempo <= 3) {
        xx->noMoreTempo = 1;
    }
    GenLong(xx->outSeq, &xx->outSeqIndex, 3, xx->destLen);
    GenLong(xx->outSeq, &xx->outSeqIndex, 0x10000, xx->destLen);
    seqPtrLoc = xx->outSeqIndex;
    xx->outSeqIndex += 4;
    GenWord(xx->outSeq, &xx->outSeqIndex, (int16_t)xx->midi_tempo, xx->destLen);
    GenWord(xx->outSeq, &xx->outSeqIndex, xx->beatsPerMeasure, xx->destLen);
    GenWord(xx->outSeq, &xx->outSeqIndex, xx->beatTo, xx->destLen);
    GenWord(xx->outSeq, &xx->outSeqIndex, 240, xx->destLen);
    GenWord(xx->outSeq, &xx->outSeqIndex, 32, xx->destLen);
    for (i = 0; i <= 31; i++) {
        GenWord(xx->outSeq, &xx->outSeqIndex, 100, xx->destLen);
    }
    for (i = 0; i <= 31; i++) {
        if (i < totalTracks) {
            GenWord(xx->outSeq, &xx->outSeqIndex, 1, xx->destLen);
        } else {
            GenWord(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        }
    }
    for (i = 0; i <= 31; i++) {
        GenWord(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    }
    for (i = 0; i <= 0x7f; i++) {
        GenWord(xx->outSeq, &xx->outSeqIndex, xx->usedPgms[i], xx->destLen);
    }
    for (i = 0; i <= 31; i++) {
        GenByte(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
    }
    if (xx->SMFCopyright != 0) {
        GenString(xx->outSeq, &xx->outSeqIndex, &xx->inSeq[xx->SMFCopyright], 256, xx->destLen);
    } else {
        GenString(xx->outSeq, &xx->outSeqIndex, &crNotice[0], 256, xx->destLen);
    }
    GenLong(xx->outSeq, &seqPtrLoc, xx->outSeqIndex, xx->destLen);
    infoLoc = xx->outSeqIndex;
    curTrack = 0;
    xx->gotTempo = 0;
    xx->gotTimeSig = 0;
    for (xx->trackNum = 0; xx->trackNum <= 31; xx->trackNum++) {
        if (xx->trackStatus[xx->trackNum] != 0) {
            for (i = 0; i <= 1; i++) {
                if ((l_UsedChans[xx->trackNum] & (1 << i)) != 0) {
                    GenLong(xx->outSeq, &xx->outSeqIndex, curTrack, xx->destLen);
                    GenLong(xx->outSeq, &xx->outSeqIndex, xx->trackFlags[xx->trackNum], xx->destLen);
                    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
                    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
                    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
                    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
                    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
                    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
                    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
                    GenLong(xx->outSeq, &xx->outSeqIndex, (xx->usedChans[xx->trackNum] >> 1) & xx->noteChanBits, xx->destLen);
                    GenWord(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
                    GenWord(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
                    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
                    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
                    GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
                    if (curTrack == 0) {
                        GenString(xx->outSeq, &xx->outSeqIndex, &mName[0], 32, xx->destLen);
                    } else if (xx->trackName[xx->trackNum] != 0) {
                        GenString(xx->outSeq, &xx->outSeqIndex, &xx->inSeq[xx->trackName[xx->trackNum]], 32, xx->destLen);
                    } else {
                        GenString(xx->outSeq, &xx->outSeqIndex, &nName[0], 32, xx->destLen);
                    }
                    curTrack++;
                }
            }
        }
    }
    for (xx->trackNum = totalTracks; xx->trackNum <= 31; xx->trackNum++) {
        GenLong(xx->outSeq, &xx->outSeqIndex, curTrack, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 4, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenLong(xx->outSeq, &xx->outSeqIndex, 0, xx->destLen);
        GenString(xx->outSeq, &xx->outSeqIndex, &nName[0], 32, xx->destLen);
        curTrack++;
    }
    curTrack = 0;
    for (xx->trackNum = 0; xx->trackNum <= 31; xx->trackNum++) {
        if (xx->trackStatus[xx->trackNum] != 0) {
            for (i = 0; i <= 1; i++) {
                if ((l_UsedChans[xx->trackNum] & (1 << i)) != 0) {
                    seqPtrLoc = curTrack * 88 + infoLoc + 8;
                    GenLong(xx->outSeq, &seqPtrLoc, xx->outSeqIndex - infoLoc, xx->destLen);
                    dataStart = xx->outSeqIndex;
                    inSeqIndex = xx->trackIndex[xx->trackNum];
                    xx->lyricsAllowed = 0;
                    xx->phonsAllowed = 0;
                    xx->regParam = -1;
                    xx->lastMIDI_Vol = 1.0f;
                    xx->lastMIDI_Express = 1.0f;
                    if (i == 0) {
                        xx->chanFilter = i;
                    } else {
                        xx->chanFilter = -1;
                    }
                    error = ParseTrack(xx, &inSeqIndex, 0);
                    if (error != 0) {
                        return error;
                    }
                    seqPtrLoc = curTrack * 88 + infoLoc + 12;
                    GenLong(xx->outSeq, &seqPtrLoc, xx->outSeqIndex - dataStart, xx->destLen);
                    curTrack++;
                    progAccum += progStep;
                    progVal = progAccum >> 16;
                    if (progVal > 100) {
                        progVal = 100;
                    }
                    if (xx->convert_InfoCB != 0) {
                        xx->convert_InfoCB(0, progVal, 0, xx->convert_RefCon);
                    }
                }
            }
        }
    }
    for (xx->trackNum = totalTracks; xx->trackNum <= 31; xx->trackNum++) {
        seqPtrLoc = curTrack * 88 + infoLoc + 8;
        GenLong(xx->outSeq, &seqPtrLoc, xx->outSeqIndex - infoLoc, xx->destLen);
        GenWord(xx->outSeq, &xx->outSeqIndex, -1, xx->destLen);
        GenWord(xx->outSeq, &xx->outSeqIndex, -1, xx->destLen);
        curTrack++;
    }
    progAccum += progStep;
    progVal = progAccum >> 16;
    if (progVal > 100) {
        progVal = 100;
    }
    if (xx->convert_InfoCB != 0) {
        xx->convert_InfoCB(0, progVal, 0, xx->convert_RefCon);
    }
    if (xx->outSeqIndex > xx->destLen) {
        error = -9;
    }
    (*destLen) = xx->outSeqIndex;
    return error;
}
