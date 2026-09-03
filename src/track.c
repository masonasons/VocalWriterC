/* track.c -- reading a VocalWriter track's event records (Music.c).
 *
 * A record is 12 bytes, big-endian: a 24-bit time, a byte, the status, key
 * and velocity, and for notes a 24-bit duration and a 16-bit syllable index.
 * A time of 0xffffff ends the track. Lifted from the original; see
 * src/speech.c.
 */
#include "vw_engine.h"

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
