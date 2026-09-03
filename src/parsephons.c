/* ParsePhons.c -- VocalWriter's phoneme parser.
 *
 * Turns the syllables attached to a vocal track (MakeSpeechData) or a
 * looked-up word (TunePhons) into the phoneme, control and duration
 * lists the synthesiser plays: closures and releases inserted, durations
 * fitted to the notes, syllable and word boundaries flagged. Lifted from
 * the original's machine code like the rest; see src/speech.c.
 */
#include "vw_engine.h"

/* ParsePhons.c:65  (0x79968) */
int16_t GetPhon_FE(synthVarsPtr xx, int16_t index)
{
    int16_t ret;

    if (index >= 0 && index < xx->phonBuf_2_In_Index) {
        ret = xx->phon_Buf_2[index];
        return ret;
    }
    ret = 23;
    return ret;
}

/* ParsePhons.c:82  (0x79a04) */
int16_t GetPhonCtrl_FE(synthVarsPtr xx, int16_t index)
{
    int16_t ret;

    if (index >= 0 && index < xx->phonBuf_2_In_Index) {
        ret = xx->phon_Ctrl_Buf_2[index];
        return ret;
    }
    ret = 0;
    return ret;
}

/* ParsePhons.c:101  (0x79aa0) */
void Insert_Closure_Release(synthVarsPtr xx)
{
    int16_t i;
    int16_t index;
    int16_t cur_Phon;
    int32_t cur_Flags;
    int16_t prev_Phon;
    int16_t next_Phon;
    int16_t src;
    int16_t dest;

    for (i = 0; i < xx->phonBuf_2_In_Index; i++) {
        cur_Phon = GetPhon_FE(xx, i);
        cur_Flags = xx->phonFlags2[cur_Phon];
        prev_Phon = GetPhon_FE(xx, (int16_t)((uint16_t)i - 1));
        next_Phon = GetPhon_FE(xx, (int16_t)((uint16_t)i + 1));
        if (next_Phon == 23 && (((uint32_t)cur_Flags >> 23) & 1) != 0 && xx->phonBuf_2_In_Index < xx->destParseLen) {
            for (index = xx->phonBuf_2_In_Index; index > i; index--) {
                src = index - 1;
                dest = index;
                xx->phon_Buf_2[dest] = xx->phon_Buf_2[src];
                xx->phon_Ctrl_Buf_2[dest] = xx->phon_Ctrl_Buf_2[src];
                xx->dur_Buf[dest] = xx->dur_Buf[src];
            }
            xx->phon_Ctrl_Buf_2[i + 1] = xx->phon_Ctrl_Buf_2[i] | 16384;
            i++;
            xx->phonBuf_2_In_Index++;
            if ((((uint32_t)xx->phonFlags2[prev_Phon] >> 21) & 1) != 0 || cur_Phon == 46 || cur_Phon == 47) {
                xx->phon_Buf_2[i] = 22;
            } else {
                xx->phon_Buf_2[i] = 8;
            }
            xx->dur_Buf[i] = 12;
        }
    }
}

/* ParsePhons.c:177  (0x79df0) */
void Mod_Duration_FE(synthVarsPtr xx)
{
    int16_t cur_Phon;
    int16_t cur_PhonCtrl;
    int32_t cur_PhonFlags;
    int16_t cur_SyllableType;
    int16_t cur_VowelFlag;
    int16_t cur_Stress;
    int16_t prev_Phon;
    int16_t prev_PhonCtrl;
    int32_t prev_PhonFlags;
    int16_t next_Phon;
    int16_t next_PhonCtrl;
    int32_t next_PhonFlags;
    int16_t next2_Phon;
    int16_t next2_PhonCtrl;
    int32_t next2_PhonFlags;
    int16_t i;
    int16_t maxDur;
    int16_t minDur;
    int16_t fixed_Duration;
    int16_t percent_Duration;
    int16_t eFlag;
    int16_t theObstr;
    int16_t vocFlag;
    int16_t dur_Hold;
    int32_t obstrFlags;
    int32_t num_1;
    int16_t total_Dur;
    int16_t firstPass;

    prev_Phon = 23;
    prev_PhonCtrl = 0;
    eFlag = 0;
    firstPass = 1;
    total_Dur = 0;
    (*xx->dur_Buf) = 1;
    for (i = 1; i < xx->phonBuf_2_In_Index; i++) {
        cur_Phon = GetPhon_FE(xx, i);
        cur_PhonCtrl = GetPhonCtrl_FE(xx, i);
        cur_SyllableType = cur_PhonCtrl & 3;
        cur_Stress = cur_PhonCtrl & 7168;
        cur_PhonFlags = xx->phonFlags2[cur_Phon];
        if ((cur_PhonFlags & 1) != 0) {
            cur_VowelFlag = 1;
        } else {
            cur_VowelFlag = 0;
        }
        prev_Phon = GetPhon_FE(xx, (int16_t)((uint16_t)i - 1));
        prev_PhonCtrl = GetPhonCtrl_FE(xx, (int16_t)((uint16_t)i - 1));
        prev_PhonFlags = xx->phonFlags2[prev_Phon];
        next_Phon = GetPhon_FE(xx, (int16_t)((uint16_t)i + 1));
        next_PhonCtrl = GetPhonCtrl_FE(xx, (int16_t)((uint16_t)i + 1));
        next_PhonFlags = xx->phonFlags2[next_Phon];
        next2_Phon = GetPhon_FE(xx, (int16_t)((uint16_t)i + 2));
        next2_PhonCtrl = GetPhonCtrl_FE(xx, (int16_t)((uint16_t)i + 2));
        next2_PhonFlags = xx->phonFlags2[next2_Phon];
        percent_Duration = 128;
        fixed_Duration = 0;
        maxDur = xx->maxDurTbl[cur_Phon];
        minDur = xx->minDurTbl[cur_Phon];
        if (cur_Phon == 23) {
            dur_Hold = 200;
            dur_Hold = (dur_Hold * xx->rate_Ratio) >> 16;
            if (dur_Hold <= 9) {
                dur_Hold = 10;
            }
        } else {
            if ((((uint32_t)(uint16_t)cur_SyllableType >> 1) & 1) != 0) {
                if ((((uint32_t)cur_PhonFlags >> 12) & 1) != 0) {
                    fixed_Duration = 0;
                } else if (((((uint32_t)cur_PhonFlags >> 2) & 1) ^ 1) == 0 && ((((uint32_t)cur_PhonFlags >> 27) & 1) ^ 1) == 0) {
                    fixed_Duration = 20;
                } else if (((((uint32_t)cur_PhonFlags >> 26) & 1) ^ 1) == 0 && ((((uint32_t)next_PhonFlags >> 10) & 1) ^ 1) == 0 && (((uint32_t)next_PhonFlags >> 2) & 1) == 0) {
                    fixed_Duration = 15;
                } else {
                    fixed_Duration = 40;
                }
                if ((((uint32_t)next_PhonFlags >> 4) & 1) != 0) {
                    fixed_Duration -= 20;
                }
                if (xx->phonBuf_2_In_Index <= 9 && cur_Stress != 0 && cur_VowelFlag != 0) {
                    fixed_Duration = (10 - xx->phonBuf_2_In_Index) * 5 + fixed_Duration;
                }
            }
            if (cur_VowelFlag != 0) {
                if (cur_SyllableType <= 1) {
                    percent_Duration = (percent_Duration * 39300) >> 16;
                }
                if ((cur_Stress & 5120) == 0 && (cur_PhonCtrl & 768) == 0) {
                    if ((((uint32_t)(uint16_t)cur_Stress >> 11) & 1) != 0) {
                        percent_Duration = (percent_Duration * 55675) >> 16;
                    } else {
                        percent_Duration = (percent_Duration * 36025) >> 16;
                    }
                } else if ((cur_PhonCtrl & 768) != 0 && cur_SyllableType <= 0 && (cur_Stress & 5120) == 0) {
                    if ((cur_PhonCtrl & 768) <= 256) {
                        percent_Duration = (percent_Duration * 55675) >> 16;
                    } else {
                        percent_Duration = (percent_Duration * 52400) >> 16;
                    }
                }
                if ((cur_PhonCtrl & 768) != 0) {
                    percent_Duration = (percent_Duration * 52400) >> 16;
                }
            }
            if (cur_VowelFlag == 0 && (((uint32_t)(uint16_t)cur_PhonCtrl >> 7) & 1) == 0) {
                if (((((uint32_t)cur_PhonFlags >> 27) & 1) ^ 1) == 0 && ((cur_SyllableType & 1) ^ 1) == 0) {
                    fixed_Duration += 20;
                } else {
                    percent_Duration = (percent_Duration * 55675) >> 16;
                }
            }
            if ((cur_Stress & 5120) == 0) {
                if ((((uint32_t)cur_PhonFlags >> 10) & 1) == 0 && (((uint32_t)cur_PhonFlags >> 20) & 1) == 0) {
                    minDur -= minDur >> 2;
                }
                if (cur_VowelFlag != 0) {
                    if ((cur_PhonCtrl & 768) == 512) {
                        percent_Duration = (percent_Duration * 36025) >> 16;
                    } else {
                        percent_Duration = (percent_Duration * 45850) >> 16;
                    }
                } else if (cur_Phon > 27 && cur_Phon <= 31) {
                    percent_Duration = (percent_Duration * 39300) >> 16;
                } else {
                    percent_Duration = (percent_Duration * 45850) >> 16;
                }
            }
            if ((((uint32_t)(uint16_t)cur_PhonCtrl >> 7) & 1) != 0 || cur_VowelFlag != 0 && cur_Stress != 4096) {
                eFlag = 0;
            }
            if (cur_Stress == 4096) {
                eFlag = 1;
            }
            if (eFlag != 0) {
                if (cur_VowelFlag != 0) {
                    fixed_Duration += 60;
                } else {
                    fixed_Duration += 20;
                }
            }
            vocFlag = 0;
            theObstr = 23;
            num_1 = 0x10000;
            if (cur_VowelFlag != 0 || ((((uint32_t)cur_PhonFlags >> 26) & 1) != 0 || (((uint32_t)cur_PhonFlags >> 6) & 1) != 0) && (cur_PhonCtrl & 7296) == 0 && (((uint32_t)next_PhonFlags >> 10) & 1) != 0) {
                if ((next_PhonFlags & 1) == 0 && (next_PhonCtrl & 7296) == 0) {
                    theObstr = next_Phon;
                    if (((((uint32_t)next_PhonFlags >> 26) & 1) != 0 || (((uint32_t)next_PhonFlags >> 6) & 1) != 0) && (next2_PhonCtrl & 7296) == 0 && (((uint32_t)next2_PhonFlags >> 10) & 1) != 0) {
                        vocFlag = 1;
                        theObstr = next2_Phon;
                    }
                    if (theObstr != 23) {
                        obstrFlags = xx->phonFlags2[theObstr];
                        if ((((uint32_t)obstrFlags >> 2) & 1) == 0) {
                            fixed_Duration -= fixed_Duration >> 1;
                            num_1 = 52400;
                            if ((obstrFlags & 0x1001000) != 0) {
                                num_1 = 36025;
                            }
                        } else if ((((uint32_t)obstrFlags >> 10) & 1) != 0) {
                            num_1 = 78600;
                            if ((((uint32_t)obstrFlags >> 12) & 1) == 0 && theObstr != 53 && (cur_PhonFlags & 5120) != 0) {
                                fixed_Duration += 25;
                            }
                        } else if ((((uint32_t)obstrFlags >> 6) & 1) != 0) {
                            num_1 = 55675;
                        }
                    }
                }
                if (cur_SyllableType <= 1 || vocFlag != 0) {
                    num_1 = (num_1 >> 1) + 0x8000;
                }
                percent_Duration = (percent_Duration * num_1) >> 16;
            }
            if (cur_VowelFlag != 0) {
                if ((next_PhonFlags & 1) != 0) {
                    fixed_Duration += 30;
                }
                if ((cur_PhonCtrl & 768) == 256 && (cur_PhonCtrl & 5120) != 0 && (((uint32_t)(uint16_t)prev_PhonCtrl >> 7) & 1) == 0) {
                    fixed_Duration += 25;
                }
                if (next_Phon == 25) {
                    fixed_Duration -= 20;
                }
            } else if ((((uint32_t)cur_PhonFlags >> 1) & 1) != 0) {
                if ((((uint32_t)next_PhonFlags >> 1) & 1) != 0 && cur_SyllableType <= 1) {
                    num_1 = 36025;
                    if ((((uint32_t)cur_PhonFlags >> 6) & 1) != 0 && (((uint32_t)(uint16_t)next_PhonCtrl >> 7) & 1) != 0) {
                        num_1 = 98250;
                    }
                    minDur -= minDur >> 2;
                    if ((cur_Phon == 40 || cur_Phon == 38) && (((uint32_t)next_PhonFlags >> 12) & 1) != 0) {
                        num_1 = 32750;
                    }
                    percent_Duration = (percent_Duration * num_1) >> 16;
                }
                if ((((uint32_t)prev_PhonFlags >> 1) & 1) != 0) {
                    num_1 = 36025;
                    minDur -= minDur >> 2;
                    if ((((uint32_t)cur_PhonFlags >> 12) & 1) != 0) {
                        if (prev_Phon == 40) {
                            num_1 = 39300;
                        } else if ((((uint32_t)prev_PhonFlags >> 6) & 1) != 0 && cur_Stress == 0) {
                            num_1 = 6550;
                        }
                    }
                    percent_Duration = (percent_Duration * num_1) >> 16;
                }
            }
            if ((((uint32_t)cur_PhonFlags >> 4) & 1) != 0 && (((uint32_t)prev_PhonFlags >> 2) & 1) == 0 && (((uint32_t)prev_PhonFlags >> 12) & 1) != 0) {
                fixed_Duration += 20;
            }
            if ((((uint32_t)cur_PhonFlags >> 3) & 1) != 0 && (((uint32_t)prev_PhonFlags >> 8) & 1) != 0 && (((uint32_t)prev_PhonFlags >> 6) & 1) == 0 && fixed_Duration == 0) {
                fixed_Duration = 20;
            }
            if (xx->phonBuf_2_In_Index <= 9 && minDur != maxDur) {
                fixed_Duration = (5 - (xx->phonBuf_2_In_Index >> 1)) * 5 + fixed_Duration;
            }
            dur_Hold = ((percent_Duration * (maxDur - minDur)) >> 7) + minDur;
            if (xx->speech_Rate != 180 && dur_Hold != 0) {
                dur_Hold = (dur_Hold * xx->rate_Ratio_LowGain) >> 16;
                fixed_Duration = (fixed_Duration * xx->rate_Ratio) >> 16;
            }
            dur_Hold += fixed_Duration;
        }
        dur_Hold /= 5;
        xx->dur_Buf[i] = dur_Hold;
    }
}

/* ParsePhons.c:723  (0x7ac60) */
void Fill_Phon_Buf_2(synthVarsPtr xx)
{
    int16_t cur_Phon;
    int16_t cur_PhonCtrl;
    int32_t cur_PhonFlags;
    int16_t prev_Phon;
    int16_t prev2_Phon;
    int16_t prev3_Phon;
    int16_t prev_PhonCtrl;
    int32_t prev_PhonFlags;
    int32_t prev2_PhonFlags;
    int32_t prev3_PhonFlags;
    int16_t next_Phon;
    int16_t next2_Phon;
    int16_t next3_Phon;
    int16_t next_PhonCtrl;
    int16_t next2_PhonCtrl;
    int32_t next_PhonFlags;
    int16_t last_stored_phon;
    int32_t last_PhonFlags;
    int16_t delFwd;
    int16_t target_phon;
    int16_t insertGlot;
    int16_t cur_Syll;
    int16_t t_58;

    xx->phonBuf_2_In_Index = 0;
    last_stored_phon = 23;
    for (xx->phonBuf_1_Out_Index = 0; xx->phonBuf_1_Out_Index < xx->phonBuf_1_In_Index; xx->phonBuf_1_Out_Index++) {
        cur_Phon = xx->phon_Buf_1[xx->phonBuf_1_Out_Index];
        cur_PhonCtrl = xx->phon_Ctrl_Buf_1[xx->phonBuf_1_Out_Index];
        cur_PhonFlags = xx->phonFlags2[cur_Phon];
        cur_Syll = cur_PhonCtrl & 768;
        if (xx->phonBuf_1_Out_Index < xx->phonBuf_1_In_Index - 1) {
            next_Phon = xx->phon_Buf_1[xx->phonBuf_1_Out_Index + 1];
            next_PhonCtrl = xx->phon_Ctrl_Buf_1[xx->phonBuf_1_Out_Index + 1];
        } else {
            next_Phon = 23;
            next_PhonCtrl = 0;
        }
        next_PhonFlags = xx->phonFlags2[next_Phon];
        if (xx->phonBuf_1_Out_Index < xx->phonBuf_1_In_Index - 2) {
            next2_Phon = xx->phon_Buf_1[xx->phonBuf_1_Out_Index + 2];
            next2_PhonCtrl = xx->phon_Ctrl_Buf_1[xx->phonBuf_1_Out_Index + 2];
        } else {
            next2_Phon = 23;
            next2_PhonCtrl = 0;
        }
        if (xx->phonBuf_1_Out_Index < xx->phonBuf_1_In_Index - 3) {
            next3_Phon = xx->phon_Buf_1[xx->phonBuf_1_Out_Index + 3];
        } else {
            next3_Phon = 23;
        }
        if (xx->phonBuf_1_Out_Index > 0) {
            prev_Phon = xx->phon_Buf_1[xx->phonBuf_1_Out_Index - 1];
            prev_PhonCtrl = xx->phon_Ctrl_Buf_1[xx->phonBuf_1_Out_Index - 1];
        } else {
            prev_Phon = 23;
            prev_PhonCtrl = 0;
        }
        prev_PhonFlags = xx->phonFlags2[prev_Phon];
        if (xx->phonBuf_1_Out_Index > 1) {
            prev2_Phon = xx->phon_Buf_1[xx->phonBuf_1_Out_Index - 2];
        } else {
            prev2_Phon = 23;
        }
        prev2_PhonFlags = xx->phonFlags2[prev2_Phon];
        if (xx->phonBuf_1_Out_Index > 2) {
            prev3_Phon = xx->phon_Buf_1[xx->phonBuf_1_Out_Index - 3];
        } else {
            prev3_Phon = 23;
        }
        prev3_PhonFlags = xx->phonFlags2[prev3_Phon];
        if (xx->phonBuf_1_Out_Index == 0) {
            last_stored_phon = 23;
        } else {
            last_stored_phon = xx->phon_Buf_2[xx->phonBuf_2_In_Index - 1];
        }
        last_PhonFlags = xx->phonFlags2[last_stored_phon];
        target_phon = cur_Phon;
        delFwd = 0;
        insertGlot = 0;
        if (cur_Phon == 34 && prev_Phon == 22 && (((uint32_t)prev2_PhonFlags >> 10) & 1) != 0 && prev2_Phon != 45 && prev2_Phon != 49 && (prev2_Phon != 47 || (prev3_PhonFlags & 1) == 0)) {
            xx->phon_Buf_2[xx->phonBuf_2_In_Index - 1] = 27;
            delFwd = 1;
        }
        if (cur_Phon == 31 && (cur_PhonCtrl & 13440) == 0 && (prev_Phon == 8 || prev_Phon == 7)) {
            xx->phon_Buf_2[xx->phonBuf_2_In_Index - 1] = 26;
            delFwd = 1;
        } else {
            if ((cur_PhonCtrl & 5248) == 0 && (((uint32_t)prev_PhonFlags >> 3) & 1) != 0) {
                if (cur_Phon == 31) {
                    target_phon = 25;
                } else if (cur_Phon == 30 && (uint32_t)prev_Phon <= 15) {
                    switch (prev_Phon) {
                    case 7:
                    case 15:
                        xx->phon_Buf_2[xx->phonBuf_2_In_Index - 1] = 21;
                        delFwd = 1;
                        break;
                    case 6:
                    case 14:
                        xx->phon_Buf_2[xx->phonBuf_2_In_Index - 1] = 20;
                        delFwd = 1;
                        break;
                    case 4:
                        xx->phon_Buf_2[xx->phonBuf_2_In_Index - 1] = 19;
                        delFwd = 1;
                        break;
                    case 5:
                    case 8:
                        xx->phon_Buf_2[xx->phonBuf_2_In_Index - 1] = 9;
                        delFwd = 1;
                        break;
                    case 0:
                    case 1:
                        xx->phon_Buf_2[xx->phonBuf_2_In_Index - 1] = 17;
                        delFwd = 1;
                        break;
                    case 2:
                    case 3:
                    case 10:
                        xx->phon_Buf_2[xx->phonBuf_2_In_Index - 1] = 18;
                        delFwd = 1;
                        break;
                    case 9:
                    case 11:
                    case 12:
                    case 13:
                        break;
                    }
                }
            }
            if ((((uint32_t)(uint16_t)prev_PhonCtrl >> 7) & 1) != 0 && prev_Phon == 29 && cur_Phon == 15 && next_Phon != 30 && (cur_PhonCtrl & 3) > 0) {
                xx->phon_Buf_2[xx->phonBuf_2_In_Index - 1] = 16;
                xx->phon_Ctrl_Buf_2[xx->phonBuf_2_In_Index - 1] = (prev_PhonCtrl & 8192) + cur_PhonCtrl;
                delFwd = 1;
            }
            if ((next_PhonFlags & 1) != 0 && cur_Phon == 5 && (cur_PhonCtrl & 3) != 0 && prev_Phon == 39 && (((uint32_t)(uint16_t)prev_PhonCtrl >> 7) & 1) != 0 && (next_PhonCtrl & 5120) != 0) {
                target_phon = 0;
            }
            if (cur_Phon == 23 && next_Phon == 2 && next2_Phon == 34 && next3_Phon == 47 && (next_PhonCtrl & 5120) != 0) {
                xx->phon_Buf_1[xx->phonBuf_1_Out_Index + 1] = 3;
                next_Phon = 3;
                next_PhonFlags = xx->phonFlags2[next_Phon];
            }
            if ((cur_PhonFlags & 1) != 0 && (next_PhonFlags & 1) != 0 && (next_PhonCtrl & 5120) != 0 && (cur_PhonCtrl & 1) != 0) {
                insertGlot = 1;
            }
            if ((next_Phon == 16 || next_Phon == 29) && (next_PhonCtrl & 5120) == 0 && cur_Phon == 47) {
                target_phon = 51;
            } else {
                if (cur_Phon == 46) {
                    if (next_Phon == 15 && (next_PhonCtrl & 3) > 0 && (cur_PhonCtrl & 5120) == 0 && (next2_Phon == 23 || (xx->phonFlags2[next2_Phon] & 1) != 0)) {
                        xx->phon_Buf_1[xx->phonBuf_1_Out_Index + 1] = 15;
                    } else if (next_Phon == 31 || next_Phon == 39) {
                        if ((((uint32_t)last_PhonFlags >> 4) & 1) != 0) {
                            target_phon = 52;
                        } else {
                            target_phon = 47;
                        }
                        goto L_7bba8;
                    }
                }
                if ((cur_Phon == 47 || cur_Phon == 46) && (next_Phon != 22 || next2_Phon != 34 || cur_Phon != 46 && (prev_PhonFlags & 1) != 0) && (next_PhonFlags & 1) != 0 && (((uint32_t)last_PhonFlags >> 4) & 1) != 0 && (((uint32_t)last_PhonFlags >> 6) & 1) == 0) {
                    if ((((uint32_t)(uint16_t)next_PhonCtrl >> 2) & 1) != 0) {
                        target_phon = 53;
                    } else if ((cur_PhonCtrl & 5120) == 0) {
                        if ((((uint32_t)(uint16_t)cur_PhonCtrl >> 7) & 1) != 0) {
                            if (next_Phon == 8 || next_Phon == 22 || next_Phon == 7) {
                                target_phon = 53;
                            }
                        } else if (cur_Phon == 46) {
                            if (next_Phon == 14) {
                                if ((xx->phon_Ctrl_Buf_2[xx->phonBuf_2_In_Index - 1] & 7168) != 0 && (next2_Phon != 30 || (((uint32_t)(uint16_t)next2_PhonCtrl >> 7) & 1) != 0)) {
                                    target_phon = 53;
                                }
                            } else if ((next_Phon == 5 || next_Phon == 8) && next2_Phon == 30 && (((uint32_t)(uint16_t)next_PhonCtrl >> 10) & 1) == 0) {
                                if ((((uint32_t)(uint16_t)cur_PhonCtrl >> 7) & 1) == 0 && (((uint32_t)(uint16_t)next_PhonCtrl >> 10) & 1) == 0) {
                                    target_phon = 53;
                                }
                            } else if (next_Phon == 9) {
                                if ((((uint32_t)(uint16_t)cur_PhonCtrl >> 7) & 1) == 0) {
                                    target_phon = 53;
                                }
                            } else if ((next_Phon == 8 || next_Phon == 0 || next_Phon == 22 || next_Phon == 26) && (next2_Phon != 30 || (((uint32_t)(uint16_t)next2_PhonCtrl >> 7) & 1) != 0) && (((uint32_t)(uint16_t)next_PhonCtrl >> 10) & 1) == 0) {
                                target_phon = 53;
                            }
                        } else {
                            switch (next_Phon) {
                            case 14:
                                if ((xx->phon_Ctrl_Buf_2[xx->phonBuf_2_In_Index - 1] & 7168) != 0) {
                                    target_phon = 53;
                                }
                                break;
                            case 0:
                            case 1:
                            case 4:
                            case 5:
                            case 8:
                            case 9:
                            case 22:
                            case 26:
                                target_phon = 53;
                                break;
                            }
                        }
                    }
                }
                if (cur_Phon == 39 && (((uint32_t)(uint16_t)cur_PhonCtrl >> 10) & 1) == 0) {
                    switch (last_stored_phon) {
                    case 46:
                    case 47:
                    case 52:
                        target_phon = 55;
                        break;
                    case 34:
                        target_phon = 34;
                        break;
                    }
                }
            }
        }
L_7bba8:
        if (delFwd == 0) {
            xx->phon_Buf_2[xx->phonBuf_2_In_Index] = target_phon;
            xx->phon_Ctrl_Buf_2[xx->phonBuf_2_In_Index] = cur_PhonCtrl;
            if (xx->phonBuf_2_In_Index < xx->destParseLen) {
                xx->phonBuf_2_In_Index++;
            }
            if (insertGlot != 0) {
                xx->phon_Buf_2[xx->phonBuf_2_In_Index] = 54;
                xx->phon_Ctrl_Buf_2[xx->phonBuf_2_In_Index] = 0;
                if (xx->phonBuf_2_In_Index < xx->destParseLen) {
                    xx->phonBuf_2_In_Index++;
                }
            }
        } else {
            if ((((uint32_t)(uint16_t)cur_PhonCtrl >> 13) & 1) != 0) {
                xx->phon_Ctrl_Buf_1[xx->phonBuf_1_Out_Index + 1] |= 8192;
            }
            if ((((uint32_t)(uint16_t)cur_PhonCtrl >> 5) & 1) != 0) {
                xx->phon_Ctrl_Buf_1[xx->phonBuf_1_Out_Index + 1] |= 32;
            }
        }
    }
}

/* ParsePhons.c:1307  (0x7bdf4) */
void Fill_Phon_Buf_2A(synthVarsPtr xx)
{
    int16_t cur_Phon;
    int16_t cur_PhonCtrl;
    int32_t cur_PhonFlags;
    int16_t next_Phon;
    int16_t next_PhonCtrl;
    int32_t next_PhonFlags;
    int16_t prev_Phon;
    int16_t prev_PhonCtrl;
    int32_t prev_PhonFlags;
    int16_t target_phon;
    int16_t insertGlot;

    xx->phonBuf_2_In_Index = 0;
    for (xx->phonBuf_1_Out_Index = 0; xx->phonBuf_1_Out_Index < xx->phonBuf_1_In_Index; xx->phonBuf_1_Out_Index++) {
        cur_Phon = xx->phon_Buf_1[xx->phonBuf_1_Out_Index];
        cur_PhonCtrl = xx->phon_Ctrl_Buf_1[xx->phonBuf_1_Out_Index];
        cur_PhonFlags = xx->phonFlags2[cur_Phon];
        if (xx->phonBuf_1_Out_Index < xx->phonBuf_1_In_Index - 1) {
            next_Phon = xx->phon_Buf_1[xx->phonBuf_1_Out_Index + 1];
            next_PhonCtrl = xx->phon_Ctrl_Buf_1[xx->phonBuf_1_Out_Index + 1];
        } else {
            next_Phon = 23;
            next_PhonCtrl = 0;
        }
        next_PhonFlags = xx->phonFlags2[next_Phon];
        if (xx->phonBuf_1_Out_Index > 0) {
            prev_Phon = xx->phon_Buf_1[xx->phonBuf_1_Out_Index - 1];
            prev_PhonCtrl = xx->phon_Ctrl_Buf_1[xx->phonBuf_1_Out_Index - 1];
        } else {
            prev_Phon = 23;
            prev_PhonCtrl = 0;
        }
        prev_PhonFlags = xx->phonFlags2[prev_Phon];
        target_phon = cur_Phon;
        insertGlot = 0;
        if ((cur_PhonCtrl & 5248) == 0 && (((uint32_t)prev_PhonFlags >> 3) & 1) != 0 && cur_Phon == 30) {
            target_phon = 24;
        }
        if ((cur_PhonFlags & 1) != 0 && (next_PhonFlags & 1) != 0 && (next_PhonCtrl & 5120) != 0 && (cur_PhonCtrl & 1) != 0) {
            insertGlot = 1;
        }
        xx->phon_Buf_2[xx->phonBuf_2_In_Index] = target_phon;
        xx->phon_Ctrl_Buf_2[xx->phonBuf_2_In_Index] = cur_PhonCtrl;
        if (xx->phonBuf_2_In_Index < xx->destParseLen) {
            xx->phonBuf_2_In_Index++;
        }
        if (insertGlot != 0) {
            xx->phon_Buf_2[xx->phonBuf_2_In_Index] = 54;
            xx->phon_Ctrl_Buf_2[xx->phonBuf_2_In_Index] = 0;
            if (xx->phonBuf_2_In_Index < xx->destParseLen) {
                xx->phonBuf_2_In_Index++;
            }
        }
    }
}

/* ParsePhons.c:1411  (0x7c1e4) */
int16_t If_Consonant_Cluster(int16_t Consonant_1st, int16_t Consonant_2nd)
{
    int16_t ret;

    ret = 0;
    if ((uint32_t)(Consonant_1st - 36) > 13) {
        return ret;
    }
    switch (Consonant_1st) {
    case 36:
        if ((uint32_t)(Consonant_2nd - 30) > 1) {
            return ret;
        }
        ret = 1;
        return ret;
    case 37:
        if ((uint32_t)(Consonant_2nd - 30) > 1) {
            return ret;
        }
        ret = 1;
        return ret;
    case 38:
        if (Consonant_2nd != 28 && Consonant_2nd != 30) {
            return ret;
        }
        ret = 1;
        return ret;
    case 40:
        if ((uint32_t)(Consonant_2nd - 28) > 20) {
            return ret;
        }
        if (((1 << (Consonant_2nd - 28)) & 1376617) == 0) {
            return ret;
        }
        ret = 1;
        return ret;
    case 42:
        if ((uint32_t)(Consonant_2nd - 28) > 18) {
            return ret;
        }
        if (((1 << (Consonant_2nd - 28)) & 327789) == 0) {
            return ret;
        }
        ret = 1;
        return ret;
    case 44:
        if ((uint32_t)(Consonant_2nd - 30) > 1) {
            return ret;
        }
        ret = 1;
        return ret;
    case 45:
        if ((uint32_t)(Consonant_2nd - 30) > 1) {
            return ret;
        }
        ret = 1;
        return ret;
    case 46:
        if (Consonant_2nd != 28 && Consonant_2nd != 30) {
            return ret;
        }
        ret = 1;
        return ret;
    case 47:
        if (Consonant_2nd != 28 && Consonant_2nd != 30) {
            return ret;
        }
        ret = 1;
        return ret;
    case 48:
        if ((uint32_t)Consonant_2nd > 31) {
            return ret;
        }
        if (((1 << Consonant_2nd) & -0x30000000) == 0) {
            return ret;
        }
        ret = 1;
        return ret;
    case 49:
        if ((uint32_t)Consonant_2nd > 31) {
            return ret;
        }
        if (((1 << Consonant_2nd) & -0x30000000) == 0) {
            return ret;
        }
        ret = 1;
    case 39:
    case 41:
    case 43:
        return ret;
    }
}

/* ParsePhons.c:1502  (0x7c4f4) */
int16_t Find_Next_Word_Bound(synthVarsPtr xx, int16_t index)
{
    int16_t i;

    for (i = index + 1; i < xx->phonBuf_1_In_Index; i++) {
        if ((xx->phon_Ctrl_Buf_1[i] & 12) != 0) {
            return i;
        }
    }
    return i;
}

/* ParsePhons.c:1517  (0x7c5a0) */
void MarkSyllableStart(synthVarsPtr xx)
{
    int16_t index;
    int16_t cur_Phon;
    int16_t cur_Ctrl;
    int32_t cur_PhonFlags;
    int16_t dist;
    int16_t syllable_index;
    int16_t phon_1st;
    int16_t phon_2nd;
    int16_t syllOrder;

    syllable_index = 0;
    index = 0;
    goto L_7ca50;
L_7c5e0:
    syllable_index++;
    index++;
    if (index >= xx->phonBuf_1_In_Index) {
        return;
    }
L_7c614:
    if (xx->phon_Buf_1[index] == 23) {
        goto L_7c5e0;
    }
    cur_Phon = xx->phon_Buf_1[index];
    cur_Ctrl = xx->phon_Ctrl_Buf_1[index];
    cur_PhonFlags = xx->phonFlags2[cur_Phon];
    if ((cur_PhonFlags & 1) != 0) {
        xx->phon_Ctrl_Buf_1[syllable_index] |= 8192;
        syllOrder = cur_Ctrl & 768;
        if (syllOrder == 0 || syllOrder == 768) {
            index = Find_Next_Word_Bound(xx, index);
            syllable_index = index;
        } else {
            dist = -1;
            do {
                index++;
                cur_PhonFlags = xx->phonFlags2[xx->phon_Buf_1[index]];
                dist++;
            } while ((cur_PhonFlags & 1) == 0);
            switch (dist) {
            case 0:
                syllable_index = index;
                goto L_7ca50;
            case 1:
                index--;
                syllable_index = index;
                goto L_7ca50;
            case 2:
                phon_2nd = xx->phon_Buf_1[index - 1];
                phon_1st = xx->phon_Buf_1[index - 2];
                if (If_Consonant_Cluster(phon_1st, phon_2nd) != 0) {
                    index -= 2;
                } else {
                    index--;
                }
                syllable_index = index;
                goto L_7ca50;
            case 3:
                phon_2nd = xx->phon_Buf_1[index - 1];
                phon_1st = xx->phon_Buf_1[index - 2];
                if (If_Consonant_Cluster(phon_1st, phon_2nd) != 0) {
                    if (xx->phon_Buf_1[index - 3] == 40) {
                        index -= 3;
                    } else {
                        index -= 2;
                    }
                } else {
                    index--;
                }
                syllable_index = index;
                goto L_7ca50;
            }
            phon_2nd = xx->phon_Buf_1[index - dist];
            phon_1st = xx->phon_Buf_1[index - dist + 1];
            if (If_Consonant_Cluster(phon_1st, phon_2nd) != 0) {
                index = index - dist + 2;
            } else {
                index -= dist >> 1;
            }
            syllable_index = index;
        }
    } else {
        index++;
    }
L_7ca50:
    if (index < xx->phonBuf_1_In_Index) {
        goto L_7c614;
    }
}

/* ParsePhons.c:1628  (0x7ca80) */
void Place_Stress_In_Consonant(synthVarsPtr xx, int16_t scanIndex)
{
    int16_t index;
    int16_t cur_Phon;
    int16_t cur_Ctrl;
    int32_t cur_PhonFlags;
    int16_t dist;
    int16_t phon_1st;
    int16_t phon_2nd;

    for (index = scanIndex + 1; index < xx->phonBuf_1_In_Index; index++) {
        cur_Phon = xx->phon_Buf_1[index];
        cur_Ctrl = xx->phon_Ctrl_Buf_1[index];
        cur_PhonFlags = xx->phonFlags2[cur_Phon];
        if ((cur_Ctrl & 7168) != 0) {
            dist = index - scanIndex;
            if (dist > 3) {
                return;
            }
            if (dist > 1) {
                phon_2nd = xx->phon_Buf_1[index - 1];
                phon_1st = xx->phon_Buf_1[index - 2];
                if (If_Consonant_Cluster(phon_1st, phon_2nd) == 0) {
                    return;
                }
                if (dist == 3) {
                    if (xx->phon_Buf_1[index - 3] != 40) {
                        return;
                    }
                }
            }
            if ((((uint32_t)(uint16_t)cur_Ctrl >> 10) & 1) != 0) {
                xx->phon_Ctrl_Buf_1[scanIndex] |= 1024;
                return;
            }
            if ((((uint32_t)(uint16_t)cur_Ctrl >> 11) & 1) != 0) {
                xx->phon_Ctrl_Buf_1[scanIndex] |= 2048;
                return;
            }
            if ((((uint32_t)(uint16_t)cur_Ctrl >> 12) & 1) == 0) {
                return;
            }
            xx->phon_Ctrl_Buf_1[scanIndex] |= 4096;
            return;
        }
        if ((cur_PhonFlags & 1) != 0) {
            return;
        }
        if ((cur_Ctrl & 12) != 0) {
            return;
        }
    }
}

/* ParsePhons.c:1704  (0x7cd94) */
void MarkSyllable(synthVarsPtr xx, int16_t scanIndex)
{
    int16_t index;
    int16_t cur_Phon;
    int16_t cur_Bound;
    int32_t cur_PhonFlags;
    int16_t order;
    int16_t cur_SyllableType;

    order = 0;
    for (index = scanIndex - 1; index > 0; index--) {
        cur_Phon = xx->phon_Buf_1[index];
        cur_PhonFlags = xx->phonFlags2[cur_Phon];
        cur_SyllableType = xx->phon_Ctrl_Buf_1[index] & 3;
        if (cur_SyllableType > 0) {
            break;
        }
        if ((cur_PhonFlags & 1) != 0) {
            order = 768;
            break;
        }
    }
    for (index = scanIndex + 1; index < xx->phonBuf_1_In_Index; index++) {
        cur_Phon = xx->phon_Buf_1[index];
        cur_Bound = xx->phon_Ctrl_Buf_1[index] & 12;
        cur_PhonFlags = xx->phonFlags2[cur_Phon];
        if (cur_Bound != 0) {
            xx->phon_Ctrl_Buf_1[scanIndex] |= order;
            return;
        }
        if ((cur_PhonFlags & 1) != 0) {
            switch (order) {
            case 768:
                order = 512;
                break;
            case 0:
                order = 256;
                break;
            }
        }
    }
}

/* ParsePhons.c:1771  (0x7cfd4) */
void MarkBoundry(synthVarsPtr xx, int16_t scanIndex)
{
    int16_t index;
    int16_t cur_Phon;
    int32_t cur_PhonFlags;
    int16_t cur_Bound;
    int16_t boundType;

    for (index = scanIndex + 1; index < xx->phonBuf_1_In_Index; index++) {
        cur_Phon = xx->phon_Buf_1[index];
        cur_PhonFlags = xx->phonFlags2[cur_Phon];
        cur_Bound = xx->phon_Ctrl_Buf_1[index] & 12;
        if (cur_Bound != 0) {
            boundType = 0;
            if ((((uint32_t)(uint16_t)cur_Bound >> 3) & 1) != 0) {
                boundType |= 3;
            }
            if ((((uint32_t)(uint16_t)cur_Bound >> 2) & 1) != 0) {
                boundType |= 1;
            }
            xx->phon_Ctrl_Buf_1[scanIndex] |= boundType;
            if (index + 1 == xx->phonBuf_1_In_Index) {
                xx->phon_Ctrl_Buf_1[index] |= boundType;
            }
        }
        if ((cur_PhonFlags & 1) != 0) {
            return;
        }
    }
}

/* ParsePhons.c:1805  (0x7d1cc) */
void Flag_PhonBuf_1(synthVarsPtr xx)
{
    int16_t cur_Phon;
    int32_t cur_PhonFlags;
    int16_t cur_Ctrl;
    int16_t scanIndex;

    for (scanIndex = 0; scanIndex < xx->phonBuf_1_In_Index; scanIndex++) {
        cur_Phon = xx->phon_Buf_1[scanIndex];
        cur_PhonFlags = xx->phonFlags2[cur_Phon];
        cur_Ctrl = xx->phon_Ctrl_Buf_1[scanIndex];
        if ((cur_PhonFlags & 1) != 0) {
            MarkSyllable(xx, scanIndex);
        }
        MarkBoundry(xx, scanIndex);
    }
    MarkSyllableStart(xx);
}

/* ParsePhons.c:1841  (0x7d2e0) */
void Store_Phon_In_PhonBuf_1(synthVarsPtr xx, int16_t phon)
{
    int16_t cur_PhonType;

    cur_PhonType = xx->phonTypeTbl[phon];
    if ((cur_PhonType & 1) == 0) {
        return;
    }
    xx->phon_Buf_1[xx->phonBuf_1_In_Index] = phon;
    if (xx->phonBuf_1_In_Index < xx->destParseLen) {
        xx->phonBuf_1_In_Index++;
    }
    xx->phon_Ctrl_Buf_1[xx->phonBuf_1_In_Index] = 0;
}

/* ParsePhons.c:1868  (0x7d3d0) */
void Init_Rate_Params(synthVarsPtr xx)
{
    if (xx->speech_Rate <= 39) {
        xx->speech_Rate = 40;
    }
    xx->rate_Ratio = 0xb40000 / xx->speech_Rate;
    xx->rate_Ratio_LowGain = 0xb40000 / (((xx->speech_Rate * 39300 - 7074000) >> 16) + 180);
}

/* ParsePhons.c:1898  (0x7d47c) */
static void AdjustNoteStart(synthVarsPtr xx)
{
    int16_t i;
    int32_t curFlags;
    int32_t prevFlags;
    int16_t curCtrl;
    int16_t prevCntrl;

    prevCntrl = *xx->phon_Ctrl_Buf_2 & 2;
    for (i = 1; i < xx->phonBuf_2_In_Index; i++) {
        curCtrl = xx->phon_Ctrl_Buf_2[i] & 2;
        if (curCtrl != 0 && prevCntrl == 0) {
            curFlags = xx->phonFlags2[xx->phon_Buf_2[i]];
            prevFlags = xx->phonFlags2[xx->phon_Buf_2[i - 1]];
            if ((((uint32_t)curFlags >> 4) & 1) != 0 && (((uint32_t)prevFlags >> 4) & 1) == 0) {
                xx->phon_Ctrl_Buf_2[i] &= 0xfffffffd;
                xx->phon_Ctrl_Buf_2[i - 1] |= 2;
            }
        }
        prevCntrl = curCtrl;
    }
}

/* ParsePhons.c:1933  (0x7d674) */
void FindNotes(synthVarsPtr xx)
{
    int16_t allowNuke;
    int16_t allowStart;
    int16_t i;
    int16_t curCtrl;

    allowNuke = 0;
    allowStart = 0;
    for (i = 0; i < xx->phonBuf_2_In_Index; i++) {
        curCtrl = xx->phon_Ctrl_Buf_2[i] & -772;
        if (xx->phon_Buf_2[i] == 23) {
            curCtrl |= 3;
            allowNuke = 0;
            allowStart = 0;
        } else if ((((uint32_t)(uint16_t)xx->phon_Ctrl_Buf_2[i] >> 2) & 1) != 0) {
            allowNuke = 1;
            allowStart = 1;
        } else if ((((uint32_t)(uint16_t)xx->phon_Ctrl_Buf_2[i] >> 5) & 1) != 0) {
            if ((((uint32_t)(uint16_t)curCtrl >> 13) & 1) != 0) {
                allowNuke = 1;
                allowStart = 1;
            } else {
                curCtrl |= 2;
                allowNuke = 1;
                allowStart = 0;
            }
        }
        if (allowNuke != 0 && (xx->phonFlags2[xx->phon_Buf_2[i]] & 1) != 0 && (((uint32_t)(uint16_t)curCtrl >> 14) & 1) == 0) {
            curCtrl |= 1;
            allowNuke = 0;
        }
        if (allowStart != 0 && (((uint32_t)(uint16_t)curCtrl >> 13) & 1) != 0) {
            curCtrl |= 2;
            allowStart = 0;
        }
        xx->phon_Ctrl_Buf_2[i] = curCtrl;
    }
}

/* ParsePhons.c:1997  (0x7d8f8) */
static int32_t CountVocals(unsigned char *targetTrack, int32_t startTime, int32_t endTime)
{
    int32_t count;
    int32_t lastEndTime;
    MIDI_Event me;

    me.targetTrack = targetTrack;
    me.target_time = 0;
    me.target_endTime = 0xffffff;
    lastEndTime = 0;
    count = 0;
    while (GetNextTrackEvent(&me, 0) != 0) {
        if (me.target_time > (uint32_t)endTime) {
            break;
        }
        if (me.target_cmd == 6 && me.target_time >= (uint32_t)startTime) {
            count++;
            if ((uint32_t)lastEndTime < me.target_time) {
                count++;
            }
            lastEndTime = me.target_time + me.target_dur;
        }
    }
    if (count <= 0) {
        return count;
    }
    count++;
    return count;
}

/* ParsePhons.c:2028  (0x7da14) */
static int32_t GetVocals(unsigned char *targetVocals, unsigned char *targetTrack, unsigned char *textP, unsigned char *phonP, int32_t startTime, int32_t endTime)
{
    int32_t len;
    int32_t i;
    int32_t lastEndTime;
    MIDI_Event me;
    int32_t phonCount;
    Ptr tPtr;

    me.targetTrack = targetTrack;
    me.target_time = 0;
    me.target_endTime = 0xffffff;
    lastEndTime = 0;
    phonCount = 0;
    while (GetNextTrackEvent(&me, 0) != 0) {
        if (me.target_time > (uint32_t)endTime) {
            break;
        }
        if (me.target_cmd == 6 && me.target_time >= (uint32_t)startTime) {
            if ((uint32_t)lastEndTime < me.target_time) {
                (*textP) = 1;
                textP[1] = 44;
                (*phonP) = 1;
                phonP[1] = 23;
                phonCount++;
                textP += 13;
                phonP += 13;
            }
            tPtr = &targetVocals[me.target_vocals * 26];
            len = (int8_t)*tPtr;
            for (i = 0; i <= len; i++) {
                textP[i] = tPtr[i];
            }
            tPtr += 13;
            len = (int8_t)*tPtr;
            phonCount += len;
            for (i = 0; i <= len; i++) {
                phonP[i] = tPtr[i];
            }
            textP += 13;
            phonP += 13;
            lastEndTime = me.target_time + me.target_dur;
        }
    }
    if (phonCount <= 0) {
        return phonCount;
    }
    (*textP) = 1;
    textP[1] = 44;
    (*phonP) = 1;
    phonP[1] = 23;
    phonCount++;
    textP += 13;
    phonP += 13;
    return phonCount;
}

/* ParsePhons.c:2101  (0x7dcf0) */
static void Fill_Phon_Buf_1(synthVarsPtr xx, int32_t noteCount, unsigned char *textP, unsigned char *phonP)
{
    int16_t i;
    int16_t j;
    unsigned char *curPhonStr;
    unsigned char *curTextStr;
    int16_t cur_Phon;
    int32_t cur_PhonFlags;
    int16_t word_Initial;
    int16_t newWord;

    xx->phonBuf_1_In_Index = 0;
    (*xx->phon_Ctrl_Buf_1) = 0;
    newWord = 1;
    word_Initial = 1;
    curTextStr = textP;
    curPhonStr = phonP;
    for (i = 0; i < noteCount; i++) {
        if (curPhonStr[1] == 23) {
            if (i > 0) {
                xx->phon_Ctrl_Buf_1[xx->phonBuf_1_In_Index] |= 8;
            }
        } else if (newWord != 0) {
            xx->phon_Ctrl_Buf_1[xx->phonBuf_1_In_Index] |= 4;
            word_Initial = 1;
        }
        xx->phon_Ctrl_Buf_1[xx->phonBuf_1_In_Index] |= 32;
        for (j = 1; j <= *curPhonStr; j++) {
            cur_Phon = curPhonStr[j];
            cur_PhonFlags = xx->phonFlags2[cur_Phon];
            if ((cur_PhonFlags & 1) != 0) {
                word_Initial = 0;
            } else if (word_Initial != 0) {
                xx->phon_Ctrl_Buf_1[xx->phonBuf_1_In_Index] |= 128;
            }
            Store_Phon_In_PhonBuf_1(xx, cur_Phon);
        }
        if (curTextStr[*curTextStr] == 45) {
            newWord = 0;
        } else {
            newWord = 1;
        }
        curTextStr += 13;
        curPhonStr += 13;
    }
}

/* ParsePhons.c:2178  (0x7e020) */
int16_t MakeSpeechData(synthVarsPtr xx, unsigned char *targetVocals, unsigned char *targetTrack, Handle speechData, int32_t *speechDataLen, int32_t rate)
{
    int32_t noteCount;
    int32_t phonCount;
    int32_t totalCount;
    unsigned char *textP;
    unsigned char *phonP;
    int16_t error;
    int32_t i;
    int16_t data;
    int16_t *dataPtr;

    error = 0;
    (*speechDataLen) = 0;
    textP = NULL;
    phonP = NULL;
    xx->phon_Buf_1 = NULL;
    xx->phon_Ctrl_Buf_1 = NULL;
    xx->phon_Buf_2 = NULL;
    xx->phon_Ctrl_Buf_2 = NULL;
    xx->dur_Buf = NULL;
    noteCount = CountVocals(targetTrack, 0, 0xffffff);
    if (noteCount > 0) {
        textP = (unsigned char *)NewPtr(noteCount * 13);
        if (textP == 0) {
            error = 1000;
        } else {
            phonP = (unsigned char *)NewPtr(noteCount * 13);
            if (phonP == 0) {
                error = 1000;
            } else {
                phonCount = GetVocals(targetVocals, targetTrack, textP, phonP, 0, 0xffffff);
                totalCount = phonCount + noteCount + 1;
                xx->destParseLen = totalCount;
                xx->phon_Buf_1 = (int16_t *)NewPtr(totalCount << 1);
                if (xx->phon_Buf_1 == 0) {
                    error = 1000;
                } else {
                    xx->phon_Ctrl_Buf_1 = (int16_t *)NewPtr(totalCount << 1);
                    if (xx->phon_Ctrl_Buf_1 == 0) {
                        error = 1000;
                    } else {
                        Fill_Phon_Buf_1(xx, noteCount, textP, phonP);
                        DisposePtr(textP);
                        DisposePtr(phonP);
                        textP = NULL;
                        phonP = NULL;
                        totalCount += totalCount >> 1;
                        xx->destParseLen = totalCount;
                        xx->phon_Buf_2 = (int16_t *)NewPtr(totalCount << 1);
                        if (xx->phon_Buf_2 == 0) {
                            error = 1000;
                        } else {
                            xx->phon_Ctrl_Buf_2 = (int16_t *)NewPtr(totalCount << 1);
                            if (xx->phon_Ctrl_Buf_2 == 0) {
                                error = 1000;
                            } else {
                                xx->dur_Buf = (int16_t *)NewPtr(totalCount << 1);
                                if (xx->dur_Buf == 0) {
                                    error = 1000;
                                } else {
                                    xx->speech_Rate = rate;
                                    Init_Rate_Params(xx);
                                    Flag_PhonBuf_1(xx);
                                    Fill_Phon_Buf_2A(xx);
                                    Mod_Duration_FE(xx);
                                    Insert_Closure_Release(xx);
                                    FindNotes(xx);
                                    totalCount = (xx->phonBuf_2_In_Index << 3) + 4;
                                    SetHandleSize(speechData, totalCount);
                                    if (MemError() != 0) {
                                        error = 1000;
                                    } else {
                                        (*speechDataLen) = totalCount;
                                        HLock(speechData);
                                        dataPtr = (int16_t *)*speechData;
                                        (*dataPtr) = 0;
                                        dataPtr++;
                                        (*dataPtr) = xx->phonBuf_2_In_Index;
                                        dataPtr++;
                                        for (i = 0; xx->phonBuf_2_In_Index > i; i++) {
                                            (*dataPtr) = xx->phon_Buf_2[i];
                                            dataPtr++;
                                        }
                                        for (i = 0; xx->phonBuf_2_In_Index > i; i++) {
                                            (*dataPtr) = xx->phon_Ctrl_Buf_2[i];
                                            dataPtr++;
                                        }
                                        for (i = 0; xx->phonBuf_2_In_Index > i; i++) {
                                            (*dataPtr) = xx->dur_Buf[i];
                                            dataPtr++;
                                        }
                                        for (i = 0; xx->phonBuf_2_In_Index > i; i++) {
                                            (*dataPtr) = xx->dur_Buf[i];
                                            dataPtr++;
                                        }
                                        HUnlock(speechData);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        totalCount = 4;
        SetHandleSize(speechData, totalCount);
        if (MemError() != 0) {
            error = 1000;
        } else {
            (*speechDataLen) = totalCount;
            HLock(speechData);
            dataPtr = (int16_t *)*speechData;
            (*dataPtr) = 0;
            dataPtr++;
            (*dataPtr) = 0;
            dataPtr++;
            HUnlock(speechData);
        }
    }
    if (textP != 0) {
        DisposePtr(textP);
    }
    if (phonP != 0) {
        DisposePtr(phonP);
    }
    if (xx->phon_Buf_1 != 0) {
        DisposePtr(xx->phon_Buf_1);
    }
    if (xx->phon_Ctrl_Buf_1 != 0) {
        DisposePtr(xx->phon_Ctrl_Buf_1);
    }
    if (xx->phon_Buf_2 != 0) {
        DisposePtr(xx->phon_Buf_2);
    }
    if (xx->phon_Ctrl_Buf_2 != 0) {
        DisposePtr(xx->phon_Ctrl_Buf_2);
    }
    if (xx->dur_Buf == 0) {
        return error;
    }
    DisposePtr(xx->dur_Buf);
    return error;
}

/* ParsePhons.c:2384  (0x7e710) */
int16_t Collect_Phons(synthVarsPtr xx)
{
    int16_t cur_Phon;
    int16_t cur_PhonType;
    int32_t cur_PhonFlags;
    int16_t word_Initial;
    int16_t wordCount;
    int16_t i;

    word_Initial = 1;
    wordCount = 0;
    xx->phonBuf_1_In_Index = 0;
    (*xx->phon_Ctrl_Buf_1) = 0;
    for (i = 0; i < xx->srcParseLen; i++) {
        if (xx->phonBuf_1_In_Index >= xx->destParseLen) {
            break;
        }
        cur_Phon = xx->srcParseBuf[i];
        cur_PhonType = xx->phonTypeTbl[cur_Phon];
        cur_PhonFlags = xx->phonFlags2[cur_Phon];
        if ((((uint32_t)(uint16_t)cur_PhonType >> 1) & 1) != 0) {
            switch (cur_Phon) {
            case 60:
                xx->phon_Ctrl_Buf_1[xx->phonBuf_1_In_Index] |= 1024;
                break;
            case 61:
                xx->phon_Ctrl_Buf_1[xx->phonBuf_1_In_Index] |= 2048;
                break;
            case 62:
                xx->phon_Ctrl_Buf_1[xx->phonBuf_1_In_Index] |= 4;
                word_Initial = 1;
                wordCount++;
                break;
            }
        } else {
            if ((cur_PhonFlags & 1) != 0) {
                word_Initial = 0;
            } else if (word_Initial != 0) {
                xx->phon_Ctrl_Buf_1[xx->phonBuf_1_In_Index] |= 128;
            }
            Store_Phon_In_PhonBuf_1(xx, cur_Phon);
        }
    }
    xx->phon_Ctrl_Buf_1[xx->phonBuf_1_In_Index - 1] |= 8;
    Flag_PhonBuf_1(xx);
    return wordCount;
}

/* ParsePhons.c:2475  (0x7ea74) */
void TunePhons(synthVarsPtr xx)
{
    int16_t wordCount;

    wordCount = Collect_Phons(xx);
    Fill_Phon_Buf_2(xx);
}

/* ParsePhons.c:2489  (0x7eacc) */
static void AdjustBoundry(synthVarsPtr xx, int32_t noteCount, unsigned char *textP, unsigned char *phonP, int32_t flags)
{
    int16_t i;
    int16_t j;
    int16_t k;
    unsigned char *curPhonStr;
    unsigned char *nextPhonStr;
    unsigned char *curTextStr;
    int16_t tailPhons[3];
    int32_t tailFlags[3];
    int16_t tailLen;
    int16_t headPhons[3];
    int32_t headFlags[3];
    int16_t headLen;

    curTextStr = textP;
    curPhonStr = phonP;
    nextPhonStr = &curPhonStr[13];
    noteCount--;
    for (i = 0; i < noteCount; i++) {
        if (curTextStr[*curTextStr] != 45) {
            for (j = 0; j <= 2; j++) {
                tailPhons[j] = 23;
                tailFlags[j] = 0;
                headPhons[j] = 23;
                headFlags[j] = 0;
            }
            k = 2;
            tailLen = *curPhonStr;
            for (j = *curPhonStr; j > 0; j--) {
                tailPhons[k] = curPhonStr[j];
                tailFlags[k] = xx->phonFlags2[tailPhons[k]];
                k--;
                if (k < 0) {
                    break;
                }
            }
            k = 0;
            headLen = *nextPhonStr;
            for (j = 1; j <= *nextPhonStr; j++) {
                headPhons[k] = nextPhonStr[j];
                headFlags[k] = xx->phonFlags2[headPhons[k]];
                k++;
                if (k > 2) {
                    break;
                }
            }
            if ((flags & 1) != 0 && tailLen == 2 && tailPhons[1] == 39 && tailPhons[2] == 5 && (headFlags[0] & 1) != 0) {
                curPhonStr[2] = 0;
                if ((((uint32_t)flags >> 2) & 1) != 0) {
                    curPhonStr[3] = 54;
                    (*curPhonStr)++;
                }
            } else if ((((uint32_t)flags >> 1) & 1) != 0 && tailLen == 2 && tailPhons[1] == 36 && tailPhons[2] == 20 && ((headFlags[0] ^ 1) & 1) != 0) {
                curPhonStr[2] = 9;
            } else if ((((uint32_t)flags >> 2) & 1) != 0 && (tailFlags[2] & 1) != 0 && (((uint32_t)tailFlags[2] >> 29) & 1) == 0 && (headFlags[0] & 1) != 0 && (((uint32_t)headFlags[0] >> 28) & 1) == 0 && (uint32_t)*curTextStr <= 11) {
                curPhonStr[*curPhonStr + 1] = 54;
                (*curPhonStr)++;
            } else {
                if ((((uint32_t)flags >> 3) & 1) != 0 && (headPhons[0] == 16 || headPhons[0] == 29)) {
                    if (tailPhons[2] == 46) {
                        curPhonStr[*curPhonStr] = 50;
                    } else {
                        if (tailPhons[2] != 47) {
                            goto L_7f048;
                        }
                        curPhonStr[*curPhonStr] = 51;
                    }
                    goto L_7f184;
                }
L_7f048:
                if ((((uint32_t)flags >> 4) & 1) != 0 && tailPhons[2] == 46) {
                    if (headPhons[0] == 31 || headPhons[0] == 39) {
                        curPhonStr[*curPhonStr] = 52;
                    } else {
                        if ((((uint32_t)headFlags[0] >> 8) & 1) == 0 && headPhons[0] != 32) {
                            goto L_7f0f8;
                        }
                        curPhonStr[*curPhonStr] = 52;
                    }
                    goto L_7f184;
                }
L_7f0f8:
                if ((((uint32_t)flags >> 5) & 1) != 0 && (tailPhons[2] == 47 || tailPhons[2] == 46) && (headFlags[0] & 1) != 0 && (((uint32_t)tailFlags[1] >> 4) & 1) != 0 && (((uint32_t)tailFlags[1] >> 6) & 1) == 0) {
                    curPhonStr[*curPhonStr] = 53;
                }
            }
        }
L_7f184:
        curTextStr += 13;
        curPhonStr += 13;
        nextPhonStr += 13;
    }
}

/* ParsePhons.c:2732  (0x7f1d4) */
static void SetVocals(unsigned char *targetVocals, unsigned char *targetTrack, unsigned char *phonP, int32_t startTime, int32_t endTime)
{
    int32_t len;
    int32_t i;
    MIDI_Event me;
    int32_t phonCount;
    Ptr tPtr;

    me.targetTrack = targetTrack;
    me.target_time = 0;
    me.target_endTime = 0xffffff;
    while (GetNextTrackEvent(&me, 0) != 0) {
        if (me.target_time > (uint32_t)endTime) {
            return;
        }
        if (me.target_cmd == 6) {
            if (me.target_time >= (uint32_t)startTime) {
                while (*phonP == 1 && phonP[1] == 23) {
                    phonP += 13;
                }
                tPtr = &targetVocals[me.target_vocals * 26 + 13];
                len = *phonP;
                for (i = 0; i <= len; i++) {
                    tPtr[i] = phonP[i];
                }
                phonP += 13;
                continue;
            }
            continue;
        }
    }
}

/* ParsePhons.c:2775  (0x7f360) */
int16_t AdjustBoundryPhons(synthVarsPtr xx, unsigned char *targetVocals, unsigned char *targetTrack, int32_t startTime, int32_t endTime, int32_t flags)
{
    int32_t noteCount;
    int32_t phonCount;
    unsigned char *textP;
    unsigned char *phonP;
    int16_t error;
    int32_t i;

    error = 0;
    textP = NULL;
    phonP = NULL;
    noteCount = CountVocals(targetTrack, startTime, endTime);
    if (noteCount > 1) {
        textP = (unsigned char *)NewPtr(noteCount * 13);
        if (textP == 0) {
            error = 1000;
        } else {
            phonP = (unsigned char *)NewPtr(noteCount * 13);
            if (phonP == 0) {
                error = 1000;
            } else {
                phonCount = GetVocals(targetVocals, targetTrack, textP, phonP, startTime, endTime);
                AdjustBoundry(xx, noteCount, textP, phonP, flags);
                SetVocals(targetVocals, targetTrack, phonP, startTime, endTime);
            }
        }
    }
    if (textP != 0) {
        DisposePtr(textP);
    }
    if (phonP == 0) {
        return error;
    }
    DisposePtr(phonP);
    return error;
}
