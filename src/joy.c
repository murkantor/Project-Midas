#include <ultra64.h>
#include "deb.h"
#include "joy.h"
#include <PR/os.h>

#define JOY_CLAMP_MIN          0
#define JOY_CLAMP_MAX        120
#define JOY_CLAMP_MAX_F   120.0f
#define JOY_CLAMP_OFFSET      60



struct contdata {
    /* 0x000 */ struct contsample samples[CONTSAMPLE_LEN];
    /* 0x1E0 */ s32 curlast;
    /* 0x1E4 */ s32 curstart;
    /* 0x1E8 */ s32 nextlast;
    /* 0x1EC */ s32 nextsecondlast;
    /* 0x1F0 */ u16 buttonspressed[MAXCONTROLLERS];
    /* 0x1F8 */ s32 playbackcontcount;
};

/**
 * Regular controller data for struct contdata.
 */
#define CONTDATA_REGULAR  0

/**
 * Playback controller data for struct contdata.
 */
#define CONTDATA_PLAYBACK 1

/**
 * Length of struct contdata[].
 */
#define CONTDATA_LEN      2

/**
 * Contains controller data for "regular" and playback.
 */
struct contdata g_ContData[CONTDATA_LEN];

#define CONT_INPUT_BUFFER_LEN                10
#define CONT_DISABLE_POLL_SEND_BUFFER_LEN     1
#define CONT_DISABLE_POLL_RECEIVE_BUFFER_LEN  1
#define CONT_ENABLE_POLL_SEND_BUFFER_LEN      1
#define CONT_ENABLE_POLL_RECEIVE_BUFFER_LEN   1

OSMesg      g_ContInputMessageBuffer[CONT_INPUT_BUFFER_LEN];
OSMesgQueue g_ContInputMessageQueue;

OSMesg      g_ContDisablePollSendMessageBuffer[CONT_DISABLE_POLL_SEND_BUFFER_LEN];
OSMesgQueue g_ContDisablePollSendMessageQueue;

OSMesg      g_ContDisablePollReceiveMessageBuffer[CONT_DISABLE_POLL_RECEIVE_BUFFER_LEN];
OSMesgQueue g_ContDisablePollReceiveMessageQueue;

OSMesg      g_ContEnablePollSendMessageBuffer[CONT_ENABLE_POLL_SEND_BUFFER_LEN];
OSMesgQueue g_ContEnablePollSendMessageQueue;

OSMesg      g_ContEnablePollReceiveMessageBuffer[CONT_ENABLE_POLL_RECEIVE_BUFFER_LEN];
OSMesgQueue g_ContEnablePollReceiveMessageQueue;

OSContStatus g_ContStatus[MAXCONTROLLERS];
OSPfs g_ContPfs[MAXCONTROLLERS];
s32 g_ContDebugData = 0;

struct contdata *g_ContDataPtr = &g_ContData[CONTDATA_REGULAR];
s32 g_ContBusy = 0;
s32 g_ContPollDisableCount = 0;
u8 g_ConnectedControllers = 0;

/**
 * Uses 1 bit per controller.
 */
u8 g_ControllerStates = 0;

typedef enum {
    RUMBLEPAKINITSTATE_ERROR = -1,
    RUMBLEPAKINITSTATE_NOT_READY,
    RUMBLEPAKINITSTATE_READY
} RUMBLEPAKINITSTATE;

typedef enum {
    RUMBLEPAKSTATE_OFF,
    RUMBLEPAKSTATE_ON,
    RUMBLEPAKSTATE_UNKNOWN
} RUMBLEPAKSTATE;

// forward declarations
void joyCheckStatus(void);

//

s32 g_ContRumblePakInitState[MAXCONTROLLERS] = {0};

#define set_rumble_pak_init_state_not_ready(i) do { g_ContRumblePakInitState[i] = RUMBLEPAKINITSTATE_NOT_READY; } while (0)

s32 g_ContRumblePakCurrentState[MAXCONTROLLERS] = {0};
s32 g_ContRumblePakTimer60[MAXCONTROLLERS] = {0};
s32 g_ContRumblePakTargetState[MAXCONTROLLERS] = {0};


s32 g_ContQueuesCreated = 0;
s32 g_ContInitDone = 0;
s32 g_ContCheckStatusTimer60 = 0;

contplaybackfunc g_ContPlaybackFunc = NULL;
contrecordfunc g_ContRecordFunc = NULL;

/**
 * Startup flag, cleared after first call to joyCheckStatus.
 */
s32 g_ContNeedsInit = 1;

u32 g_ContBadReadsStickX[MAXCONTROLLERS] = {0};
u32 g_ContBadReadsStickY[MAXCONTROLLERS] = {0};
u32 g_ContBadReadsButtons[MAXCONTROLLERS] = {0};
u32 g_ContBadReadsButtonsPressed[MAXCONTROLLERS] = {0};

void joyInit(void)
{
    s32 i;
    s32 j;

    debTryAdd(&g_ContDebugData, "joy_c_debug");

    osCreateMesgQueue(&g_ContDisablePollSendMessageQueue, g_ContDisablePollSendMessageBuffer, CONT_DISABLE_POLL_SEND_BUFFER_LEN);
    osCreateMesgQueue(&g_ContDisablePollReceiveMessageQueue, g_ContDisablePollReceiveMessageBuffer, CONT_DISABLE_POLL_RECEIVE_BUFFER_LEN);
    osCreateMesgQueue(&g_ContEnablePollSendMessageQueue, g_ContEnablePollSendMessageBuffer, CONT_ENABLE_POLL_SEND_BUFFER_LEN);
    osCreateMesgQueue(&g_ContEnablePollReceiveMessageQueue, g_ContEnablePollReceiveMessageBuffer, CONT_ENABLE_POLL_RECEIVE_BUFFER_LEN);
    osCreateMesgQueue(&g_ContInputMessageQueue, g_ContInputMessageBuffer, CONT_INPUT_BUFFER_LEN);

    osSetEventMesg(OS_EVENT_SI, &g_ContInputMessageQueue, NULL);

    g_ContQueuesCreated = TRUE;
    g_ContPlaybackFunc = NULL;
    g_ContRecordFunc = NULL;

    for (i = 0; i < CONTDATA_LEN; i++)
    {
        g_ContData[i].curlast = 0;
        g_ContData[i].curstart = 0;
        g_ContData[i].nextlast = 0;
        g_ContData[i].nextsecondlast = 0;
        g_ContData[i].playbackcontcount = -1;

        for (j = 0; j < MAXCONTROLLERS; j++)
        {
            g_ContData[i].samples[0].pads[j].button = 0;
            g_ContData[i].samples[0].pads[j].stick_x = 0;
            g_ContData[i].samples[0].pads[j].stick_y = 0;
            g_ContData[i].samples[0].pads[j].errno = 0;
        }
    }
}

void joyCheckStatusThreadSafe(void)
{
    OSMesg  msg;

    if (g_ContQueuesCreated)
    {
        osSendMesg(&g_ContDisablePollSendMessageQueue, &msg, OS_MESG_NOBLOCK);
        osRecvMesg(&g_ContDisablePollReceiveMessageQueue, &msg, OS_MESG_BLOCK);

        joyCheckStatus();

        osSendMesg(&g_ContEnablePollSendMessageQueue, &msg, OS_MESG_NOBLOCK);
        osRecvMesg(&g_ContEnablePollReceiveMessageQueue, &msg, OS_MESG_BLOCK);
    }
}

s32 osPfsChecker(OSPfs *pfs)
{
    return PFS_ERR_INCONSISTENT;
}

void joyRumblePakInit(s32 index)
{
    s32 ret;

    if (g_ContRumblePakInitState[index] > RUMBLEPAKINITSTATE_ERROR)
    {
        if ((g_ContStatus[index].type & CONT_JOYPORT) && (g_ContStatus[index].status & CONT_CARD_ON))
        {
            ret = osPfsInit(&g_ContInputMessageQueue, &g_ContPfs[index], index);

            if ((ret == PFS_ERR_ID_FATAL) || (ret == PFS_ERR_DEVICE))
            {
                if (osMotorInit(&g_ContInputMessageQueue, &g_ContPfs[index], index) == 0)
                {
                    g_ContRumblePakInitState[index] = RUMBLEPAKINITSTATE_READY;
                }
                else
                {
                    g_ContRumblePakInitState[index] = RUMBLEPAKINITSTATE_ERROR;
                }
            }
        }
    }
}

void joyCheckStatus(void)
{
    s8 i;

    if (g_ContNeedsInit)
    {
        g_ContNeedsInit = FALSE;
        osContInit(&g_ContInputMessageQueue, &g_ConnectedControllers, g_ContStatus);
        g_ContInitDone = TRUE;
    }
    else
    {
        u32 slots = 0xF;
        s32 i;

        // The following three function calls (+for) show up in the same sequence
        // in devkit demos, but there doesn't seem to be much else in common
        // with Rare's implementation.
        // n64devkit\ultra\usr\src\pr\demos\gbpak\siproc.c line 244
        // n64devkit\ultra\usr\src\pr\demos\voice\siproc.c line 89
        osContStartQuery(&g_ContInputMessageQueue);
        osRecvMesg(&g_ContInputMessageQueue, NULL, OS_MESG_BLOCK);
        osContGetQuery(g_ContStatus);
        // end similarity to gbpak\siproc.c

        for (i = 0; i < MAXCONTROLLERS; i++)
        {
            if (g_ContStatus[i].errno & CONT_NO_RESPONSE_ERROR)
            {
                slots -= 1 << i;
            }
        }
        // end similarity to voice\siproc.c

        g_ConnectedControllers = slots;
    }

    if (0)
    {
        // Removed
    }

    for (i = 0; i < MAXCONTROLLERS; i++)
    {
        // Removed
    }

    for (i = 0; i < MAXCONTROLLERS; i++)
    {
        if ((g_ConnectedControllers & (1 << i))
            && (g_ContStatus[i].type & (CONT_ABSOLUTE | CONT_RELATIVE))
            && !(g_ContStatus[i].errno))
        {
            // This seems like a typo in the original, doing a bitwise AND
            // between a logical test on the left and a bitshift on the right.
            if ((!(g_ControllerStates) & (1 << i)) || (g_ContRumblePakInitState[i] < RUMBLEPAKINITSTATE_READY))
            {
                joyRumblePakInit(i);
            }

            g_ControllerStates |= (1 << i);
        }
        else if (g_ControllerStates & (1 << i))
        {
            g_ControllerStates ^= (1 << i);
            g_ContRumblePakInitState[i] = RUMBLEPAKINITSTATE_NOT_READY;
        }
    }
}

s8 joyGetControllerCount(void)
{
    s32 i;

    if (g_ContDataPtr->playbackcontcount >= 0)
    {
        return g_ContDataPtr->playbackcontcount;
    }

    for (i = 0; i < MAXCONTROLLERS; i++)
    {
        if ((g_ConnectedControllers & (1 << i)) == 0)
        {
            return i;
        }
    }

    return MAXCONTROLLERS;
}

u8 joyGetConnectedControllers(void)
{
    return g_ConnectedControllers;
}

void joyRumblePakTick(void)
{
    s32 i;

    for (i = 0; i < MAXCONTROLLERS; i++)
    {
        if (g_ContRumblePakCurrentState[i] != g_ContRumblePakTargetState[i])
        {
            if (g_ContRumblePakTargetState[i] == RUMBLEPAKSTATE_ON)
            {
                if (0 == osMotorStart(&g_ContPfs[i]))
                {
                    g_ContRumblePakCurrentState[i] = RUMBLEPAKSTATE_ON;
                }
                else
                {
                    set_rumble_pak_init_state_not_ready(i);
                }
#ifdef BUGFIX_R1
            }
            else if (g_ContRumblePakTargetState[i] == RUMBLEPAKSTATE_UNKNOWN)
            {
                if (osMotorInit(&g_ContInputMessageQueue, &g_ContPfs[i], i) != 0)
                {
                    set_rumble_pak_init_state_not_ready(i);
                }

                osMotorStop(&g_ContPfs[i]);
                g_ContRumblePakCurrentState[i] = RUMBLEPAKSTATE_OFF;
                g_ContRumblePakTargetState[i] = RUMBLEPAKSTATE_OFF;
#endif
            }
            else
            {
                if (0 == osMotorStop(&g_ContPfs[i]))
                {
                    g_ContRumblePakCurrentState[i] = RUMBLEPAKSTATE_OFF;
                }
                else
                {
                    set_rumble_pak_init_state_not_ready(i);
                }
            }
        }

        if (g_ContRumblePakTimer60[i] <= 0)
        {
            g_ContRumblePakTimer60[i] = 0;
        }
        else
        {
            g_ContRumblePakTimer60[i]--;

            if (g_ContRumblePakTimer60[i] <= 0)
            {
                g_ContRumblePakTimer60[i] = 0;
                g_ContRumblePakTargetState[i] = 0;
            }
        }
    }
}

void joySetPlaybackFunc(contplaybackfunc func, s32 controllercount)
{
    g_ContPlaybackFunc = func;
    g_ContData[CONTDATA_PLAYBACK].playbackcontcount = controllercount;
}

void joySetRecordFunc(contrecordfunc func)
{
    g_ContRecordFunc = func;
}

void joyConsumeSamples(struct contdata *contdata)
{
    s8 i;
    s32 samplenum;
    u16 buttons1;
    u16 buttons2;

    contdata->curstart = contdata->curlast;
    contdata->curlast = contdata->nextlast;

    for (i = 0; i < MAXCONTROLLERS; i++)
    {
        contdata->buttonspressed[i] = 0;

        if (contdata->curlast != contdata->curstart)
        {
            // Do not remove the following trailing backslash. The "while(true)"
            // needs to be on the same line as previous, otherwise the build breaks.
            // Search for WHILE_ONE_LINE to see other places in code like this.
            samplenum = ((contdata->curstart + 1) % CONTSAMPLE_LEN); \
            while (TRUE)
            {
                buttons1 = contdata->samples[samplenum].pads[i].button;
                buttons2 = contdata->samples[(samplenum + (CONTSAMPLE_LEN-1)) % CONTSAMPLE_LEN].pads[i].button;
                contdata->buttonspressed[i] |= buttons1 & ~buttons2;

                if (samplenum == contdata->curlast)
                {
                    break;
                }

                samplenum = ((samplenum + 1) % CONTSAMPLE_LEN);
            }
        }
    }
}

void joyConsumeSamplesWrapper(void)
{
#ifndef TARGET_N64
    { /* PORT: the mouse is polled more often than the sim ticks, so a
       * latest-poll-wins delta dropped motion. Hand over the accumulated motion
       * exactly once per tick, before anything reads it (no-op without
       * input_pc.c). */
        extern void portInputMouseTickConsume(void);
        extern void portReturnStartTickConsume(void); /* video.c; weak no-op headless */
        portInputMouseTickConsume();
        portReturnStartTickConsume(); /* Return key as Start, same poll window as buttonspressed */
    }
#endif

    if (g_ContPlaybackFunc)
    {
        g_ContData[CONTDATA_PLAYBACK].nextlast = g_ContPlaybackFunc(g_ContData[CONTDATA_PLAYBACK].samples, g_ContData[CONTDATA_PLAYBACK].curlast);
        joyConsumeSamples(&g_ContData[CONTDATA_PLAYBACK]);
    }

    joyConsumeSamples(&g_ContData[CONTDATA_REGULAR]);

#ifndef TARGET_N64
    { /* diagnostic (GE_DUMP_BOND): once-a-second dump of the button
       * edge-detection window, for debugging dead buttons. */
        extern char *getenv(const char *);
        static int dump = -1;
        static int calls;
        struct contdata *cd = &g_ContData[CONTDATA_REGULAR];
        if (dump < 0) dump = getenv("GE_DUMP_BOND") != NULL;
        calls++;
        if (dump && (calls % 60) == 0)
        {
            extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
            extern s32 ge_joy_calls, ge_joy_reads, ge_joy_disables, ge_joy_enables;
            f32 v[9];
            v[0] = (f32)cd->buttonspressed[0];
            v[1] = (f32)cd->samples[cd->curlast].pads[0].button;
            v[2] = (f32)cd->curstart;
            v[3] = (f32)cd->curlast;
            v[4] = (f32)cd->nextlast;
            v[5] = (f32)ge_joy_calls;
            v[6] = (f32)ge_joy_reads;
            v[7] = (f32)ge_joy_disables;
            v[8] = (f32)g_ContPollDisableCount;
            ge_dbg_vec("joywin press/raw/cs/cl/nl/calls/reads/dis/cnt", v, 9);
        }
    }
#endif

    if (g_ContRecordFunc)
    {
        g_ContRecordFunc(g_ContData[CONTDATA_REGULAR].samples, g_ContData[CONTDATA_REGULAR].curstart, g_ContData[CONTDATA_REGULAR].curlast);
    }
}


#ifndef TARGET_N64
/* diagnostic counters (GE_DUMP_BOND, printed by the joywin probe) */
s32 ge_joy_calls, ge_joy_reads, ge_joy_disables, ge_joy_enables, ge_joy_noMsg;
#endif

void joyPoll(void)
{
    OSMesg msg;
    s8 i_s8;
    s32 i;
    s32 index;
    s32 padding[2];

#ifndef TARGET_N64
    ge_joy_calls++;
#endif

    // Check if there are any disable requests
    if (osRecvMesg(&g_ContDisablePollSendMessageQueue, &msg, 0) == 0)
    {
#ifndef TARGET_N64
        ge_joy_disables++;
#endif
        if (g_ContBusy)
        {
            osRecvMesg(&g_ContInputMessageQueue, &msg, 1);
            g_ContBusy = 0;
        }

        osSendMesg(&g_ContDisablePollReceiveMessageQueue, &msg, 0);
        g_ContPollDisableCount++;
        return;
    }

    // Check if there are any enable requests
    if (osRecvMesg(&g_ContEnablePollSendMessageQueue, &msg, 0) == 0)
    {
#ifndef TARGET_N64
        ge_joy_enables++;
#endif
        osContStartReadData(&g_ContInputMessageQueue);
        g_ContBusy = 1;
        osSendMesg(&g_ContEnablePollReceiveMessageQueue, &msg, 0);
        g_ContPollDisableCount--;
        return;
    }

    if (g_ContPollDisableCount)
    {
        return;
    }

    // Poll controller input from SI
    if (g_ContInitDone && osRecvMesg(&g_ContInputMessageQueue, &msg, 0) == 0)
    {
        static s32 count = 0;

#ifndef TARGET_N64
        ge_joy_reads++;
#endif
        g_ContBusy = 0;

        index = (g_ContData[0].nextlast + 1) % 20;

        if (index == g_ContData[0].curstart)
        {
            index = g_ContData[0].nextlast;
        }

        osContGetReadData(g_ContData[0].samples[index].pads);
        g_ContData[0].nextlast = index;
        g_ContData[0].nextsecondlast = (g_ContData[0].nextlast + 19) % 20;

        g_ContCheckStatusTimer60++;

        if ((g_ContCheckStatusTimer60 % 120) == 0)
        {
            joyCheckStatus();
        }

        for (i_s8 = 0; i_s8 < 4; i_s8++)
        {
            if (((g_ContData[0].samples[g_ContData[0].nextlast].pads[i_s8].errno == 0) &&
                 (g_ContData[0].samples[g_ContData[0].nextsecondlast].pads[i_s8].errno != 0)) ||
                ((g_ContData[0].samples[g_ContData[0].nextlast].pads[i_s8].errno != 0) &&
                 (g_ContData[0].samples[g_ContData[0].nextsecondlast].pads[i_s8].errno == 0)))
            {
                joyCheckStatus();
                break;
            }
        }

        joyRumblePakTick();
        osContStartReadData(&g_ContInputMessageQueue);

        g_ContBusy = 1;
        count++;

        if (count >= 60)
        {
            for (i = 0; i < 4; i++)
            {
                if (g_ContBadReadsStickX[i] != 0
                        || g_ContBadReadsStickY[i] != 0
                        || g_ContBadReadsButtons[i] != 0
                        || g_ContBadReadsButtonsPressed[i] != 0)
                {
                    // These empty checks are required for matching.
                    if (g_ContBadReadsStickX[i]);
                    if (g_ContBadReadsStickY[i]);
                    if (g_ContBadReadsButtons[i]);
                    if (g_ContBadReadsButtonsPressed[i]);

                    g_ContBadReadsStickX[i] = 0;
                    g_ContBadReadsStickY[i] = 0;
                    g_ContBadReadsButtons[i] = 0;
                    g_ContBadReadsButtonsPressed[i] = 0;
                }
            }

            count = 0;
        }
    }
}


s8 joyGetStickX(s8 contpadnum)
{
    //this assert is on ALL stick functions below
#ifdef DEBUG
    assert(contpadnum > 0); //j
#endif

    if ((g_ContDataPtr->playbackcontcount < 0) && ((g_ConnectedControllers >> contpadnum & 1) == 0))
    {
        g_ContBadReadsStickX[contpadnum]++;
        return 0;
    }

    return g_ContDataPtr->samples[g_ContDataPtr->curlast].pads[contpadnum].stick_x;
}
//duplicate?
s8 joy7000C174(s8 contpadnum)
{
    if ((g_ContDataPtr->playbackcontcount < 0) && ((g_ConnectedControllers >> contpadnum & 1) == 0))
    {
        g_ContBadReadsStickX[contpadnum]++;
        return 0;
    }

    return g_ContDataPtr->samples[g_ContDataPtr->curstart].pads[contpadnum].stick_x;
}

s8 joyGetStickY(s8 contpadnum)
{
    if ((g_ContDataPtr->playbackcontcount < 0) && ((g_ConnectedControllers >> contpadnum & 1) == 0))
    {
        g_ContBadReadsStickY[contpadnum]++;
        return 0;
    }

    return g_ContDataPtr->samples[g_ContDataPtr->curlast].pads[contpadnum].stick_y;
}

s8 joy7000C284(s8 contpadnum)
{
    if ((g_ContDataPtr->playbackcontcount < 0) && ((g_ConnectedControllers >> contpadnum & 1) == 0))
    {
        g_ContBadReadsStickY[contpadnum]++;
        return 0;
    }

    return g_ContDataPtr->samples[g_ContDataPtr->curstart].pads[contpadnum].stick_y;
}

u16 joyGetButtons(s8 contpadnum, u16 mask)
{
    if ((g_ContDataPtr->playbackcontcount < 0) && ((g_ConnectedControllers >> contpadnum & 1) == 0))
    {
        g_ContBadReadsButtons[contpadnum]++;
        return 0;
    }

    return g_ContDataPtr->samples[g_ContDataPtr->curlast].pads[contpadnum].button & mask;
}

u16 joyGetButtonsPressedThisFrame(s8 contpadnum, u16 mask)
{
    if ((g_ContDataPtr->playbackcontcount < 0) && ((g_ConnectedControllers >> contpadnum & 1) == 0))
    {
        g_ContBadReadsButtonsPressed[contpadnum]++;
        return 0;
    }

    return g_ContDataPtr->buttonspressed[contpadnum] & mask;
}

#ifndef TARGET_N64
/* Port: OR a synthetic press into this poll's edge set (menu mouse-back:
 * front.c menu_init turns a mouse aim press into B). No-op during demo
 * playback so recorded input stays authoritative. */
void portJoyInjectPressed(s8 contpadnum, u16 mask)
{
    if (g_ContDataPtr->playbackcontcount < 0)
    {
        g_ContDataPtr->buttonspressed[contpadnum] |= mask;
    }
}

/* Port: OR a synthetic held button into the latest sample, so menu sliders can
 * be dragged while the "use" bind is held. This never creates a press or
 * release edge on its own; the press edge comes from portJoyInjectPressed. */
void portJoyInjectHeld(s8 contpadnum, u16 mask)
{
    if (g_ContDataPtr->playbackcontcount < 0)
    {
        g_ContDataPtr->samples[g_ContDataPtr->curlast].pads[contpadnum].button |= mask;
    }
}
#endif

void joy7000C430(s8 *bytes, u16 bitfield)
{
    s32 i;
    for (i = 15; i >= 0; i--)
    {
        *bytes++ = (((((bitfield >> i) & 1) > 0) * 17) + 32);
    }
}

void joy7000C470(void)
{
    s32 i = 0;
    for (i = 0; i < joyGetControllerCount(); i++)
    {
        // Removed
    }
}

/**
 * Reads controller joystick x value. JOY_CLAMP_OFFSET is first
 * added to the raw value, then it is clamped between JOY_CLAMP_MIN
 * and JOY_CLAMP_MAX. The value is then normalized against supplied range parameters.
 *
 * @param contpadnum controller to read.
 * @param rangemin min value of range to normalize against.
 * @param rangemax max value of range to normalize against.
 *
 * @return returns normalized value between range, as an s32.
 */
s32 joyGetStickXInRange(s8 contpadnum, s32 rangemin, s32 rangemax)
{
    s32 range;
    s32 stick_x = joyGetStickX(contpadnum) + JOY_CLAMP_OFFSET;

    if (stick_x > JOY_CLAMP_MAX)
    {
        stick_x = JOY_CLAMP_MAX;
    }

    if (stick_x < JOY_CLAMP_MIN)
    {
        stick_x = JOY_CLAMP_MIN;
    }

    range = (rangemax - rangemin);
    return (((stick_x * range) / JOY_CLAMP_MAX) + rangemin);
}

/**
 * Reads controller joystick y value. JOY_CLAMP_OFFSET is first
 * added to the raw value, then it is clamped between JOY_CLAMP_MIN
 * and JOY_CLAMP_MAX. The value is then normalized against supplied range parameters.
 *
 * @param contpadnum controller to read.
 * @param rangemin min value of range to normalize against.
 * @param rangemax max value of range to normalize against.
 *
 * @return returns normalized value between range, as an s32.
 */
s32 joyGetStickYInRange(s8 contpadnum, s32 rangemin, s32 rangemax)
{
    s32 range;
    s32 stick_y = joyGetStickY(contpadnum) + JOY_CLAMP_OFFSET;

    if (stick_y > JOY_CLAMP_MAX)
    {
        stick_y = JOY_CLAMP_MAX;
    }

    if (stick_y < JOY_CLAMP_MIN)
    {
        stick_y = JOY_CLAMP_MIN;
    }

    range = (rangemax - rangemin);
    return (((stick_y * range) / JOY_CLAMP_MAX) + rangemin);
}

/**
 * Reads controller joystick x value. JOY_CLAMP_OFFSET is first
 * added to the raw value, then it is clamped between JOY_CLAMP_MIN
 * and JOY_CLAMP_MAX. The value is then normalized against supplied range parameters.
 *
 * @param contpadnum controller to read.
 * @param rangemin min value of range to normalize against.
 * @param rangemax max value of range to normalize against.
 *
 * @return returns normalized value between range, as a float.
 */
f32 joyGetStickXInRangef(s8 contpadnum, f32 rangemin, f32 rangemax)
{
    f32 range;
    s32 stick_x = joyGetStickX(contpadnum) + JOY_CLAMP_OFFSET;

    if (stick_x > JOY_CLAMP_MAX)
    {
        stick_x = JOY_CLAMP_MAX;
    }

    if (stick_x < JOY_CLAMP_MIN)
    {
        stick_x = JOY_CLAMP_MIN;
    }

    range = (rangemax - rangemin);
    return (((stick_x / JOY_CLAMP_MAX_F) * range) + rangemin);
}

/**
 * Reads controller joystick y value. JOY_CLAMP_OFFSET is first
 * added to the raw value, then it is clamped between JOY_CLAMP_MIN
 * and JOY_CLAMP_MAX. The value is then normalized against supplied range parameters.
 *
 * @param contpadnum controller to read.
 * @param rangemin min value of range to normalize against.
 * @param rangemax max value of range to normalize against.
 *
 * @return returns normalized value between range, as a float.
 */
f32 joyGetStickYInRangef(s8 contpadnum, f32 rangemin, f32 rangemax)
{
    f32 range;
    s32 stick_y = joyGetStickY(contpadnum) + JOY_CLAMP_OFFSET;

    if (stick_y > JOY_CLAMP_MAX)
    {
        stick_y = JOY_CLAMP_MAX;
    }

    if (stick_y < JOY_CLAMP_MIN)
    {
        stick_y = JOY_CLAMP_MIN;
    }

    range = (rangemax - rangemin);
    return (((stick_y / JOY_CLAMP_MAX_F) * range) + rangemin);
}

/**
 * Disables os message polling.
 */
void joyDisablePoll(void)
{
    OSMesg msg;

    osSendMesg(&g_ContDisablePollSendMessageQueue, &msg, OS_MESG_NOBLOCK);
#ifdef TARGET_N64
    osRecvMesg(&g_ContDisablePollReceiveMessageQueue, &msg, OS_MESG_BLOCK);
#else
    /* Single-threaded PC: nothing guarantees joyPoll runs to post the ack, so waiting
     * for it hung and controller input died after the first EEPROM access. Run
     * joyPoll inline instead. */
    joyPoll();
    osRecvMesg(&g_ContDisablePollReceiveMessageQueue, &msg, OS_MESG_NOBLOCK);
#endif
}

/**
 * Enables os message polling.
 */
void joyEnablePoll(void)
{
    OSMesg msg;

    osSendMesg(&g_ContEnablePollSendMessageQueue, &msg, OS_MESG_NOBLOCK);
#ifdef TARGET_N64
    osRecvMesg(&g_ContEnablePollReceiveMessageQueue, &msg, OS_MESG_BLOCK);
#else
    /* see joyDisablePoll */
    joyPoll();
    osRecvMesg(&g_ContEnablePollReceiveMessageQueue, &msg, OS_MESG_NOBLOCK);
#endif
}

s32 joyGamePakProbe(void)
{
    s32 type;

    joyDisablePoll();
    type = osEepromProbe(&g_ContInputMessageQueue);
    joyEnablePoll();

    return type;
}

s32 joyGamePakRead(u8 address, u8 *buffer)
{
    s32 ret;

    joyDisablePoll();
    ret = osEepromRead(&g_ContInputMessageQueue, address, buffer);
    joyEnablePoll();

    return ret;
}

s32 joyGamePakWrite(u8 address, u8 *buffer)
{
    s32 ret;

    joyDisablePoll();
    ret = osEepromWrite(&g_ContInputMessageQueue, address, buffer);
    joyEnablePoll();

    return ret;
}

s32 joyGamePakLongRead(u8 address, u8 *buffer, s32 nbytes)
{
    s32 ret;

    joyDisablePoll();
    ret = osEepromLongRead(&g_ContInputMessageQueue, address, buffer, nbytes);
    joyEnablePoll();

    return ret;
}

s32 joyGamePakLongWrite(u8 address, u8 *buffer, s32 nbytes)
{
    s32 ret;

    joyDisablePoll();
    ret = osEepromLongWrite(&g_ContInputMessageQueue, address, buffer, nbytes);
    joyEnablePoll();

    return ret;
}

void joyRumblePakStart(s32 controller, f32 duration)
{
    s32 duration60 = (duration * 60.0f);

    if ((g_ContPlaybackFunc == NULL) && (g_ContRumblePakInitState[controller] > RUMBLEPAKINITSTATE_NOT_READY))
    {
        if (g_ContRumblePakTimer60[controller] < duration60)
        {
            g_ContRumblePakTimer60[controller] = duration60;
        }
        if (g_ContRumblePakCurrentState[controller] == RUMBLEPAKSTATE_OFF)
        {
            g_ContRumblePakTargetState[controller] = RUMBLEPAKSTATE_ON;
        }
    }
}

void joyRumblePakStop(void)
{
    s32 i;

    for (i = 0; i < MAXCONTROLLERS; i++)
    {
#if defined(BUGFIX_R0)
        g_ContRumblePakCurrentState[i] = RUMBLEPAKSTATE_ON;
        g_ContRumblePakTargetState[i] = RUMBLEPAKSTATE_OFF;
#else
        g_ContRumblePakTargetState[i] = RUMBLEPAKSTATE_UNKNOWN;
#endif
    }
}

void joySetContDataIndex(s32 index)
{
    g_ContDataPtr = &g_ContData[index];
}

s32 joyGetContDataIndex(void)
{
    return (g_ContDataPtr - g_ContData);
}
