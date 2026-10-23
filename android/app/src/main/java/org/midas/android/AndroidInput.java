package org.midas.android;

/**
 * Java side of the touch overlay to SDL virtual-controller bridge.
 *
 * <p>The view calls these from the UI thread. The native methods only store
 * atomics; the game thread attaches the virtual SDL controller and copies the
 * state across at its next input poll. Calls are deliberately safe no-ops when
 * the native library is not loaded (preview, plain JVM unit tests).</p>
 */
public final class AndroidInput {
    /* Axis ids understood by port/android/android_input.c. Sticks are -1..1,
     * triggers are 0..1. */
    public static final int AXIS_LEFT_X = 0;
    public static final int AXIS_LEFT_Y = 1;
    public static final int AXIS_RIGHT_X = 2;
    public static final int AXIS_RIGHT_Y = 3;
    public static final int AXIS_TRIGGER_LEFT = 4;
    public static final int AXIS_TRIGGER_RIGHT = 5;

    /* Button ids: SDL's masked-controller order used by the virtual pad.
     * Keep in step with the descriptor in android_input.c. */
    public static final int BUTTON_A = 0;
    public static final int BUTTON_B = 1;
    public static final int BUTTON_X = 2;
    public static final int BUTTON_Y = 3;
    public static final int BUTTON_BACK = 4;
    public static final int BUTTON_GUIDE = 5;
    public static final int BUTTON_START = 6;
    public static final int BUTTON_L3 = 7;
    public static final int BUTTON_R3 = 8;
    public static final int BUTTON_LB = 9;
    public static final int BUTTON_RB = 10;
    public static final int BUTTON_DPAD_UP = 11;
    public static final int BUTTON_DPAD_DOWN = 12;
    public static final int BUTTON_DPAD_LEFT = 13;
    public static final int BUTTON_DPAD_RIGHT = 14;

    private static boolean sLoaded;

    private AndroidInput() {
    }

    /**
     * SDLActivity normally loaded libmain.so already. Retrying here also makes
     * a view constructed before the SDL thread comes up harmless.
     */
    private static boolean ensureLoaded() {
        if (sLoaded) {
            return true;
        }
        try {
            // SDLActivity loads libSDL2.so before libmain.so.
            System.loadLibrary("main");
            sLoaded = true;
        } catch (UnsatisfiedLinkError ignored) {
            // Preview/unit-test process, or SDL not up yet: callers no-op.
        }
        return sLoaded;
    }

    /** True once the native bridge is callable. */
    public static boolean isAvailable() {
        return ensureLoaded();
    }

    /**
     * Publish one axis. Sticks accept -1..1 (screen Y is positive down, as SDL
     * expects); triggers accept 0 (released) to 1 (fully pulled).
     */
    public static void setAxis(int axis, float value) {
        if (!ensureLoaded()) {
            return;
        }
        setAxisNative(axis, value);
    }

    /** Publish one button press/release. */
    public static void setButton(int button, boolean pressed) {
        if (!ensureLoaded()) {
            return;
        }
        setButtonNative(button, pressed);
    }

    /**
     * Release every touch axis/button. The physical controller is untouched;
     * the native side keeps its virtual pad neutral until it is detached.
     */
    public static void reset() {
        if (!ensureLoaded()) {
            return;
        }
        resetNative();
    }

    /**
     * Ask the game thread to attach (true) or detach (false) the virtual touch
     * controller. Hidden controls detach it so the overlay cannot leave a
     * second player slot behind.
     */
    public static void setTouchActive(boolean active) {
        if (!ensureLoaded()) {
            return;
        }
        setTouchActiveNative(active);
    }

    private static native void setAxisNative(int axis, float value);

    private static native void setButtonNative(int button, boolean pressed);

    private static native void resetNative();

    private static native void setTouchActiveNative(boolean active);
}
