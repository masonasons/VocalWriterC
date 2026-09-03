/* synthglue.c -- what the sequencer's Macintosh.c glue asked of Mac OS.
 *
 * The lifted Synth_* code (src/synthapi.c) still makes the calls the
 * original made into the Sound Manager, the Time Manager, the Deferred
 * Task Manager and the Resource Manager. Rendering offline, none of those
 * has anything to do: there is no sound channel to feed (Synth_GetNextBuffer
 * hands the buffers over directly, which is how the application's own
 * "Play to Disk" export works), and the timers that delivered beat and
 * tempo callbacks a little after the fact deliver them at once.
 *
 * InitSynth is the one substantive replacement: the original pulled the
 * engine's data tables out of its own resource fork; here the caller
 * installs them with vw_load_ttvi first.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vw_engine.h"

int16_t g_instanceCount;

/* Macintosh.c:284 -- the shared tables come from the `ttvi` resource;
   vw_load_ttvi has already put it in g_DataHandle */
OSErr InitSynth(void)
{
    /* One more than the instances: Synth_ShutDown disposes the shared
       tables when the count reaches zero, and they are the port's own
       buffers (vw_load_ttvi), freed by vw_engine_close. */
    g_instanceCount = 1;
    g_Freq_Tbl = NULL;
    if (g_DataHandle == NULL || *g_DataHandle == NULL)
        return -192;                             /* resNotFound */
    InitSharedTables();
    return 0;
}

/* -- the 68k A5 world: nothing to switch ---------------------------------- */

uint32_t SetA5(uint32_t a5)
{
    return a5;
}

/* -- the clock: microseconds since start, advancing per call so the load
      figures the original derived stay finite ------------------------------ */

static uint32_t fake_clock;

void Microseconds(UnsignedWide *now)
{
    fake_clock += 1000;
    now->hi = 0;
    now->lo = fake_clock;
}

/* -- Time Manager: a primed task fires at once ----------------------------- */

int16_t InsTime(void *task)
{
    (void)task;
    return 0;
}

int16_t RmvTime(void *task)
{
    (void)task;
    return 0;
}

int16_t PrimeTime(void *task, int32_t count)
{
    TMTask *t = (TMTask *)task;
    (void)count;
    if (t->tmAddr != NULL)
        ((void (*)(TMTask *))t->tmAddr)(t);
    return 0;
}

/* -- Deferred Task Manager: nothing waits for an interrupt here ----------- */

int16_t DTInstall(void *task)
{
    DeferredTask *dt = (DeferredTask *)task;
    if (dt->dtAddr != NULL)
        ((void (*)(int32_t))dt->dtAddr)((int32_t)(intptr_t)dt->dtParam);
    return 0;
}

/* -- Sound Manager: no channel; a buffer command completes immediately ---- */

/* The channel is a record nothing plays through; it exists because the
   startup path frees its buffers twice if opening one fails -- a slip the
   original never reached. */
int16_t SndNewChannel(void *chan, int16_t synth, int32_t init, void *userRoutine)
{
    void **rec;
    (void)synth; (void)init; (void)userRoutine;
    rec = (void **)calloc(1, 1060);              /* sizeof(SndChannel) on the Mac */
    if (rec == NULL)
        return -108;
    /* the shell record owning the channel, for SndDoCommand below */
    rec[0] = (char *)chan - offsetof(shellVar, SndChan);
    *(void **)chan = rec;
    return 0;
}

int16_t SndDisposeChannel(void *chan, int16_t quietNow)
{
    (void)quietNow;
    free(chan);
    return 0;
}

int16_t SndDoImmediate(void *chan, void *cmd)
{
    (void)chan; (void)cmd;
    return 0;
}

/* A queued buffer command (SendBufCmd's callback request) completes at
   once: the Sound Manager would have called back when the buffer had
   played, and Synth_StopMusic waits for that count to reach zero. */
int16_t SndDoCommand(void *chan, void *cmd, int16_t noWait)
{
    shellVarPtr svv = chan ? (shellVarPtr)((void **)chan)[0] : NULL;
    (void)cmd; (void)noWait;
    if (svv != NULL && svv->soundCB_Count > 0)
        svv->soundCB_Count--;
    return 0;
}

/* Gestalt: the only selector asked is gestaltSoundAttr, for 16-bit output */
int16_t Gestalt(uint32_t selector, int32_t *response)
{
    (void)selector;
    *response = 1 << 7;                          /* gestalt16BitSoundIO */
    return 0;
}

/* universal procedure pointers are plain function pointers */
void *NewTimerUPP(void *proc) { return proc; }
void *NewSndCallBackUPP(void *proc) { return proc; }
void *NewDeferredTaskUPP(void *proc) { return proc; }

/* -- Resource Manager: InitSynth is replaced, so nothing asks for one ----- */

void *GetResource(uint32_t type, int16_t id)
{
    (void)type; (void)id;
    return NULL;
}

void DetachResource(void *h)
{
    (void)h;
}

/* -- Text Utilities: a number as a Pascal string --------------------------- */

void NumToString(int32_t n, unsigned char *str)
{
    char buf[16];
    int len = snprintf(buf, sizeof buf, "%d", (int)n);
    str[0] = (unsigned char)len;
    memcpy(str + 1, buf, (size_t)len);
}
