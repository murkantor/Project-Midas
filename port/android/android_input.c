/**
 * Android touch overlay's native side.
 *
 * Java publishes six axes and fifteen buttons as atomics.  This file owns the
 * single SDL virtual game controller they drive: attach/detach and every
 * SDL_JoystickSetVirtual* call happen from portAndroidInputPump(), which
 * input_pc.c runs on the game thread before the pad merge.  No SDL call is
 * made from the Android UI thread.
 *
 * The virtual pad has SDL's standard controller type, so SDL builds its
 * gamecontroller mapping automatically (button indices below follow the order
 * in SDL_virtualjoystick.c).  That lets the existing layout, deadzone, stick
 * mode, twin-stick and rebinding code treat touch exactly like a real pad.
 *
 * Build note: port/src/input_pc.c must stay in the Android target.  Physical
 * Retroid/Bluetooth/USB pads are still owned by that backend; this file only
 * adds the virtual controller and the hooks it calls.
 */
#include <jni.h>
#include <ultra64.h>
#include <SDL.h>

#include "input_android.h"
#include "port_console.h"

unsigned int g_PortRaphnetPolls = 0;

/* Keep in step with AndroidInput.java and TouchControlsView.java. */
#define AINPUT_AXIS_LEFT_X        0
#define AINPUT_AXIS_LEFT_Y        1
#define AINPUT_AXIS_RIGHT_X       2
#define AINPUT_AXIS_RIGHT_Y       3
#define AINPUT_AXIS_TRIGGER_LEFT  4
#define AINPUT_AXIS_TRIGGER_RIGHT 5
#define AINPUT_AXIS_COUNT         6

#define AINPUT_BUTTON_A            0
#define AINPUT_BUTTON_B            1
#define AINPUT_BUTTON_X            2
#define AINPUT_BUTTON_Y            3
#define AINPUT_BUTTON_BACK         4
#define AINPUT_BUTTON_GUIDE        5
#define AINPUT_BUTTON_START        6
#define AINPUT_BUTTON_L3           7
#define AINPUT_BUTTON_R3           8
#define AINPUT_BUTTON_LB           9
#define AINPUT_BUTTON_RB          10
#define AINPUT_BUTTON_DPAD_UP     11
#define AINPUT_BUTTON_DPAD_DOWN   12
#define AINPUT_BUTTON_DPAD_LEFT   13
#define AINPUT_BUTTON_DPAD_RIGHT  14
#define AINPUT_BUTTON_COUNT       15

/* UI thread -> game thread.  GCC/Clang atomics are used instead of stdatomic
 * so the file stays C99-clean for the NDK toolchain. */
static volatile int s_WantActive;
static volatile int s_Axis[AINPUT_AXIS_COUNT] = {
    0, 0, 0, 0, -32768, -32768
};
static volatile unsigned int s_Buttons;

/* Game-thread only. */
static SDL_Joystick *s_Joy;
static SDL_GameController *s_Ctrl;
static SDL_JoystickID s_Instance = -1;
static int s_DeviceIndex = -1;
static int s_Shutdown;
static Uint32 s_AttachRetryAt;
static Sint16 s_ShadowAxis[AINPUT_AXIS_COUNT];
static unsigned int s_ShadowButtons;

static int inputVirtualIndex(void)
{
    int i;
    if (s_Instance < 0) return s_DeviceIndex;
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_JoystickGetDeviceInstanceID(i) == s_Instance) return i;
    }
    return -1;
}

static int inputActive(void)
{
    return __atomic_load_n(&s_WantActive, __ATOMIC_ACQUIRE) != 0;
}

static void inputSetNeutral(void)
{
    int i;

    for (i = 0; i < AINPUT_AXIS_COUNT; i++)
    {
        s_ShadowAxis[i] = (Sint16) ((i >= AINPUT_AXIS_TRIGGER_LEFT) ? -32768 : 0);
        SDL_JoystickSetVirtualAxis(s_Joy, i, s_ShadowAxis[i]);
    }
    for (i = 0; i < AINPUT_BUTTON_COUNT; i++)
    {
        SDL_JoystickSetVirtualButton(s_Joy, i, 0);
    }
    s_ShadowButtons = 0;
}

static int inputAttach(void)
{
#if SDL_VERSION_ATLEAST(2, 24, 0)
    SDL_VirtualJoystickDesc desc;
    SDL_zero(desc);

    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = AINPUT_AXIS_COUNT;
    desc.nbuttons = AINPUT_BUTTON_COUNT;
    desc.nhats = 0;
    desc.vendor_id = 0x1209;
    desc.product_id = 0x4d54;
    desc.name = "Midas Touch";
    /* SDL assigns button/axis indices by walking the mask in its own order;
     * AINPUT_BUTTON_* above is that order. */
    desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_A)
                     | (1u << SDL_CONTROLLER_BUTTON_B)
                     | (1u << SDL_CONTROLLER_BUTTON_X)
                     | (1u << SDL_CONTROLLER_BUTTON_Y)
                     | (1u << SDL_CONTROLLER_BUTTON_BACK)
                     | (1u << SDL_CONTROLLER_BUTTON_GUIDE)
                     | (1u << SDL_CONTROLLER_BUTTON_START)
                     | (1u << SDL_CONTROLLER_BUTTON_LEFTSTICK)
                     | (1u << SDL_CONTROLLER_BUTTON_RIGHTSTICK)
                     | (1u << SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
                     | (1u << SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
                     | (1u << SDL_CONTROLLER_BUTTON_DPAD_UP)
                     | (1u << SDL_CONTROLLER_BUTTON_DPAD_DOWN)
                     | (1u << SDL_CONTROLLER_BUTTON_DPAD_LEFT)
                     | (1u << SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_LEFTX)
                   | (1u << SDL_CONTROLLER_AXIS_LEFTY)
                   | (1u << SDL_CONTROLLER_AXIS_RIGHTX)
                   | (1u << SDL_CONTROLLER_AXIS_RIGHTY)
                   | (1u << SDL_CONTROLLER_AXIS_TRIGGERLEFT)
                   | (1u << SDL_CONTROLLER_AXIS_TRIGGERRIGHT);

    s_DeviceIndex = SDL_JoystickAttachVirtualEx(&desc);
    if (s_DeviceIndex < 0)
    {
        portConLogf("ge: input: touch virtual controller attach failed: %s\n",
                SDL_GetError());
        return 0;
    }
    /* The descriptor's controller type makes SDL synthesise the mapping. */
    s_Ctrl = SDL_GameControllerOpen(s_DeviceIndex);
    if (s_Ctrl == NULL)
    {
        portConLogf("ge: input: touch virtual controller open failed: %s\n",
                SDL_GetError());
        SDL_JoystickDetachVirtual(s_DeviceIndex);
        s_DeviceIndex = -1;
        return 0;
    }
    /* Open our own joystick LAST: SDL's virtual driver pushes state to the most
     * recently opened handle, and this is the one the supplemental merge watches. */
    s_Joy = SDL_JoystickOpen(s_DeviceIndex);
    if (s_Joy == NULL)
    {
        portConLogf("ge: input: touch virtual joystick open failed: %s\n",
                SDL_GetError());
        SDL_GameControllerClose(s_Ctrl);
        s_Ctrl = NULL;
        SDL_JoystickDetachVirtual(s_DeviceIndex);
        s_DeviceIndex = -1;
        return 0;
    }
    s_Instance = SDL_JoystickInstanceID(s_Joy);
    inputSetNeutral();
    SDL_JoystickUpdate();
    portConLogf("ge: input: touch virtual controller attached (%s)\n",
            SDL_GameControllerName(s_Ctrl) ? SDL_GameControllerName(s_Ctrl) : "Midas Touch");
    /* Let input_pc.c put it in a player slot (or keep it supplemental when a
     * physical pad already owns player 1). */
    portAndroidInputDevicesChanged();
    SDL_JoystickUpdate();   /* the newly opened slot handle gets the neutral state */
    return 1;
#else
    portConLogf("ge: input: this SDL has no virtual joystick API; touch controls disabled\n");
    return 0;
#endif
}

static void inputDetach(void)
{
    /* SDL device indices move when another pad is unplugged; instance IDs do
     * not. Never detach a different controller using the original index. */
    s_DeviceIndex = inputVirtualIndex();
    if (s_Joy == NULL && s_DeviceIndex < 0)
    {
        return;
    }
    /* Close any player slot first: SDL_GameControllerClose must happen before
     * the device disappears, or the slot keeps a detached handle. */
    if (s_Instance >= 0)
    {
        portAndroidInputForgetVirtual();
    }
    if (s_Ctrl != NULL)
    {
        SDL_GameControllerClose(s_Ctrl);
        s_Ctrl = NULL;
    }
    if (s_Joy != NULL)
    {
        SDL_JoystickClose(s_Joy);
        s_Joy = NULL;
    }
    if (s_DeviceIndex >= 0)
    {
        SDL_JoystickDetachVirtual(s_DeviceIndex);
    }
    s_DeviceIndex = -1;
    s_Instance = -1;
    s_ShadowButtons = 0;
    portAndroidInputDevicesChanged();
}

void portAndroidInputInit(void)
{
    portAndroidInputReset();
    s_Shutdown = 0;
    s_AttachRetryAt = 0;
    portConLogf("ge: input: Android touch bridge ready\n");
}

void portAndroidInputPump(void)
{
    int i;
    unsigned int buttons;
    int axes[AINPUT_AXIS_COUNT];
    int changed = 0;

    if (s_Shutdown)
    {
        return;
    }
    if (!inputActive())
    {
        if (s_Joy != NULL || s_DeviceIndex >= 0)
        {
            inputDetach();
        }
        return;
    }
    if (s_Joy == NULL)
    {
        const Uint32 now = SDL_GetTicks();

        if (s_AttachRetryAt != 0 && (Sint32) (now - s_AttachRetryAt) < 0)
        {
            return;
        }
        s_AttachRetryAt = now + 1000;
        if (!inputAttach())
        {
            return;
        }
        s_AttachRetryAt = 0;
        changed = 1;
    }

    for (i = 0; i < AINPUT_AXIS_COUNT; i++)
    {
        axes[i] = __atomic_load_n(&s_Axis[i], __ATOMIC_RELAXED);
        if (axes[i] < -32768) axes[i] = -32768;
        if (axes[i] > 32767) axes[i] = 32767;
    }
    buttons = __atomic_load_n(&s_Buttons, __ATOMIC_RELAXED);

    for (i = 0; i < AINPUT_AXIS_COUNT; i++)
    {
        if ((Sint16) axes[i] != s_ShadowAxis[i])
        {
            s_ShadowAxis[i] = (Sint16) axes[i];
            SDL_JoystickSetVirtualAxis(s_Joy, i, s_ShadowAxis[i]);
            changed = 1;
        }
    }
    if (buttons != s_ShadowButtons)
    {
        unsigned int diff = buttons ^ s_ShadowButtons;

        for (i = 0; i < AINPUT_BUTTON_COUNT; i++)
        {
            if (diff & (1u << i))
            {
                SDL_JoystickSetVirtualButton(s_Joy, i,
                        (buttons & (1u << i)) ? 1 : 0);
            }
        }
        s_ShadowButtons = buttons;
        changed = 1;
    }
    if (changed || portAndroidInputVirtualInSlot())
    {
        /* The virtual driver only copies hwdata into a handle's arrays here;
         * the pad merge in the same tick reads that handle directly. */
        SDL_JoystickUpdate();
    }
}

void portAndroidInputShutdown(void)
{
    if (s_Shutdown)
    {
        return;
    }
    s_Shutdown = 1;
    __atomic_store_n(&s_WantActive, 0, __ATOMIC_RELEASE);
    if (SDL_WasInit(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER))
    {
        inputDetach();
    }
    s_Joy = NULL;
    s_Ctrl = NULL;
    s_DeviceIndex = -1;
    s_Instance = -1;
}

void portAndroidInputReset(void)
{
    int i;

    for (i = 0; i < AINPUT_AXIS_COUNT; i++)
    {
        __atomic_store_n(&s_Axis[i], (i >= AINPUT_AXIS_TRIGGER_LEFT) ? -32768 : 0,
                __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_Buttons, 0u, __ATOMIC_RELAXED);
}

int portAndroidInputVirtualDeviceIndex(void)
{
    return inputVirtualIndex();
}

SDL_GameController *portAndroidInputVirtualController(void)
{
    return s_Ctrl;
}

SDL_Joystick *portAndroidInputVirtualJoystick(void)
{
    return s_Joy;
}

int portAndroidInputAxisValue(int axis)
{
    if (axis < 0 || axis >= AINPUT_AXIS_COUNT)
    {
        return 0;
    }
    return __atomic_load_n(&s_Axis[axis], __ATOMIC_RELAXED);
}

unsigned int portAndroidInputButtonMask(void)
{
    return __atomic_load_n(&s_Buttons, __ATOMIC_RELAXED);
}

/* ---- raphnet HID backend is not built for Android ------------------------
 * input_pc.c still calls this interface.  Weak definitions keep the Android
 * link self-contained while allowing a future raphnet.c build to override
 * them.  Retroid/Bluetooth/USB pads all arrive through SDL.
 */
#if defined(__ANDROID__)
__attribute__((weak)) s32 portRaphnetAvailable(void) { return 0; }
__attribute__((weak)) const char *portRaphnetName(void) { return "raphnet (unavailable on Android)"; }
__attribute__((weak)) s32 portRaphnetPoll(s32 chan, u16 *btn, s8 *x, s8 *y)
{
    (void) chan;
    (void) btn;
    (void) x;
    (void) y;
    return 0;
}
__attribute__((weak)) s32 portRaphnetPeek(s32 chan) { (void) chan; return 0; }
__attribute__((weak)) void portRaphnetProbeMode(void) { }
__attribute__((weak)) s32 portRaphnetRumble(s32 chan, s32 on)
{
    (void) chan;
    (void) on;
    return 0;
}
#endif

/* ---- JNI: UI thread only touches atomics -------------------------------- */

JNIEXPORT void JNICALL
Java_org_midas_android_AndroidInput_setAxisNative(JNIEnv *env, jclass cls, jint axis, jfloat value)
{
    int v;

    (void) env;
    (void) cls;
    if (axis < 0 || axis >= AINPUT_AXIS_COUNT)
    {
        return;
    }
    if (value != value)   /* NaN from a stray Java call */
    {
        value = 0.0f;
    }
    if (axis >= AINPUT_AXIS_TRIGGER_LEFT)
    {
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        v = -32768 + (int) (value * 65535.0f + 0.5f);
    }
    else
    {
        if (value < -1.0f) value = -1.0f;
        if (value > 1.0f) value = 1.0f;
        v = (int) (value * 32767.0f);
    }
    __atomic_store_n(&s_Axis[axis], v, __ATOMIC_RELAXED);
}

JNIEXPORT void JNICALL
Java_org_midas_android_AndroidInput_setButtonNative(JNIEnv *env, jclass cls, jint button, jboolean pressed)
{
    (void) env;
    (void) cls;
    if (button < 0 || button >= AINPUT_BUTTON_COUNT)
    {
        return;
    }
    if (pressed)
    {
        __atomic_fetch_or(&s_Buttons, 1u << button, __ATOMIC_RELAXED);
    }
    else
    {
        __atomic_fetch_and(&s_Buttons, ~(1u << button), __ATOMIC_RELAXED);
    }
}

JNIEXPORT void JNICALL
Java_org_midas_android_AndroidInput_resetNative(JNIEnv *env, jclass cls)
{
    (void) env;
    (void) cls;
    portAndroidInputReset();
}

JNIEXPORT void JNICALL
Java_org_midas_android_AndroidInput_setTouchActiveNative(JNIEnv *env, jclass cls, jboolean active)
{
    (void) env;
    (void) cls;
    if (!active) portAndroidInputReset();
    __atomic_store_n(&s_WantActive, active ? 1 : 0, __ATOMIC_RELEASE);
}
