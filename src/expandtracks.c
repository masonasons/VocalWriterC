/* ExpandTracks.c -- rearranging imported tracks.
 *
 * Lifted from the original's machine code; see src/speech.c.
 */
#include "vw_engine.h"

int16_t g_GenOverflow;          /* set when an output track overflows */

static void E_GenByte(unsigned char *dest, int32_t *index, int16_t data, int32_t limit);
static void E_GenLong(unsigned char *dest, int32_t *index, int32_t data, int32_t limit);
static void E_GenWord(unsigned char *dest, int32_t *index, int16_t data, int32_t limit);
static void E_Gen24(unsigned char *dest, int32_t *index, int32_t data, int32_t limit);
static void E_GenTime(unsigned char *outTrackPtr, int32_t *oIndexPtr, int32_t oLimit, int32_t deltaTime);
static int16_t E_GenChanMessage(Convert_EventPtr cc, int32_t deltaTime, unsigned char *outTrackPtr, int32_t *oIndexPtr, int32_t oLimit);
static int16_t E_GetNextTrackEvent(Convert_EventPtr me);
static void Expand_CopyPStr(Ptr src, unsigned char *dest);
static void Expand_ConcatPStr(unsigned char *st1, unsigned char *st2);
static void Expand_ClearTrack(Ptr trackDataPtr, TrackInfoPtr trackInfo);
static void Expand_NameTrack(TrackInfoPtr trackInfo, int16_t trackNum);
static void Expand_MatchChanToTrack(Ptr targetTrack, int16_t trackNum);
static int32_t Expand_CountEvents(Ptr trackPtr, int16_t chan, int16_t includeNotes);
static int16_t FindFreeTrack_1(Expand_SMF_RecPtr esr);
static int16_t FindFreeTrack_2(Expand_SMF_RecPtr esr);
static void Expand_MixTrack(E_TrackEditInfoPtr jj);
static void Expand_CopyTrack(E_TrackEditInfoPtr jj);
static void Expand_CutTrack(E_TrackEditInfoPtr jj);

/* ExpandTracks.c:34  (0x8ec18) */
static void E_GenByte(unsigned char *dest, int32_t *index, int16_t data, int32_t limit)
{
    if (*index < limit) {
        dest[*index] = data;
    } else {
        g_GenOverflow = 1;
    }
    (*index)++;
}

/* ExpandTracks.c:85  (0x8ecc4) */
static void E_GenLong(unsigned char *dest, int32_t *index, int32_t data, int32_t limit)
{
    E_GenByte(dest, index, (int16_t)(uint8_t)(data >> 24), limit);
    E_GenByte(dest, index, (int16_t)(uint8_t)(data >> 16), limit);
    E_GenByte(dest, index, (int16_t)(uint8_t)(data >> 8), limit);
    E_GenByte(dest, index, (int16_t)(uint8_t)data, limit);
}

/* ExpandTracks.c:95  (0x8edac) */
static void E_GenWord(unsigned char *dest, int32_t *index, int16_t data, int32_t limit)
{
    E_GenByte(dest, index, (int16_t)(uint8_t)(data >> 8), limit);
    E_GenByte(dest, index, data, limit);
}

/* ExpandTracks.c:102  (0x8ee48) */
static void E_Gen24(unsigned char *dest, int32_t *index, int32_t data, int32_t limit)
{
    E_GenByte(dest, index, (int16_t)(uint8_t)(data >> 16), limit);
    E_GenByte(dest, index, (int16_t)(uint8_t)(data >> 8), limit);
    E_GenByte(dest, index, (int16_t)(uint8_t)data, limit);
}

/* ExpandTracks.c:110  (0x8ef08) */
static void E_GenTime(unsigned char *outTrackPtr, int32_t *oIndexPtr, int32_t oLimit, int32_t deltaTime)
{
    if (deltaTime > 0xffffff) {
        deltaTime = 0xffffff;
    }
    E_GenByte(outTrackPtr, oIndexPtr, (int16_t)(uint8_t)(deltaTime >> 16), oLimit);
    E_GenByte(outTrackPtr, oIndexPtr, (int16_t)(uint8_t)(deltaTime >> 8), oLimit);
    E_GenByte(outTrackPtr, oIndexPtr, (int16_t)(uint8_t)deltaTime, oLimit);
}

/* ExpandTracks.c:125  (0x8efe8) */
static int16_t E_GenChanMessage(Convert_EventPtr cc, int32_t deltaTime, unsigned char *outTrackPtr, int32_t *oIndexPtr, int32_t oLimit)
{
    int16_t eventWasGen;
    int32_t i;
    int32_t textLen;

    eventWasGen = 0;
    if (cc->chanFilter >= 0) {
        if (cc->target_chan != cc->chanFilter) {
            return eventWasGen;
        }
    }
    if ((uint32_t)cc->target_cmd > 71) {
        return eventWasGen;
    }
    switch (cc->target_cmd) {
    case 1:
    case 6:
    case 8:
        if ((cc->eventMask & 1) == 0) {
            return eventWasGen;
        }
        if (cc->target_key < cc->minKeyVal) {
            return eventWasGen;
        }
        if (cc->target_key > cc->maxKeyVal) {
            return eventWasGen;
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_cmd, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_Gen24(outTrackPtr, oIndexPtr, cc->target_dur, oLimit);
        if (cc->target_cmd == 6) {
            E_GenWord(outTrackPtr, oIndexPtr, (int16_t)cc->target_vocals, oLimit);
        } else {
            E_GenWord(outTrackPtr, oIndexPtr, 0, oLimit);
        }
        eventWasGen = 1;
        return eventWasGen;
    case 33:
        if ((((uint32_t)cc->eventMask >> 2) & 1) == 0) {
            return eventWasGen;
        }
        if (cc->cntrlNum >= 0) {
            if (cc->target_cmd != cc->cntrlNum) {
                return eventWasGen;
            }
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 33, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 34:
        if ((((uint32_t)cc->eventMask >> 2) & 1) == 0) {
            return eventWasGen;
        }
        if (cc->cntrlNum >= 0) {
            if (cc->target_cmd != cc->cntrlNum) {
                return eventWasGen;
            }
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 34, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 32:
        if ((((uint32_t)cc->eventMask >> 5) & 1) == 0) {
            return eventWasGen;
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 32, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 38:
        if ((((uint32_t)cc->eventMask >> 8) & 1) == 0) {
            return eventWasGen;
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 38, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 3:
        if ((((uint32_t)cc->eventMask >> 3) & 1) == 0) {
            return eventWasGen;
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 3, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 4:
        if ((((uint32_t)cc->eventMask >> 7) & 1) == 0) {
            return eventWasGen;
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenWord(outTrackPtr, oIndexPtr, 4, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 5:
        if ((((uint32_t)cc->eventMask >> 9) & 1) == 0) {
            return eventWasGen;
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenWord(outTrackPtr, oIndexPtr, 5, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 7:
        if ((((uint32_t)cc->eventMask >> 7) & 1) == 0) {
            return eventWasGen;
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenWord(outTrackPtr, oIndexPtr, 7, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 64:
        if ((((uint32_t)cc->eventMask >> 2) & 1) == 0) {
            return eventWasGen;
        }
        if (cc->cntrlNum >= 0) {
            if (cc->target_cmd != cc->cntrlNum) {
                return eventWasGen;
            }
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 64, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 71:
        if ((((uint32_t)cc->eventMask >> 2) & 1) == 0) {
            return eventWasGen;
        }
        if (cc->cntrlNum >= 0) {
            if (cc->target_cmd != cc->cntrlNum) {
                return eventWasGen;
            }
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 71, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 67:
        if ((((uint32_t)cc->eventMask >> 2) & 1) == 0) {
            return eventWasGen;
        }
        if (cc->cntrlNum >= 0) {
            if (cc->target_cmd != cc->cntrlNum) {
                return eventWasGen;
            }
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 67, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 68:
        if ((((uint32_t)cc->eventMask >> 2) & 1) == 0) {
            return eventWasGen;
        }
        if (cc->cntrlNum >= 0) {
            if (cc->target_cmd != cc->cntrlNum) {
                return eventWasGen;
            }
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 68, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 69:
        if ((((uint32_t)cc->eventMask >> 2) & 1) == 0) {
            return eventWasGen;
        }
        if (cc->cntrlNum >= 0) {
            if (cc->target_cmd != cc->cntrlNum) {
                return eventWasGen;
            }
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 69, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 66:
        if ((((uint32_t)cc->eventMask >> 2) & 1) == 0) {
            return eventWasGen;
        }
        if (cc->cntrlNum >= 0) {
            if (cc->target_cmd != cc->cntrlNum) {
                return eventWasGen;
            }
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 66, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 39:
        if ((((uint32_t)cc->eventMask >> 2) & 1) == 0) {
            return eventWasGen;
        }
        if (cc->cntrlNum >= 0) {
            if (cc->target_cmd != cc->cntrlNum) {
                return eventWasGen;
            }
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 39, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 70:
        if ((((uint32_t)cc->eventMask >> 2) & 1) == 0) {
            return eventWasGen;
        }
        if (cc->cntrlNum >= 0) {
            if (cc->target_cmd != cc->cntrlNum) {
                return eventWasGen;
            }
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 70, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_key, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_vol, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
        return eventWasGen;
    case 9:
        if (cc->target_chan != 0) {
            return eventWasGen;
        }
        if ((((uint32_t)cc->eventMask >> 11) & 1) == 0) {
            return eventWasGen;
        }
        E_GenTime(outTrackPtr, oIndexPtr, oLimit, deltaTime);
        E_GenByte(outTrackPtr, oIndexPtr, (int16_t)cc->target_chan, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 9, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenLong(outTrackPtr, oIndexPtr, 0, oLimit);
        E_GenByte(outTrackPtr, oIndexPtr, 0, oLimit);
        eventWasGen = 1;
    case 0:
    case 2:
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
        return eventWasGen;
    }
}

/* ExpandTracks.c:464  (0x90260) */
static int16_t E_GetNextTrackEvent(Convert_EventPtr me)
{
    uint32_t seqItemTime;
    int16_t midiStatus;
    MIDI_ItemPtr curItem;
    double scaledTime;
    int32_t i;
    int32_t len;
    int16_t status;

    status = 1;
    seqItemTime = (uint32_t)VW_LD32BE(me->targetTrack) >> 8;
    if (seqItemTime == 0xffffff) {
        status = 0;
        me->endOfTrack = 1;
        return status;
    }
    me->curEventPtr = me->targetTrack;
    me->target_time = seqItemTime;
    if (me->target_endTime >= 0 && me->target_time >= me->target_endTime) {
        status = 0;
        return status;
    }
    me->targetTrack += 3;
    me->target_chan = *me->targetTrack;
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
        me->target_vocals = (uint16_t)VW_LD16BE(me->targetTrack);
        me->targetTrack += 2;
        return status;
    }
    me->targetTrack += 5;
    return status;
}

/* ExpandTracks.c:524  (0x90498) */
static void Expand_CopyPStr(Ptr src, unsigned char *dest)
{
    int16_t srcLen;

    for (srcLen = (int8_t)*src; srcLen >= 0; srcLen--) {
        dest[srcLen] = src[srcLen];
    }
}

/* ExpandTracks.c:534  (0x90534) */
static void Expand_ConcatPStr(unsigned char *st1, unsigned char *st2)
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

/* ExpandTracks.c:552  (0x90624) */
static void Expand_ClearTrack(Ptr trackDataPtr, TrackInfoPtr trackInfo)
{
    int32_t oIndex;
    int32_t oLimit;

    oIndex = 0;
    oLimit = 4;
    E_GenWord(trackDataPtr, &oIndex, -1, oLimit);
    E_GenWord(trackDataPtr, &oIndex, -1, oLimit);
    trackInfo->trackDataLen = 4;
    trackInfo->flags = 0;
    trackInfo->speechDataLen = 0;
    trackInfo->trackChannels = 0;
    trackInfo->trackName[0] = 0;
}

/* ExpandTracks.c:574  (0x906ec) */
static void Expand_NameTrack(TrackInfoPtr trackInfo, int16_t trackNum)
{
    char tName[8];
    Str255 nameStr;
    Str255 numbStr;

    VW_ST32BE(&tName[0], 0x6547261 /* '\x06Tra' */);
    VW_ST32BE(&tName[4], 0x636b2000 /* 'ck \x00' */);
    Expand_CopyPStr(&tName[0], &nameStr[0]);
    NumToString(trackNum + 1, &numbStr[0]);
    Expand_ConcatPStr(&nameStr[0], &numbStr[0]);
    Expand_CopyPStr(&nameStr[0], &trackInfo->trackName[0]);
}

/* ExpandTracks.c:589  (0x907ac) */
static void Expand_MatchChanToTrack(Ptr targetTrack, int16_t trackNum)
{
    uint32_t curTime;

    for (;;) {
        curTime = (uint32_t)VW_LD32BE(targetTrack) >> 8;
        if (curTime == 0xffffff) {
            return;
        }
        targetTrack[3] = trackNum;
        targetTrack += 12;
        continue;
    }
}

/* ExpandTracks.c:617  (0x90828) */
static int32_t Expand_CountEvents(Ptr trackPtr, int16_t chan, int16_t includeNotes)
{
    SeqEvent *sePtr;
    int32_t count;
    uint32_t curTime;
    int16_t countIt;

    count = 0;
L_90864:
    countIt = 1;
    curTime = (uint32_t)VW_LD32BE(trackPtr) >> 8;
    if (curTime != 0xffffff) {
        sePtr = (SeqEvent *)trackPtr;
        if (chan >= 0 && chan != sePtr->chan) {
            countIt = 0;
        }
        if (includeNotes == 0 && sePtr->cmd == 1) {
            countIt = 0;
        }
        if (countIt != 0) {
            count++;
        }
        trackPtr += 12;
        goto L_90864;
    }
    count *= 12;
    return count;
}

/* ExpandTracks.c:666  (0x9094c) */
static int16_t FindFreeTrack_1(Expand_SMF_RecPtr esr)
{
    int16_t result;
    int16_t i;

    result = 0;
    for (i = 1; i <= 31; i++) {
        if (esr->trackDataLen[i] == 4) {
            result = i;
            return result;
        }
    }
    return result;
}

/* ExpandTracks.c:685  (0x909e8) */
static int16_t FindFreeTrack_2(Expand_SMF_RecPtr esr)
{
    int16_t result;
    int16_t i;

    result = 0;
    for (i = 1; i <= 31; i++) {
        if (esr->freeTracks[i] != 0) {
            result = i;
            return result;
        }
    }
    return result;
}

/* ExpandTracks.c:711  (0x90a88) */
static void Expand_MixTrack(E_TrackEditInfoPtr jj)
{
    unsigned char *targetTrackPtr;
    unsigned char *clipBoardPtr;
    unsigned char *seqTrackPtr;
    Convert_Event meS;
    Convert_Event meD;
    int32_t oIndex;
    int32_t oLimit;
    int32_t timeNorm;
    int32_t count1;
    int32_t count2;

    oLimit = jj->oLimit;
    oIndex = 0;
    count2 = 0;
    count1 = count2;
    clipBoardPtr = jj->srcPtr1;
    seqTrackPtr = jj->srcPtr2;
    targetTrackPtr = jj->destPtr;
    meS.endOfTrack = 0;
    meS.chanFilter = jj->chanFilter;
    meS.eventMask = jj->eventMask;
    meS.maxKeyVal = jj->maxKeyVal;
    meS.minKeyVal = jj->minKeyVal;
    meS.cntrlNum = jj->cntrlNum;
    meS.targetTrack = clipBoardPtr;
    meS.target_time = 0;
    meS.target_endTime = 0;
    E_GetNextTrackEvent(&meS);
    meS.targetTrack = clipBoardPtr;
    timeNorm = 0;
    meS.target_time = jj->seqTrackStartTick;
    meD.endOfTrack = 0;
    meD.chanFilter = jj->chanFilter;
    meD.eventMask = jj->eventMask;
    meD.maxKeyVal = jj->maxKeyVal;
    meD.minKeyVal = jj->minKeyVal;
    meD.cntrlNum = jj->cntrlNum;
    meD.targetTrack = seqTrackPtr;
    meD.target_time = 0;
    meD.target_endTime = 0;
    E_GetNextTrackEvent(&meD);
    meD.targetTrack = seqTrackPtr;
    meD.target_time = 0;
    while (meS.endOfTrack == 0 || meD.endOfTrack == 0) {
        if (meD.endOfTrack != 0) {
            meD.target_time = 0xffffff;
        } else {
            meD.target_endTime = meS.target_time + 1;
            while (E_GetNextTrackEvent(&meD) != 0) {
                count2++;
                E_GenChanMessage(&meD, meD.target_time, targetTrackPtr, &oIndex, oLimit);
            }
        }
        if (meS.endOfTrack != 0) {
            meS.target_time = 16777214;
        } else {
            meS.target_endTime = meD.target_time;
            while (E_GetNextTrackEvent(&meS) != 0) {
                count1++;
                meS.target_time += timeNorm;
                E_GenChanMessage(&meS, meS.target_time, targetTrackPtr, &oIndex, oLimit);
            }
        }
    }
    E_GenWord(targetTrackPtr, &oIndex, -1, oLimit);
    E_GenWord(targetTrackPtr, &oIndex, -1, oLimit);
    jj->trackLen = oIndex;
    jj->count1 = count1 * 12;
    jj->count2 = count2 * 12;
}

/* ExpandTracks.c:829  (0x90d88) */
static void Expand_CopyTrack(E_TrackEditInfoPtr jj)
{
    unsigned char *clipBoardPtr;
    unsigned char *seqTrackPtr;
    Convert_Event meS;
    int32_t oIndex;
    int32_t oLimit;
    int32_t lastTime;

    oLimit = jj->oLimit;
    oIndex = 0;
    clipBoardPtr = jj->destPtr;
    seqTrackPtr = jj->srcPtr1;
    meS.endOfTrack = 0;
    meS.targetTrack = seqTrackPtr;
    meS.target_time = 0;
    if (jj->seqTrackStartTick > 0) {
        meS.target_time = 0;
        meS.target_endTime = jj->seqTrackStartTick;
        do {
        } while (E_GetNextTrackEvent(&meS) != 0);
    }
    meS.target_endTime = jj->seqTrackEndTick;
    meS.chanFilter = jj->chanFilter;
    meS.eventMask = jj->eventMask;
    meS.maxKeyVal = jj->maxKeyVal;
    meS.minKeyVal = jj->minKeyVal;
    meS.cntrlNum = jj->cntrlNum;
    lastTime = jj->seqTrackStartTick;
    while (E_GetNextTrackEvent(&meS) != 0) {
        if (E_GenChanMessage(&meS, meS.target_time, clipBoardPtr, &oIndex, oLimit) != 0) {
            lastTime = meS.target_time;
        }
    }
    E_GenWord(clipBoardPtr, &oIndex, -1, oLimit);
    E_GenWord(clipBoardPtr, &oIndex, -1, oLimit);
    jj->trackLen = oIndex;
}

/* ExpandTracks.c:892  (0x90f2c) */
static void Expand_CutTrack(E_TrackEditInfoPtr jj)
{
    unsigned char *prunedTrackPtr;
    unsigned char *clipBoardPtr;
    unsigned char *seqTrackPtr;
    Convert_Event meS;
    Convert_Event meC;
    int32_t oIndexPrune;
    int32_t oIndexClip;
    int32_t lastTimePrune;
    int32_t lastTimeClip;
    int32_t oLimit;
    int32_t oLimit1;
    int16_t error;

    error = 0;
    oLimit = jj->oLimit;
    oLimit1 = jj->oLimit1;
    prunedTrackPtr = jj->srcPtr2;
    oIndexPrune = 0;
    clipBoardPtr = jj->destPtr;
    oIndexClip = 0;
    seqTrackPtr = jj->srcPtr1;
    meS.endOfTrack = 0;
    meS.chanFilter = -1;
    meS.eventMask = 3071;
    meS.maxKeyVal = 0x7f;
    meS.minKeyVal = 0;
    meS.cntrlNum = -1;
    meS.target_endTime = jj->seqTrackStartTick;
    meS.targetTrack = seqTrackPtr;
    meS.target_time = 0;
    meC.chanFilter = jj->chanFilter;
    meC.eventMask = jj->eventMask;
    meC.maxKeyVal = jj->maxKeyVal;
    meC.minKeyVal = jj->minKeyVal;
    meC.cntrlNum = jj->cntrlNum;
    lastTimePrune = 0;
    if (jj->seqTrackStartTick > 0) {
        goto L_91088;
    }
    goto L_910a0;
L_91050:
    if (E_GenChanMessage(&meS, meS.target_time, prunedTrackPtr, &oIndexPrune, oLimit) != 0) {
        lastTimePrune = meS.target_time;
    }
L_91088:
    if (E_GetNextTrackEvent(&meS) != 0) {
        goto L_91050;
    }
L_910a0:
    meS.target_endTime = jj->seqTrackEndTick;
    lastTimeClip = meS.target_time;
    while (E_GetNextTrackEvent(&meS) != 0) {
        meC.target_chan = meS.target_chan;
        meC.target_cmd = meS.target_cmd;
        meC.target_key = meS.target_key;
        meC.target_vol = meS.target_vol;
        meC.target_dur = meS.target_dur;
        meC.target_vocals = meS.target_vocals;
        if (E_GenChanMessage(&meC, meS.target_time, clipBoardPtr, &oIndexClip, oLimit1) != 0) {
            lastTimeClip = meS.target_time;
        } else if (E_GenChanMessage(&meS, meS.target_time, prunedTrackPtr, &oIndexPrune, oLimit) != 0) {
            lastTimePrune = meS.target_time;
        }
    }
    meS.target_endTime = 0xffffff;
    while (E_GetNextTrackEvent(&meS) != 0) {
        if (E_GenChanMessage(&meS, meS.target_time, prunedTrackPtr, &oIndexPrune, oLimit) != 0) {
            lastTimePrune = meS.target_time;
        }
    }
    E_GenWord(prunedTrackPtr, &oIndexPrune, -1, oLimit);
    E_GenWord(prunedTrackPtr, &oIndexPrune, -1, oLimit);
    E_GenWord(clipBoardPtr, &oIndexClip, -1, oLimit);
    E_GenWord(clipBoardPtr, &oIndexClip, -1, oLimit);
    jj->trackLen = oIndexPrune;
    jj->trackLen1 = oIndexClip;
}

/* ExpandTracks.c:996  (0x91260) */
int16_t ExpandTracks_1(Expand_SMF_RecPtr esr)
{
    SeqInfoPtr theSeq;
    TrackInfoPtr curTrack;
    int32_t flags;
    int32_t mergeLenTotal;
    int32_t buf_2_Size;
    int32_t buf_3_Size;
    int32_t cSize;
    int32_t csizeT;
    int16_t i;
    int16_t j;
    int16_t k;
    int16_t x;
    int32_t maxCSize;
    int16_t error;
    int32_t mergedSize;
    int32_t sepSize;

    esr->workBufLen1 = 0;
    esr->workBufLen2 = 0;
    esr->seqBufLen = 0;
    error = 0;
    mergeLenTotal = 0;
    buf_3_Size = 0;
    buf_2_Size = buf_3_Size;
    maxCSize = 0;
    theSeq = (SeqInfoPtr)((char *)esr->o_SeqHeader + esr->o_SeqHeader->seqData);
    for (i = 0; i <= 31; i++) {
        esr->tracksToSep[i] = 0;
        esr->tracksToMerge[i] = 0;
        esr->freeTracks[i] = 0;
        curTrack = &theSeq->tracks[i];
        esr->o_TrackInfos[i] = curTrack;
        esr->o_TrackDataPtr[i] = (Ptr)((char *)theSeq + curTrack->trackData);
        esr->o_TrackDataLen[i] = curTrack->trackDataLen;
        esr->trackDataLen[i] = esr->o_TrackDataLen[i];
        if (esr->trackDataLen[i] <= 3) {
            esr->trackDataLen[i] = 4;
        }
        esr->trackChannels[i] = curTrack->trackChannels;
    }
    esr->numToMerge = 0;
    esr->totalTracks = 1;
    esr->mergedChannels = 0;
    for (i = 1; i <= 31; i++) {
        curTrack = esr->o_TrackInfos[i];
        flags = curTrack->flags;
        if ((((uint32_t)flags >> 3) & 1) != 0) {
            if ((((uint32_t)flags >> 2) & 1) != 0) {
                for (j = 0; j <= 15; j++) {
                    if ((curTrack->trackChannels & (1 << j)) != 0) {
                        esr->totalTracks++;
                    }
                }
                esr->tracksToSep[i] = 1;
            } else {
                esr->tracksToMerge[i] = 1;
                esr->mergedChannels |= curTrack->trackChannels;
                esr->numToMerge++;
                esr->trackDataLen[i] = 4;
                esr->trackChannels[i] = 0;
            }
        } else if ((((uint32_t)flags >> 2) & 1) != 0) {
            esr->totalTracks++;
        } else if (esr->trackDataLen[i] > 4) {
            esr->tracksToMerge[i] = 1;
            esr->mergedChannels |= curTrack->trackChannels;
            esr->numToMerge++;
            esr->trackDataLen[i] = 4;
            esr->trackChannels[i] = 0;
        }
    }
    if (esr->totalTracks > 32) {
        error = -5;
        return error;
    }
    if (esr->numToMerge != 0) {
        for (i = 1; i <= 31; i++) {
            if (esr->tracksToMerge[i] != 0) {
                mergeLenTotal = esr->o_TrackDataLen[i] + mergeLenTotal - 4;
            }
        }
        mergeLenTotal += 4;
    }
    buf_2_Size = mergeLenTotal;
    for (i = 1; i <= 31; i++) {
        if (esr->tracksToSep[i] != 0) {
            for (j = 0; j <= 15; j++) {
                if ((esr->o_TrackInfos[i]->trackChannels & (1 << j)) != 0) {
                    k = FindFreeTrack_1(esr);
                    if (k == 0) {
                        error = -5;
                        return error;
                    }
                    esr->freeTracks[k] = 1;
                    cSize = Expand_CountEvents(esr->o_TrackDataPtr[i], j, 1);
                    esr->trackDataLen[k] = cSize + 4;
                    esr->trackChannels[k] = 1 << j;
                }
            }
            esr->trackDataLen[i] = 4;
            esr->trackChannels[i] = 0;
        }
    }
    if (esr->numToMerge > 0) {
        for (i = 1; i <= 31; i++) {
            j = esr->trackChannels[i] & esr->mergedChannels;
            if (esr->trackDataLen[i] > 4 && j != 0) {
                k = 0;
                while ((j & 1) == 0) {
                    k++;
                    j >>= 1;
                }
                cSize = 0;
                for (x = 1; x <= 31; x++) {
                    if (esr->tracksToMerge[x] != 0) {
                        cSize += Expand_CountEvents(esr->o_TrackDataPtr[x], k, 1);
                    }
                }
                if (esr->trackDataLen[i] > buf_2_Size) {
                    buf_2_Size = esr->trackDataLen[i];
                }
                esr->trackDataLen[i] += cSize;
            }
        }
    }
    for (i = 1; i <= 31; i++) {
        if (esr->trackDataLen[i] > 4) {
            j = esr->trackChannels[i];
            k = 0;
            while ((j & 1) == 0) {
                k++;
                j >>= 1;
            }
            esr->trackChannels_I[i] = k;
        }
    }
    esr->channelDup = 0;
    for (j = 0; j <= 15; j++) {
        esr->channelUsage[j] = 0;
        for (i = 1; i <= 31; i++) {
            if (esr->trackDataLen[i] > 4 && esr->trackChannels_I[i] == j) {
                esr->channelUsage[j]++;
                if (esr->channelUsage[j] > 1) {
                    esr->channelDup = 1;
                }
            }
        }
    }
    if (esr->channelDup != 0) {
        for (j = 0; j <= 15; j++) {
            if (esr->channelUsage[j] > 1) {
                mergedSize = 0;
                sepSize = 0;
                for (i = 1; i <= 31; i++) {
                    if (esr->tracksToMerge[i] != 0) {
                        mergedSize += Expand_CountEvents(esr->o_TrackDataPtr[i], j, 0);
                    } else if (esr->tracksToSep[i] != 0) {
                        sepSize += Expand_CountEvents(esr->o_TrackDataPtr[i], j, 0);
                    }
                }
                csizeT = sepSize;
                for (i = 1; i <= 31; i++) {
                    if (esr->trackDataLen[i] > 4 && esr->trackChannels_I[i] == j) {
                        if (esr->trackDataLen[i] > buf_2_Size) {
                            buf_2_Size = esr->trackDataLen[i];
                        }
                        cSize = Expand_CountEvents(esr->o_TrackDataPtr[i], j, 0);
                        cSize += mergedSize;
                        esr->trackDataLen[i] -= cSize;
                        csizeT += cSize;
                        if (cSize > buf_3_Size) {
                            buf_3_Size = cSize;
                        }
                    }
                }
                for (i = 1; i <= 31; i++) {
                    if (esr->trackDataLen[i] > 4 && esr->trackChannels_I[i] == j) {
                        esr->trackDataLen[i] += csizeT;
                        if (esr->trackDataLen[i] > buf_2_Size) {
                            buf_2_Size = esr->trackDataLen[i];
                        }
                    }
                }
                if (csizeT > maxCSize) {
                    maxCSize = csizeT;
                }
            }
        }
    }
    if (mergeLenTotal > maxCSize) {
        esr->workBufLen1 = mergeLenTotal + 4;
    } else {
        esr->workBufLen1 = maxCSize + 4;
    }
    esr->workBufLen2 = buf_2_Size + 4;
    esr->workBufLen3 = buf_3_Size + 4;
    if (esr->workBufLen1 < esr->workBufLen3) {
        esr->workBufLen1 = esr->workBufLen3;
    }
    for (i = 0; i <= 31; i++) {
        esr->seqBufLen += esr->trackDataLen[i];
    }
    esr->seqBufLen += 3574;
    return error;
}

/* ExpandTracks.c:1358  (0x92318) */
int16_t ExpandTracks_2(Expand_SMF_RecPtr esr)
{
    int16_t error;
    SeqInfoPtr theSeq;
    Ptr bPtr;
    int16_t firstPass;
    int32_t buf1Size;
    int16_t i;
    int16_t j;
    int16_t k;
    int16_t x;
    Ptr e_TrackDataPtr[32];
    TrackInfoPtr e_TrackInfos[32];
    SeqHeaderPtr e_SeqheaderPtr;
    E_TrackEditInfo tei;

    error = 0;
    g_GenOverflow = 0;
    theSeq = (SeqInfoPtr)((char *)esr->o_SeqHeader + esr->o_SeqHeader->seqData);
    bPtr = esr->seqBuf;
    e_SeqheaderPtr = (SeqHeaderPtr)bPtr;
    BlockMove(esr->o_SeqHeader, bPtr, 758);
    e_SeqheaderPtr->seqData = 758;
    bPtr += 758;
    for (i = 0; i <= 31; i++) {
        e_TrackInfos[i] = (TrackInfoPtr)bPtr;
        BlockMove(&theSeq->tracks[i], bPtr, 88);
        bPtr += 88;
        e_SeqheaderPtr->trackVol[i] = 100;
    }
    for (i = 0; i <= 31; i++) {
        e_TrackDataPtr[i] = bPtr;
        e_TrackInfos[i]->trackData = (int32_t)(bPtr - (Ptr)e_TrackInfos[0]);
        e_TrackInfos[i]->trackDataLen = esr->trackDataLen[i];
        bPtr += esr->trackDataLen[i];
    }
    for (i = 0; i <= 31; i++) {
        if (esr->trackDataLen[i] > 4 && esr->tracksToMerge[i] == 0 && esr->tracksToSep[i] == 0) {
            if (esr->o_TrackDataLen[i] > esr->trackDataLen[i]) {
                error = -9;
                return error;
            }
            BlockMove(esr->o_TrackDataPtr[i], e_TrackDataPtr[i], esr->o_TrackDataLen[i]);
        }
    }
    if (esr->numToMerge != 0) {
        tei.chanFilter = -1;
        tei.eventMask = 383;
        tei.maxKeyVal = 0x7f;
        tei.minKeyVal = 0;
        tei.cntrlNum = -1;
        tei.seqTrackStartTick = 0;
        tei.seqTrackEndTick = 0xffffff;
        firstPass = 1;
        for (i = 1; i <= 31; i++) {
            if (esr->tracksToMerge[i] != 0) {
                if (firstPass != 0) {
                    if (esr->o_TrackDataLen[i] > esr->workBufLen1) {
                        error = -9;
                        return error;
                    }
                    BlockMove(esr->o_TrackDataPtr[i], esr->workBuf1, esr->o_TrackDataLen[i]);
                    firstPass = 0;
                } else {
                    if (esr->workBufLen1 > esr->workBufLen2) {
                        error = -9;
                        return error;
                    }
                    BlockMove(esr->workBuf1, esr->workBuf2, esr->workBufLen1);
                    tei.srcPtr1 = esr->workBuf2;
                    tei.srcPtr2 = esr->o_TrackDataPtr[i];
                    tei.destPtr = esr->workBuf1;
                    tei.oLimit = esr->workBufLen1;
                    Expand_MixTrack(&tei);
                    if (g_GenOverflow != 0) {
                        error = -9;
                        return error;
                    }
                    if (tei.trackLen > esr->workBufLen1) {
                        error = -9;
                        return error;
                    }
                }
            }
        }
    }
    for (i = 1; i <= 31; i++) {
        if (esr->tracksToSep[i] != 0) {
            tei.srcPtr1 = esr->o_TrackDataPtr[i];
            tei.seqTrackStartTick = 0;
            tei.seqTrackEndTick = 0xffffff;
            tei.eventMask = 383;
            tei.maxKeyVal = 0x7f;
            tei.minKeyVal = 0;
            tei.cntrlNum = -1;
            for (j = 0; j <= 15; j++) {
                if ((esr->o_TrackInfos[i]->trackChannels & (1 << j)) != 0) {
                    tei.chanFilter = j;
                    k = FindFreeTrack_2(esr);
                    if (k == 0) {
                        error = -5;
                        return error;
                    }
                    esr->freeTracks[k] = 0;
                    tei.destPtr = e_TrackDataPtr[k];
                    tei.oLimit = esr->trackDataLen[k];
                    Expand_CopyTrack(&tei);
                    e_TrackInfos[k]->trackDataLen = tei.trackLen;
                    e_TrackInfos[k]->trackChannels = 1 << j;
                    if (g_GenOverflow != 0) {
                        error = -9;
                        return error;
                    }
                    if (tei.trackLen > esr->trackDataLen[k]) {
                        error = -9;
                        return error;
                    }
                    Expand_CopyPStr(&esr->o_TrackInfos[i]->trackName[0], &e_TrackInfos[k]->trackName[0]);
                }
            }
            Expand_ClearTrack(e_TrackDataPtr[i], e_TrackInfos[i]);
            e_SeqheaderPtr->trackVol[i] = 100;
            if (g_GenOverflow != 0) {
                error = -9;
                return error;
            }
        }
    }
    if (esr->numToMerge > 0) {
        tei.eventMask = 383;
        tei.maxKeyVal = 0x7f;
        tei.minKeyVal = 0;
        tei.cntrlNum = -1;
        tei.seqTrackStartTick = 0;
        tei.seqTrackEndTick = 0xffffff;
        tei.srcPtr1 = esr->workBuf1;
        for (i = 1; i <= 31; i++) {
            j = esr->trackChannels[i] & esr->mergedChannels;
            if (esr->trackDataLen[i] > 4 && j != 0) {
                k = 0;
                while ((j & 1) == 0) {
                    k++;
                    j >>= 1;
                }
                tei.chanFilter = k;
                if (esr->o_TrackDataLen[i] > esr->workBufLen2) {
                    error = -9;
                    return error;
                }
                BlockMove(e_TrackDataPtr[i], esr->workBuf2, esr->o_TrackDataLen[i]);
                tei.srcPtr2 = esr->workBuf2;
                tei.destPtr = e_TrackDataPtr[i];
                tei.oLimit = esr->trackDataLen[i];
                Expand_MixTrack(&tei);
                e_TrackInfos[i]->trackDataLen = tei.trackLen;
                if (g_GenOverflow != 0) {
                    error = -9;
                    return error;
                }
                if (tei.trackLen > esr->trackDataLen[i]) {
                    error = -9;
                    return error;
                }
            }
        }
    }
    for (i = 1; i <= 31; i++) {
        e_TrackInfos[i]->trackChannels = esr->trackChannels_I[i];
    }
    if (esr->channelDup != 0) {
        tei.chanFilter = -1;
        tei.maxKeyVal = 0x7f;
        tei.minKeyVal = 0;
        tei.cntrlNum = -1;
        tei.seqTrackStartTick = 0;
        tei.seqTrackEndTick = 0xffffff;
        for (j = 0; j <= 15; j++) {
            if (esr->channelUsage[j] > 1) {
                firstPass = 1;
                for (i = 1; i <= 31; i++) {
                    if (esr->trackDataLen[i] > 4 && esr->trackChannels_I[i] == j) {
                        if (esr->trackDataLen[i] > esr->workBufLen2) {
                            error = -9;
                            return error;
                        }
                        BlockMove(e_TrackDataPtr[i], esr->workBuf2, esr->trackDataLen[i]);
                        tei.srcPtr1 = esr->workBuf2;
                        tei.srcPtr2 = e_TrackDataPtr[i];
                        tei.destPtr = esr->workBuf3;
                        tei.oLimit = esr->trackDataLen[i];
                        tei.oLimit1 = esr->workBufLen3;
                        tei.eventMask = 382;
                        Expand_CutTrack(&tei);
                        e_TrackInfos[i]->trackDataLen = tei.trackLen;
                        if (g_GenOverflow != 0) {
                            error = -9;
                            return error;
                        }
                        if (tei.trackLen > esr->trackDataLen[i]) {
                            error = -9;
                            return error;
                        }
                        if (tei.trackLen1 > esr->workBufLen3) {
                            error = -9;
                            return error;
                        }
                        if (firstPass != 0) {
                            if (tei.trackLen1 > esr->workBufLen1) {
                                error = -9;
                                return error;
                            }
                            BlockMove(esr->workBuf3, esr->workBuf1, tei.trackLen1);
                            buf1Size = tei.trackLen1;
                            firstPass = 0;
                        } else {
                            if (esr->workBufLen2 < buf1Size) {
                                error = -9;
                                return error;
                            }
                            BlockMove(esr->workBuf1, esr->workBuf2, buf1Size);
                            tei.srcPtr1 = esr->workBuf3;
                            tei.srcPtr2 = esr->workBuf2;
                            tei.destPtr = esr->workBuf1;
                            tei.oLimit = esr->workBufLen1;
                            tei.eventMask = 383;
                            Expand_MixTrack(&tei);
                            buf1Size = tei.trackLen;
                            if (g_GenOverflow != 0) {
                                error = -9;
                                return error;
                            }
                            if (tei.trackLen > esr->workBufLen1) {
                                error = -9;
                                return error;
                            }
                        }
                    }
                }
                tei.chanFilter = -1;
                tei.eventMask = 383;
                tei.maxKeyVal = 0x7f;
                tei.minKeyVal = 0;
                tei.cntrlNum = -1;
                tei.seqTrackStartTick = 0;
                tei.seqTrackEndTick = 0xffffff;
                tei.srcPtr1 = esr->workBuf1;
                for (i = 1; i <= 31; i++) {
                    if (esr->trackDataLen[i] > 4 && esr->trackChannels_I[i] == j) {
                        if (esr->trackDataLen[i] > esr->workBufLen2) {
                            error = -9;
                            return error;
                        }
                        BlockMove(e_TrackDataPtr[i], esr->workBuf2, esr->trackDataLen[i]);
                        tei.srcPtr2 = esr->workBuf2;
                        tei.destPtr = e_TrackDataPtr[i];
                        tei.oLimit = esr->trackDataLen[i];
                        Expand_MixTrack(&tei);
                        e_TrackInfos[i]->trackDataLen = tei.trackLen;
                        if (g_GenOverflow != 0) {
                            error = -9;
                            return error;
                        }
                        if (tei.trackLen > esr->trackDataLen[i]) {
                            error = -9;
                            return error;
                        }
                    }
                }
            }
        }
    }
    for (i = 1; i <= 31; i++) {
        if (e_TrackInfos[i]->trackDataLen > 4 && e_TrackInfos[i]->trackChannels == 9) {
            e_TrackInfos[i]->flags |= 2;
        }
    }
    for (i = 1; i <= 31; i++) {
        if (e_TrackInfos[i]->trackDataLen > 4) {
            if ((int8_t)e_TrackInfos[i]->trackName[0] == 0) {
                Expand_NameTrack(e_TrackInfos[i], i);
            }
        } else {
            Expand_ClearTrack(e_TrackDataPtr[i], e_TrackInfos[i]);
            e_SeqheaderPtr->trackVol[i] = 100;
            if (g_GenOverflow != 0) {
                error = -9;
                return error;
            }
        }
    }
    for (i = 1; i <= 31; i++) {
        if (e_TrackInfos[i]->trackDataLen > 4) {
            Expand_MatchChanToTrack(e_TrackDataPtr[i], i);
        }
    }
    return error;
}
