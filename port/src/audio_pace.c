/**
 * Render pacing policy for the PC audio backend. See port/include/audio_pace.h
 * for the invariants these functions protect and for why they are separated
 * from the SDL device.
 *
 * Dependency-free on purpose: no SDL, no Ultra64 headers, no globals, no clock.
 * That is what lets port/tests/audio_pace.c exercise the shipped policy
 * directly, and it is also why it compiles in a fraction of a second.
 */

#include "audio_pace.h"

port_s32 portAudioDrainTick(PortAudioDrain *d, port_u32 queued)
{
    port_s32 consumed = (port_s32) d->at_exit - (port_s32) queued;

    if (queued > 0 && consumed <= 0) {
        d->famished++;
    } else {
        d->famished = 0;
    }
    return d->famished < PORT_AUDIO_DRAIN_PUMPS;
}

port_s32 portAudioShouldReopen(const PortAudioDrain *d)
{
    return d->famished >= PORT_AUDIO_STALL_PUMPS;
}

port_s32 portAudioShouldRender(const PortAudioPace *p)
{
    if (!p->dev_live) {
        return p->owed >= p->frame;
    }
    return p->queued < PORT_AUDIO_QUEUE_LOW;
}

port_u32 portAudioQuantum(port_u32 frame_size, port_u32 max_samples,
                          port_u32 frame_div)
{
    port_u32 samples;

    if (frame_size == 0) {
        frame_size = 736; /* audi.c's NTSC field at 22050 Hz */
    }
    if (frame_div == 0) {
        frame_div = 1;
    }
    samples = (frame_size / frame_div) & ~0xFu;
    if (samples < 64) {
        samples = 64;
    }
    if (max_samples != 0 && samples > max_samples) {
        samples = max_samples;
    }
    return samples;
}
