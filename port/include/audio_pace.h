#ifndef PORT_AUDIO_PACE_H
#define PORT_AUDIO_PACE_H

/*
 * Output pacing policy for the PC audio pump, as pure functions.
 *
 * Nothing here reads a clock, touches SDL, or includes an engine header. The
 * caller gathers observations (elapsed ms, queued bytes) and obeys the
 * verdicts, which is what makes the rules testable without a device or a ROM:
 * see port/tests/audio_pace.c.
 *
 * WHY IT IS SHAPED THIS WAY
 *
 * alAudioFrame() is the only thing that advances the game's audio clock: the
 * SFX player's event queue, the sound-state list and the music sequencers all
 * move in *rendered samples* (src/libultra/audio/synthesizer.c:143-227). So
 * "how much to render" is simultaneously "how fast audio time runs", and there
 * are exactly two honest answers to where that rate comes from:
 *
 *   - A LIVE DEVICE. On N64 the DAC was the clock: AI DMA pulled a buffer, and
 *     amHandleFrameMessage sized the next frame as
 *     `(g_FrameSize - (osAiGetLength() >> 2)) + 16 + EXTRA_SAMPLES`
 *     (src/audi.c:529) - that is, "refill exactly what drained". The render
 *     rate then equals the DAC's rate, the queue depth is stable, and audio
 *     time is the hardware's own time. portAudioPaceFrame() reproduces this as
 *     a level controller: render what it takes to restore the target depth.
 *   - NO DEVICE, OR A DEAD ONE. Nothing pulls, so the wall clock paces and the
 *     samples are discarded. Game audio time still advances, so the SFX event
 *     queue cannot saturate and the sequencers cannot stall.
 *
 * A cautionary note from the first cut of this file, because it is an easy
 * mistake and it compiles and tests clean: pacing on the wall clock *and*
 * steering the queue depth from the same budget does not work. If the total
 * rendered is forced to equal wall time while the device also consumes wall
 * time, the queue level is a constant of integration - a bounded bias can
 * change when samples arrive but never the running total, so the depth never
 * converges to the target. Measured on a real device it sat at 1.2-3.6 KB
 * against a 10 KB target and starved exactly as before. Hence the split: the
 * device's rate sets delivery, the clock exists for its absence.
 *
 * NO FRAME IS EVER DROPPED OR DUPLICATED. Refill granularity is a few samples,
 * never a whole frame. 1.0.2b corrected an over-full queue by skipping entire
 * rendered field frames (~16.7 ms of synthesised audio discarded mid-stream),
 * which is a step discontinuity and therefore an audible click.
 */

typedef struct {
    unsigned rate;        /* output sample rate, Hz                       */
    unsigned frame_align; /* sample multiple alAudioFrame wants (16)      */
    unsigned max_frame;   /* largest single alAudioFrame request, samples */
    unsigned target;      /* resting queue depth, bytes (stereo s16)      */
    unsigned target_max;  /* ceiling the target may ratchet to, bytes     */
    unsigned target_step; /* added to the target after a dry queue, bytes */
    unsigned target_ease; /* eased back per quiet window, bytes           */
    unsigned catchup_ms;  /* wall time one update may credit, ms          */
    unsigned drain_ms;    /* no consumption this long = stalled, ms       */
    unsigned device_period_ms; /* cushion over one pump interval, ms     */
    unsigned queue_max;   /* floor for the backlog cap, bytes             */
    unsigned max_interval_ms; /* longest pump interval the floor covers, ms */
} PortAudioPaceCfg;

/* Samples owed by the wall clock, scaled by 1000 so sub-millisecond pump
 * intervals accumulate instead of rounding to zero. Used only while no device
 * is draining; a live device sets the pace through the queue level. */
typedef struct {
    long long credit;
} PortAudioPaceClock;

/* Current target depth, ratcheted up by underruns and eased back when quiet. */
typedef struct {
    unsigned target;
    int primed;
} PortAudioPaceTarget;

/* Device consumption watchdog. */
typedef struct {
    unsigned prev_bytes; /* queue depth when the previous pump ENDED */
    unsigned since_ms;   /* last time the device was seen to consume */
    int primed;
    int stalled;
} PortAudioPaceDrain;

void portAudioPaceClockReset(PortAudioPaceClock *c);

/*
 * How many samples the next frame should render, or 0 when nothing is due.
 *
 * has_sink != 0: refill toward target, aligned to cfg->frame_align and clamped
 * to cfg->max_frame. The clock is kept at rest, so if the device disappears the
 * fallback below starts from zero instead of replaying a backlog.
 *
 * has_sink == 0: credit dt_ms of wall time (capped at cfg->catchup_ms so a
 * stall is not replayed as a burst) and return what is owed.
 */
unsigned portAudioPaceFrame(PortAudioPaceClock *c, unsigned dt_ms,
                            unsigned queued_bytes, unsigned target, int has_sink,
                            const PortAudioPaceCfg *cfg);

/*
 * Samples to render to restore the target depth, aligned and clamped. Positive
 * or zero; never asks for more than cfg->max_frame.
 *
 * max_frame need not be a multiple of frame_align: when the clamp binds, the
 * result is rounded DOWN to the largest aligned request that still fits.
 */
unsigned portAudioPaceRefill(unsigned queued_bytes, unsigned target,
                             const PortAudioPaceCfg *cfg);

/*
 * The smallest queue depth that can survive a pump interval of dt_ms.
 *
 * A target below one pump interval is unsound by construction: between two
 * pumps the device drains dt_ms of audio, and the refill stops once the queue
 * reaches the target, so anything shallower than that interval starves every
 * single pump. Measured at 20 fps with a 40 ms target: the sink ran dry on
 * every pump, because one interval was 50 ms of audio.
 *
 * Returns dt_ms worth of bytes plus one device period of margin, so the queue
 * refills to a little more than the interval it must cover. The caller uses
 * max(this, its own target).
 *
 * dt_ms is clamped to cfg->max_interval_ms, so one pathological hitch cannot
 * demand an absurd backlog: this is a frame-RATE floor, not a stall absorber.
 */
unsigned portAudioPaceMinTarget(unsigned dt_ms, const PortAudioPaceCfg *cfg);

/*
 * The backlog cap: never below cfg->queue_max, and never so shallow that a
 * target at the pump-interval floor cannot be reached.
 *
 * This matters at low frame rates, where one pump interval exceeds a fixed cap.
 * The classic and demo modes can drop to single-digit fps, so at 5 fps the pump
 * interval is 200 ms of audio while a 16 KB cap holds only 185 ms: the sink
 * would starve on every pump no matter how the target is set. The cap therefore
 * has to scale with the interval it must cover.
 */
unsigned portAudioPaceCap(unsigned target, const PortAudioPaceCfg *cfg);

/*
 * Advance the target: step it up when the queue ran dry, ease it back toward
 * the configured floor during a quiet window.
 */
unsigned portAudioPaceTargetStep(PortAudioPaceTarget *t, int ran_dry,
                                 int relax, const PortAudioPaceCfg *cfg);

/*
 * One pump's worth of drain observation. opened_bytes is the depth when the
 * pump began (before it refilled), closed_bytes is the depth when it ended.
 *
 * The comparison is deliberately against the *previous* pump's closing depth,
 * not against the reading just before it: in steady state the pump refills
 * exactly what the device consumed, so a healthy queue shows no change between
 * two arbitrary observations. Between "last pump closed" and "this pump
 * opened", though, only the device ran, so a decrease there is real
 * consumption. An empty queue that stays empty is a device consuming
 * everything offered, not a stalled one.
 *
 * Returns 1 while the device has consumed nothing for cfg->drain_ms.
 */
int portAudioPaceDrainObserve(PortAudioPaceDrain *d, unsigned opened_bytes,
                              unsigned closed_bytes, unsigned now_ms,
                              const PortAudioPaceCfg *cfg);

#endif /* PORT_AUDIO_PACE_H */
