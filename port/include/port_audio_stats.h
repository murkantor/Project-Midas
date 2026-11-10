/**
 * port_audio_stats.h - a live snapshot of the PC audio pump for the in-game
 * overlay (console `cl_audio_pump`).
 *
 * The pump in audio_pc.c owns these numbers; the overlay only reads them, so
 * the port keeps no display state here beyond the on/off toggle. FPS and TPS
 * are deliberately NOT in this struct: they belong to the frame-timing code
 * (frametiming.c g_PortStatFps / g_PortStatTps) and the overlay reads those
 * directly, so a stale copy cannot disagree with the strip above it.
 *
 * UNITS, because getting these wrong is easy: `rendered` and `needed` are
 * sample TOTALS for the window, not rates. The overlay divides them by the
 * output rate to show milliseconds, which is the only form a reader can judge.
 * `pumps` and `busy` are also totals; dividing by window_ms gives the rates.
 */
#ifndef PORT_AUDIO_STATS_H
#define PORT_AUDIO_STATS_H

#ifndef TARGET_N64

#include <ultra64.h>

/* Rates are measured over this window, and refreshed once per window so the
 * numbers are readable instead of flickering per frame. The instantaneous
 * fields refresh on every pump regardless. */
#define PORT_AUDIO_STATS_WINDOW_MS 500

struct PortAudioPumpStats
{
    /* --- the last completed window (PORT_AUDIO_STATS_WINDOW_MS) --- */
    u32 pumps;       /* portAudioPump calls in the window */
    u32 busy;        /* of those, the ones that rendered any samples */
    u32 window_ms;   /* wall time the window actually covered */
    u32 rendered;    /* samples synthesised */
    u32 needed;      /* samples that window was worth at the output rate */
    u32 dry;         /* pumps that found the queue empty: the sink starved */
    u32 overcap;     /* frames refused because they would not fit the cap */
    u32 skipped;     /* frames the RSP validator rejected (played as silence) */
    u32 forced;      /* portAudioPumpForce frames */

    /* --- the newest pump, so the overlay is live between windows --- */
    u32 queue;       /* bytes queued when the pump closed */
    u32 target;      /* effective target depth, bytes (incl. the interval floor) */
    u32 cap;         /* effective backlog cap, bytes */
    u32 rate;        /* output sample rate, Hz */
    s32 stalled;     /* the drain watchdog's verdict */
    s32 has_sink;    /* a device is open and draining */
    s32 credit;      /* wall-clock credit while no device paces, samples x1000 */

    s32 valid;       /* 0 until the first window closes: rates are meaningless */
};

/* Console `cl_audio_pump 0|1`; shown state is read by the overlay. */
s32 portAudioPumpStatsShown(void);
void portAudioPumpStatsSet(s32 on);

/* Console `cl_audio_log 0|1`: the same numbers, once a second, to
 * Data/Logs/audio_pump.log. Separate from the overlay so a report can carry the
 * history even when nobody was watching the corner of the screen. */
s32 portAudioPumpLogShown(void);
void portAudioPumpLogSet(s32 on);

/* Fill *out from the pump's live state. Callable from the render thread: plain
 * word stores with no locks. Under a race a field can be one pump stale, which
 * is exactly right for a diagnostic and never wrong enough to mislead. */
void portAudioPumpStatsGet(struct PortAudioPumpStats *out);

#endif /* !TARGET_N64 */

#endif /* PORT_AUDIO_STATS_H */
