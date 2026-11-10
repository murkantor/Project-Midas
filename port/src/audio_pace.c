/*
 * Output pacing policy for the PC audio pump - implementation.
 *
 * Pure functions: no SDL, no engine headers, no globals, no clock reads. The
 * pump in port/src/audio_pc.c supplies observations and obeys these verdicts;
 * port/tests/audio_pace.c drives the same functions against a simulated device.
 *
 * Policy in one paragraph: with a live device, render what drained - a level
 * controller on the queue, which is what N64's amHandleFrameMessage did
 * (src/audi.c:529) and what makes the render rate equal the DAC's rate. With no
 * usable device, the wall clock paces and the samples are discarded. Nothing is
 * ever dropped or duplicated, so there is no discontinuity to hear.
 */

#include "audio_pace.h"

/* Stereo s16: one sample frame is this many bytes downstream. */
#define PACE_BYTES_PER_SAMPLE 4

/* Credit is in sample*1000 units, so 1 ms of 22050 Hz output is 22050 units. */
#define PACE_CREDIT_PER_SAMPLE 1000

void portAudioPaceClockReset(PortAudioPaceClock *c)
{
    c->credit = 0;
}

unsigned portAudioPaceRefill(unsigned queued_bytes, unsigned target,
                             const PortAudioPaceCfg *cfg)
{
    unsigned deficit;
    unsigned samples;

    if (cfg->frame_align == 0 || cfg->max_frame < cfg->frame_align) {
        return 0;
    }
    if (queued_bytes >= target) {
        return 0; /* at or above the mark: the device is keeping up */
    }

    deficit = target - queued_bytes;
    if (deficit < PACE_BYTES_PER_SAMPLE * cfg->frame_align) {
        return 0; /* less than one aligned sample frame: not worth a call */
    }

    /* The clamp happens once, below, after the alignment round-up. A second
     * guard here would be dead: rounding up from any value at or above
     * max_frame lands above max_frame, so the final clamp subsumes it. */
    samples = deficit / PACE_BYTES_PER_SAMPLE;

    /* Round UP to the synthesizer's alignment, not down. Rounding down loses a
     * few samples on EVERY pump, which is a systematic drain: at 300 fps that
     * is ~2400 samples/s, 11% short, and the queue starves no matter what the
     * target is. Rounding up overshoots by at most one aligned unit, and the
     * "at or above the mark" early return above absorbs it as hysteresis. */
    if (samples % cfg->frame_align != 0) {
        samples += cfg->frame_align - (samples % cfg->frame_align);
    }
    if (samples > cfg->max_frame) {
        /* Rounding up can cross max_frame. Align DOWN to the largest aligned
         * request that still fits, rather than assuming max_frame is itself a
         * multiple of frame_align: with max_frame=1000, align=16, clamping to
         * 1000 returns an unaligned request (1000 % 16 == 8), which contradicts
         * the "aligned and never more than max_frame" contract above. The
         * pump's 1024 is a multiple of 16, so this only bites a caller that
         * configures otherwise. */
        samples = cfg->max_frame - cfg->max_frame % cfg->frame_align;
    }

    return samples;
}

unsigned portAudioPaceMinTarget(unsigned dt_ms, const PortAudioPaceCfg *cfg)
{
    unsigned long long bytes;
    unsigned margin;

    if (cfg->max_interval_ms != 0 && dt_ms > cfg->max_interval_ms) {
        dt_ms = cfg->max_interval_ms;
    }

    /* A pump interval of dt_ms drains dt_ms of stereo s16 audio. Add one device
     * period of cushion so the queue is not already empty when the next pump
     * arrives. */
    bytes = (unsigned long long) dt_ms * (unsigned long long) cfg->rate
            * PACE_BYTES_PER_SAMPLE / 1000u;
    margin = (unsigned long long) cfg->device_period_ms * cfg->rate
             * PACE_BYTES_PER_SAMPLE / 1000u;

    if (bytes + margin > 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }
    return (unsigned) (bytes + margin);
}

unsigned portAudioPaceCap(unsigned target, const PortAudioPaceCfg *cfg)
{
    unsigned long long need = (unsigned long long) target
                            + (unsigned long long) cfg->max_frame * PACE_BYTES_PER_SAMPLE;

    if (need > 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }
    if ((unsigned) need > cfg->queue_max) {
        return (unsigned) need;
    }
    return cfg->queue_max;
}

unsigned portAudioPaceFrame(PortAudioPaceClock *c, unsigned dt_ms,
                            unsigned queued_bytes, unsigned target, int has_sink,
                            const PortAudioPaceCfg *cfg)
{
    long long ceiling;
    long long want;

    if (cfg->frame_align == 0 || cfg->max_frame < cfg->frame_align) {
        return 0;
    }

    if (has_sink) {
        /* The DAC is the clock, exactly as on N64: refill what it drained.
         * Keep the wall clock at rest so that if this device vanishes, the
         * fallback starts from zero rather than replaying a backlog. */
        c->credit = 0;
        return portAudioPaceRefill(queued_bytes, target, cfg);
    }

    /* No usable sink: the wall clock paces the mixer so game audio time still
     * advances. A stall (window drag, stage load, debugger) must not be
     * replayed as a burst of frames, so one update credits at most catchup_ms. */
    if (dt_ms > cfg->catchup_ms) {
        dt_ms = cfg->catchup_ms;
    }
    c->credit += (long long) dt_ms * (long long) cfg->rate;

    ceiling = (long long) cfg->catchup_ms * (long long) cfg->rate;
    if (c->credit > ceiling) {
        c->credit = ceiling;
    }

    want = c->credit / PACE_CREDIT_PER_SAMPLE;
    if (want < 0) {
        want = 0;
    }
    if (want > (long long) cfg->max_frame) {
        want = (long long) cfg->max_frame;
    }
    want -= want % (long long) cfg->frame_align;

    if (want == 0) {
        return 0;
    }

    c->credit -= want * PACE_CREDIT_PER_SAMPLE;

    /* Bound how far ahead the pump may run, so an unbroken stream of forced
     * frames cannot be repaid as a long silence. */
    {
        long long debt_floor = -(long long) cfg->max_frame * PACE_CREDIT_PER_SAMPLE;

        if (c->credit < debt_floor) {
            c->credit = debt_floor;
        }
    }

    return (unsigned) want;
}

unsigned portAudioPaceTargetStep(PortAudioPaceTarget *t, int ran_dry,
                                 int relax, const PortAudioPaceCfg *cfg)
{
    if (!t->primed) {
        t->primed = 1;
        t->target = cfg->target;
    }

    if (ran_dry) {
        /* The caller reports dry only once a sink has carried audio: a freshly
         * opened device starts empty and that is not an underrun. */
        if (t->target < cfg->target_max) {
            t->target += cfg->target_step;
            if (t->target > cfg->target_max) {
                t->target = cfg->target_max;
            }
        }
    } else if (relax && t->target > cfg->target) {
        unsigned over = t->target - cfg->target;

        t->target -= (over > cfg->target_ease) ? cfg->target_ease : over;
    }

    return t->target;
}

int portAudioPaceDrainObserve(PortAudioPaceDrain *d, unsigned opened_bytes,
                              unsigned closed_bytes, unsigned now_ms,
                              const PortAudioPaceCfg *cfg)
{
    if (!d->primed) {
        d->primed = 1;
        d->since_ms = now_ms;
        d->prev_bytes = closed_bytes;
        d->stalled = 0;
        return 0;
    }

    if (opened_bytes < d->prev_bytes) {
        /* The device ate into the backlog between the last pump closing and
         * this one opening. That is the only interval in which only the device
         * ran, so it is the only clean measurement of consumption. */
        d->since_ms = now_ms;
        d->stalled = 0;
    } else if (opened_bytes == 0 && d->prev_bytes == 0) {
        /* Empty and staying empty: the device consumed everything offered. */
        d->since_ms = now_ms;
        d->stalled = 0;
    } else if (!d->stalled && (now_ms - d->since_ms) >= cfg->drain_ms) {
        d->stalled = 1;
    }

    d->prev_bytes = closed_bytes;

    return d->stalled;
}
