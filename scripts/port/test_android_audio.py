"""Test Android device negotiation/lifecycle using a deterministic SDL mock.

These tests exercise the real backend helpers, not synthesis or device audio.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

MOCK = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t s16;
typedef int32_t s32;
typedef float f32;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef unsigned SDL_AudioDeviceID;
typedef struct { int freq; unsigned short format; u8 channels; unsigned short samples; } SDL_AudioSpec;
typedef struct { int pending; } SDL_AudioStream;
typedef struct { unsigned type; struct { unsigned which; int iscapture; } adevice; } SDL_Event;
#define PORT_AUDIO_RATE 22050
#define AUDIO_S16SYS 16
#define SDL_AUDIO_BITSIZE(format) ((format) & 255)
#define SDL_INIT_AUDIO 16
#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 1
#define SDL_AUDIO_ALLOW_FORMAT_CHANGE 2
#define SDL_AUDIO_ALLOW_CHANNELS_CHANGE 4
enum { SDL_APP_WILLENTERBACKGROUND=1, SDL_APP_DIDENTERBACKGROUND,
       SDL_APP_DIDENTERFOREGROUND, SDL_AUDIODEVICEREMOVED,
       SDL_AUDIODEVICEADDED, SDL_APP_TERMINATING };
static SDL_AudioDeviceID s_AudioDev;
static s32 s_AudioDead;
static SDL_AudioStream *s_AudioStream;
static SDL_AudioSpec s_AudioOutput;
static Uint64 s_AudioRetryAt;
static s32 s_AudioPaused;
static SDL_AudioStream stream;
static unsigned queued, opens, closes, frees, init_count, allowed;
static int paused, fail_open, fail_stream, fail_put, fail_get, fail_queue;
static Uint64 ticks = 123;
typedef int SDL_SpinLock;
static void SDL_AtomicLock(SDL_SpinLock *lock) { (void)lock; }
static void SDL_AtomicUnlock(SDL_SpinLock *lock) { (void)lock; }
static unsigned flushes;
static void portVideoFlushSettings(void) { ++flushes; }
void portAndroidAudioHandleEvent(const SDL_Event *event);
static void SDL_CloseAudioDevice(unsigned dev) { assert(dev==42); ++closes; }
static void SDL_FreeAudioStream(SDL_AudioStream *s) { assert(s==&stream); ++frees; }
static Uint64 SDL_GetTicks64(void) { return ticks; }
static void SDL_PauseAudioDevice(unsigned dev,int pause) { assert(dev==42); paused=pause; }
static void SDL_ClearQueuedAudio(unsigned dev) { assert(dev==42); queued=0; }
static void SDL_AudioStreamClear(SDL_AudioStream *s) { s->pending=0; }
static unsigned SDL_GetQueuedAudioSize(unsigned dev) { assert(dev==42); return queued; }
static int SDL_AudioStreamPut(SDL_AudioStream *s,const void *p,int bytes) {
    (void)p; if (fail_put) return -1;
    s->pending += (int)((uint64_t)bytes*48000*8/(22050*4)); return 0;
}
static int SDL_AudioStreamAvailable(SDL_AudioStream *s) { return s->pending; }
static int SDL_AudioStreamGet(SDL_AudioStream *s,void *out,int bytes) {
    (void)out; if (fail_get) return -1;
    if (bytes>s->pending) bytes=s->pending; s->pending-=bytes; return bytes;
}
static int SDL_QueueAudio(unsigned dev,const void *data,unsigned bytes) {
    (void)data; assert(dev==42); if (fail_queue) return -1;
    queued+=bytes; return 0;
}
static unsigned SDL_WasInit(unsigned flags) { (void)flags; return init_count!=0; }
static int SDL_InitSubSystem(unsigned flags) { (void)flags; ++init_count; return 0; }
static unsigned SDL_OpenAudioDevice(const char *name,int capture,const SDL_AudioSpec *want,
                                   SDL_AudioSpec *have,int flags) {
    (void)name; assert(!capture); assert(want->freq==22050);
    ++opens; allowed=flags; *have=*want; have->freq=48000; have->format=32;
    return fail_open ? 0 : 42;
}
static SDL_AudioStream *SDL_NewAudioStream(unsigned short fmt,u8 channels,int rate,
                                         unsigned short outfmt,u8 outchannels,int outrate) {
    assert(fmt==16 && channels==2 && rate==22050);
    assert(outfmt==32 && outchannels==2 && outrate==48000);
    return fail_stream ? NULL : &stream;
}
static void ge_dbg_vec(const char *tag,const f32 *v,s32 n) { (void)tag;(void)v;(void)n; }
'''

CASES = r'''
int main(int argc,char **argv) {
    SDL_Event event = {0}; s16 samples[1000]={0};
    assert(argc==2);
    if (!strcmp(argv[1],"negotiation")) {
        portAudioInit();
        assert(s_AudioDev==42 && s_AudioStream==&stream && allowed==7 && !paused);
        queued=192000; assert(portAndroidAudioQueuedBytes()==44100);
        queued=0; portAndroidAudioQueue(samples,441);
        assert(queued==7680 && stream.pending==0);
        portAndroidAudioClose(); portAudioInit(); assert(init_count==1 && opens==2);
    } else if (!strcmp(argv[1],"pause")) {
        portAudioInit(); queued=100; stream.pending=100;
        event.type=SDL_APP_WILLENTERBACKGROUND; portAndroidAudioHandleEvent(&event);
        assert(s_AudioPaused && paused && queued==0 && stream.pending==0);
        event.type=SDL_APP_DIDENTERFOREGROUND; portAndroidAudioHandleEvent(&event);
        assert(!s_AudioPaused && !paused && !s_AudioDead);
        event.type=SDL_APP_TERMINATING; portAndroidAudioHandleEvent(&event);
        assert(s_AudioPaused && !s_AudioDev && !s_AudioStream && closes==1 && frees==1);
    } else if (!strcmp(argv[1],"route")) {
        portAudioInit(); event.type=SDL_AUDIODEVICEREMOVED;
        event.adevice.which=42; event.adevice.iscapture=1;
        portAndroidAudioHandleEvent(&event); assert(s_AudioDev==42);
        event.adevice.iscapture=0; event.adevice.which=43;
        portAndroidAudioHandleEvent(&event); assert(s_AudioDev==42);
        event.adevice.which=42; portAndroidAudioHandleEvent(&event);
        assert(!s_AudioDev && !s_AudioStream && s_AudioDead && s_AudioRetryAt==1123);
        event.type=SDL_AUDIODEVICEADDED; portAndroidAudioHandleEvent(&event);
        assert(!s_AudioDead && s_AudioRetryAt==0);
        portAudioInit(); assert(s_AudioDev==42);
    } else if (!strcmp(argv[1],"deferred")) {
        portAudioInit(); queued=100;
        event.type=SDL_APP_WILLENTERBACKGROUND; portAndroidAudioQueueEvent(&event);
        assert(!s_AudioPaused && queued==100 && flushes==0);
        portAndroidAudioPumpEvents();
        assert(s_AudioPaused && queued==0 && flushes==1);
        event.type=SDL_APP_DIDENTERFOREGROUND; portAndroidAudioQueueEvent(&event);
        portAndroidAudioPumpEvents(); assert(!s_AudioPaused);
    } else if (!strcmp(argv[1],"open-failure")) {
        fail_open=1; portAudioInit();
        assert(s_AudioDead && !s_AudioDev && !s_AudioStream && s_AudioRetryAt==1123);
    } else if (!strcmp(argv[1],"stream-failure")) {
        fail_stream=1; portAudioInit();
        assert(s_AudioDead && !s_AudioDev && !s_AudioStream && closes==1);
    } else if (!strcmp(argv[1],"put-failure") || !strcmp(argv[1],"get-failure") ||
               !strcmp(argv[1],"queue-failure")) {
        portAudioInit();
        fail_put=!strcmp(argv[1],"put-failure");
        fail_get=!strcmp(argv[1],"get-failure");
        fail_queue=!strcmp(argv[1],"queue-failure");
        portAndroidAudioQueue(samples,100);
        assert(s_AudioDead && !s_AudioDev && !s_AudioStream && closes==1 && frees==1);
    } else { return 2; }
    return 0;
}
'''


class AndroidAudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
        if not compiler:
            raise unittest.SkipTest("A host C compiler is required")
        source = (ROOT / "port/src/audio_pc.c").read_text(encoding="utf-8")
        start = source.index("#ifdef __ANDROID__\nstatic SDL_SpinLock s_AudioEventLock")
        end = source.index("/**\n * Called once per video frame", start)
        cls.tmp = tempfile.TemporaryDirectory(prefix="midas-audio-")
        cls.addClassCleanup(cls.tmp.cleanup)
        directory = Path(cls.tmp.name)
        code = directory / "audio.c"
        code.write_text(MOCK + "\n#define __ANDROID__ 1\n" + source[start:end] + CASES,
                        encoding="utf-8")
        cls.binary = directory / ("audio.exe" if os.name == "nt" else "audio")
        subprocess.run([compiler, "-std=c11", str(code), "-o", str(cls.binary)], check=True)

    def test_device_negotiation_and_rate_scaled_queue(self):
        subprocess.run([str(self.binary), "negotiation"], check=True)

    def test_background_clears_audio_and_foreground_resumes(self):
        subprocess.run([str(self.binary), "pause"], check=True)

    def test_output_route_removal_and_reconnection(self):
        subprocess.run([str(self.binary), "route"], check=True)

    def test_event_watch_defers_sdl_work_until_game_thread(self):
        subprocess.run([str(self.binary), "deferred"], check=True)

    def test_device_and_stream_failures_schedule_retry(self):
        for case in ("open-failure", "stream-failure", "put-failure", "get-failure", "queue-failure"):
            with self.subTest(case=case):
                subprocess.run([str(self.binary), case], check=True)


if __name__ == "__main__":
    unittest.main()
