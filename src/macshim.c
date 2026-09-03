/* macshim.c -- the handful of Mac OS calls the engine's front end makes.
 *
 * The synthesiser proper needs nothing from the operating system; the text
 * front end (OrthToPhon.c, ParsePhons.c) uses the Memory Manager for its
 * working buffers and the File Manager to binary-search the pronunciation
 * dictionary on disk. These are the smallest faithful stand-ins:
 *
 *   NewPtr/DisposePtr           malloc/free (NewPtr does not clear)
 *   NewHandle/SetHandleSize/... a handle is a pointer to a malloc'd block
 *   SetFPos/FSRead              over a file the caller loaded into memory
 *
 * MemError reports the last SetHandleSize; nothing else here can fail.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vw_engine.h"

static int16_t last_mem_error;

/* Tests keep freed blocks alive to look at the buffers a call left behind. */
int vw_shim_defer_free;
/* Tests: fill what NewPtr returns with a byte (-1: leave it as malloc did). */
int vw_shim_fill = -1;
static void *deferred[256];
static int ndeferred;

/* Every block carries its size in front and guard bytes behind, so an
 * overrun -- the original's code trusts its own arithmetic -- is reported
 * when the block is freed rather than corrupting the heap silently. */
#define GUARD 64
#define GUARD_BYTE 0xa5
#define HEADER 16

int vw_shim_overruns;

static Ptr block_new(int32_t size, int clear)
{
    size_t n = size > 0 ? (size_t)size : 0;
    unsigned char *raw = (unsigned char *)malloc(HEADER + n + GUARD);
    if (raw == NULL)
        return NULL;
    memcpy(raw, &n, sizeof n);
    if (clear)
        memset(raw + HEADER, 0, n);
    else if (vw_shim_fill >= 0)
        memset(raw + HEADER, vw_shim_fill, n);
    memset(raw + HEADER + n, GUARD_BYTE, GUARD);
    return (Ptr)(raw + HEADER);
}

static void block_free(void *p)
{
    unsigned char *raw;
    size_t n, k, over = 0;
    if (p == NULL)
        return;
    raw = (unsigned char *)p - HEADER;
    memcpy(&n, raw, sizeof n);
    for (k = 0; k < GUARD; k++)
        if (raw[HEADER + n + k] != GUARD_BYTE)
            over = k + 1;
    if (over) {
        vw_shim_overruns++;
        fprintf(stderr, "vw: a %u-byte block was overrun by at least %u bytes\n",
                (unsigned)n, (unsigned)over);
    }
    free(raw);
}

Ptr NewPtr(int32_t size)
{
    return size < 0 ? NULL : block_new(size, 0);
}

Ptr NewPtrClear(int32_t size)
{
    return size < 0 ? NULL : block_new(size, 1);
}

void DisposePtr(void *p)
{
    if (vw_shim_defer_free && ndeferred < 256) {
        deferred[ndeferred++] = p;
        return;
    }
    block_free(p);
}

void vw_shim_flush_deferred(void)
{
    while (ndeferred > 0)
        block_free(deferred[--ndeferred]);
}

Handle NewHandle(int32_t size)
{
    Handle h = (Handle)malloc(sizeof(Ptr));
    if (h == NULL)
        return NULL;
    *h = (Ptr)calloc(1, size > 0 ? (size_t)size : 1);
    last_mem_error = *h == NULL ? -108 : 0;      /* memFullErr */
    return h;
}

void SetHandleSize(Handle h, int32_t size)
{
    Ptr p = (Ptr)realloc(*h, size > 0 ? (size_t)size : 1);
    if (p == NULL) {
        last_mem_error = -108;
        return;
    }
    *h = p;
    last_mem_error = 0;
}

void DisposeHandle(Handle h)
{
    if (h == NULL)
        return;
    free(*h);
    free(h);
}

void HLock(Handle h) { (void)h; }
void HUnlock(Handle h) { (void)h; }

int16_t MemError(void)
{
    return last_mem_error;
}

void DebugStr(const char *s) { (void)s; }

void BlockMove(const void *src, void *dst, int32_t n)
{
    if (n > 0)
        memmove(dst, src, (size_t)n);
}

/* -- the File Manager, over files in memory ----------------------------- */

#define VW_MAX_FILES 8

static struct {
    const unsigned char *data;
    size_t len;
    size_t pos;
} files[VW_MAX_FILES + 1];

int16_t vw_fs_open(const unsigned char *data, size_t len)
{
    int i;
    for (i = 1; i <= VW_MAX_FILES; i++) {
        if (files[i].data == NULL) {
            files[i].data = data;
            files[i].len = len;
            files[i].pos = 0;
            return (int16_t)i;
        }
    }
    return 0;
}

void vw_fs_close(int16_t refNum)
{
    if (refNum > 0 && refNum <= VW_MAX_FILES)
        files[refNum].data = NULL;
}

int16_t SetFPos(int16_t refNum, int16_t posMode, int32_t posOff)
{
    if (refNum <= 0 || refNum > VW_MAX_FILES || files[refNum].data == NULL)
        return -51;                              /* rfNumErr */
    switch (posMode) {
    case 1:  files[refNum].pos = (size_t)posOff; break;           /* fsFromStart */
    case 2:  files[refNum].pos = files[refNum].len + posOff; break; /* fsFromLEOF */
    case 3:  files[refNum].pos += posOff; break;                   /* fsFromMark */
    default: break;
    }
    return 0;
}

int16_t FSRead(int16_t refNum, int32_t *count, void *buf)
{
    size_t want, got;
    if (refNum <= 0 || refNum > VW_MAX_FILES || files[refNum].data == NULL)
        return -51;
    want = (size_t)*count;
    got = files[refNum].pos < files[refNum].len ? files[refNum].len - files[refNum].pos : 0;
    if (got > want)
        got = want;
    memcpy(buf, files[refNum].data + files[refNum].pos, got);
    files[refNum].pos += got;
    *count = (int32_t)got;
    return got == want ? 0 : -39;                /* eofErr */
}
