#ifndef _AUDIO_PACE_H_
#define _AUDIO_PACE_H_

/**
 * Render pacing policy for the PC audio backend.
 *
 * These are pure functions: no SDL, no globals, no clock reads, no ROM data,
 * and no dependency on the Ultra64 headers. port/src/audio_pc.c feeds them
 * observations and obeys the answers, and port/tests/audio_pace.c drives the
 * same functions under GE_PORT_TESTS. The policy under test is therefore the
 * policy that ships, which is the whole point of splitting them out - a test
 * of a reimplementation would only prove the test agreed with itself.
 *
 * The invariant these functions exist to protect: the AL synthesizer must keep
 * running on schedule whether or not an audio device is present. alAudioFrame
 * is the only caller of the AL client handlers, so it is also the only thing
 * that advances the SFX event queue, the sound-state list and the three music
 * sequencers. Letting device state stop it is what produced the silent-system
 * reports (#19, #56): an event queue pinned at 256 events, thousands dropped,
 * no voice ever allocated.
 *
 * The second invariant, and the one the latency work added: while a device IS
 * present and consuming, it - not the wall clock - must set the pace. Pacing to
 * the clock instead lets the backlog drift up to the queue cap on any device a
 * little slower than real time, and the backlog is exactly the delay between an
 * event firing and that sample reaching a speaker.
 */

/* Fixed-width spellings, identical in size to PR/ultratypes.h's s32/u32/u64.
 * Declared locally so this unit compiles with nothing but itself; the port_
 * prefix keeps them clear of the Ultra64 typedefs when both are in scope. */
typedef int                port_s32;
typedef unsigned int       port_u32;
typedef unsigned long long port_u64;

/**
 * Top-up watermark, in bytes. The pump refills only while the queue is below
 * this, so the steady-state backlog oscillates in
 * [PORT_AUDIO_QUEUE_LOW, PORT_AUDIO_QUEUE_LOW + one render quantum] and this
 * value is a direct addition to output latency. At 22050 Hz stereo s16
 * (88200 B/s) 2048 B = 23.2 ms.
 */
#define PORT_AUDIO_QUEUE_LOW (2 * 1024)

/**
 * Hard ceiling on the backlog. The policy keeps the queue far below this; the
 * ceiling only stops a bug from growing it without bound.
 */
#define PORT_AUDIO_QUEUE_MAX (16 * 1024)

/**
 * Consecutive pumps in which the device consumed nothing before it stops being
 * trusted as the pacing authority (~54 ms at 147 Hz).
 *
 * It must exceed the number of pumps that fit inside one SDL period, because a
 * healthy device legitimately consumes nothing on pumps that fall between two
 * of its period-sized drains. At 147 Hz the pump runs every 6.8 ms while a
 * 256-sample device period lasts 11.6 ms, so single-pump gaps are normal and a
 * threshold of 1 would misfire constantly.
 */
#define PORT_AUDIO_DRAIN_PUMPS 8

/**
 * Consecutive dead pumps before the device is closed and reopened (~0.8 s at
 * 147 Hz). Reopening is how a sink that was unplugged, or a sound server that
 * restarted, gets picked up again.
 */
#define PORT_AUDIO_STALL_PUMPS 120

/** Samples per device period requested from SDL (11.6 ms at 22050 Hz). */
#define PORT_AUDIO_DEV_SAMPLES 256

typedef struct {
    port_u32 at_exit;  /**< queue size left at the end of the previous pump */
    port_u32 famished; /**< consecutive pumps in which it consumed nothing   */
} PortAudioDrain;

typedef struct {
    port_s32 dev_live; /**< device open and still consuming        */
    port_u32 queued;   /**< bytes currently queued for it          */
    port_u64 owed;     /**< samples x1000 the wall clock owes      */
    port_u64 frame;    /**< samples x1000 one render quantum costs */
} PortAudioPace;

/**
 * Fold one pump's queue observation into the drain tracker.
 *
 * `queued` is the queue size seen BEFORE this pump renders anything, so
 * (at_exit - queued) is exactly what the device consumed since the last pump.
 * Returns 1 while the device is still consuming, 0 once it has gone quiet with
 * audio still queued.
 *
 * A queue of zero counts as live: there is nothing to drain yet, and on a
 * fresh device that is precisely the state the pump has to fill out of.
 */
port_s32 portAudioDrainTick(PortAudioDrain *d, port_u32 queued);

/** Has the sink been dead long enough to close and reopen it? */
port_s32 portAudioShouldReopen(const PortAudioDrain *d);

/**
 * Should the pump render another frame?
 *
 * A live device is the authority: refill to the watermark, then stop, so the
 * backlog cannot creep upward on a device running slower than real time.
 * With no device, or one that stopped consuming, the wall clock paces so the
 * game's audio clock still advances and the samples are simply discarded.
 */
port_s32 portAudioShouldRender(const PortAudioPace *p);

/**
 * Split one video field into render quanta.
 *
 * Field work stays whole - the synthesizer's per-field budget is unchanged -
 * but halving the chunk handed to alAudioFrame tightens how long a newly
 * triggered event waits for its first rendered sample. `max_samples` caps the
 * result (0 = uncapped); `frame_div` is the split (0 is treated as 1).
 */
port_u32 portAudioQuantum(port_u32 frame_size, port_u32 max_samples,
                          port_u32 frame_div);

#endif /* _AUDIO_PACE_H_ */
