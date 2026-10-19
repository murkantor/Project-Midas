#include <ultra64.h>
#ifndef TARGET_N64
#include <stdlib.h> /* getenv for GE_SNDEVTQ */
#endif
#include <PR/libaudio.h>
#include <os_extension.h>
#include "music.h"
#include "snd.h"
//likely named gslibaudio.c from xbla
/**
 * EU .data, offset from start of data_seg : 0x3620
*/

/**
 * @file snd.c
 * This file contains code to deal with snd.
 */

#define DEFAULT_SETUP_PITCH_SHIFT (-0x1770)

/**
 * Based on \n64devkit\ultra\usr\src\pr\libsrc\libultra\audio\sndp.h
 * ALSndpEvent
 */
typedef union ALSndpEvent_u {

    struct {
        // offset 0
        u16             type;
        // offset 4
        ALSoundState    *state;
    } common;

    struct {
        u16             type;
        ALSoundState    *state;
        s32             vol;
    } vol;

    struct {
        u16             type;
        ALSoundState    *state;
        f32             pitch;
    } pitch;

    struct {
        u16             type;
        ALSoundState    *state;
        s32           pan32;
    } pan32;

    struct {
        u16             type;
        ALSoundState    *state;
        s32             mix32;
    } fx32;

    struct {
        u16 type;
        ALSoundState *state;
        s32 soundIndex;
        struct ALBankAlt_s *soundBank;
    } playSfx;

    struct {
        s16 type;
        ALSoundState *state;
        s32 val8;
        s32 valc;
    } unks32;

    struct {
        s32 unk0;
        s32 unk4;
        s32 unk8;
        s32 unkC;
    } align_size;

} ALSndpEvent;

union ALSndpSmallEvent_u {
    struct {
        u16 type;
        ALSoundState *state;
    } msg;

    union {
        s32 unk0;
        s32 unk4;
    } align_size;
};

// TODO: is this struct really the answer?
// 800243E4
struct D_800243E4_s {
    // address 800243E4 and 800243E8
    ALLink node;
    // address 800243EC
    struct ALSoundState_s *g_sndPlayerSoundStatePtr;
};

s32 g_sndUnused800243E0 = 0;

// // TODO: is this struct really the answer?
struct D_800243E4_s D_800243E4 = { {NULL, NULL}, NULL};

ALSndPlayer *g_sndPlayerPtr = &g_sndPlayer;

/**
 * Current number of allocated voices, via alSynAllocVoice
 */
s16 g_sndAllocatedVoicesCount = 0;

/**
 * Boot flag. If set, sound is disabled.
 */
s8 g_sndBootswitchSound = 0;

/**
 * Used in level load/setup, sound effect slot volume will be scaled by this amount.
 */
f32 g_sndSfxVolumeScale = 1.0;

// forward declarations

ALMicroTime sndPlayerVoiceHandler(void *node);
void sndHandleEvent(ALSndPlayer *sndp, ALSndpEvent *event);
void sndDisposeSound(ALSoundState *state);
void sndCreatePitchEvent(ALSoundState *state);
void sndRemoveEvents(ALEventQueue *evtq, ALSoundState *state, u16 eventType);
s32 sndCountAllocList(s16 *allocListCount, s16 *freeListCount);
ALSoundState *sndSetupSound(struct ALBankAlt_s *soundBank, ALSound* sound);
void sndUnlinkClearSound(ALSoundState *state);
void sndSetPriority(ALSoundState *state, u8 priority);
u8 sndGetPlayingState(ALSoundState *state);
void sndDeactivateAllSfxByFlag(u8 flag);
void sndDeactivateAllSfxByFlag_1(void);
void sndDeactivateAllSfxByFlag_11(void);
void sndDeactivateAllSfxByFlag_3(void);
u16 sndGetSfxSlotFirstNaturalVolume(void);
void sndApplyVolumeAllSfxSlot(u16 arg0);
void sndSetScalerApplyVolumeAllSfxSlot(f32 arg0);
u16 sndGetSfxSlotNaturalVolume(u8 arg0);
void sndSetSfxSlotVolume(u8 arg0, u16 arg1);

// end forward declarations

#ifndef TARGET_N64
/* ============================ SFX EVENT QUEUE ============================
 * PORT: every SFX event post goes through here so a full event queue can't make
 * sounds cut out.
 *
 * Why drops matter: a dropped PLAY event leaves a sound that is waiting for a
 * free voice (SOUND_STATE_WAIT_VOICE) with no retry, so it never plays and
 * never frees its state. A dropped END means the voice is never freed. Both
 * leave the queue fuller, making the next drop likelier.
 *
 * Why the queue fills: the event pool is sized 1:1 with the sound-state pool
 * (music.c, 0x40 x GE_SNDSCALE), every live state can hold a timed event, and
 * the per-field pan/volume refreshers (portSndPanTick in propobj.c and the
 * positional volume owners) post far more than the player drains each frame.
 *
 * The policy:
 *  1. Coalesce: volume and pan are last-value-wins and always posted at delta 0,
 *     so a pending one for the same (state, type) is simply overwritten.
 *  2. Evict: if the queue is full and the post is not pan/vol, drop the oldest
 *     pending pan/vol to make room. Nothing that decides WHETHER a sound plays
 *     may lose its slot to something that only decides HOW it sounds.
 *  3. Census: if a post is still dropped, log what is in the queue by type and
 *     whether the API heartbeat is still there.
 *
 * Zero-delta events form a prefix of allocList (alEvtqPostEvent walks past
 * equal deltas), so both walks stop at the first timed entry and never touch an
 * event that is waiting on a clock.
 */
#define SND_EVT_CONTINUOUS (AL_SNDP_VOL_EVT | AL_SNDP_PAN_EVT)

s32 portSndEvtqPolicy(void)
{
    static s32 on = -1;

    if (on < 0)
    {
        const char *e = getenv("GE_SNDEVTQ");

        on = (e != NULL && e[0] == '0') ? 0 : 1;
    }
    return on;
}

static s32 s_EvtqCoalesced;
static s32 s_EvtqEvicted;
static s32 s_EvtqFullPosts;

static void portSndEvtqCensus(ALEventQueue *evtq)
{
    extern int printf(const char *, ...);
    ALLink *node;
    ALSoundState *st;
    s32 used = 0, due = 0, heartbeat = 0, states = 0, voiced = 0, waiting = 0;
    s32 pan = 0, vol = 0, play = 0, decay = 0, stop = 0, end = 0, other = 0;
    s32 guard;

    for (node = evtq->allocList.next; node != NULL && used < 4096; node = node->next)
    {
        ALEventListItem *item = (ALEventListItem *) node;
        ALSndpEvent *e = (ALSndpEvent *) &item->evt;

        used++;
        if (item->delta == 0)
        {
            due++;
        }

        switch (e->common.type)
        {
            case AL_SNDP_PAN_EVT:   pan++;         break;
            case AL_SNDP_VOL_EVT:   vol++;         break;
            case AL_SNDP_PLAY_EVT:  play++;        break;
            case AL_SNDP_DECAY_EVT: decay++;       break;
            case AL_SNDP_STOP_EVT:  stop++;        break;
            case AL_SNDP_END_EVT:   end++;         break;
            case AL_SNDP_API_EVT:   heartbeat = 1; break;
            default:                other++;       break;
        }
    }

    /* the player holds one dequeued event outside the queue */
    if (g_sndPlayerPtr->nextEvent.type == AL_SNDP_API_EVT)
    {
        heartbeat = 1;
    }

    st = (ALSoundState *) D_800243E4.node.next;
    for (guard = 0; st != NULL && guard < 512; guard++)
    {
        states++;
        if (st->unk3e & 4)
        {
            voiced++;
        }
        if (st->playingState == SOUND_STATE_WAIT_VOICE)
        {
            waiting++;
        }
        st = (ALSoundState *) st->link.next;
    }

    printf("ge: snd: evtq FULL - %d queued (%d due now) | pan %d vol %d play %d decay %d stop %d end %d other %d"
           " | states %d live, %d voiced, %d waiting for a voice (%d of %d voices) | heartbeat %s"
           " | coalesced %d evicted %d dropped %d\n",
           used, due, pan, vol, play, decay, stop, end, other,
           states, voiced, waiting, (s32) g_sndAllocatedVoicesCount, (s32) g_sndPlayerPtr->maxSounds,
           heartbeat ? "alive" : "LOST",
           s_EvtqCoalesced, s_EvtqEvicted, s_EvtqFullPosts);
}

void portSndPostEvent(ALEventQueue *evtq, ALEvent *evt, ALMicroTime delta)
{
    ALSndpEvent *in = (ALSndpEvent *) evt;
    s32 continuous;
    ALLink *node;

    continuous = portSndEvtqPolicy() && (delta == 0) && (in->common.state != NULL)
                 && ((in->common.type & SND_EVT_CONTINUOUS) != 0);

    if (continuous)
    {
        for (node = evtq->allocList.next; node != NULL; node = node->next)
        {
            ALEventListItem *item = (ALEventListItem *) node;
            ALSndpEvent *q = (ALSndpEvent *) &item->evt;

            if (item->delta != 0)
            {
                break; /* past the due-now prefix */
            }
            if (q->common.type == in->common.type && q->common.state == in->common.state)
            {
                /* vol.vol, pan32.pan32 and unks32.val8 are the same slot */
                q->unks32.val8 = in->unks32.val8;
                s_EvtqCoalesced++;
                return;
            }
        }
    }

    /* Deliberately NOT reserving the last slot for the API heartbeat: that starved
     * the player's own follow-up posts (END after STOP, DECAY, the WAIT_VOICE
     * retry) and leaked voices. */
#define SND_EVTQ_FULL() (evtq->freeList.next == NULL)

    if (SND_EVTQ_FULL() && !continuous && portSndEvtqPolicy())
    {
        /* Evict a PAN before a VOL: an evicted pan is re-posted by portSndPanPost,
         * but some volume owners only re-post when their value changes, so a
         * lost volume can stick. */
        s32 pass;

        for (pass = 0; pass < 2; pass++)
        {
            s32 type = (pass == 0) ? AL_SNDP_PAN_EVT : AL_SNDP_VOL_EVT;

            for (node = evtq->allocList.next; node != NULL; node = node->next)
            {
                ALEventListItem *item = (ALEventListItem *) node;
                ALSndpEvent *q = (ALSndpEvent *) &item->evt;

                if (item->delta != 0)
                {
                    break;
                }
                if (q->common.type == type)
                {
                    /* its delta is 0, so the successor inherits nothing */
                    alUnlink(node);
                    alLink(node, &evtq->freeList);
                    s_EvtqEvicted++;
                    break;
                }
            }
            if (!SND_EVTQ_FULL())
            {
                break;
            }
        }
    }

    if (SND_EVTQ_FULL())
    {
        if (s_EvtqFullPosts < 4 || (s_EvtqFullPosts % 256) == 0)
        {
            portSndEvtqCensus(evtq);
        }
        s_EvtqFullPosts++;
        if (evtq->freeList.next != NULL)
        {
            return; /* only the heartbeat's slot is left: drop this post */
        }
    }
#undef SND_EVTQ_FULL

    alEvtqPostEvent(evtq, evt, delta);
}
#else
#define portSndPostEvent(q, e, d) alEvtqPostEvent((q), (e), (d))
#endif


/**
 * 8720    70007B20
 *
 * Mostly identical to n64devkit\ultra\usr\src\pr\libsrc\libultra\audio\sndplayer.c
 * method alSndpNew.
 */
void sndNewPlayerInit(ALSeqpSfxConfig *sfxSeqpConfig)
{
    u8 *ptr;
    struct ALSoundState_s *sState;
    ALEvent evt;
    u32 i;

    /*
     * Init member variables
     */
    g_sndPlayerPtr->maxSounds = sfxSeqpConfig->maybeMaxSounds;
    g_sndPlayerPtr->target = 0;
    g_sndPlayerPtr->frameTime = AL_USEC_PER_FRAME_30FPS;
    sState = alHeapAlloc(sfxSeqpConfig->heap, 1, sfxSeqpConfig->maybeSndStateCount * sizeof(struct ALSoundState_s));
    g_sndPlayerPtr->sndState = sState;

    /*
     * init the event queue
     */
    ptr = alHeapAlloc(sfxSeqpConfig->heap, 1, sfxSeqpConfig->maxEvents * sizeof(ALEventListItem));
    alEvtqNew(&g_sndPlayerPtr->evtq, (ALEventListItem *)ptr, sfxSeqpConfig->maxEvents);

    D_800243E4.g_sndPlayerSoundStatePtr = g_sndPlayerPtr->sndState;

    for(i = 1; i < sfxSeqpConfig->maybeSndStateCount; i++)
    {
        // The compiler says this reassignment matters ...
        sState = (struct ALSoundState_s*)g_sndPlayerPtr->sndState;

        // this works because `ALLink node` is at offset zero.
        alLink((ALLink*)(&sState[i]), (ALLink*)(&sState[i]-1));
    }

    g_sndSfxSlotVolume = alHeapAlloc(sfxSeqpConfig->heap, sizeof(s16), SFX_SLOT_COUNT);
    g_sndSfxSlotNaturalVolume = alHeapAlloc(sfxSeqpConfig->heap, sizeof(s16), SFX_SLOT_COUNT);

    for(i = 0; i < SFX_SLOT_COUNT; i++)
    {
        g_sndSfxSlotNaturalVolume[i] = \
            g_sndSfxSlotVolume[i] = (s16)0x7FFF;
    }

    /*
     * add ourselves to the driver
     */
    g_sndPlayerPtr->drvr = &alGlobals->drvr;
    g_sndPlayerPtr->node.next = NULL;
    g_sndPlayerPtr->node.handler = &sndPlayerVoiceHandler;
    g_sndPlayerPtr->node.clientData = g_sndPlayerPtr;
    alSynAddPlayer(g_sndPlayerPtr->drvr, &g_sndPlayerPtr->node);

    /*
     * Start responding to API events
     */
    evt.type = AL_SNDP_API_EVT;
#ifndef TARGET_N64
    /* Vanilla sets only this event's type, leaving `state` as stack garbage that
     * sndRemoveEvents could match against a disposed state and purge the
     * heartbeat, so set it to NULL so it can never collide with a live state. */
    ((ALSndpEvent *) &evt)->common.state = NULL;
#endif
    portSndPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, g_sndPlayerPtr->frameTime);
    g_sndPlayerPtr->nextDelta = alEvtqNextEvent(&g_sndPlayerPtr->evtq, &g_sndPlayerPtr->nextEvent);
}

/**
 * 89DC    70007DDC
 *
 * Almost identical to \n64devkit\ultra\usr\src\pr\libsrc\libultra\audio\sndplayer.c
 * method ALMicroTime _sndpVoiceHandler(void *node).
 */
ALMicroTime sndPlayerVoiceHandler(void *node)
{
    ALSndPlayer *sndp = (ALSndPlayer *) node;
    ALSndpEvent evt;

#ifndef TARGET_N64
    /* Safety net: once the event queue is exhausted alEvtqNextEvent returns delta 0
     * with type -1 forever, which would spin this loop and freeze the game; the
     * cause is fixed in sndCreatePostEvent and this is the net. */
    s32 ge_spin = 0;
#endif

    do
    {
#ifndef TARGET_N64
        /* An empty allocated list means the self-perpetuating API heartbeat was
         * lost (its re-post can lose to a full queue), leaving nothing to wake
         * the player, so post a new one instead of spinning on type -1. */
        if (sndp->nextEvent.type == -1)
        {
            static s32 healed;
            ALSndpEvent hb;

            hb.common.type = (s16) AL_SNDP_API_EVT;
            hb.common.state = NULL;
            portSndPostEvent(&sndp->evtq, (ALEvent *) &hb, sndp->frameTime);
            sndp->nextDelta = alEvtqNextEvent(&sndp->evtq, &sndp->nextEvent);

            if (!healed)
            {
                extern int printf(const char *, ...);
                healed = 1;
                printf("ge: snd: event queue exhausted - heartbeat lost, re-posted\n");
            }
            if (sndp->nextDelta == 0)
            {
                return sndp->frameTime; /* the post itself failed - try again next frame */
            }
            break;
        }
        if (++ge_spin > 4096)
        {
            static s32 reported;
            if (!reported)
            {
                extern int printf(const char *, ...);
                reported = 1;
                printf("ge: snd: event queue exhausted (nextDelta stuck at 0) - breaking out\n");
            }
            return sndp->frameTime;
        }
#endif
        switch (sndp->nextEvent.type)
        {
            case (AL_SNDP_API_EVT):
                evt.common.type = (s16)AL_SNDP_API_EVT;
#ifndef TARGET_N64
                evt.common.state = NULL; /* see sndNewPlayerInit - never let it name a state */
#endif
                portSndPostEvent(&sndp->evtq, (ALEvent *)&evt, sndp->frameTime);
                break;

            default:
                sndHandleEvent(sndp, (ALSndpEvent *)&sndp->nextEvent);
                break;
        }

        sndp->nextDelta = alEvtqNextEvent(&sndp->evtq, &sndp->nextEvent);

    } while (sndp->nextDelta == 0);

    sndp->curTime += sndp->nextDelta;

    return sndp->nextDelta;
}


void sndHandleEvent(ALSndPlayer *sndp, ALSndpEvent *event) {
    ALVoiceConfig config;
    ALVoice *voice;  // dead but load-bearing. Do not remove.
    s32 delta;
    s32 limitReached;
    ALSndpEvent spAC;
    ALSndpEvent nextStateEvent;
    ALSound *sound;
    ALKeyMap *keyMap;
    s32 volume;
    s32 fxMix;
    s32 isEventForSingleSound;
    ALPan pan;
    s32 lastInSequence;
    s32 isVoiceAllocated;
    ALSoundState *soundState;
    ALSoundState *nextState;

    lastInSequence = TRUE;
    isVoiceAllocated = FALSE;
    nextState = NULL;

    do {
        if (nextState != NULL) {
            // NB: soundState is uninitialised on the first pass — original bug, preserved.
            nextStateEvent.common.state = soundState;
            nextStateEvent.common.type = event->common.type;
            nextStateEvent.vol.vol = event->vol.vol;
            event = &nextStateEvent;
        }

        soundState = event->common.state;
        sound = soundState->sound;

        if (sound == NULL) {
            s16 numFree, numAlloc;
            sndCountAllocList(&numFree, &numAlloc);
            return;
        }

        keyMap = sound->keyMap;
        nextState = (ALSoundState *) soundState->link.next;

        switch (event->common.type) {
            case AL_SNDP_PLAY_EVT:
                if (1) {} // fake to get s5/s6 swapped
                if (soundState->playingState != SOUND_STATE_INIT && soundState->playingState != SOUND_STATE_WAIT_VOICE) {
                    return;
                }

                config.fxBus = 0;
                config.priority = soundState->priority;
                config.unityPitch = 0;

                limitReached = sndp->maxSounds <= g_sndAllocatedVoicesCount;

                if (!limitReached || (soundState->unk3e & SOUND_FLAG_RETRIGGER)) {
                    // for retriggered sounds, ignore the limit
                    isVoiceAllocated = alSynAllocVoice(sndp->drvr, &soundState->voice, &config);
                }

                if (!isVoiceAllocated) {
                    // No free voices available, wait for another sound to stop.
                    if ((soundState->unk3e & (SOUND_FLAG_RETRIGGER | SOUND_FLAG_LOOPED)) || soundState->unk38 > 0) {
                        // Retry on the next frame.
                        // For looped and retriggered sounds, keep retrying on each frame.
                        soundState->playingState = SOUND_STATE_WAIT_VOICE;
                        soundState->unk38--;
                        portSndPostEvent(&sndp->evtq, (ALEvent *) event, DELTA_33_MS);
                    } else {
                        // Not a looped or retriggered sound, and all retries have been exhausted.
                        if (limitReached) {
                            // Check if we can preempt a lower-priority sound.
                            ALSoundState *iterState = (ALSoundState *) D_800243E4.node.prev;

                            do {
                                if (!(iterState->unk3e & 0x12) && (iterState->unk3e & 0x4) &&
                                    iterState->playingState != SOUND_STATE_PREEMPT) {
                                    // Found a lower-priority sound; it can be preempted
                                    ALSndpEvent interruptEvent;

                                    interruptEvent.common.type = AL_SNDP_END_EVT;
                                    interruptEvent.common.state = iterState;
                                    iterState->playingState = SOUND_STATE_PREEMPT;
                                    limitReached = FALSE;
                                    portSndPostEvent(&sndp->evtq, (ALEvent *) &interruptEvent, DELTA_1_MS);
                                    alSynSetVol(sndp->drvr, &iterState->voice, (soundState->playingState == 1) * 0, DELTA_1_MS); // FAKE
                                }
                                iterState = (ALSoundState *) iterState->link.prev;
                            } while (limitReached && iterState != NULL);

                            if (!limitReached) {
                                // Retry the sound that was preempted.
                                soundState->unk38 = 2;
                                portSndPostEvent(&sndp->evtq, (ALEvent *) event, DELTA_1_MS + 1);
                            } else {
                                // No lower-priority sound to preempt, so stop the sound.
                                sndDisposeSound(soundState);
                            }
                        } else {
                            // It seems the developers made a mistake with the logic here.
                            // Should we stop the sound immediately if the maximum number of sounds hasn't been reached?
                            // Perhaps it would be better to look for a sound to preempt, just like when the limit is
                            // reached. It's strange that we only check for sounds to preempt when the limit is reached,
                            // but not when it hasn't been.
                            sndDisposeSound(soundState);
                        }
                    }
                    return;
                }

                // Set volume
                soundState->unk3e |= SOUND_FLAG_PLAYING;
                alSynStartVoice(sndp->drvr, &soundState->voice, sound->wavetable);
                soundState->playingState = SOUND_STATE_PLAYING;
                g_sndAllocatedVoicesCount++;

                delta = sound->envelope->attackTime / soundState->pitch_2c / soundState->pitch_28;
                volume =
                    MAX(0, g_sndSfxSlotVolume[SOUND_PARAM_GROUP(keyMap)] *
                                   (sound->envelope->attackVolume * soundState->vol * sound->sampleVolume / 16129) /
                                   AL_SNDP_GROUP_VOLUME_MAX - 1);
                alSynSetVol(sndp->drvr, &soundState->voice, 0, 0);
                alSynSetVol(sndp->drvr, &soundState->voice, volume, delta);

                // Set pan
                pan = MIN(MAX((soundState->pan + sound->samplePan - AL_PAN_CENTER), AL_PAN_LEFT), AL_PAN_RIGHT);
                alSynSetPan(sndp->drvr, &soundState->voice, pan);

                // Set pitch
                alSynSetPitch(sndp->drvr, &soundState->voice, soundState->pitch_2c * soundState->pitch_28);

                // Set FX mix
                //!@bug: SOUND_PARAM_FXMIX is allocated only four bits, so it needs to be multiplied by 8
                // to scale it to a range of 0 to 127.
                // However, it's unclear why soundState->fxmix also needs to be multiplied by 8.
                // The same issue appears in the AL_SNDP_FX_EVT handler.
                fxMix = (soundState->fxMix + SOUND_PARAM_FXMIX(keyMap)) * 8;
                fxMix = MIN(127, MAX(0, fxMix));
                alSynSetFXMix(sndp->drvr, &soundState->voice, fxMix);

                // Queue the decay event
                spAC.common.type = AL_SNDP_DECAY_EVT;
                spAC.common.state = soundState;
                portSndPostEvent(&sndp->evtq, (ALEvent *) &spAC,
                                 sound->envelope->attackTime / soundState->pitch_2c / soundState->pitch_28);
                break;
            case AL_SNDP_STOP_EVT:
            case AL_SNDP_DEACTIVATE_EVT:
            case AL_SNDP_UNKNOWN_12_EVT:
                // If any sound in the composite sound is in the release phase, ignore this event for all other sounds
                // in the sequence, because they haven't started yet.
                // However, if the other sound is looped, process the event anyway.
                //
                // The purpose of checking for a looped sound seems unclear.
                // Does it imply that a composite sound can't contain looped simple sounds?
                // It seems logical, but the check may still be redundant.
                if (event->common.type != AL_SNDP_UNKNOWN_12_EVT || (soundState->unk3e & SOUND_FLAG_LOOPED)) {
                    switch (soundState->playingState) {
                        case SOUND_STATE_PLAYING:
                            sndRemoveEvents(&sndp->evtq, soundState, AL_SNDP_DECAY_EVT);
                            delta = sound->envelope->releaseTime / soundState->pitch_28 / soundState->pitch_2c;
                            alSynSetVol(sndp->drvr, &soundState->voice, 0, delta);
                            if (delta != 0) {
                                spAC.common.type = AL_SNDP_END_EVT;
                                spAC.common.state = soundState;
                                portSndPostEvent(&sndp->evtq, (ALEvent *) &spAC, delta);
                                soundState->playingState = SOUND_STATE_STOPPING;
                            } else {
                                sndDisposeSound(soundState);
                            }
                            break;
                        case SOUND_STATE_WAIT_VOICE:
                        case SOUND_STATE_INIT:
                            sndDisposeSound(soundState);
                            break;
                    }
                    if (event->common.type == AL_SNDP_STOP_EVT) {
                        event->common.type = AL_SNDP_UNKNOWN_12_EVT;
                    }
                }
                break;
            case AL_SNDP_PAN_EVT:
                soundState->pan = event->pan32.pan32;
                if (soundState->playingState == SOUND_STATE_PLAYING) {
                    pan = MIN(MAX((soundState->pan + sound->samplePan - AL_PAN_CENTER), AL_PAN_LEFT), AL_PAN_RIGHT);
                    alSynSetPan(sndp->drvr, &soundState->voice, pan);
                }
                break;
            case AL_SNDP_PITCH_EVT:
                soundState->pitch_2c = event->pitch.pitch;
                if (soundState->playingState == SOUND_STATE_PLAYING) {
                    alSynSetPitch(sndp->drvr, &soundState->voice, soundState->pitch_2c * soundState->pitch_28);
                    if (soundState->unk3e & SOUND_FLAG_PITCH_SLIDE) {
                        sndCreatePitchEvent(soundState);
                    }
                }
                break;
            case AL_SNDP_FX_EVT:
                soundState->fxMix = event->fx32.mix32;
                if (soundState->playingState == SOUND_STATE_PLAYING) {
                    //!@bug: unnecessary multiplication by 8, as in AL_SNDP_PLAY_EVT.
                    // The same issue appears in the AL_SNDP_PLAY_EVT handler.
                    fxMix = (soundState->fxMix + SOUND_PARAM_FXMIX(keyMap)) * 8;
                    fxMix = MIN(127, MAX(0, fxMix));
                    alSynSetFXMix(sndp->drvr, &soundState->voice, fxMix);
                }
                break;
            case AL_SNDP_VOL_EVT:
                soundState->vol = event->vol.vol;
                if (soundState->playingState == SOUND_STATE_PLAYING) {
                    volume = MAX(
                        0, g_sndSfxSlotVolume[SOUND_PARAM_GROUP(keyMap)] *
                                   (sound->envelope->decayVolume * soundState->vol * sound->sampleVolume / 16129) /
                                   AL_SNDP_GROUP_VOLUME_MAX -
                               1);
                    alSynSetVol(sndp->drvr, &soundState->voice, volume, 1000);
                }
                break;
            case AL_SNDP_RELEASE_EVT:
                if (soundState->playingState == SOUND_STATE_PLAYING) {
                    delta = sound->envelope->releaseTime / soundState->pitch_28 / soundState->pitch_2c;
                    volume = MAX(
                        0, g_sndSfxSlotVolume[SOUND_PARAM_GROUP(keyMap)] *
                                   (sound->envelope->decayVolume * soundState->vol * sound->sampleVolume / 16129) /
                                   AL_SNDP_GROUP_VOLUME_MAX -
                               1);
                    alSynSetVol(sndp->drvr, &soundState->voice, volume, delta);
                }
                break;
            case AL_SNDP_DECAY_EVT:
                /*
                 * The voice has theoretically reached its attack velocity,
                 * set up callback for release envelope - except for a looped sound
                 */
                if (!(soundState->unk3e & SOUND_FLAG_LOOPED)) {
                    volume = MAX(
                        0, g_sndSfxSlotVolume[SOUND_PARAM_GROUP(keyMap)] *
                                   (sound->envelope->decayVolume * soundState->vol * sound->sampleVolume / 16129) /
                                   AL_SNDP_GROUP_VOLUME_MAX -
                               1);
                    delta = sound->envelope->decayTime / soundState->pitch_28 / soundState->pitch_2c;
                    alSynSetVol(sndp->drvr, &soundState->voice, volume, delta);

                    spAC.common.type = AL_SNDP_STOP_EVT;
                    spAC.common.state = soundState;
                    portSndPostEvent(&sndp->evtq, (ALEvent *) &spAC, delta);

                    // Start applying the pitch slide only when the decay phase is reached.
                    if (soundState->unk3e & SOUND_FLAG_PITCH_SLIDE) {
                        sndCreatePitchEvent(soundState);
                    }
                }
                break;
            case AL_SNDP_END_EVT:
                sndDisposeSound(soundState);
                break;
            case AL_SNDP_PLAY_SFX_EVT:
                if (soundState->unk3e & SOUND_FLAG_RETRIGGER) {
                    sndPlaySfx(event->playSfx.soundBank, event->playSfx.soundIndex, soundState->state);
                }
                break;
            default:
                break;
        }

        soundState = nextState;
        isEventForSingleSound = event->common.type & (AL_SNDP_PLAY_EVT | AL_SNDP_PITCH_EVT | AL_SNDP_DECAY_EVT |
                                                      AL_SNDP_END_EVT | AL_SNDP_PLAY_SFX_EVT);

        if (soundState != NULL && !isEventForSingleSound) {
            lastInSequence = soundState->unk3e & SOUND_FLAG_FINAL_IN_SEQUENCE;
        }

    } while (!lastInSequence && soundState != NULL && !isEventForSingleSound);
}


/**
 * 9548    70008948
 */
void sndDisposeSound(ALSoundState *state)
{
    if (state->unk3e & 4)
    {
        alSynStopVoice(g_sndPlayerPtr->drvr, &state->voice);
        alSynFreeVoice(g_sndPlayerPtr->drvr, &state->voice);
    }

    sndUnlinkClearSound(state);
    sndRemoveEvents(&g_sndPlayerPtr->evtq, state, 0xffff);
#ifndef TARGET_N64
    {
        /* A deferred volume/pan must die with its sound, or the next sndPlaySfx
         * that reuses this state would get the old sound's volume at the next flush. */
        extern void portSndDropDeferred(ALSoundState *s);
        portSndDropDeferred(state);
    }
#endif
}

/**
 * 95C4    700089C4
 */
void sndCreatePitchEvent(ALSoundState *state)
{
    ALSndpEvent evt;
    f32 pitch;

    pitch = (f32) (alCents2Ratio(state->sound->keyMap->detune) * (f32)state->pitch_2c);
    evt.pitch.state = state;
    evt.pitch.type = AL_SNDP_PITCH_EVT;

    // TODO: surely there's a better way to match target, especially since there's already a union type used with f32 for pitch.
    evt.unks32.val8 = *(s32*)&pitch;

    portSndPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, DELTA_33_MS);
}

/**
 * 9630     70008A30
 * Based on (almost identical to) the method
 * static void _removeEvents(ALEventQueue *evtq, ALSoundState *state)
 * from n64devkit\ultra\usr\src\pr\libsrc\libultra\audio\sndplayer.c
 */
void sndRemoveEvents(ALEventQueue *evtq, ALSoundState *state, u16 eventType)
{
    ALLink              *thisNode;
    ALLink              *nextNode;
    ALEventListItem     *thisItem;
    ALEventListItem     *nextItem;
    ALSndpEvent         *thisEvent;
    OSIntMask           mask;

    mask = osSetIntMask(OS_IM_NONE);

    thisNode = evtq->allocList.next;

    while(thisNode != NULL)
    {
	    nextNode = thisNode->next;
        thisItem = (ALEventListItem *)thisNode;
        nextItem = (ALEventListItem *)nextNode;
        thisEvent = (ALSndpEvent *)&thisItem->evt;

        if (thisEvent->common.state == state && (((u16)thisItem->evt.type & (u16)eventType) != 0)
#ifndef TARGET_N64
            /* Never purge the heartbeat: AL_SNDP_API_EVT is the only self-reposting
             * event and has no owning state, so losing it stops the player's wake-ups. */
            && thisEvent->common.type != AL_SNDP_API_EVT
#endif
           )
        {
            if (nextItem != NULL)
            {
                nextItem->delta += thisItem->delta;
            }

            alUnlink(thisNode);
            alLink(thisNode, &evtq->freeList);
        }

	    thisNode = nextNode;
    }

    osSetIntMask(mask);
}

/**
 * 96F0     70008AF0
 * Has similarities to
 * void alEvtqPrintEvtQueue(ALEventQueue *evtq)
 * from n64devkit\ultra\usr\src\pr\libsrc\libultra\audio\event.c
 *
 * @param allocListCount Out param. Will contain the number of (next) nodes in the D_800243E4 allocList.
 * @param freeListCount Out param. Will contain the number of (next) nodes in the D_800243E4 freeList.
 * @return Number of (prev) nodes in the D_800243E4 freeList.
 */
s32 sndCountAllocList(s16 *allocListCount, s16 *freeListCount)
{
    u16 counter1;
    u16 counter2;
    u16 returnCounter;

    ALEventQueue *evtq = (ALEventQueue *)&D_800243E4;

    ALLink *freeListNodeForward = evtq->freeList.next;
    ALLink *allocListNodeForward = evtq->allocList.next;
    ALLink *freeListNodeBackward = evtq->freeList.prev;

    for (counter1 = 0; freeListNodeForward != NULL; freeListNodeForward = freeListNodeForward->next)
    {
         counter1++;
    }

    for (counter2 = 0; allocListNodeForward != NULL; allocListNodeForward = allocListNodeForward->next)
    {
         counter2++;
    }

    for (returnCounter = 0; freeListNodeBackward != NULL; freeListNodeBackward = freeListNodeBackward->prev)
    {
         returnCounter++;
    }

    *allocListCount = (s16) counter2;
    *freeListCount = (s16) counter1;

    return returnCounter;
}

/**
 * 9770    70008B70
 * initializes soundstate to sound based on global g_sndPlayerSoundStatePtr.
 *     accepts: A0=sound data offset?, A1=sample address?
 *
 * @param soundBank unused.
 * @param sound sound to use.
 */
ALSoundState *sndSetupSound(struct ALBankAlt_s *soundBank, ALSound* sound)
{
    s32 decayTimeFlag;
    ALKeyMap *keymap = sound->keyMap;
    ALSoundState *state = (ALSoundState *)D_800243E4.g_sndPlayerSoundStatePtr;
    OSIntMask mask;

    if (state != NULL)
    {
        mask = osSetIntMask(OS_IM_NONE);

        D_800243E4.g_sndPlayerSoundStatePtr = (void *)state->link.next;
        alUnlink(&state->link);

        if (D_800243E4.node.next != NULL)
        {
            state->link.next = (void *)D_800243E4.node.next;
            state->link.prev = NULL;
            D_800243E4.node.next->prev = (void *)state; // what?
            D_800243E4.node.next = (void *)state;
        }
        else
        {
            state->link.prev = NULL;
            state->link.next = NULL;
            D_800243E4.node.next = (void *)state;
            D_800243E4.node.prev = (void *)state;
        }

        osSetIntMask(mask);

        decayTimeFlag = (sound->envelope->decayTime == -1);
        state->priority = decayTimeFlag + 0x40;

        state->playingState = AL_UNKOWN_5;
        state->unk38 = 2;
        state->sound = sound;
        state->pitch_2c = 1.0f;
        state->unk3e = (keymap->keyMax & (u8)0xf0);
        state->state = NULL;

        if ((state->unk3e & 0x20) != 0)
        {
            state->pitch_28 = alCents2Ratio(((keymap->keyBase * 100) + DEFAULT_SETUP_PITCH_SHIFT));
        }
        else
        {
            state->pitch_28 = alCents2Ratio((((keymap->keyBase * 100) + keymap->detune) + DEFAULT_SETUP_PITCH_SHIFT));
        }

        if (decayTimeFlag)
        {
            state->unk3e |= 2;
        }

        state->fxMix = (u8)AL_DEFAULT_FXMIX;
        state->pan = (u8)AL_PAN_CENTER;
        state->vol = (u16)0x7fff;
#ifndef TARGET_N64
        /* STEREO PANNING: this state has just been claimed off the free
         * list for a new sound, so it must not inherit the previous
         * occupant's emitter - it would be re-panned to wherever that one
         * was until its own owner posts. This is the whole lifetime
         * story for the tracking (see snd.h). */
        state->portPanTracked = 0;
#endif
    }

    return state;
}

#ifndef TARGET_N64
/**
 * Head of the allocated (live) sound state list. sndSetupSound pushes onto
 * it and sndUnlinkClearSound removes from it, so it is exactly the set of
 * sounds that still exist - which is what makes it safe for propobj.c
 * portSndPanTick to walk without any liveness bookkeeping of its own.
 */
ALSoundState *sndGetAllocatedList(void)
{
    return (ALSoundState *) D_800243E4.node.next;
}
#endif


#ifndef TARGET_N64
/**
 * Stage unload kill: stop every live SFX state synchronously after the
 * cleanup*() calls have run.
 *
 * The cleanups stop sounds by posting AL_SNDP_DEACTIVATE_EVT into the sound
 * player's fixed-size event queue, which silently drops posts when full and is
 * only drained by the audio pump after the whole teardown (on N64 the audio
 * thread drained it as it went). Late posts, such as the tank engine's, could
 * be lost, leaving a looping sound nobody can stop once the next stage resets
 * its owner pointer.
 *
 * Each state is disposed exactly as the END handler would (stop and free the
 * synth voice, unlink, purge its queued events, clear the owner back-pointer so
 * g_TankSfxState etc. read NULL). Main thread only: portAudioPump runs on the
 * same thread, so the player is never mid-callback here. Returns the number of
 * states killed; `voices` = how many owned a synth voice.
 */
s32 portSndKillAllSfx(s32 *voices)
{
    s32 n = 0, v = 0;
    ALSoundState *state = (ALSoundState *) D_800243E4.node.next;

    while (state != NULL)
    {
        ALSoundState *next = (ALSoundState *) state->link.next;
        if (state->unk3e & 4)
        {
            v++;
        }
        sndDisposeSound(state);
        n++;
        state = next;
    }
    /* The player holds one dequeued event (nextEvent) that sndRemoveEvents can't
     * reach; if it names a freed state, turn it into a harmless PAN event so it
     * can't dispose the state twice (the API heartbeat is left alone). */
    if (n > 0 && g_sndPlayerPtr->nextEvent.type != AL_SNDP_API_EVT)
    {
        ((ALSndpEvent *) &g_sndPlayerPtr->nextEvent)->common.type = AL_SNDP_PAN_EVT;
    }
    if (voices != NULL)
    {
        *voices = v;
    }
    return n;
}
#endif

/**
 * 9904    70008D04
 * some kind of dispose method, unlinks next/prev pointers.
 */
void sndUnlinkClearSound(ALSoundState *state)
{
    if (state == (ALSoundState *)D_800243E4.node.next)
    {
        D_800243E4.node.next = state->link.next;
    }

    if (state == (ALSoundState *)D_800243E4.node.prev)
    {
        D_800243E4.node.prev = state->link.prev;
    }

    alUnlink(&state->link);

    if (D_800243E4.g_sndPlayerSoundStatePtr != NULL)
    {
        state->link.next = (void *)D_800243E4.g_sndPlayerSoundStatePtr;
        state->link.prev = NULL;
        D_800243E4.g_sndPlayerSoundStatePtr->link.prev = (void *)state;
        D_800243E4.g_sndPlayerSoundStatePtr = state;
    }
    else
    {
        state->link.prev = NULL;
        state->link.next = NULL;
        D_800243E4.g_sndPlayerSoundStatePtr = state;
    }

    if ((state->unk3e & 4) != 0)
    {
        g_sndAllocatedVoicesCount--;
    }

    state->playingState = AL_STOPPED;

    if (state->state != NULL)
    {
        if (state == (ALSoundState *)state->state->link.next)
        {
            state->state->link.next = NULL;
        }

        state->state = NULL;
    }
}

/**
 * 99D8    70008DD8
 * Sets priority of ALSoundState.
 */
void sndSetPriority(ALSoundState *state, u8 priority)
{
    if (state != NULL)
    {
        state->priority = priority;
    }
}

/**
 * 99F0    70008DF0
 * Gets Playing State if a state is available
 * @param state: the state to check
 * @return AL_PLAYSTATE
 */
u8 sndGetPlayingState(ALSoundState *state)
{
    if (state != NULL)
    {
        return state->playingState;
    }

    return AL_STOPPED;
}

#ifdef DEBUG
#    define sndPlaySfx(sbank, id, state) _sndPlaySfx(sbank, id, state, g_sndSfxVolume, __FILE__, __LINE__)
ALSoundState *_sndPlaySfx(struct ALBankAlt_s *soundBank, s16 soundIndex, ALSoundState *pendingState, f32 volume, char*file, int line)
#else
/**
 * 9A08    70008E08
 *     sets sound effect; used by sound effect routines
 *
 * Old comments:
 *
 *     accepts: A0=p->SE buffer, A1=SE #, A2=p->data
 *          data:    0x0    4    p->SE entry
 *              0x4    4    target volume
 *              0x8    4    audible range (timer)
 *              0xC    4    initial volume
 *              0x10    4    p->preset emitting sound
 *              0x14    4    p->object emitting sound
 *
 * // end old comments.
 *
 * @param soundBank sound bank
 * @param soundIndex index into sound bank: soundBank->instArray[0]->soundArray[soundIndex]
 * @param pendingState Optional pointer. If provided, its link.next pointer will be
 * to the newly created soundState.
 */
ALSoundState *sndPlaySfx(struct ALBankAlt_s *soundBank, s16 soundIndex, ALSoundState *pendingState)
#endif
{
    // declarations

    // declaration order doesn't seem to matter for these.

    ALMicroTime deltaTotal;
    ALSound *sound;
    ALSoundState *newState;
    ALSoundState *nextState;

    // declaration order matters:

    s16 eventSoundIndex;       // 110(sp)
    s16 unused_sp6c;           // 108(sp)
    ALMicroTime playSfxDelta;  // 104(sp)
    ALMicroTime deltaLoop; // 100(sp)

    // end declarations

    nextState = NULL;
    eventSoundIndex = 0;
    deltaTotal = 0;

    if(0); // debug?

    if (g_sndBootswitchSound)
    {
        return NULL;
    }

    if (soundIndex == 0)
    {
        return NULL;
    }

    do
    {
        ALKeyMap *keyMap;

#ifdef GE_NATIVE_64
        /* GE's SFX IDs are one-based; its N64 Alt view starts one word early. */
        sound = ((ALBank *)soundBank)->instArray[0]->soundArray[soundIndex - 1];
#else
        sound = (soundBank->instArray[0]->soundArray[soundIndex]);
#endif

        newState = sndSetupSound(soundBank, sound);

        if (newState != NULL)
        {
            ALSndpEvent playEvent;

            g_sndPlayerPtr->target = (intptr_t)newState;
            playEvent.common.type = AL_SNDP_PLAY_EVT;
            playEvent.common.state = newState;
            deltaLoop = sound->keyMap->velocityMax * DELTA_33_MS;

            if (newState->unk3e & 0x10)
            {
                newState->unk3e &= (~(s16)(0x10));
                portSndPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&playEvent, deltaTotal + 1);
                playSfxDelta = deltaLoop + 1;
                eventSoundIndex = soundIndex;
            }
            else
            {
                portSndPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&playEvent, deltaLoop + 1);
            }

            nextState = newState;
        }

        deltaTotal += deltaLoop;

        keyMap = sound->keyMap;
        soundIndex = (s16)((s32)keyMap->velocityMin + ((s32)(keyMap->keyMin & 0xC0) * 4));
    } while (soundIndex != 0 && newState != NULL);

    if(!soundIndex)
    {
        // removed
    }

    if(!sound)
    {
        // removed
    }

    if (nextState != NULL)
    {
        nextState->unk3e |= 0x1;
        nextState->state = pendingState;

        if (eventSoundIndex != 0)
        {
            ALSndpEvent playSfxEvent;

            nextState->unk3e |= 0x10;

            playSfxEvent.playSfx.type = AL_SNDP_PLAY_SFX_EVT;
            playSfxEvent.playSfx.state = nextState;
            playSfxEvent.playSfx.soundIndex = eventSoundIndex; // types dont match
            playSfxEvent.playSfx.soundBank = soundBank;

            portSndPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&playSfxEvent, playSfxDelta);
        }
    }

    if (pendingState != NULL)
    {
        pendingState->link.next = (void*)nextState;
    }

    return nextState;
}

/**
 * 9C20    70009020
 *     decativates sound effect
 *     accepts: A0=p->SE buffer
 */
void sndDeactivate(ALSoundState *state)
{
    ALSndpEvent evt;

    evt.common.type = AL_SNDP_DEACTIVATE_EVT;
    evt.common.state = state;

    if (state != NULL)
    {
        state->unk3e = (s8) (state->unk3e & (~(s16)(0x10)));

        portSndPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, 0);
    }
}

/**
 * 9C6C    7000906C
 * Similar to sndDeactivate, but iterates the global list and deactivates
 * items with the same unk3e flag.
 *
 * @param flag flag bitmask to match item on.
 */
void sndDeactivateAllSfxByFlag(u8 flag)
{
    OSIntMask mask;
    ALSndpEvent evt;
    ALSoundState *item;

    mask = osSetIntMask(OS_IM_NONE);

    item = (ALSoundState *)D_800243E4.node.next;
    while (item != NULL)
    {
        evt.common.type = AL_SNDP_DEACTIVATE_EVT;
        evt.common.state = item;

        if ((item->unk3e & flag) == flag)
        {
            item->unk3e = (s8) (item->unk3e & (~(s16)(0x10)));
            portSndPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, 0);
        }

        item = (ALSoundState *)item->link.next;
    }

    osSetIntMask(mask);
}

/**
 * 9D24    70009124
 *     redirect to 7000906C: A0=1
 */
void sndDeactivateAllSfxByFlag_1(void)
{
    sndDeactivateAllSfxByFlag(1);
}

/**
 * 9D44    70009144
 *     redirect to 7000906C: A0=11
 */
void sndDeactivateAllSfxByFlag_11(void)
{
    sndDeactivateAllSfxByFlag(0x11);
}

/**
 * 9D64    70009164
 *     redirect to 7000906C: A0=3
 */
void sndDeactivateAllSfxByFlag_3(void)
{
    sndDeactivateAllSfxByFlag(3);
}

/**
 * 9D84    70009184
 * Calls alEvtqPostEvent with the method parameters and delta=0.
 *
 * @param state sound state.
 * @param eventType type of event to post.
 * @param arg2 event data value (interpretation depends on eventType).
 */
#ifndef TARGET_N64
/* Sub-tick volume and pan deferral (GE_TICKHZ).
 *
 * At raised tick rates, volume/pan posts that land on a sub-tick are deferred
 * rather than dropped: one-shot posts (like a weapon-drop clatter's distance
 * volume) would otherwise vanish and play at full volume. Entries coalesce per
 * (state, type), so a sound holds at most one pending volume and one pending
 * pan and the flood stays bounded; the table is flushed on the next field tick
 * (lv.c clock advance) and is sized for both types. */
#define SND_DEFERRED_VOLS 32
static ALSoundState *s_DeferredVolState[SND_DEFERRED_VOLS];
static s32 s_DeferredVolValue[SND_DEFERRED_VOLS];
static s16 s_DeferredVolType[SND_DEFERRED_VOLS];
static s32 s_DeferredVolCount = 0;

void sndFlushDeferredVolumes(void)
{
    s32 i;

    for (i = 0; i < s_DeferredVolCount; i++)
    {
        ALSndpEvent evt;

        evt.common.type = s_DeferredVolType[i];
        evt.common.state = s_DeferredVolState[i];
        evt.unks32.val8 = s_DeferredVolValue[i];
        portSndPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, 0);
    }
    s_DeferredVolCount = 0;
}

/* forget every deferred entry for a state that is being disposed
 * (sndDisposeSound: end, stop, preempt, stage unload all come through it) */
void portSndDropDeferred(ALSoundState *s)
{
    s32 i = 0;

    while (i < s_DeferredVolCount)
    {
        if (s_DeferredVolState[i] == s)
        {
            s_DeferredVolCount--;
            s_DeferredVolState[i] = s_DeferredVolState[s_DeferredVolCount];
            s_DeferredVolValue[i] = s_DeferredVolValue[s_DeferredVolCount];
            s_DeferredVolType[i] = s_DeferredVolType[s_DeferredVolCount];
            continue; /* re-examine the entry moved into slot i */
        }
        i++;
    }
}
#endif

#ifndef TARGET_N64
/**
 * Gadget equip cue: play a one-shot sfx at a pitch multiplier (2.0 = double
 * speed). The pitch is posted as a float directly, because sndCreatePostEvent
 * writes an s32 into a slot AL_SNDP_PITCH_EVT reads as f32. It is a one-shot,
 * so the GE_TICKHZ deferral does not apply.
 */
ALSoundState *portSndPlaySfxPitched(void *soundBankv, s16 soundIndex, f32 pitch)
{
    struct ALBankAlt_s *soundBank = (struct ALBankAlt_s *) soundBankv;
    ALSoundState *state = sndPlaySfx(soundBank, soundIndex, NULL);

    if (state != NULL && pitch > 0.0f)
    {
        ALSndpEvent evt;

        evt.pitch.type = AL_SNDP_PITCH_EVT;
        evt.pitch.state = state;
        evt.pitch.pitch = pitch;
        portSndPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, 0);
    }
    return state;
}
#endif

void sndCreatePostEvent(ALSoundState *state, s16 eventType, s32 arg2)
{
    ALSndpEvent evt;

    evt.common.type = eventType;
    evt.common.state = state;
    evt.unks32.val8 = arg2;

    if (state != NULL)
    {
#ifndef TARGET_N64
        /* Every event here posts at delta 0; at raised tick rates the per-frame
         * volume/pan refreshers would flood the queue and stall the player, so
         * sub-tick volume/pan posts are deferred and coalesced (see
         * sndFlushDeferredVolumes), while one-shot events are never deferred. */
        extern s32 g_ClockTimer;
        extern s32 lvlGetControlsLockedFlag(void);
        extern s32 checkGamePaused(void);
        s32 deferrable = (eventType == AL_SNDP_VOL_EVT || eventType == AL_SNDP_PAN_EVT);

        /* A sound that hasn't started yet (INIT / WAIT_VOICE) reads ->vol and ->pan
         * when it starts, so write them by running the handler now instead of
         * queuing an event that could be dropped (and play at full volume).
         * Playing sounds keep the queued/deferred path. */
        if (deferrable && state->playingState != SOUND_STATE_PLAYING
            && state->playingState != SOUND_STATE_STOPPING)
        {
            sndHandleEvent(g_sndPlayerPtr, &evt);
            return;
        }

        /* Defer only a real raised-tick sub-tick: g_ClockTimer is also 0 while
         * paused or locked, and the table only flushes on a running clock, so
         * paused posts go straight to the queue (which coalesces them). */
        if (deferrable && g_ClockTimer == 0 && lvlGetControlsLockedFlag() == 0 && !checkGamePaused())
        {
            s32 i;

            for (i = 0; i < s_DeferredVolCount; i++)
            {
                if (s_DeferredVolState[i] == state && s_DeferredVolType[i] == eventType)
                {
                    s_DeferredVolValue[i] = arg2;
                    return;
                }
            }
            if (s_DeferredVolCount < SND_DEFERRED_VOLS)
            {
                s_DeferredVolState[s_DeferredVolCount] = state;
                s_DeferredVolValue[s_DeferredVolCount] = arg2;
                s_DeferredVolType[s_DeferredVolCount] = eventType;
                s_DeferredVolCount++;
            }
            return;
        }
        if (deferrable && s_DeferredVolCount != 0)
        {
            /* field tick arrived via a poster before the lv.c flush -
             * deliver the backlog first so this event stays newest */
            sndFlushDeferredVolumes();
        }
#endif
        portSndPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, 0);
    }
}

/**
 * 9DC8    700091C8
 *     redirect to 70009264: A0=0
 */
u16 sndGetSfxSlotFirstNaturalVolume(void)
{
    return sndGetSfxSlotNaturalVolume(0);
}

/**
 * 9DE8    700091E8
 */
void sndApplyVolumeAllSfxSlot(u16 volume)
{
    u8 i;

    for (i = 0; i < SFX_SLOT_COUNT; i++)
    {
        sndSetSfxSlotVolume(i, volume);
    }
}

/**
 * 9E38    70009238
 */
void sndSetScalerApplyVolumeAllSfxSlot(f32 volumeScale)
{
    g_sndSfxVolumeScale = volumeScale;
    sndApplyVolumeAllSfxSlot(sndGetSfxSlotFirstNaturalVolume());
}

/**
 * 9E64    70009264
 *     V0= halfword A0 in table at [80063BA8]; fries T6,T7,T8,T9
 */
u16 sndGetSfxSlotNaturalVolume(u8 sfxIndex)
{
    return g_sndSfxSlotNaturalVolume[sfxIndex];
}

/**
 * 9E84    70009284
 */
void sndSetSfxSlotVolume(u8 sfxIndex, u16 volume)
{
    // Not sure if these are debug leftovers, or is the type `ALSndpEvent` wrong?
    u32 unused[2];

    ALSndpEvent evt;
    ALSoundState *item;

    item = (ALSoundState *)D_800243E4.node.next;

    g_sndSfxSlotNaturalVolume[sfxIndex] = volume;
    g_sndSfxSlotVolume[sfxIndex] = (s16) ((f32) volume * g_sndSfxVolumeScale);

    while (item != NULL)
    {
        if (item->sound != NULL)
        {
            if ((item->sound->keyMap->keyMin & 0x3f) == sfxIndex)
            {
                evt.common.type = AL_SNDP_RELEASE_EVT;
                evt.common.state = item;

                portSndPostEvent(&g_sndPlayerPtr->evtq, (ALEvent *)&evt, 0);
            }
        }

        item = (ALSoundState *)item->link.next;
    }
}
