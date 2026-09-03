/* OrthToPhon.c -- VocalWriter's letter-to-sound front end.
 *
 * A word is looked up in the pronunciation dictionary (EnglishLex, read
 * through the File Manager: SearchDict binary-searches the file itself),
 * failing that decomposed into a stem and a suffix and looked up again
 * (DoMorph), failing that spelled out by the letter-to-sound rules
 * (EngToP, dorule). Lifted from the original's machine code like the rest;
 * see src/speech.c.
 */
#include "vw_engine.h"

/* The dictionary header as it lies in the file: 140 bytes, big-endian.
 * The original declared it with pointers (struct Dict) and read the file
 * straight over it; the port keeps the file's layout and byte order. */
typedef struct {
    uint32_t nextDict, version, type, wordCount;
    uint32_t hash[27];
    uint32_t words, index, flags, data[1];
} vw_DictFile;

/* OrthToPhon.c:33  (0xa080c) */
int16_t DecompressString(Ptr dest, unsigned char *src)
{
    int16_t strLen;
    int16_t bits;
    int16_t pass1;
    uint16_t w1;
    int16_t compBytes;

    w1 = *src;
    src++;
    compBytes = 1;
    strLen = ((uint32_t)w1 >> 3) + 1;
    w1 <<= 5;
    bits = 5;
    pass1 = 1;
    for (;;) {
        if (bits <= 8) {
            if (pass1 == 0) {
                VW_ST16BE(dest, w1 + 15872);
                if ((int8_t)*dest <= 64) {
                    if ((int8_t)*dest == 62) {
                        (*dest) = 45;
                    } else if ((int8_t)*dest == 63) {
                        (*dest) = 46;
                    } else {
                        (*dest) = 39;
                    }
                }
            } else {
                VW_ST16BE(dest, w1);
                pass1 = 0;
            }
            dest++;
            strLen--;
            if (strLen == 0) {
                return compBytes;
            }
            w1 = (uint8_t)w1;
            w1 <<= 5;
            bits += 5;
            continue;
        }
        bits -= 8;
        w1 = (*src << bits) | w1;
        src++;
        compBytes++;
        continue;
    }
    return compBytes;
}

/* OrthToPhon.c:92  (0xa0a14) */
static int16_t SearchDict(synthVarsPtr xx, unsigned char *text, FETokenPtr tok, int16_t saveIt)
{
    int32_t index;
    int32_t hi;
    int32_t lo;
    int32_t testKey;
    unsigned char *textTargPtr;
    unsigned char *phonOutPtr;
    unsigned char *dictPtr;
    uint16_t tLen;
    uint16_t dLen;
    int16_t len;
    int16_t diff;
    int16_t i;
    unsigned char decompText[33];
    int16_t compBytes;
    unsigned char phon;
    int16_t stillMore;
    int16_t fErr;
    int32_t entryIndex;
    unsigned char entryString[64];
    vw_DictFile dictHeader;
    int32_t inOutCount;
    int32_t t_168;
    int16_t t_16c;

    fErr = SetFPos(xx->dictFile, 1, 0);
    inOutCount = 140;
    fErr = FSRead(xx->dictFile, &inOutCount, &dictHeader);
    if ((uint32_t)text[1] > 64 && (uint32_t)text[1] <= 90) {
        diff = text[1] - 65;
        lo = VW_BE32(dictHeader.hash[diff]);
        hi = VW_BE32(dictHeader.hash[diff + 1]) - 1;
        index = VW_BE32(dictHeader.index);
        tLen = *text;
        while (lo <= hi) {
            testKey = (hi + lo) >> 1;
            fErr = SetFPos(xx->dictFile, 1, 0);
            inOutCount = 140;
            fErr = FSRead(xx->dictFile, &inOutCount, &dictHeader);
            fErr = SetFPos(xx->dictFile, 1, (testKey << 2) + index);
            inOutCount = 4;
            fErr = FSRead(xx->dictFile, &inOutCount, &entryIndex);
            entryIndex = VW_BE32(entryIndex);
            fErr = SetFPos(xx->dictFile, 1, entryIndex);
            inOutCount = 64;
            fErr = FSRead(xx->dictFile, &inOutCount, &entryString[0]);
            compBytes = DecompressString(&decompText[0], &entryString[0]);
            dLen = decompText[0];
            dictPtr = &decompText[1];
            textTargPtr = &text[1];
            if ((uint32_t)tLen < (uint32_t)dLen) {
                t_16c = (int16_t)tLen - 1;
            } else {
                t_16c = (int16_t)dLen - 1;
            }
            for (len = t_16c; len >= 0; len--) {
                diff = *textTargPtr - *dictPtr;
                textTargPtr++;
                dictPtr++;
                if ((uint32_t)-(uint16_t)diff >> 31 != 0) {
                    break;
                }
            }
            if (diff == 0) {
                diff = tLen - dLen;
            }
            if (diff > 0) {
                lo = testKey + 1;
            } else if (diff < 0) {
                hi = testKey - 1;
            } else {
                if (saveIt != 0) {
                    dictPtr = &entryString[compBytes];
                    phonOutPtr = &tok->phonStr[1];
                    len = 0;
                    stillMore = 1;
                    while (stillMore != 0) {
                        len++;
                        phon = *dictPtr;
                        dictPtr++;
                        if ((int8_t)phon < 0) {
                            phon &= 0x7f;
                            stillMore = 0;
                        }
                        if ((((uint32_t)phon >> 6) & 1) != 0) {
                            (*phonOutPtr) = 60;
                            phonOutPtr++;
                            len++;
                            phon &= 0xffffffbf;
                        }
                        (*phonOutPtr) = phon;
                        phonOutPtr++;
                    }
                    tok->phonStr[0] = len;
                    if (*dictPtr == 0) {
                        dictPtr++;
                        phonOutPtr = &tok->phonHold[1];
                        len = 0;
                        stillMore = 1;
                        while (stillMore != 0) {
                            len++;
                            phon = *dictPtr;
                            dictPtr++;
                            if ((int8_t)phon < 0) {
                                phon &= 0x7f;
                                stillMore = 0;
                            }
                            if ((((uint32_t)phon >> 6) & 1) != 0) {
                                (*phonOutPtr) = 60;
                                phonOutPtr++;
                                len++;
                                phon &= 0xffffffbf;
                            }
                            (*phonOutPtr) = phon;
                            phonOutPtr++;
                        }
                        tok->phonHold[0] = len;
                        tok->hasAlt = 1;
                    } else {
                        tok->hasAlt = 0;
                    }
                }
                t_168 = 1;
                return (int16_t)t_168;
            }
        }
    }
    t_168 = 0;
    return (int16_t)t_168;
}

/* OrthToPhon.c:274  (0xa1078) */
void AddPhon(FETokenPtr tok, int16_t phon)
{
    int16_t phonLen;

    phonLen = tok->phonStr[0] + 1;
    tok->phonStr[phonLen] = phon;
    tok->phonStr[0] = phonLen;
    if (tok->hasAlt == 0) {
        return;
    }
    phonLen = tok->phonHold[0] + 1;
    tok->phonHold[phonLen] = phon;
    tok->phonHold[0] = phonLen;
}

/* OrthToPhon.c:294  (0xa114c) */
void Store_S_or_Z(synthVarsPtr xx, FETokenPtr tok)
{
    int16_t phonLen;
    unsigned char lastPhon;
    int32_t phonFlags;

    phonLen = tok->phonStr[0];
    lastPhon = tok->phonStr[phonLen];
    phonFlags = xx->phonFlags2[lastPhon];
    if ((((uint32_t)phonFlags >> 17) & 1) != 0 || lastPhon == 40 || lastPhon == 41) {
        AddPhon(tok, 22);
        AddPhon(tok, 41);
        return;
    }
    if (((((uint32_t)phonFlags >> 1) & 1) ^ 1) == 0 && (((uint32_t)phonFlags >> 2) & 1) == 0) {
        AddPhon(tok, 40);
        return;
    }
    AddPhon(tok, 41);
}

/* OrthToPhon.c:330  (0xa1270) */
void Consonant_Doubling_Adjust(unsigned char *wordPtr)
{
    int16_t len;
    unsigned char endChar;

    len = *wordPtr;
    endChar = wordPtr[len];
    if ((uint32_t)(endChar - 65) <= 20) {
        if (((1 << (endChar - 65)) & 1329457) != 0) {
            return;
        }
    }
    len--;
    if (len <= 0) {
        return;
    }
    if (endChar != wordPtr[len]) {
        return;
    }
    (*wordPtr) = len;
}

/* OrthToPhon.c:369  (0xa1360) */
int16_t Decompose_E_Common(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t wordLen;
    int16_t gotMorph;
    unsigned char charSave;

    wordPtr = (unsigned char *)tok;
    wordLen = *wordPtr;
    gotMorph = 0;
    wordLen++;
    (*wordPtr) = wordLen;
    charSave = wordPtr[wordLen];
    wordPtr[wordLen] = 69;
    if (SearchDict(xx, wordPtr, tok, 1) != 0) {
        wordPtr[wordLen] = charSave;
        wordLen--;
        (*wordPtr) = wordLen;
        gotMorph = 1;
    } else {
        wordPtr[wordLen] = charSave;
        wordLen--;
        (*wordPtr) = wordLen;
        Consonant_Doubling_Adjust(wordPtr);
        if (SearchDict(xx, wordPtr, tok, 1) != 0) {
            gotMorph = 1;
        }
    }
    (*wordPtr) = wordLen;
    return gotMorph;
}

/* OrthToPhon.c:415  (0xa1500) */
int16_t Decompose_I_Common(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t wordLen;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    wordLen = *wordPtr + 1;
    gotMorph = 0;
    wordPtr[wordLen] = 89;
    (*wordPtr) = wordLen;
    if (SearchDict(xx, wordPtr, tok, 1) != 0) {
        gotMorph = 1;
    }
    wordPtr[wordLen] = 73;
    return gotMorph;
}

/* OrthToPhon.c:442  (0xa15e8) */
int16_t Do_IZING_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 11);
    AddPhon(tok, 41);
    AddPhon(tok, 1);
    AddPhon(tok, 35);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:468  (0xa16a0) */
int16_t Do_IZINGS_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 11);
    AddPhon(tok, 41);
    AddPhon(tok, 1);
    AddPhon(tok, 35);
    AddPhon(tok, 41);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:495  (0xa1764) */
int16_t Do_IZER_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 11);
    AddPhon(tok, 41);
    AddPhon(tok, 9);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:520  (0xa1810) */
int16_t Do_IZERS_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 11);
    AddPhon(tok, 41);
    AddPhon(tok, 9);
    AddPhon(tok, 41);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:548  (0xa18c8) */
int16_t Do_IZE_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    gotMorph = 1;
    AddPhon(tok, 11);
    AddPhon(tok, 41);
    return gotMorph;
}

/* OrthToPhon.c:573  (0xa1968) */
int16_t Do_IZED_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 11);
    AddPhon(tok, 41);
    AddPhon(tok, 47);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:598  (0xa1a14) */
int16_t Do_IZES_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 11);
    AddPhon(tok, 41);
    AddPhon(tok, 22);
    AddPhon(tok, 41);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:625  (0xa1acc) */
int16_t Do_INESS_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    int16_t wordLen;
    int16_t gotMorph;
    unsigned char *wordPtr;

    wordPtr = (unsigned char *)tok;
    wordLen = *wordPtr + 1;
    gotMorph = 0;
    wordPtr[wordLen] = 89;
    (*wordPtr) = wordLen;
    if (SearchDict(xx, wordPtr, tok, 1) != 0) {
        AddPhon(tok, 34);
        AddPhon(tok, 22);
        AddPhon(tok, 40);
        gotMorph = 1;
    }
    wordPtr[wordLen] = 73;
    wordLen--;
    (*wordPtr) = wordLen;
    if (gotMorph != 0) {
        return gotMorph;
    }
    if (wordPtr[wordLen] != 76) {
        return gotMorph;
    }
    (*wordPtr)--;
    if (SearchDict(xx, wordPtr, tok, 1) != 0) {
        AddPhon(tok, 31);
        AddPhon(tok, 0);
        AddPhon(tok, 34);
        AddPhon(tok, 22);
        AddPhon(tok, 40);
        gotMorph = 1;
    }
    (*wordPtr)++;
    return gotMorph;
}

/* OrthToPhon.c:674  (0xa1cd4) */
int16_t Do_INESSES_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t wordLen;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    wordLen = *wordPtr + 1;
    gotMorph = 0;
    wordPtr[wordLen] = 89;
    (*wordPtr) = wordLen;
    if (SearchDict(xx, wordPtr, tok, 1) != 0) {
        AddPhon(tok, 34);
        AddPhon(tok, 22);
        AddPhon(tok, 40);
        AddPhon(tok, 22);
        AddPhon(tok, 41);
        gotMorph = 1;
    }
    wordPtr[wordLen] = 73;
    wordLen--;
    (*wordPtr) = wordLen;
    if (gotMorph != 0) {
        return gotMorph;
    }
    if (wordPtr[wordLen] != 76) {
        return gotMorph;
    }
    (*wordPtr)--;
    if (SearchDict(xx, wordPtr, tok, 1) != 0) {
        AddPhon(tok, 31);
        AddPhon(tok, 0);
        AddPhon(tok, 34);
        AddPhon(tok, 22);
        AddPhon(tok, 40);
        AddPhon(tok, 22);
        AddPhon(tok, 40);
        gotMorph = 1;
    }
    (*wordPtr)++;
    return gotMorph;
}

/* OrthToPhon.c:729  (0xa1f0c) */
int16_t Do_NESS_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 34);
    AddPhon(tok, 22);
    AddPhon(tok, 40);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:756  (0xa1fb8) */
int16_t Do_NESSES_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 34);
    AddPhon(tok, 22);
    AddPhon(tok, 40);
    AddPhon(tok, 22);
    AddPhon(tok, 41);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:787  (0xa207c) */
int16_t Do_ISM_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 22);
    AddPhon(tok, 41);
    AddPhon(tok, 8);
    AddPhon(tok, 33);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:813  (0xa2134) */
int16_t Do_ISMS_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 22);
    AddPhon(tok, 41);
    AddPhon(tok, 8);
    AddPhon(tok, 33);
    AddPhon(tok, 41);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:843  (0xa21f8) */
int16_t Do_OR_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t wordLen;
    int16_t gotMorph;
    unsigned char charSave;

    wordPtr = (unsigned char *)tok;
    wordLen = *wordPtr;
    gotMorph = 0;
    wordLen++;
    (*wordPtr) = wordLen;
    charSave = wordPtr[wordLen];
    wordPtr[wordLen] = 69;
    if (SearchDict(xx, wordPtr, tok, 1) != 0) {
        wordPtr[wordLen] = charSave;
        wordLen--;
        (*wordPtr) = wordLen;
        AddPhon(tok, 9);
        gotMorph = 1;
        return gotMorph;
    }
    wordPtr[wordLen] = charSave;
    wordLen--;
    (*wordPtr) = wordLen;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    gotMorph = 1;
    AddPhon(tok, 9);
    return gotMorph;
}

/* OrthToPhon.c:889  (0xa2398) */
int16_t Do_ORS_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t wordLen;
    int16_t gotMorph;
    unsigned char charSave;

    wordPtr = (unsigned char *)tok;
    wordLen = *wordPtr;
    gotMorph = 0;
    wordLen++;
    (*wordPtr) = wordLen;
    charSave = wordPtr[wordLen];
    wordPtr[wordLen] = 69;
    if (SearchDict(xx, wordPtr, tok, 1) != 0) {
        wordPtr[wordLen] = charSave;
        wordLen--;
        (*wordPtr) = wordLen;
        AddPhon(tok, 9);
        AddPhon(tok, 41);
        gotMorph = 1;
        return gotMorph;
    }
    wordPtr[wordLen] = charSave;
    wordLen--;
    (*wordPtr) = wordLen;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    gotMorph = 1;
    AddPhon(tok, 9);
    AddPhon(tok, 41);
    return gotMorph;
}

/* OrthToPhon.c:938  (0xa2550) */
int16_t Do_BLY_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t wordLen;
    int16_t gotMorph;
    unsigned char origLen;

    wordPtr = (unsigned char *)tok;
    origLen = *wordPtr;
    wordLen = origLen + 3;
    gotMorph = 0;
    (*wordPtr) = wordLen;
    wordPtr[wordLen] = 69;
    if (SearchDict(xx, wordPtr, tok, 1) != 0) {
        gotMorph = 1;
        AddPhon(tok, 31);
        AddPhon(tok, 0);
    }
    wordPtr[wordLen] = 89;
    (*wordPtr) = origLen;
    return gotMorph;
}

/* OrthToPhon.c:969  (0xa2660) */
int16_t Do_CALLY_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    int16_t gotMorph;
    unsigned char *wordPtr;

    wordPtr = (unsigned char *)tok;
    (*wordPtr)++;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 31);
    AddPhon(tok, 0);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:992  (0xa2724) */
int16_t Do_LY_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 31);
    AddPhon(tok, 0);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:1018  (0xa27c4) */
void Do_ED_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    int16_t phonLen;
    unsigned char lastPhon;
    int32_t phonFlags;

    phonLen = tok->phonStr[0];
    lastPhon = tok->phonStr[phonLen];
    phonFlags = xx->phonFlags2[lastPhon];
    if (lastPhon == 46 || lastPhon == 47) {
        AddPhon(tok, 22);
        AddPhon(tok, 47);
        return;
    }
    if (((((uint32_t)phonFlags >> 1) & 1) ^ 1) == 0 && (((uint32_t)phonFlags >> 2) & 1) == 0) {
        AddPhon(tok, 46);
        return;
    }
    AddPhon(tok, 47);
}

/* OrthToPhon.c:1053  (0xa28d4) */
void Do_ER_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    AddPhon(tok, 9);
}

/* OrthToPhon.c:1059  (0xa2924) */
void Do_ERS_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    AddPhon(tok, 9);
    AddPhon(tok, 41);
}

/* OrthToPhon.c:1066  (0xa2980) */
void Do_EST_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    AddPhon(tok, 22);
    AddPhon(tok, 40);
    AddPhon(tok, 46);
}

/* OrthToPhon.c:1074  (0xa29e8) */
void Do_IED_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    AddPhon(tok, 47);
}

/* OrthToPhon.c:1080  (0xa2a38) */
void Do_IER_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    AddPhon(tok, 9);
}

/* OrthToPhon.c:1086  (0xa2a88) */
void Do_IERS_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    AddPhon(tok, 9);
    AddPhon(tok, 41);
}

/* OrthToPhon.c:1093  (0xa2ae4) */
int16_t Do_IEST_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;
    int16_t wordLen;

    wordPtr = (unsigned char *)tok;
    wordLen = *wordPtr;
    gotMorph = 0;
    if (Decompose_I_Common(xx, tok) != 0) {
        AddPhon(tok, 22);
        AddPhon(tok, 40);
        AddPhon(tok, 46);
        gotMorph = 1;
    }
    (*wordPtr) = wordLen;
    if (gotMorph != 0) {
        return gotMorph;
    }
    if (wordPtr[wordLen] != 76) {
        return gotMorph;
    }
    (*wordPtr)--;
    if (SearchDict(xx, wordPtr, tok, 1) != 0) {
        AddPhon(tok, 31);
        AddPhon(tok, 0);
        AddPhon(tok, 22);
        AddPhon(tok, 40);
        AddPhon(tok, 46);
        gotMorph = 1;
    }
    (*wordPtr)++;
    return gotMorph;
}

/* OrthToPhon.c:1137  (0xa2c88) */
void Do_ING_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    AddPhon(tok, 22);
    AddPhon(tok, 35);
}

/* OrthToPhon.c:1144  (0xa2ce4) */
void Do_INGS_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    AddPhon(tok, 22);
    AddPhon(tok, 35);
    AddPhon(tok, 41);
}

/* OrthToPhon.c:1152  (0xa2d4c) */
void Do_IMENT_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    AddPhon(tok, 33);
    AddPhon(tok, 8);
    AddPhon(tok, 34);
    AddPhon(tok, 46);
}

/* OrthToPhon.c:1161  (0xa2dc0) */
void Do_IMENTS_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    AddPhon(tok, 33);
    AddPhon(tok, 8);
    AddPhon(tok, 34);
    AddPhon(tok, 46);
    AddPhon(tok, 40);
}

/* OrthToPhon.c:1172  (0xa2e40) */
void Do_ABLE_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    AddPhon(tok, 8);
    AddPhon(tok, 45);
    AddPhon(tok, 26);
}

/* OrthToPhon.c:1186  (0xa2ea8) */
int16_t Do_MENT_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 33);
    AddPhon(tok, 8);
    AddPhon(tok, 34);
    AddPhon(tok, 46);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:1215  (0xa2f60) */
int16_t Do_MENTS_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 33);
    AddPhon(tok, 8);
    AddPhon(tok, 34);
    AddPhon(tok, 46);
    AddPhon(tok, 40);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:1247  (0xa3024) */
int16_t Do_IES_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t wordLen;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    wordLen = *wordPtr + 1;
    gotMorph = 0;
    wordPtr[wordLen] = 89;
    (*wordPtr) = wordLen;
    if (SearchDict(xx, wordPtr, tok, 1) != 0) {
        wordPtr[wordLen] = 73;
        Store_S_or_Z(xx, tok);
        gotMorph = 1;
        return gotMorph;
    }
    wordPtr[wordLen] = 73;
    wordLen++;
    (*wordPtr) = wordLen;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    Store_S_or_Z(xx, tok);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:1291  (0xa3188) */
int16_t Do_ES_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t wordLen;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    wordLen = *wordPtr;
    gotMorph = 0;
    if (wordPtr[wordLen] == 72 && (wordPtr[wordLen - 1] == 83 || wordPtr[wordLen - 1] == 67)) {
        if (SearchDict(xx, wordPtr, tok, 1) == 0) {
            return gotMorph;
        }
        Store_S_or_Z(xx, tok);
        gotMorph = 1;
        return gotMorph;
    }
    if (wordPtr[wordLen] == 83 && wordPtr[wordLen - 1] == 83) {
        if (SearchDict(xx, wordPtr, tok, 1) == 0) {
            return gotMorph;
        }
        Store_S_or_Z(xx, tok);
        gotMorph = 1;
        return gotMorph;
    }
    if (wordPtr[wordLen] == 88) {
        if (SearchDict(xx, wordPtr, tok, 1) == 0) {
            return gotMorph;
        }
        Store_S_or_Z(xx, tok);
        gotMorph = 1;
        return gotMorph;
    }
    wordLen++;
    (*wordPtr) = wordLen;
    if (SearchDict(xx, wordPtr, tok, 1) != 0) {
        Store_S_or_Z(xx, tok);
        gotMorph = 1;
        return gotMorph;
    }
    wordLen--;
    (*wordPtr) = wordLen;
    if (wordPtr[wordLen] != 83) {
        if (wordPtr[wordLen] != 90) {
            return gotMorph;
        }
    }
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    Store_S_or_Z(xx, tok);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:1376  (0xa3470) */
int16_t Do_S_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    Store_S_or_Z(xx, tok);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:1396  (0xa3504) */
void Do_IN_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    AddPhon(tok, 22);
    AddPhon(tok, 34);
}

/* OrthToPhon.c:1404  (0xa3560) */
void Do_INS_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    AddPhon(tok, 27);
    AddPhon(tok, 41);
}

/* OrthToPhon.c:1415  (0xa35bc) */
int16_t Do_IZIN_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 11);
    AddPhon(tok, 41);
    AddPhon(tok, 27);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:1440  (0xa3668) */
int16_t Do_IZINS_Morph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;

    wordPtr = (unsigned char *)tok;
    gotMorph = 0;
    if (SearchDict(xx, wordPtr, tok, 1) == 0) {
        return gotMorph;
    }
    AddPhon(tok, 11);
    AddPhon(tok, 41);
    AddPhon(tok, 27);
    AddPhon(tok, 41);
    gotMorph = 1;
    return gotMorph;
}

/* OrthToPhon.c:1468  (0xa3720) */
int16_t Search_Suffix(synthVarsPtr xx, unsigned char *wordPtr)
{
    int16_t index;
    int16_t len;
    int16_t origLen;
    int16_t ret;
    unsigned char *sPtr;

    sPtr = xx->SuffixTab;
    index = 0;
    origLen = *wordPtr;
    len = origLen;
    ret = 0;
    if (wordPtr[len] == 39) {
        len--;
        origLen--;
    }
    while (*sPtr != 0xff) {
        if (*sPtr == wordPtr[len] && len > 1) {
            sPtr++;
            len--;
            if (*sPtr != 0) {
                continue;
            }
            (*wordPtr) = len;
            ret = xx->SuffixType[index];
            return ret;
        }
        while (*sPtr != 0) {
            sPtr++;
        }
        sPtr++;
        index++;
        len = origLen;
    }
    return ret;
}

/* OrthToPhon.c:1514  (0xa38c8) */
int16_t DoMorph(synthVarsPtr xx, FETokenPtr tok)
{
    unsigned char *wordPtr;
    int16_t gotMorph;
    int16_t sufType;

    wordPtr = (unsigned char *)tok;
    tok->tokLen = *wordPtr;
    gotMorph = 0;
    tok->suffix = -1;
    if (wordPtr[*wordPtr] == 83) {
        (*wordPtr)--;
        if (Do_S_Morph(xx, tok) != 0) {
            gotMorph = 1;
            sufType = 40;
            goto L_a4a18;
        }
        (*wordPtr)++;
    }
    sufType = Search_Suffix(xx, wordPtr);
    if (sufType <= 0) {
        goto L_a4a24;
    }
    if ((uint32_t)sufType <= 40) {
        switch (sufType) {
        case 1:
            (*wordPtr) = tok->tokLen - 3;
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_ING_Morph(xx, tok);
                sufType = 15;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 5;
                if (Do_IZING_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 2:
            (*wordPtr) = tok->tokLen - 4;
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_INGS_Morph(xx, tok);
                sufType = 14;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 6;
                if (Do_IZINGS_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 4:
            (*wordPtr) = tok->tokLen - 2;
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_ER_Morph(xx, tok);
                sufType = 12;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 4;
                if (Do_IZER_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 5:
            (*wordPtr) = tok->tokLen - 3;
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_ERS_Morph(xx, tok);
                sufType = 11;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 5;
                if (Do_IZERS_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 40:
            if (Do_S_Morph(xx, tok) != 0) {
                gotMorph = 1;
            }
            break;
        case 39:
            (*wordPtr) = tok->tokLen - 1;
            if (Do_S_Morph(xx, tok) != 0) {
                sufType = 40;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 2;
                if (Do_ES_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 6:
            (*wordPtr) = tok->tokLen - 1;
            if (Do_S_Morph(xx, tok) != 0) {
                sufType = 40;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 3;
                if (Do_IES_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 38:
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_ED_Morph(xx, tok);
                gotMorph = 1;
            }
            break;
        case 12:
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_ER_Morph(xx, tok);
                gotMorph = 1;
            }
            break;
        case 11:
            (*wordPtr) = tok->tokLen - 1;
            if (Do_S_Morph(xx, tok) != 0) {
                sufType = 40;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 3;
                if (Decompose_E_Common(xx, tok) != 0) {
                    Do_ERS_Morph(xx, tok);
                    gotMorph = 1;
                }
            }
            break;
        case 13:
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_EST_Morph(xx, tok);
                gotMorph = 1;
            }
            break;
        case 9:
            if (Decompose_I_Common(xx, tok) != 0) {
                Do_IED_Morph(xx, tok);
                gotMorph = 1;
            }
            break;
        case 8:
            if (Decompose_I_Common(xx, tok) != 0) {
                Do_IER_Morph(xx, tok);
                gotMorph = 1;
            }
            break;
        case 7:
            (*wordPtr) = tok->tokLen - 1;
            if (Do_S_Morph(xx, tok) != 0) {
                sufType = 40;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 4;
                if (Decompose_I_Common(xx, tok) != 0) {
                    Do_IERS_Morph(xx, tok);
                    gotMorph = 1;
                }
            }
            break;
        case 10:
            if (Do_IEST_Morph(xx, tok) != 0) {
                gotMorph = 1;
            }
            break;
        case 15:
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_ING_Morph(xx, tok);
                gotMorph = 1;
            }
            break;
        case 14:
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_INGS_Morph(xx, tok);
                gotMorph = 1;
            }
            break;
        case 23:
            if (Do_MENT_Morph(xx, tok) != 0) {
                gotMorph = 1;
            }
            break;
        case 22:
            (*wordPtr) = tok->tokLen - 1;
            if (Do_S_Morph(xx, tok) != 0) {
                sufType = 40;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 5;
                if (Do_MENTS_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 21:
            if (Decompose_I_Common(xx, tok) != 0) {
                Do_IMENT_Morph(xx, tok);
                gotMorph = 1;
            }
            break;
        case 20:
            (*wordPtr) = tok->tokLen - 1;
            if (Do_S_Morph(xx, tok) != 0) {
                sufType = 40;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 6;
                if (Decompose_I_Common(xx, tok) != 0) {
                    Do_IMENTS_Morph(xx, tok);
                    gotMorph = 1;
                }
            }
            break;
        case 17:
            if (Do_BLY_Morph(xx, tok) != 0) {
                gotMorph = 1;
            } else {
                (*wordPtr)++;
                if (Do_LY_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 18:
            if (Do_CALLY_Morph(xx, tok) != 0) {
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 2;
                if (Do_LY_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 19:
            if (Do_LY_Morph(xx, tok) != 0) {
                gotMorph = 1;
            }
            break;
        case 25:
            if (Do_OR_Morph(xx, tok) != 0) {
                gotMorph = 1;
            }
            break;
        case 24:
            (*wordPtr) = tok->tokLen - 1;
            if (Do_S_Morph(xx, tok) != 0) {
                sufType = 40;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 3;
                if (Do_ORS_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 31:
            if (Do_IZE_Morph(xx, tok) != 0) {
                gotMorph = 1;
            }
            break;
        case 30:
            (*wordPtr) = tok->tokLen - 2;
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_ED_Morph(xx, tok);
                sufType = 38;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 4;
                if (Do_IZED_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 3:
            (*wordPtr) = tok->tokLen - 1;
            if (Do_S_Morph(xx, tok) != 0) {
                sufType = 40;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 4;
                if (Do_IZES_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 26:
            if (Do_INESS_Morph(xx, tok) != 0) {
                gotMorph = 1;
            }
            break;
        case 27:
            (*wordPtr) = tok->tokLen - 2;
            if (Do_ES_Morph(xx, tok) != 0) {
                sufType = 39;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 7;
                if (Do_INESSES_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 28:
            if (Do_NESS_Morph(xx, tok) != 0) {
                gotMorph = 1;
            }
            break;
        case 29:
            (*wordPtr) = tok->tokLen - 2;
            if (Do_ES_Morph(xx, tok) != 0) {
                sufType = 39;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 6;
                if (Do_NESSES_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 33:
            if (Do_ISM_Morph(xx, tok) != 0) {
                gotMorph = 1;
            }
            break;
        case 32:
            (*wordPtr) = tok->tokLen - 1;
            if (Do_S_Morph(xx, tok) != 0) {
                sufType = 40;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 4;
                if (Do_ISMS_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 16:
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_ABLE_Morph(xx, tok);
                gotMorph = 1;
            }
            break;
        case 34:
            (*wordPtr) = tok->tokLen - 2;
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_IN_Morph(xx, tok);
                sufType = 37;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 4;
                if (Do_IZIN_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 35:
            (*wordPtr) = tok->tokLen - 3;
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_INGS_Morph(xx, tok);
                sufType = 36;
                gotMorph = 1;
            } else {
                (*wordPtr) = tok->tokLen - 5;
                if (Do_IZINS_Morph(xx, tok) != 0) {
                    gotMorph = 1;
                }
            }
            break;
        case 36:
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_INS_Morph(xx, tok);
                gotMorph = 1;
            }
            break;
        case 37:
            if (Decompose_E_Common(xx, tok) != 0) {
                Do_IN_Morph(xx, tok);
                gotMorph = 1;
            }
            break;
        case 0:
            break;
        }
    }
L_a4a18:
    tok->suffix = sufType;
L_a4a24:
    (*wordPtr) = tok->tokLen;
    return gotMorph;
}

/* OrthToPhon.c:2054  (0xa4a5c) */
void EngToP(synthVarsPtr xx, char *inalpha, char *phon)
{
    unsigned char *scan_ptr;
    unsigned char *input_ptr;
    unsigned char *rule_ptr;
    unsigned char *nextrule;
    int16_t strLen;
    char *phonStr;

    phonStr = &phon[1];
    strLen = (int8_t)*inalpha;
    (*inalpha) = 32;
    input_ptr = &inalpha[1];
    while (*input_ptr != 32) {
        if (*input_ptr == 39 || *input_ptr == 46) {
            input_ptr++;
        } else {
            nextrule = &xx->rule[xx->hash[*input_ptr - 65]];
            do {
L_a4b3c:
                scan_ptr = input_ptr;
                rule_ptr = nextrule;
                nextrule = &rule_ptr[*rule_ptr];
                rule_ptr++;
L_a4b74:
                scan_ptr++;
                if (*scan_ptr == *rule_ptr) {
                    rule_ptr++;
                    goto L_a4b74;
                }
                if (*rule_ptr != 0xff) {
                    goto L_a4b3c;
                }
                rule_ptr++;
                xx->e_direction = -1;
                rule_ptr = dorule(xx, &input_ptr[-1], rule_ptr);
                if (rule_ptr == 0) {
                    goto L_a4b3c;
                }
                xx->e_direction = 1;
                rule_ptr = dorule(xx, scan_ptr, rule_ptr);
            } while (rule_ptr == 0);
            input_ptr = scan_ptr;
            while (*rule_ptr != 0xff) {
                (*phonStr) = *rule_ptr - 1;
                phonStr++;
                rule_ptr++;
            }
        }
    }
    (*inalpha) = strLen;
    (*phon) = (int8_t)(phonStr - phon - 1);
}

/* OrthToPhon.c:2132  (0xa4cf0) */
int16_t FindConsonant(synthVarsPtr xx, unsigned char **p_i_ptr)
{
    int32_t t_18;

    if ((xx->kind[**p_i_ptr] & 1) != 0) {
        (*p_i_ptr) += xx->e_direction;
        t_18 = 0;
        return (int16_t)t_18;
    }
    if (xx->e_direction == -1) {
        if (**p_i_ptr != 85 || (*p_i_ptr)[-1] != 71 && (*p_i_ptr)[-1] != 81) {
            goto L_a4e68;
        }
        (*p_i_ptr) -= 2;
        t_18 = 0;
        return (int16_t)t_18;
    }
    if ((**p_i_ptr == 81 || **p_i_ptr == 71) && (*p_i_ptr)[1] == 85) {
        (*p_i_ptr) += 2;
        t_18 = 0;
        return (int16_t)t_18;
    }
L_a4e68:
    t_18 = -1;
    return (int16_t)t_18;
}

/* OrthToPhon.c:2172  (0xa4e84) */
int16_t FindSibilant(synthVarsPtr xx, unsigned char **p_i_ptr)
{
    int32_t t_18;

    if ((((uint32_t)xx->kind[**p_i_ptr] >> 1) & 1) != 0) {
        (*p_i_ptr) += xx->e_direction;
        t_18 = 0;
        return (int16_t)t_18;
    }
    if (xx->e_direction == 1) {
        if (**p_i_ptr != 67 && **p_i_ptr != 83 || (*p_i_ptr)[1] != 72) {
            goto L_a4ffc;
        }
        (*p_i_ptr) += 2;
        t_18 = 0;
        return (int16_t)t_18;
    }
    if (**p_i_ptr == 72 && ((*p_i_ptr)[-1] == 67 || (*p_i_ptr)[-1] == 83)) {
        (*p_i_ptr) -= 2;
        t_18 = 0;
        return (int16_t)t_18;
    }
L_a4ffc:
    t_18 = -1;
    return (int16_t)t_18;
}

/* OrthToPhon.c:2212  (0xa5018) */
int16_t FindVowel(synthVarsPtr xx, unsigned char **p_i_ptr)
{
    int32_t t_18;

    if ((((uint32_t)xx->kind[**p_i_ptr] >> 4) & 1) != 0) {
        (*p_i_ptr) += xx->e_direction;
        t_18 = 0;
        return (int16_t)t_18;
    }
    t_18 = -1;
    return (int16_t)t_18;
}

/* OrthToPhon.c:2236  (0xa50bc) */
int16_t search_special(synthVarsPtr xx, unsigned char **p_i_ptr, unsigned char *sprule)
{
    unsigned char *i_ptr;
    int32_t t_28;

    i_ptr = *p_i_ptr;
    while (*sprule != 0) {
        if (*i_ptr != *sprule) {
            while (*sprule != 44) {
                sprule++;
            }
            sprule++;
            i_ptr = *p_i_ptr;
            continue;
        }
        goto L_a5158;
L_a5158:
        i_ptr += xx->e_direction;
        sprule++;
        if (*sprule == 44) {
            (*p_i_ptr) = i_ptr;
            t_28 = 0;
            return (int16_t)t_28;
        }
    }
    t_28 = -1;
    return (int16_t)t_28;
}

/* OrthToPhon.c:2312  (0xa51dc) */
unsigned char *dorule(synthVarsPtr xx, unsigned char *i_ptr, unsigned char *r_ptr)
{
    unsigned char *r_local;
    unsigned char *old_r_ptr2;
    unsigned char *old_r_ptr1;
    unsigned char *old_r_ptr;
    unsigned char *t_48;

    if (*r_ptr == 0xff) {
        t_48 = &r_ptr[1];
        return (unsigned char *)t_48;
    }
    while (*r_ptr != 0xff) {
        if (xx->kind[*r_ptr] == 128) {
            if ((uint32_t)(*r_ptr - 35) <= 87) {
                switch (*r_ptr) {
                case 42:
                    if (FindConsonant(xx, &i_ptr) != 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    while ((xx->kind[*i_ptr] & 1) != 0) {
                        r_local = dorule(xx, i_ptr, &r_ptr[1]);
                        if (r_local != 0) {
                            t_48 = r_local;
                            return (unsigned char *)t_48;
                        }
                        i_ptr += xx->e_direction;
                    }
                    break;
                case 36:
                    if (FindVowel(xx, &i_ptr) != 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    break;
                case 94:
                    if (FindConsonant(xx, &i_ptr) != 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    break;
                case 58:
                    FindConsonant(xx, &i_ptr);
                    while ((xx->kind[*i_ptr] & 1) != 0) {
                        r_local = dorule(xx, i_ptr, &r_ptr[1]);
                        if (r_local != 0) {
                            t_48 = r_local;
                            return (unsigned char *)t_48;
                        }
                        i_ptr += xx->e_direction;
                    }
                    break;
                case 43:
                    if ((((uint32_t)xx->kind[*i_ptr] >> 5) & 1) == 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    i_ptr += xx->e_direction;
                    break;
                case 118:
                    FindVowel(xx, &i_ptr);
                    while ((((uint32_t)xx->kind[*i_ptr] >> 4) & 1) != 0) {
                        r_local = dorule(xx, i_ptr, &r_ptr[1]);
                        if (r_local != 0) {
                            t_48 = r_local;
                            return (unsigned char *)t_48;
                        }
                        i_ptr += xx->e_direction;
                    }
                    break;
                case 108:
                    if (search_special(xx, &i_ptr, xx->lruletab) != 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    break;
                case 45:
                    if (search_special(xx, &i_ptr, xx->dashruletab) != 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    break;
                case 37:
                    if (search_special(xx, &i_ptr, xx->percentruletab) != 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    break;
                case 122:
                    if (search_special(xx, &i_ptr, xx->zruletab) != 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    break;
                case 98:
                    if (search_special(xx, &i_ptr, xx->bruletab) != 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    break;
                case 35:
                    if (FindVowel(xx, &i_ptr) != 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    while ((((uint32_t)xx->kind[*i_ptr] >> 4) & 1) != 0) {
                        r_local = dorule(xx, i_ptr, &r_ptr[1]);
                        if (r_local != 0) {
                            t_48 = r_local;
                            return (unsigned char *)t_48;
                        }
                        i_ptr += xx->e_direction;
                    }
                    break;
                case 46:
                    if ((((uint32_t)xx->kind[*i_ptr] >> 3) & 1) == 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    i_ptr += xx->e_direction;
                    break;
                case 38:
                    if (FindSibilant(xx, &i_ptr) != 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    break;
                case 64:
                    if (search_special(xx, &i_ptr, xx->atruletab) != 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    break;
                case 109:
                    if (search_special(xx, &i_ptr, xx->mruletab) != 0) {
                        t_48 = 0;
                        return (unsigned char *)t_48;
                    }
                    break;
                case 39:
                case 40:
                case 41:
                case 44:
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
                case 59:
                case 60:
                case 61:
                case 62:
                case 63:
                case 65:
                case 66:
                case 67:
                case 68:
                case 69:
                case 70:
                case 71:
                case 72:
                case 73:
                case 74:
                case 75:
                case 76:
                case 77:
                case 78:
                case 79:
                case 80:
                case 81:
                case 82:
                case 83:
                case 84:
                case 85:
                case 86:
                case 87:
                case 88:
                case 89:
                case 90:
                case 91:
                case 92:
                case 93:
                case 95:
                case 96:
                case 97:
                case 99:
                case 100:
                case 101:
                case 102:
                case 103:
                case 104:
                case 105:
                case 106:
                case 107:
                case 110:
                case 111:
                case 112:
                case 113:
                case 114:
                case 115:
                case 116:
                case 117:
                case 119:
                case 120:
                case 121:
                    break;
                }
            }
            r_ptr++;
        } else {
            old_r_ptr2 = r_ptr;
            r_ptr++;
            if ((uint32_t)-(*i_ptr ^ *old_r_ptr2) >> 31 != 0) {
                t_48 = 0;
                return (unsigned char *)t_48;
            }
            i_ptr += xx->e_direction;
        }
    }
    t_48 = &r_ptr[1];
    return t_48;
}

/* OrthToPhon.c:2430  (0xa5a04) */
static void InitToken(FETokenPtr tok)
{
    int16_t j;

    tok->tokStr[0] = 0;
    tok->phonStr[0] = 0;
    tok->phonHold[0] = 0;
    tok->hasAlt = 0;
    tok->altChoice = 0;
    tok->suffix = -1;
    tok->addFlags = 0;
    tok->inDict = 0;
    tok->inMorph = 0;
}

/* OrthToPhon.c:2451  (0xa5aa0) */
static void LookUp(synthVarsPtr xx, FETokenPtr wToken)
{
    int16_t i;
    Ptr strPtr;

    if (SearchDict(xx, (unsigned char *)wToken, wToken, 1) != 0) {
        wToken->inDict = 1;
        return;
    }
    if (DoMorph(xx, wToken) != 0) {
        wToken->inMorph = 1;
        return;
    }
    strPtr = &wToken->tokStr[wToken->tokStr[0] + 1];
    for (i = 3; i >= 0; i--) {
        (*strPtr) = 32;
        strPtr++;
    }
    wToken->tokStr[0] += 4;
    EngToP(xx, (char *)wToken, &wToken->phonStr[0]);
    wToken->tokStr[0] -= 4;
}

/* OrthToPhon.c:2485  (0xa5c04) */
int16_t OrthToPhon(synthVarsPtr xx, ConvertTextRecPtr tRec, int16_t dictFile)
{
    int16_t err;
    int16_t i;
    FEToken wToken;
    unsigned char ch;
    int16_t error;
    int32_t bSize;
    int32_t count;
    int32_t syllCount;
    int32_t syllNum;
    int32_t syllIndex[10];

    error = 0;
    xx->dictFile = dictFile;
    InitToken(&wToken);
    for (i = 0; i < tRec->textLen_Input; i++) {
        ch = tRec->text_Input[i];
        if ((uint32_t)ch > 96 && (uint32_t)ch <= 122) {
            ch -= 32;
        }
        wToken.tokStr[i + 1] = ch;
    }
    wToken.tokStr[0] = tRec->textLen_Input;
    LookUp(xx, &wToken);
    xx->destParseLen = wToken.phonStr[0] << 2;
    bSize = xx->destParseLen << 1;
    xx->phon_Buf_1 = (int16_t *)NewPtr(bSize);
    xx->phon_Ctrl_Buf_1 = (int16_t *)NewPtr(bSize);
    xx->phon_Buf_2 = (int16_t *)NewPtr(bSize);
    xx->phon_Ctrl_Buf_2 = (int16_t *)NewPtr(bSize);
    xx->srcParseBuf = (uint16_t *)NewPtr(bSize);
    bSize = 0;
    xx->srcParseBuf[bSize] = 62;
    bSize++;
    for (i = 1; i <= wToken.phonStr[0]; i++) {
        xx->srcParseBuf[bSize] = wToken.phonStr[i];
        bSize++;
    }
    xx->srcParseLen = bSize;
    TunePhons(xx);
    syllCount = 0;
    for (i = 0; i < xx->phonBuf_2_In_Index; i++) {
        if ((((uint32_t)(uint16_t)xx->phon_Ctrl_Buf_2[i] >> 13) & 1) != 0) {
            syllIndex[syllCount] = i;
            syllCount++;
        }
    }
    syllIndex[syllCount] = i;
    for (syllNum = 0; syllNum < syllCount; syllNum++) {
        count = 0;
        for (i = syllIndex[syllNum]; i < syllIndex[syllNum + 1]; i++) {
            ch = xx->phon_Buf_2[i];
            if ((uint32_t)ch <= 55) {
                if (ch == 22) {
                    ch = 1;
                }
                tRec->phon_Result[syllNum].syllStr[count] = ch;
                count++;
                if (count > 7) {
                    break;
                }
            }
        }
        tRec->phon_Result[syllNum].syllLen = count;
    }
    tRec->syllables_Result = syllCount;
    DisposePtr(xx->phon_Buf_1);
    DisposePtr(xx->phon_Ctrl_Buf_1);
    DisposePtr(xx->phon_Buf_2);
    DisposePtr(xx->phon_Ctrl_Buf_2);
    DisposePtr(xx->srcParseBuf);
    return error;
}
