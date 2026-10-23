package org.midas.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Build;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Toast;

/**
 * Full-screen touch overlay for Project Midas: two sticks, A/B/X/Y, shoulders,
 * triggers, Start/Back, D-pad and stick clicks.
 *
 * <p>Landscape-first, but laid out from the smaller screen dimension and the
 * real safe insets, so notches and gesture bars never cover a control. All
 * touch state is published through {@link AndroidInput}; the native side
 * attaches an SDL virtual game controller on the game thread, so the existing
 * controller layouts and options apply unchanged.</p>
 *
 * <p>The small PAD handle is always drawn and always clickable. Tapping it
 * hides/shows the controls; long-pressing it cycles opacity. Both choices are
 * remembered. Hiding cancels every pointer/button and detaches the virtual
 * controller, while a real Bluetooth/USB pad keeps working.</p>
 */
public final class TouchControlsView extends View {
    private static final String PREFS = "midas_touch_controls";
    private static final String KEY_VISIBLE = "visible";
    private static final String KEY_OPACITY = "opacity";

    private static final int ID_LEFT_STICK = 1;
    private static final int ID_RIGHT_STICK = 2;
    private static final int ID_TRIGGER_LEFT = 3;
    private static final int ID_TRIGGER_RIGHT = 4;
    private static final int ID_BUTTON_BASE = 100;
    private static final int ID_TOGGLE = 200;

    private static final long LONG_PRESS_MS = 650L;

    private static final float[] OPACITIES = { 1.0f, 0.75f, 0.5f, 0.35f };

    private final SharedPreferences mPrefs;
    private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mText = new Paint(Paint.ANTI_ALIAS_FLAG);

    private final RectF mToggle = new RectF();
    private final RectF[] mButtonRect = new RectF[15];
    private final RectF[] mTriggerRect = new RectF[2];
    private final float[] mStickBaseX = new float[2];
    private final float[] mStickBaseY = new float[2];
    private final float[] mStickAnchorX = new float[2];
    private final float[] mStickAnchorY = new float[2];
    private final float[] mStickKnobX = new float[2];
    private final float[] mStickKnobY = new float[2];
    private final boolean[] mButtonDown = new boolean[15];
    private final int[] mButtonRefs = new int[15];
    private final boolean[] mTriggerDown = new boolean[2];
    private final int[] mTriggerRefs = new int[2];
    private final SparseArray<Pointer> mPointers = new SparseArray<>();

    private float mDensity;
    private float mStickRadius;
    private float mFaceRadius;
    private float mToggleSize;
    private float mPillHeight;
    private float mPillWidth;
    private float mGap;
    private float mTouchSlop;
    private int mInsetLeft;
    private int mInsetTop;
    private int mInsetRight;
    private int mInsetBottom;
    private int mWidth;
    private int mHeight;
    private boolean mControlsVisible;
    private float mOpacity;
    private int mLeftPointer = -1;
    private int mRightPointer = -1;
    private int mTogglePointer = -1;

    private static final class Pointer {
        int control;
        float downX;
        float downY;
        long downTime;
        boolean moved;
    }

    public TouchControlsView(Context context) {
        super(context);
        mPrefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        mControlsVisible = mPrefs.getBoolean(KEY_VISIBLE, true);
        mOpacity = clampOpacity(mPrefs.getFloat(KEY_OPACITY, 0.78f));

        mDensity = getResources().getDisplayMetrics().density;
        for (int i = 0; i < mButtonRect.length; i++) {
            mButtonRect[i] = new RectF();
        }
        for (int i = 0; i < mTriggerRect.length; i++) {
            mTriggerRect[i] = new RectF();
        }
        mFill.setStyle(Paint.Style.FILL);
        mStroke.setStyle(Paint.Style.STROKE);
        mStroke.setStrokeWidth(dp(2f));
        mText.setTextAlign(Paint.Align.CENTER);
        mText.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        mTouchSlop = dp(12f);

        setClickable(true);
        setFocusable(true);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_YES);
        updateAccessibility();
    }

    private float dp(float value) {
        return value * mDensity;
    }

    private static float clamp01(float v) {
        return v < 0f ? 0f : (v > 1f ? 1f : v);
    }

    private static float clampOpacity(float v) {
        return v < 0.35f ? 0.35f : (v > 1f ? 1f : v);
    }

    private int a(int color) {
        int alpha = (int) (Color.alpha(color) * mOpacity);
        return (color & 0x00ffffff) | (alpha << 24);
    }

    private int aStrong(int color) {
        int alpha = (int) (Color.alpha(color) * Math.max(mOpacity, 0.85f));
        return (color & 0x00ffffff) | (alpha << 24);
    }

    /** True while the visible overlay owns the virtual SDL pad. */
    public boolean isTouchControlsVisible() {
        return mControlsVisible;
    }

    /** Show or hide the controls. The PAD toggle remains visible either way. */
    public void setTouchControlsVisible(boolean visible) {
        if (mControlsVisible == visible) {
            return;
        }
        mControlsVisible = visible;
        mPrefs.edit().putBoolean(KEY_VISIBLE, visible).apply();
        if (!visible) {
            cancelAllPointers();
        }
        AndroidInput.setTouchActive(visible);
        updateAccessibility();
        invalidate();
        announceForAccessibility(visible
                ? "Touch controls shown"
                : "Touch controls hidden");
    }

    /** 0.35 (very dim) to 1.0; long-press the PAD handle to cycle. */
    public void setControlsOpacity(float opacity) {
        mOpacity = clampOpacity(opacity);
        mPrefs.edit().putFloat(KEY_OPACITY, mOpacity).apply();
        invalidate();
    }

    public float getControlsOpacity() {
        return mOpacity;
    }

    /** Release every touch input and forget local pointer ownership. */
    public void resetTouchInput() {
        cancelAllPointers();
    }

    private void updateAccessibility() {
        setContentDescription(mControlsVisible
                ? "Hide touch controls"
                : "Show touch controls");
    }

    @Override
    public boolean performClick() {
        super.performClick();
        setTouchControlsVisible(!mControlsVisible);
        return true;
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        AndroidInput.setTouchActive(mControlsVisible);
        requestApplyInsets();
    }

    @Override
    protected void onDetachedFromWindow() {
        cancelAllPointers();
        AndroidInput.setTouchActive(false);
        super.onDetachedFromWindow();
    }

    @Override
    protected void onWindowVisibilityChanged(int visibility) {
        super.onWindowVisibilityChanged(visibility);
        if (visibility != VISIBLE) {
            // Backgrounded/covered: no finger-up will arrive, so clear now.
            cancelAllPointers();
            AndroidInput.setTouchActive(false);
        } else {
            AndroidInput.setTouchActive(mControlsVisible);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasWindowFocus) {
        super.onWindowFocusChanged(hasWindowFocus);
        if (!hasWindowFocus) {
            // A dialog/pause can take focus without a guaranteed ACTION_CANCEL.
            cancelAllPointers();
        }
    }

    @Override
    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        if (Build.VERSION.SDK_INT >= 30) {
            Insets bars = insets.getInsets(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            mInsetLeft = bars.left;
            mInsetTop = bars.top;
            mInsetRight = bars.right;
            mInsetBottom = bars.bottom;
        } else {
            mInsetLeft = insets.getSystemWindowInsetLeft();
            mInsetTop = insets.getSystemWindowInsetTop();
            mInsetRight = insets.getSystemWindowInsetRight();
            mInsetBottom = insets.getSystemWindowInsetBottom();
            if (Build.VERSION.SDK_INT >= 28 && insets.getDisplayCutout() != null) {
                mInsetLeft = Math.max(mInsetLeft, insets.getDisplayCutout().getSafeInsetLeft());
                mInsetTop = Math.max(mInsetTop, insets.getDisplayCutout().getSafeInsetTop());
                mInsetRight = Math.max(mInsetRight, insets.getDisplayCutout().getSafeInsetRight());
                mInsetBottom = Math.max(mInsetBottom, insets.getDisplayCutout().getSafeInsetBottom());
            }
        }
        cancelAllPointers();
        updateLayout(getWidth(), getHeight());
        invalidate();
        return insets;
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        cancelAllPointers();
        updateLayout(w, h);
    }

    private void updateLayout(int w, int h) {
        mWidth = w;
        mHeight = h;
        if (w <= 0 || h <= 0) {
            return;
        }
        mDensity = getResources().getDisplayMetrics().density;

        final float margin = dp(16f);
        final float safeL = mInsetLeft + margin;
        final float safeT = mInsetTop + margin;
        final float safeR = w - mInsetRight - margin;
        final float safeB = h - mInsetBottom - margin;
        final float safeW = Math.max(dp(120f), safeR - safeL);
        final float safeH = Math.max(dp(120f), safeB - safeT);
        final float base = Math.min(safeW, safeH);
        final float centreX = (safeL + safeR) * 0.5f;

        mGap = Math.max(dp(6f), Math.min(dp(14f), base * 0.035f));
        mStickRadius = clamp(base * 0.135f, dp(58f), dp(112f));
        mFaceRadius = clamp(base * 0.057f, dp(21f), dp(35f));
        mToggleSize = dp(46f);
        mPillHeight = clamp(base * 0.105f, dp(32f), dp(44f));
        mPillWidth = clamp(mPillHeight * 1.8f, dp(54f), dp(88f));

        mStickBaseX[0] = safeL + mStickRadius;
        mStickBaseY[0] = safeB - mStickRadius;
        mStickBaseX[1] = safeR - mStickRadius;
        mStickBaseY[1] = safeB - mStickRadius;
        mStickKnobX[0] = mStickBaseX[0];
        mStickKnobY[0] = mStickBaseY[0];
        mStickKnobX[1] = mStickBaseX[1];
        mStickKnobY[1] = mStickBaseY[1];

        // Shoulders/triggers hug the top corners.
        setRect(AndroidInput.BUTTON_LB, safeL, safeT, mPillWidth, mPillHeight);
        mTriggerRect[0].set(safeL + mPillWidth + mGap, safeT,
                safeL + mPillWidth * 2f + mGap, safeT + mPillHeight);
        setRect(AndroidInput.BUTTON_RB, safeR - mPillWidth, safeT, mPillWidth, mPillHeight);
        mTriggerRect[1].set(safeR - mPillWidth * 2f - mGap, safeT,
                safeR - mPillWidth, safeT + mPillHeight);

        // PAD handle is always at top centre; Start/Back sit just below it.
        final boolean compact = safeW < dp(430f);
        final float toggleY = compact ? safeT + mPillHeight + mGap : safeT;
        mToggle.set(centreX - mToggleSize * 0.5f, toggleY,
                centreX + mToggleSize * 0.5f, toggleY + mToggleSize);
        final float secondRowY = toggleY + mToggleSize + mGap;
        final float startW = Math.max(dp(58f), mPillWidth * 0.9f);
        setRect(AndroidInput.BUTTON_BACK,
                centreX - mGap * 0.5f - startW, secondRowY, startW, mPillHeight);
        setRect(AndroidInput.BUTTON_START,
                centreX + mGap * 0.5f, secondRowY, startW, mPillHeight);

        // D-pad above the left stick; face cluster above the right stick.
        final float dHalf = mFaceRadius;
        final float clusterY = Math.max(secondRowY + mPillHeight + mGap * 2f,
                mStickBaseY[0] - mStickRadius - dHalf - mGap * 2f);
        final float arm = mFaceRadius * 1.28f;
        setCircle(AndroidInput.BUTTON_DPAD_UP, mStickBaseX[0], clusterY - arm, dHalf);
        setCircle(AndroidInput.BUTTON_DPAD_DOWN, mStickBaseX[0], clusterY + arm, dHalf);
        setCircle(AndroidInput.BUTTON_DPAD_LEFT, mStickBaseX[0] - arm, clusterY, dHalf);
        setCircle(AndroidInput.BUTTON_DPAD_RIGHT, mStickBaseX[0] + arm, clusterY, dHalf);

        setCircle(AndroidInput.BUTTON_Y, mStickBaseX[1], clusterY - arm, mFaceRadius);
        setCircle(AndroidInput.BUTTON_A, mStickBaseX[1], clusterY + arm, mFaceRadius);
        setCircle(AndroidInput.BUTTON_X, mStickBaseX[1] - arm, clusterY, mFaceRadius);
        setCircle(AndroidInput.BUTTON_B, mStickBaseX[1] + arm, clusterY, mFaceRadius);

        // Stick clicks between the sticks, clear of both thumb zones.
        final float side = mStickRadius + dp(24f);
        float l3x = mStickBaseX[0] + side;
        float r3x = mStickBaseX[1] - side;
        final float minGap = dp(42f);
        if (r3x - l3x < minGap) {
            l3x = centreX - minGap * 0.5f;
            r3x = centreX + minGap * 0.5f;
        }
        setCircle(AndroidInput.BUTTON_L3, l3x, mStickBaseY[0] - dp(6f), dp(17f));
        setCircle(AndroidInput.BUTTON_R3, r3x, mStickBaseY[1] - dp(6f), dp(17f));
    }

    private float clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    private void setRect(int button, float left, float top, float width, float height) {
        mButtonRect[button].set(left, top, left + width, top + height);
    }

    private void setCircle(int button, float cx, float cy, float radius) {
        mButtonRect[button].set(cx - radius, cy - radius, cx + radius, cy + radius);
    }

    private boolean hitToggle(float x, float y) {
        return mToggle.contains(x, y);
    }

    private int hitControl(float x, float y) {
        if (!mControlsVisible) {
            return -1;
        }
        for (int i = 0; i < mButtonRect.length; i++) {
            if (mButtonRect[i].contains(x, y)) {
                return ID_BUTTON_BASE + i;
            }
        }
        if (mTriggerRect[0].contains(x, y)) {
            return ID_TRIGGER_LEFT;
        }
        if (mTriggerRect[1].contains(x, y)) {
            return ID_TRIGGER_RIGHT;
        }
        final float leftDx = x - mStickBaseX[0];
        final float leftDy = y - mStickBaseY[0];
        if (leftDx * leftDx + leftDy * leftDy
                <= (mStickRadius * 1.28f) * (mStickRadius * 1.28f)) {
            return ID_LEFT_STICK;
        }
        final float rightDx = x - mStickBaseX[1];
        final float rightDy = y - mStickBaseY[1];
        if (rightDx * rightDx + rightDy * rightDy
                <= (mStickRadius * 1.28f) * (mStickRadius * 1.28f)) {
            return ID_RIGHT_STICK;
        }
        return -1;
    }

    private void acquire(int pointerId, float x, float y) {
        if (!mControlsVisible) {
            if (hitToggle(x, y)) {
                addPointer(pointerId, ID_TOGGLE, x, y);
            }
            return;
        }
        if (hitToggle(x, y)) {
            addPointer(pointerId, ID_TOGGLE, x, y);
            return;
        }
        final int control = hitControl(x, y);
        if (control < 0) {
            return;
        }
        if (control == ID_LEFT_STICK && mLeftPointer < 0) {
            mLeftPointer = pointerId;
            mStickAnchorX[0] = x;
            mStickAnchorY[0] = y;
            mStickKnobX[0] = x;
            mStickKnobY[0] = y;
            addPointer(pointerId, control, x, y);
            updateStick(0, x, y);
        } else if (control == ID_RIGHT_STICK && mRightPointer < 0) {
            mRightPointer = pointerId;
            mStickAnchorX[1] = x;
            mStickAnchorY[1] = y;
            mStickKnobX[1] = x;
            mStickKnobY[1] = y;
            addPointer(pointerId, control, x, y);
            updateStick(1, x, y);
        } else if (control == ID_TRIGGER_LEFT || control == ID_TRIGGER_RIGHT) {
            final int trigger = control == ID_TRIGGER_LEFT ? 0 : 1;
            mTriggerRefs[trigger]++;
            if (mTriggerRefs[trigger] == 1) {
                mTriggerDown[trigger] = true;
                AndroidInput.setAxis(trigger == 0
                        ? AndroidInput.AXIS_TRIGGER_LEFT
                        : AndroidInput.AXIS_TRIGGER_RIGHT, 1f);
            }
            addPointer(pointerId, control, x, y);
            invalidate();
        } else if (control >= ID_BUTTON_BASE && control < ID_BUTTON_BASE + mButtonRefs.length) {
            final int button = control - ID_BUTTON_BASE;
            mButtonRefs[button]++;
            if (mButtonRefs[button] == 1) {
                mButtonDown[button] = true;
                AndroidInput.setButton(button, true);
            }
            addPointer(pointerId, control, x, y);
            invalidate();
        }
    }

    private void addPointer(int id, int control, float x, float y) {
        Pointer p = new Pointer();
        p.control = control;
        p.downX = x;
        p.downY = y;
        p.downTime = android.os.SystemClock.uptimeMillis();
        mPointers.put(id, p);
        if (control == ID_TOGGLE) {
            mTogglePointer = id;
        }
    }

    private void updateStick(int stick, float x, float y) {
        float dx = x - mStickAnchorX[stick];
        float dy = y - mStickAnchorY[stick];
        final float length = (float) Math.sqrt(dx * dx + dy * dy);
        if (length > mStickRadius) {
            dx = dx * mStickRadius / length;
            dy = dy * mStickRadius / length;
        }
        mStickKnobX[stick] = mStickAnchorX[stick] + dx;
        mStickKnobY[stick] = mStickAnchorY[stick] + dy;
        final float nx = clamp(dx / mStickRadius, -1f, 1f);
        final float ny = clamp(dy / mStickRadius, -1f, 1f);
        if (stick == 0) {
            AndroidInput.setAxis(AndroidInput.AXIS_LEFT_X, nx);
            AndroidInput.setAxis(AndroidInput.AXIS_LEFT_Y, ny);
        } else {
            AndroidInput.setAxis(AndroidInput.AXIS_RIGHT_X, nx);
            AndroidInput.setAxis(AndroidInput.AXIS_RIGHT_Y, ny);
        }
        invalidate();
    }

    private void releasePointer(int id, boolean cancelled) {
        Pointer p = mPointers.get(id);
        if (p == null) {
            return;
        }
        mPointers.remove(id);
        switch (p.control) {
            case ID_LEFT_STICK:
                mLeftPointer = -1;
                mStickKnobX[0] = mStickBaseX[0];
                mStickKnobY[0] = mStickBaseY[0];
                AndroidInput.setAxis(AndroidInput.AXIS_LEFT_X, 0f);
                AndroidInput.setAxis(AndroidInput.AXIS_LEFT_Y, 0f);
                break;
            case ID_RIGHT_STICK:
                mRightPointer = -1;
                mStickKnobX[1] = mStickBaseX[1];
                mStickKnobY[1] = mStickBaseY[1];
                AndroidInput.setAxis(AndroidInput.AXIS_RIGHT_X, 0f);
                AndroidInput.setAxis(AndroidInput.AXIS_RIGHT_Y, 0f);
                break;
            case ID_TOGGLE:
                mTogglePointer = -1;
                if (!cancelled) {
                    long held = android.os.SystemClock.uptimeMillis() - p.downTime;
                    if (held >= LONG_PRESS_MS && !p.moved) {
                        cycleOpacity();
                    } else if (!p.moved) {
                        performClick();
                    }
                }
                break;
            case ID_TRIGGER_LEFT:
            case ID_TRIGGER_RIGHT: {
                final int trigger = p.control == ID_TRIGGER_LEFT ? 0 : 1;
                if (mTriggerRefs[trigger] > 0 && --mTriggerRefs[trigger] == 0) {
                    mTriggerDown[trigger] = false;
                    AndroidInput.setAxis(trigger == 0
                            ? AndroidInput.AXIS_TRIGGER_LEFT
                            : AndroidInput.AXIS_TRIGGER_RIGHT, 0f);
                }
                break;
            }
            default:
                if (p.control >= ID_BUTTON_BASE
                        && p.control < ID_BUTTON_BASE + mButtonRefs.length) {
                    final int button = p.control - ID_BUTTON_BASE;
                    if (mButtonRefs[button] > 0 && --mButtonRefs[button] == 0) {
                        mButtonDown[button] = false;
                        AndroidInput.setButton(button, false);
                    }
                }
                break;
        }
        invalidate();
    }

    private void cycleOpacity() {
        int next = 0;
        for (int i = 0; i < OPACITIES.length; i++) {
            if (Math.abs(OPACITIES[i] - mOpacity) < 0.02f) {
                next = (i + 1) % OPACITIES.length;
                break;
            }
        }
        setControlsOpacity(OPACITIES[next]);
        Toast.makeText(getContext(),
                "Touch opacity " + Math.round(mOpacity * 100f) + "%",
                Toast.LENGTH_SHORT).show();
    }

    private void cancelAllPointers() {
        mPointers.clear();
        mLeftPointer = -1;
        mRightPointer = -1;
        mTogglePointer = -1;
        for (int i = 0; i < mButtonRefs.length; i++) {
            if (mButtonRefs[i] != 0) {
                mButtonRefs[i] = 0;
                mButtonDown[i] = false;
            }
        }
        for (int i = 0; i < mTriggerRefs.length; i++) {
            mTriggerRefs[i] = 0;
            mTriggerDown[i] = false;
        }
        for (int i = 0; i < mStickKnobX.length; i++) {
            mStickKnobX[i] = mStickBaseX[i];
            mStickKnobY[i] = mStickBaseY[i];
        }
        AndroidInput.reset();
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int action = event.getActionMasked();
        switch (action) {
            case MotionEvent.ACTION_DOWN: {
                final int control = hitControl(event.getX(), event.getY());
                if (control < 0 && !hitToggle(event.getX(), event.getY())) {
                    // Not ours: let the SDL surface below receive the gesture.
                    return false;
                }
                acquire(event.getPointerId(0), event.getX(), event.getY());
                break;
            }
            case MotionEvent.ACTION_POINTER_DOWN: {
                final int index = event.getActionIndex();
                acquire(event.getPointerId(index), event.getX(index), event.getY(index));
                break;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < event.getPointerCount(); i++) {
                    final int id = event.getPointerId(i);
                    final Pointer p = mPointers.get(id);
                    if (p == null) {
                        continue;
                    }
                    final float x = event.getX(i);
                    final float y = event.getY(i);
                    if (p.control == ID_LEFT_STICK) {
                        updateStick(0, x, y);
                    } else if (p.control == ID_RIGHT_STICK) {
                        updateStick(1, x, y);
                    } else if (p.control == ID_TOGGLE) {
                        if (!p.moved
                                && Math.abs(x - p.downX) + Math.abs(y - p.downY) > mTouchSlop) {
                            p.moved = true;
                        }
                    }
                }
                break;
            }
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_UP: {
                final int index = event.getActionIndex();
                releasePointer(event.getPointerId(index), false);
                break;
            }
            case MotionEvent.ACTION_CANCEL:
                cancelAllPointers();
                break;
            default:
                break;
        }
        return true;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (mWidth == 0 || mHeight == 0) {
            return;
        }
        if (mControlsVisible) {
            drawSticks(canvas);
            drawButtons(canvas);
        }
        drawToggle(canvas);
    }

    private void drawSticks(Canvas canvas) {
        for (int s = 0; s < 2; s++) {
            final boolean active = s == 0 ? mLeftPointer >= 0 : mRightPointer >= 0;
            final float bx = s == 0 ? mStickBaseX[0] : mStickBaseX[1];
            final float by = s == 0 ? mStickBaseY[0] : mStickBaseY[1];
            final float ax = active ? mStickAnchorX[s] : bx;
            final float ay = active ? mStickAnchorY[s] : by;

            mFill.setColor(a(0x24141b26));
            canvas.drawCircle(bx, by, mStickRadius, mFill);
            mStroke.setColor(a(0x80ffffff));
            mStroke.setStrokeWidth(dp(active ? 2.5f : 2f));
            canvas.drawCircle(bx, by, mStickRadius, mStroke);
            canvas.drawCircle(ax, ay, mStickRadius * 0.42f, mStroke);

            mFill.setColor(a(active ? 0x66d8ecff : 0x3affffff));
            canvas.drawCircle(mStickKnobX[s], mStickKnobY[s], mStickRadius * 0.34f, mFill);
            mStroke.setColor(a(0x99ffffff));
            canvas.drawCircle(mStickKnobX[s], mStickKnobY[s], mStickRadius * 0.34f, mStroke);

            mText.setColor(a(0x8affffff));
            mText.setTextSize(dp(10f));
            canvas.drawText(s == 0 ? "MOVE" : "LOOK", bx, by + dp(4f), mText);
        }
    }

    private void drawButtons(Canvas canvas) {
        drawPill(canvas, mButtonRect[AndroidInput.BUTTON_LB], "LB",
                mButtonDown[AndroidInput.BUTTON_LB], false);
        drawPill(canvas, mTriggerRect[0], "LT", mTriggerDown[0], true);
        drawPill(canvas, mButtonRect[AndroidInput.BUTTON_RB], "RB",
                mButtonDown[AndroidInput.BUTTON_RB], false);
        drawPill(canvas, mTriggerRect[1], "RT", mTriggerDown[1], true);
        drawPill(canvas, mButtonRect[AndroidInput.BUTTON_BACK], "BACK",
                mButtonDown[AndroidInput.BUTTON_BACK], false);
        drawPill(canvas, mButtonRect[AndroidInput.BUTTON_START], "START",
                mButtonDown[AndroidInput.BUTTON_START], false);

        drawRoundButton(canvas, AndroidInput.BUTTON_DPAD_UP, "\u25b2", 0);
        drawRoundButton(canvas, AndroidInput.BUTTON_DPAD_DOWN, "\u25bc", 0);
        drawRoundButton(canvas, AndroidInput.BUTTON_DPAD_LEFT, "\u25c0", 0);
        drawRoundButton(canvas, AndroidInput.BUTTON_DPAD_RIGHT, "\u25b6", 0);

        drawRoundButton(canvas, AndroidInput.BUTTON_A, "A", 0xff5ac878);
        drawRoundButton(canvas, AndroidInput.BUTTON_B, "B", 0xffe06a6a);
        drawRoundButton(canvas, AndroidInput.BUTTON_X, "X", 0xff6aa8e0);
        drawRoundButton(canvas, AndroidInput.BUTTON_Y, "Y", 0xffe0c46a);

        drawRoundButton(canvas, AndroidInput.BUTTON_L3, "L3", 0);
        drawRoundButton(canvas, AndroidInput.BUTTON_R3, "R3", 0);
    }

    private void drawPill(Canvas canvas, RectF r, String label, boolean down, boolean trigger) {
        final float radius = r.height() * 0.5f;
        mFill.setColor(a(down ? 0x9950a0d0 : (trigger ? 0x55285070 : 0x44202028)));
        canvas.drawRoundRect(r, radius, radius, mFill);
        mStroke.setColor(a(down ? 0xffffffff : 0x88ffffff));
        mStroke.setStrokeWidth(dp(down ? 2.5f : 1.6f));
        canvas.drawRoundRect(r, radius, radius, mStroke);
        mText.setColor(a(0xffffffff));
        mText.setTextSize(r.height() * 0.38f);
        canvas.drawText(label, r.centerX(), r.centerY() + r.height() * 0.14f, mText);
    }

    private void drawRoundButton(Canvas canvas, int button, String label, int tint) {
        final RectF r = mButtonRect[button];
        final float cx = r.centerX();
        final float cy = r.centerY();
        final float radius = r.width() * 0.5f;
        final boolean down = mButtonDown[button];
        mFill.setColor(a(down ? 0xaa4f98c8 : 0x441a2029));
        canvas.drawCircle(cx, cy, radius, mFill);
        mStroke.setColor(tint == 0
                ? a(down ? 0xffffffff : 0x88ffffff)
                : a(down ? 0xffffffff : (tint & 0x00ffffff) | 0xb0000000));
        mStroke.setStrokeWidth(dp(down ? 2.5f : 1.7f));
        canvas.drawCircle(cx, cy, radius, mStroke);
        mText.setColor(a(0xffffffff));
        mText.setTextSize(radius * (label.length() > 1 ? 0.78f : 1.0f));
        canvas.drawText(label, cx, cy + radius * (label.length() > 1 ? 0.27f : 0.35f), mText);
    }

    private void drawToggle(Canvas canvas) {
        final float radius = mToggle.height() * 0.5f;
        mFill.setColor(aStrong(mControlsVisible ? 0xcc28648c : 0xcc202028));
        canvas.drawRoundRect(mToggle, radius, radius, mFill);
        mStroke.setColor(aStrong(0xddffffff));
        mStroke.setStrokeWidth(dp(1.8f));
        canvas.drawRoundRect(mToggle, radius, radius, mStroke);
        mText.setColor(aStrong(0xffffffff));
        mText.setTextSize(mToggle.height() * 0.32f);
        canvas.drawText(mControlsVisible ? "HIDE" : "PAD",
                mToggle.centerX(), mToggle.centerY() + mToggle.height() * 0.12f, mText);
    }
}
