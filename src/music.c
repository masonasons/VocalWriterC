/* music.c -- the part of the sequencer the synthesiser depends on.
 *
 * The speech engine reads the tempo through synthVars.timeWarp, which
 * Music.c's SetTempo sets from the beats-per-minute the song asks for.
 */
#include "vw_engine.h"

/* Music.c:375 */
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
    if ((xx->seqFlags & 3) != 0 && ((uint32_t)xx->seqFlags >> 4 & 1) == 0) {
        xx->timeWarp_P = temp;
        return;
    }
    xx->timeWarp_P = 0.0f;
    xx->timeWarp = temp;
    xx->timeWarp_Inv = 1.0f / xx->timeWarp;
}
