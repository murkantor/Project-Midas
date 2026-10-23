#ifndef PORT_ANDROID_AUDIO_H
#define PORT_ANDROID_AUDIO_H

#ifdef __ANDROID__
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called on the game thread, alongside the existing SDL event handling. */
void portAndroidAudioHandleEvent(const SDL_Event *event);
/* Event watches can run on a JNI/audio thread; defer their SDL work. */
void portAndroidAudioQueueEvent(const SDL_Event *event);

#ifdef __cplusplus
}
#endif
#endif

#endif
