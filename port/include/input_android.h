/**
 * Android touch overlay -> SDL virtual game controller bridge.
 *
 * The Java view only publishes axes and buttons as atomics.  Every SDL call
 * (attach/detach the virtual controller, set its state) happens from the game
 * thread through portAndroidInputPump(), so input_pc.c's controller poll keeps
 * one owner for SDL's joystick list.
 */
#ifndef MIDAS_INPUT_ANDROID_H
#define MIDAS_INPUT_ANDROID_H

#if defined(__ANDROID__)

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called once from portInputInit, after the SDL controller subsystem is up. */
void portAndroidInputInit(void);
/* Called from portInputPollTick on the game thread, before any pad merge. */
void portAndroidInputPump(void);
/* Idempotent quit/lifecycle cleanup. */
void portAndroidInputShutdown(void);
/* Clear every published axis/button (activity pause, focus loss, lifecycle). */
void portAndroidInputReset(void);

/* Device index of the virtual touch controller, or -1 when it is detached. */
int portAndroidInputVirtualDeviceIndex(void);
/* The controller opened by this file.  input_pc.c uses it to seed the touch
 * layout; state is read through the atomics below.  NULL when detached. */
SDL_GameController *portAndroidInputVirtualController(void);
SDL_Joystick *portAndroidInputVirtualJoystick(void);

/* Snapshot of the latest UI-thread state.  input_pc.c's supplemental merge
 * reads these directly, so it never depends on SDL having pumped the virtual
 * joystick first. */
int portAndroidInputAxisValue(int axis);
unsigned int portAndroidInputButtonMask(void);

/* input_pc.c: true while the virtual pad occupies a player slot (so its SDL
 * handle needs the state pumped before the merge reads it). */
int portAndroidInputVirtualInSlot(void);

/* input_pc.c: a device was added or removed outside SDL's own hotplug event
 * path (the virtual pad is attached/detached from the game thread). */
void portAndroidInputDevicesChanged(void);
/* input_pc.c: close the player slot holding the virtual pad before this file
 * closes the SDL handles/device. */
void portAndroidInputForgetVirtual(void);

#ifdef __cplusplus
}
#endif

#endif /* __ANDROID__ */

#endif /* MIDAS_INPUT_ANDROID_H */
