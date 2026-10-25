#include <stdio.h>
#include <SDL.h>
#include <unistd.h>
#include <time.h>

#include "platform.h"
#include "system.h"

#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"
#include "glad/glad.h" /* GE bring-up: glReadPixels for GE_SHOT screenshots */
#include "icon_rgba.h"  /* the window icon (scripts/port/make_icon.py) */

#ifdef _WIN32
/* Hybrid-GPU laptops: NVIDIA / AMD drivers read these two exports to run the exe
 * on the discrete GPU instead of the integrated one. The `GL: renderer:` log
 * line shows which GPU was used. */
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

static SDL_Window* wnd;
static SDL_GLContext ctx;
static SDL_Renderer* renderer;
static int sdl_to_lus_table[512];
static bool vsync_enabled = true;
// OTRTODO: These are redundant. Info can be queried from SDL.
static int window_width = DESIRED_SCREEN_WIDTH;
static int window_height = DESIRED_SCREEN_HEIGHT;
static uint32_t fullscreen_flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
static bool fullscreen_state;
static bool maximized_state;
/* Borderless-window "fullscreen": SDL_WINDOW_FULLSCREEN_DESKTOP makes a topmost
 * popup on Windows that acts like exclusive fullscreen, so the window instead
 * drops its border and takes the display bounds. The windowed geometry is kept
 * here for the way back; exclusive SDL_WINDOW_FULLSCREEN still goes through
 * SDL. */
static int windowed_x, windowed_y, windowed_w, windowed_h;
static bool borderless_state;
/* Remembered window geometry: the last windowed, non-maximized position, size
 * and display, updated from the MOVED/SIZE_CHANGED events. It is handed to
 * video.c at exit and stored in ge.ini [display]
 * (window_x/window_y/window_display + width/height). */
static int saved_x, saved_y, saved_w, saved_h, saved_display = -1;
static bool saved_valid;

static void remember_window_geometry(void) {
    if (!wnd) {
        return;
    }
    int disp = SDL_GetWindowDisplayIndex(wnd);
    if (disp >= 0) {
        saved_display = disp;
    }
    Uint32 fl = SDL_GetWindowFlags(wnd);
    if (fullscreen_state || borderless_state ||
        (fl & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED | SDL_WINDOW_FULLSCREEN))) {
        return; /* keep the last real windowed geometry */
    }
    SDL_GetWindowPosition(wnd, &saved_x, &saved_y);
    SDL_GetWindowSize(wnd, &saved_w, &saved_h);
    saved_valid = true;
}

/* video.c (C) reads this at exit to write the ini; returns 0 if nothing
 * was ever recorded (e.g. the window never came up). */
extern "C" int gfx_sdl_get_saved_geometry(int *x, int *y, int *w, int *h, int *display) {
    if (!saved_valid) {
        return 0;
    }
    *x = saved_x; *y = saved_y; *w = saved_w; *h = saved_h; *display = saved_display;
    return 1;
}

/* A remembered top-left is usable if it lies on SOME current display
 * (monitors get unplugged / rearranged; an off-screen window is invisible
 * and the user cannot drag it back). */
static bool position_on_some_display(int x, int y) {
    int n = SDL_GetNumVideoDisplays();
    for (int i = 0; i < n; i++) {
        SDL_Rect r;
        if (SDL_GetDisplayBounds(i, &r) == 0 &&
            x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            return true;
        }
    }
    return false;
}
/* the display a remembered window top-left lies on, -1 = none */
static int display_at_position(int x, int y) {
    int n = SDL_GetNumVideoDisplays();
    for (int i = 0; i < n; i++) {
        SDL_Rect r;
        if (SDL_GetDisplayBounds(i, &r) == 0 &&
            x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            return i;
        }
    }
    return -1;
}
static bool is_running = true;
static void (*on_fullscreen_changed_callback)(bool is_now_fullscreen);

static int target_fps = 120; // above 60 since vsync is enabled by default
static uint64_t previous_time;
static uint64_t qpc_freq;

#define FRAME_INTERVAL_US_NUMERATOR 1000000
#define FRAME_INTERVAL_US_DENOMINATOR (target_fps)

static int32_t gfx_sdl_get_maximized_state(void) {
    return (int32_t)maximized_state;
}

static int32_t gfx_sdl_get_fullscreen_state(void) {
    return (int32_t)fullscreen_state;
}

static int32_t gfx_sdl_get_fullscreen_flag_mode(void) {
    return fullscreen_flag == SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : 1;
}

static void gfx_sdl_set_fullscreen_flag(int32_t mode) {
    switch (mode) {
        case 0: {
            fullscreen_flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
        } break;
        case 1: {
            fullscreen_flag = SDL_WINDOW_FULLSCREEN;
        } break;
    }
}

/* Windows treats a borderless GL window that exactly covers its monitor as
 * exclusive fullscreen, the same way SDL_WINDOW_FULLSCREEN_DESKTOP was treated
 * (see above): screenshots and overlays fail until an alt-tab hands it back to
 * the compositor. A window one pixel taller than the display stays a plain
 * window. GE_BORDERLESS_EXACT=1 makes it the exact display size again. */
static int borderless_extra_height(void) {
#ifdef _WIN32
    const char *e = SDL_getenv("GE_BORDERLESS_EXACT");
    return (e && *e && *e != '0') ? 0 : 1;
#else
    return 0;
#endif
}

/* A window must fit the display it is on. A size remembered from a bigger
 * monitor (window_width=3840 brought to a 1440p one) is cut to the largest
 * mode of this display that fits the work area with its frame, and the title
 * bar is kept inside the top of the work area (a client area the size of the
 * display, centred, otherwise puts the title bar above the screen). */
static void fit_window_to_display(void) {
    if (!wnd || fullscreen_state || borderless_state ||
        (SDL_GetWindowFlags(wnd) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED | SDL_WINDOW_FULLSCREEN))) {
        return;
    }
    const int disp = SDL_GetWindowDisplayIndex(wnd);
    SDL_Rect b, u;
    if (disp < 0 || SDL_GetDisplayBounds(disp, &b) != 0) {
        return;
    }
    if (SDL_GetDisplayUsableBounds(disp, &u) != 0) {
        u = b;
    }
    int top = 0, left = 0, bottom = 0, right = 0;
    if (SDL_GetWindowBordersSize(wnd, &top, &left, &bottom, &right) != 0) {
        top = left = bottom = right = 0;
    }
    int x, y, w, h;
    SDL_GetWindowPosition(wnd, &x, &y);
    SDL_GetWindowSize(wnd, &w, &h);
    const int ox = x, oy = y;
    if (w > b.w || h > b.h) {
        const int aw = u.w - left - right;
        const int ah = u.h - top - bottom;
        int fw = 0, fh = 0;
        for (int i = 0; i < SDL_GetNumDisplayModes(disp); i++) {
            SDL_DisplayMode m;
            if (SDL_GetDisplayMode(disp, i, &m) == 0 && m.w <= aw && m.h <= ah && m.w * m.h > fw * fh) {
                fw = m.w;
                fh = m.h;
            }
        }
        if (fw <= 0 || fh <= 0) {
            fw = aw > 320 ? aw : 320;
            fh = ah > 240 ? ah : 240;
        }
        sysLogPrintf(LOG_NOTE, "SDL: window %dx%d is bigger than display %d (%dx%d), now %dx%d", w, h, disp, b.w,
                     b.h, fw, fh);
        w = fw;
        h = fh;
        SDL_SetWindowSize(wnd, w, h);
        x = u.x + left + (aw - w) / 2;
        y = u.y + top + (ah - h) / 2;
    }
    if (y - top < u.y) {
        y = u.y + top;
    }
    if (x != ox || y != oy) {
        SDL_SetWindowPosition(wnd, x, y);
    }
}

static void set_borderless_fullscreen(bool on) {
    if (on && !borderless_state) {
        SDL_Rect r;
        int disp = SDL_GetWindowDisplayIndex(wnd);
        SDL_GetWindowPosition(wnd, &windowed_x, &windowed_y);
        SDL_GetWindowSize(wnd, &windowed_w, &windowed_h);
        if (disp < 0 || SDL_GetDisplayBounds(disp, &r) != 0) {
            if (SDL_GetDisplayBounds(0, &r) != 0) {
                return;
            }
        }
        borderless_state = true;
        SDL_SetWindowBordered(wnd, SDL_FALSE);
        SDL_SetWindowResizable(wnd, SDL_FALSE);
        SDL_SetWindowPosition(wnd, r.x, r.y);
        SDL_SetWindowSize(wnd, r.w, r.h + borderless_extra_height());
    } else if (!on && borderless_state) {
        borderless_state = false;
        SDL_SetWindowBordered(wnd, SDL_TRUE);
        SDL_SetWindowResizable(wnd, SDL_TRUE);
        SDL_SetWindowSize(wnd, windowed_w, windowed_h);
        SDL_SetWindowPosition(wnd, windowed_x, windowed_y);
        fit_window_to_display();
    }
}

static void set_fullscreen(bool on, bool call_callback) {
    fullscreen_state = on;
    if (fullscreen_flag == SDL_WINDOW_FULLSCREEN) {
        const bool was_borderless = borderless_state;
        set_borderless_fullscreen(false);
        if (on && was_borderless) {
            /* No mode was ever set, so SDL would pick one near the stale windowed size;
             * start exclusive fullscreen at the desktop size the borderless window covered. */
            SDL_DisplayMode dm;
            if (SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(wnd), &dm) == 0) {
                SDL_SetWindowDisplayMode(wnd, &dm);
            }
        }
        SDL_SetWindowFullscreen(wnd, on ? fullscreen_flag : 0);
    } else {
#ifdef __APPLE__
        /* Cocoa must own desktop fullscreen so it hides the menu bar and Dock
         * and restores them when the window loses fullscreen. */
        SDL_SetWindowFullscreen(wnd, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#else
        SDL_SetWindowFullscreen(wnd, 0);
        set_borderless_fullscreen(on);
#endif
    }
    if (call_callback && on_fullscreen_changed_callback) {
        on_fullscreen_changed_callback(on);
    }
}

static void set_maximize_window(bool on) {
	maximized_state = on;
	if (on) {
		SDL_MaximizeWindow(wnd);
	} else {
		SDL_RestoreWindow (wnd);
	}
}

static void gfx_sdl_get_active_window_refresh_rate(uint32_t* refresh_rate) {
    int display_in_use = SDL_GetWindowDisplayIndex(wnd);

    SDL_DisplayMode mode;
    SDL_GetCurrentDisplayMode(display_in_use, &mode);
    *refresh_rate = mode.refresh_rate;
}

static void gfx_sdl_init(const struct GfxWindowInitSettings *set) {
    window_width = set->width;
    window_height = set->height;

#ifdef SDL_HINT_VIDEO_HIGHDPI_DISABLED
    if (!set->allow_hidpi) {
        // HiDPI control, if available
        SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
#if defined(_WIN32) && defined(SDL_HINT_WINDOWS_DPI_AWARENESS)
        // if HiDPI is disabled, declare ourselves per-monitor DPI aware on
        // Windows so modes, bounds and window sizes are all real pixels
        // (otherwise display scaling makes windows overflow).
        SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
    }
#endif

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        sysFatalError("Could not init SDL:\n%s", SDL_GetError());
    }

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    if (sysArgCheck("--debug-gl")) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    }

    int posX = set->x;
    int posY = set->y;
    /* remembered monitor: valid only if it still exists */
    int pref_display = (set->display >= 0 && set->display < SDL_GetNumVideoDisplays()) ? set->display : -1;
    if (set->has_position && position_on_some_display(posX + 8, posY + 8)) {
        /* remembered position, still on a screen: use it as-is */
    } else if (pref_display >= 0) {
        /* position lost (or never saved) but the monitor is known: centre there */
        posX = SDL_WINDOWPOS_CENTERED_DISPLAY(pref_display);
        posY = SDL_WINDOWPOS_CENTERED_DISPLAY(pref_display);
    } else {
        posX = 100;
        posY = 100;
    }

    if (set->centered) {
        SDL_DisplayMode mode = {};
        SDL_GetCurrentDisplayMode(0, &mode);
        posX = mode.w / 2 - window_width / 2;
        posY = mode.h / 2 - window_height / 2;
    }

    if (set->fullscreen_is_exclusive) {
        fullscreen_flag = SDL_WINDOW_FULLSCREEN;
    }

    // we will unhide the window once the GL context is successfully created
    Uint32 flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;

    // if fullscreen was requested, start the window in fullscreen right away
    if (set->fullscreen) {
        fullscreen_state = true;
        if (fullscreen_flag == SDL_WINDOW_FULLSCREEN) {
            flags |= fullscreen_flag;
            /* a mode this display cannot show (exclusive_width=3840 brought to a
             * 1440p monitor) opens at the desktop mode instead */
            const int edisp = pref_display >= 0 ? pref_display : 0;
            SDL_DisplayMode want = {}, got = {};
            want.w = window_width;
            want.h = window_height;
            if (!SDL_GetClosestDisplayMode(edisp, &want, &got) && SDL_GetDesktopDisplayMode(edisp, &got) == 0) {
                sysLogPrintf(LOG_NOTE, "SDL: no %dx%d mode on display %d, exclusive opens at %dx%d", window_width,
                             window_height, edisp, got.w, got.h);
                window_width = got.w;
                window_height = got.h;
            }
        } else {
#ifdef __APPLE__
            flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#else
            // borderless window over the display (see set_borderless_fullscreen);
            // the remembered monitor first, display 0 as the fallback
            SDL_Rect r;
            int bdisp = pref_display;
            if (bdisp < 0 && set->has_position) {
                /* the saved display index is gone (monitors re-enumerated) but
                 * the saved position may still be on a screen: use that one */
                bdisp = display_at_position(set->x + 8, set->y + 8);
            }
            if ((bdisp >= 0 && SDL_GetDisplayBounds(bdisp, &r) == 0) ||
                SDL_GetDisplayBounds(0, &r) == 0) {
                windowed_x = posX;
                windowed_y = posY;
                windowed_w = window_width;
                windowed_h = window_height;
                posX = r.x;
                posY = r.y;
                window_width = r.w;
                window_height = r.h + borderless_extra_height();
                flags = (flags & ~SDL_WINDOW_RESIZABLE) | SDL_WINDOW_BORDERLESS;
                borderless_state = true;
            }
#endif
        }
    }

    if (set->maximized) {
        flags |= SDL_WINDOW_MAXIMIZED;
        maximized_state = true;
    }

#ifdef SDL_WINDOW_ALLOW_HIGHDPI
    if (set->allow_hidpi) {
        flags |= SDL_WINDOW_ALLOW_HIGHDPI;
    }
#endif

    // ideally we need 3.0 compat
    // if that doesn't work, try 3.2 core in case we're on mac, 2.1 compat as a last resort
    static u32 glver[][3] = {
        { 0, 0, 0                                    }, // for command line override
        { 3, 0, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY }, // 3.0: default, has all the features required
        { 4, 1, SDL_GL_CONTEXT_PROFILE_CORE          }, // 4.1core: macs only have core profile and this is the latest
        { 3, 2, SDL_GL_CONTEXT_PROFILE_CORE          }, // 3.2core: older macs will only have this at best
        { 3, 0, SDL_GL_CONTEXT_PROFILE_ES            }, // es3: don't really support ES properly, but we can try
        { 2, 1, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY }, // 2.1: absolute last resort, will still require GLSL130 as an extension
    };

#ifdef __APPLE__
    u32 verstart = 2; /* macOS exposes modern OpenGL through core profiles. */
#else
    u32 verstart = 1;
#endif
    const u32 verend = sizeof(glver) / sizeof(*glver);
    const char *verstr = sysArgGetString("--gl-version");
    if (verstr && *verstr) {
        // user override
        glver[0][2] = strstr(verstr, "core") ? SDL_GL_CONTEXT_PROFILE_CORE :
            (strstr(verstr, "es") ? SDL_GL_CONTEXT_PROFILE_ES :
            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        sscanf(verstr, "%d.%d", &glver[0][0], &glver[0][1]);
        if (glver[0][0] >= 1 && glver[0][0] <= 4 && glver[0][1] < 9) {
            verstart = 0;
        }
    }

    ctx = NULL;
    u32 vmin = 0, vmaj = 0, vprof = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
    const char *vprofstr = "";
    for (u32 i = verstart; i < verend && !ctx; ++i) {
        vmaj = glver[i][0];
        vmin = glver[i][1];
        vprof = glver[i][2];
        vprofstr = (vprof == SDL_GL_CONTEXT_PROFILE_CORE ? "core" :
            (vprof == SDL_GL_CONTEXT_PROFILE_ES ? "es" : ""));

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, vmaj);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, vmin);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, vprof);

        wnd = SDL_CreateWindow(set->title, posX, posY, window_width, window_height, flags);
        if (!wnd) {
            sysLogPrintf(LOG_WARNING, "SDL: could not open SDL window for GL%d.%d%s:\n%s", vmaj, vmin, vprofstr, SDL_GetError());
            continue;
        }
        { /* the window / taskbar icon: icon.png at 64 x 64 (scripts/port/make_icon.py),
           * R G B A bytes in memory, which on a little-endian host is these masks */
            SDL_Surface *icon = SDL_CreateRGBSurfaceFrom((void *) port_icon_rgba, PORT_ICON_SIZE, PORT_ICON_SIZE, 32,
                                                         PORT_ICON_SIZE * 4, 0x000000ffu, 0x0000ff00u, 0x00ff0000u, 0xff000000u);
            if (icon) {
                SDL_SetWindowIcon(wnd, icon);
                SDL_FreeSurface(icon);
            }
        }

        ctx = SDL_GL_CreateContext(wnd);
        if (!ctx) {
            sysLogPrintf(LOG_WARNING, "SDL: could not create GL%d.%d%s context: %s", vmaj, vmin, vprofstr, SDL_GetError());
            SDL_DestroyWindow(wnd);
            wnd = nullptr;
        }
    }

    if (!wnd || !ctx) {
        sysFatalError("Could not open SDL window with an OpenGL context of any supported version:\n%s", SDL_GetError());
    } else {
        sysLogPrintf(LOG_NOTE, "SDL: created GL%d.%d%s context", vmaj, vmin, vprofstr);
    }

    /* seed the remembered geometry (the windowed values, even when we
     * started borderless) so an exit before any MOVED event still saves */
    if (borderless_state) {
        saved_x = windowed_x; saved_y = windowed_y; saved_w = windowed_w; saved_h = windowed_h;
        saved_valid = (windowed_w > 0 && windowed_h > 0);
        int d = SDL_GetWindowDisplayIndex(wnd);
        saved_display = d >= 0 ? d : pref_display;
    } else {
        fit_window_to_display();
        remember_window_geometry();
    }
    {
        int wx, wy, ww, wh;
        SDL_GetWindowPosition(wnd, &wx, &wy);
        SDL_GetWindowSize(wnd, &ww, &wh);
        sysLogPrintf(LOG_NOTE, "SDL: window %dx%d at (%d,%d) display %d%s%s", ww, wh, wx, wy,
                     SDL_GetWindowDisplayIndex(wnd),
                     set->has_position ? " (remembered)" : "",
                     borderless_state ? " borderless" : "");
        /* log every display's mode and refresh rate, since display scaling and a
         * low refresh rate (which caps vsync) aren't visible from the window line alone */
        int dw = 0, dh = 0;
        SDL_GL_GetDrawableSize(wnd, &dw, &dh);
        sysLogPrintf(LOG_NOTE, "SDL: drawable %dx%d, video driver %s", dw, dh, SDL_GetCurrentVideoDriver());
        for (int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
            SDL_DisplayMode cur, desk;
            float ddpi = 0.0f;
            if (SDL_GetCurrentDisplayMode(i, &cur) != 0) { continue; }
            if (SDL_GetDesktopDisplayMode(i, &desk) != 0) { desk = cur; }
            SDL_GetDisplayDPI(i, &ddpi, nullptr, nullptr);
            sysLogPrintf(LOG_NOTE, "SDL: display %d '%s' %dx%d @ %dHz (desktop %dx%d @ %dHz, %d dpi)", i,
                         SDL_GetDisplayName(i) ? SDL_GetDisplayName(i) : "?", cur.w, cur.h, cur.refresh_rate,
                         desk.w, desk.h, desk.refresh_rate, (int)ddpi);
        }
    }

    SDL_GL_MakeCurrent(wnd, ctx);
    SDL_GL_SetSwapInterval(1);

    SDL_ShowWindow(wnd);
    if (fullscreen_state && fullscreen_flag == SDL_WINDOW_FULLSCREEN) {
        /* the display lines above were logged before an exclusive-fullscreen boot
         * switched modes, so also log the mode actually in use (its refresh caps the frame rate) */
        SDL_DisplayMode m;
        const int di = SDL_GetWindowDisplayIndex(wnd);
        if (di >= 0 && SDL_GetCurrentDisplayMode(di, &m) == 0) {
            sysLogPrintf(LOG_NOTE, "SDL: exclusive mode %dx%d @ %dHz on display %d", m.w, m.h, m.refresh_rate, di);
        }
    }

    qpc_freq = SDL_GetPerformanceFrequency();
}

static void gfx_sdl_close(void) {
    is_running = false;
}

static void gfx_sdl_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {
    on_fullscreen_changed_callback = on_fullscreen_changed;
}

static void gfx_sdl_set_fullscreen(bool enable) {
    set_fullscreen(enable, true);
}

static void gfx_sdl_set_fullscreen_exclusive(bool enable) {
    const uint32_t newflag = enable ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (fullscreen_flag != newflag) {
        fullscreen_flag = newflag;
        // reset fullscreen to take new value into account if it already is in fullscreen
        if (fullscreen_state) {
            fullscreen_state = false;
            set_fullscreen(enable, true);
        }
    }
}

static void gfx_sdl_set_maximize_window(bool enable) {
    set_maximize_window(enable);
}

static void gfx_sdl_set_cursor_visibility(bool visible) {
    if (visible) {
        SDL_ShowCursor(SDL_ENABLE);
    } else {
        SDL_ShowCursor(SDL_DISABLE);
    }
}

static void get_centered_positions_native(int32_t width, int32_t height, int32_t *posX, int32_t *posY) {
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode mode = {};
    SDL_GetDesktopDisplayMode(disp_idx, &mode);
    /* add the display's desktop origin so leaving exclusive fullscreen on a secondary
     * monitor keeps the window there; take only the origin, as the bounds' size
     * is the exclusive mode's */
    SDL_Rect r = {};
    if (disp_idx < 0 || SDL_GetDisplayBounds(disp_idx, &r) != 0) {
        SDL_GetDisplayBounds(0, &r);
    }
    *posX = r.x + mode.w / 2 - width / 2;
    *posY = r.y + mode.h / 2 - height / 2;
}

static void gfx_sdl_get_centered_positions(int32_t width, int32_t height, int32_t *posX, int32_t *posY) {
    /* centre on the window's own display using its bounds in global desktop coordinates
     * (display 0 as fallback), so a window on a secondary monitor stays there. */
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_Rect r = {};
    if (disp_idx < 0 || SDL_GetDisplayBounds(disp_idx, &r) != 0) {
        SDL_GetDisplayBounds(0, &r);
    }
    *posX = r.x + r.w / 2 - width / 2;
    *posY = r.y + r.h / 2 - height / 2;
}

static void gfx_sdl_set_closest_resolution(int32_t width, int32_t height, bool should_center) {
    const SDL_DisplayMode mode = {.w = width, .h = height};
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode closest = {};
    /* no mode that big on this display (an exclusive_width=3840 brought to a 1440p
     * monitor): take the desktop mode instead of leaving the old mode in place */
    if (SDL_GetClosestDisplayMode(disp_idx, &mode, &closest) || SDL_GetDesktopDisplayMode(disp_idx, &closest) == 0) {
        SDL_SetWindowDisplayMode(wnd, &closest);
        SDL_SetWindowSize(wnd, closest.w, closest.h);
        if (should_center) {
            int32_t posX = 0;
            int32_t posY = 0;
            get_centered_positions_native(closest.w, closest.h, &posX, &posY);
            SDL_SetWindowPosition(wnd, posX, posY);
        }
    }
}

static void gfx_sdl_set_dimensions(uint32_t width, uint32_t height, int32_t posX, int32_t posY) {
    SDL_SetWindowSize(wnd, width, height);
    SDL_SetWindowPosition(wnd, posX, posY);
    fit_window_to_display();
}

static void gfx_sdl_get_dimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) {
    SDL_GL_GetDrawableSize(wnd, static_cast<int*>((void*)width), static_cast<int*>((void*)height));
    SDL_GetWindowPosition(wnd, static_cast<int*>(posX), static_cast<int*>(posY));
}

/* the frontend's on-screen keyboard, when a real keyboard is typing into it
 * (video.c). This is the only place in the port that polls SDL events, so
 * text has to be picked up here. */
extern "C" void portTextInputPushChar(char c);
extern "C" void portTextInputPushKey(int which);
extern "C" int  portTextInputActive(void);
extern "C" void portVideoHoldOverKey(int sc); /* video.c - keep a key blanked until let go */
#define PORTTEXT_BACKSPACE 1
#define PORTTEXT_ACCEPT    2
#define PORTTEXT_CANCEL    3

/* stats log line: SDL events drained / of which mouse motion. A high-polling
 * mouse (2-8kHz) floods this queue and the drain is inside the pump. */
/* atomic - counted on the main thread, read and zeroed on the render
 * thread when it is on */
static SDL_atomic_t stat_events;
static SDL_atomic_t stat_mouse_events;

/* GE_TITLESTATS title-bar text is built on the render thread but applied on the
 * main thread in gfx_sdl_handle_events. Setting it from the render thread can
 * deadlock on Windows, where SetWindowText waits for the main thread to pump
 * messages. */
static SDL_SpinLock title_post_lock = 0;
static char title_post[200];
static bool title_post_dirty = false;

static void gfx_sdl_handle_events(void) {
    SDL_Event event;
    { /* the title the render thread posted (see title_post) */
        char title[200];
        bool dirty;
        SDL_AtomicLock(&title_post_lock);
        dirty = title_post_dirty;
        if (dirty) { SDL_memcpy(title, title_post, sizeof(title)); title_post_dirty = false; }
        SDL_AtomicUnlock(&title_post_lock);
        if (dirty) { SDL_SetWindowTitle(wnd, title); }
    }
    while (SDL_PollEvent(&event)) {
        SDL_AtomicAdd(&stat_events, 1);
        if (event.type == SDL_MOUSEMOTION) { SDL_AtomicAdd(&stat_mouse_events, 1); }
        switch (event.type) {
            case SDL_TEXTINPUT:
                if (portTextInputActive()) {
                    /* every byte, not just the first (an IME or compose
                     * commit carries several; video.c keeps only ASCII) */
                    for (const char* t = event.text.text; *t != '\0'; t++) {
                        portTextInputPushChar(*t);
                    }
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)) {
                    // alt-enter received, switch fullscreen state
                    /* Alt+Enter toggles once per press (ignoring key repeat), and its Return
                     * is held back until released so it isn't also read as START. */
                    if (!event.key.repeat) {
                        set_fullscreen(!fullscreen_state, true);
                    }
                    portVideoHoldOverKey(event.key.keysym.scancode);
                    break;
                }
                if (portTextInputActive()) {
                    /* not text, so SDL_TEXTINPUT never delivers them */
                    switch (event.key.keysym.sym) {
                        case SDLK_BACKSPACE: portTextInputPushKey(PORTTEXT_BACKSPACE); break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:  portTextInputPushKey(PORTTEXT_ACCEPT); break;
                        case SDLK_ESCAPE:    portTextInputPushKey(PORTTEXT_CANCEL); break;
                        default: break;
                    }
                }
                break;
            case SDL_WINDOWEVENT:
                /* Multi-GPU probe: dragging the window onto a display driven by
                 * another adapter can migrate or invalidate the GL context,
                 * silently corrupting textures. The renderer string is
                 * re-queried whenever the window changes display, so a change
                 * in it shows that the context migrated. */
                if (event.window.event == SDL_WINDOWEVENT_MOVED
#ifdef SDL_WINDOWEVENT_DISPLAY_CHANGED
                    || event.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED
#endif
                   ) {
                    static int last_display = -1;
                    static char first_renderer[128];
                    int disp = SDL_GetWindowDisplayIndex(wnd);

                    if (disp != last_display) {
                        const char *r = (const char *) glGetString(GL_RENDERER);
                        const char *v = (const char *) glGetString(GL_VENDOR);
                        GLenum err = glGetError();

                        if (first_renderer[0] == '\0' && r) {
                            SDL_strlcpy(first_renderer, r, sizeof(first_renderer));
                        }
                        fprintf(stderr,
                                "ge: gfx: window moved to display %d (was %d) renderer=\"%s\" vendor=\"%s\" glerr=0x%x%s\n",
                                disp, last_display, r ? r : "?", v ? v : "?", (unsigned) err,
                                (r && first_renderer[0] && SDL_strcmp(r, first_renderer) != 0)
                                    ? "   <-- RENDERER CHANGED: GL context migrated adapters" : "");
                        last_display = disp;
                    }
                }

                if (event.window.event == SDL_WINDOWEVENT_MOVED
#ifdef SDL_WINDOWEVENT_DISPLAY_CHANGED
                    || event.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED
#endif
                   ) {
                    remember_window_geometry();
                }

                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    SDL_GL_GetDrawableSize(wnd, &window_width, &window_height);
                    if (!fullscreen_state) {
                        maximized_state = SDL_GetWindowFlags(wnd) & SDL_WINDOW_MAXIMIZED ? true : false;
                    }
                    remember_window_geometry();
                } else if (event.window.event == SDL_WINDOWEVENT_CLOSE &&
                           event.window.windowID == SDL_GetWindowID(wnd)) {
                    // We listen specifically for main window close because closing main window
                    // on macOS does not trigger SDL_Quit.
                    exit(0);
                }
                break;
            case SDL_QUIT:
                exit(0);
                break;
        }
    }
}

static bool gfx_sdl_start_frame(void) {
    return true;
}

static uint64_t qpc_to_100ns(uint64_t qpc) {
    return qpc / qpc_freq * 10000000 + qpc % qpc_freq * 10000000 / qpc_freq;
}

static inline void sync_framerate_with_timer(void) {
    uint64_t t;
    t = qpc_to_100ns(SDL_GetPerformanceCounter());

    const int64_t next = previous_time + 10 * FRAME_INTERVAL_US_NUMERATOR / FRAME_INTERVAL_US_DENOMINATOR;
    int64_t left = next - t;
    // We want to exit a bit early, so we can busy-wait the rest to never miss the deadline
    left -= 15000UL;
    if (left > 0) {
        sysSleep(left);
    }

    do {
        sysCpuRelax();
        t = qpc_to_100ns(SDL_GetPerformanceCounter());
    } while ((int64_t)t < next);

    t = qpc_to_100ns(SDL_GetPerformanceCounter());
    if (left > 0 && t - next < 10000) {
        // In case it takes some time for the application to wake up after sleep,
        // or inaccurate timer,
        // don't let that slow down the framerate.
        t = next;
    }
    previous_time = t;
}

/* GE bring-up: GE_SHOT=N dumps the back buffer every N frames as
 * shot_XXXXX.bmp in the cwd, so texture/model output is inspectable
 * without a human watching the window. Remove once rendering is right.
 * ge_shot_force: one-shot trigger set by the autostart driver the frame
 * BEFORE it injects a START press, capturing exactly
 * what each menu shows at the transition. */
extern "C" int ge_shot_force;
int ge_shot_force;

static void gfx_sdl_maybe_screenshot(void) {
    static int interval = -2;
    static int framenum;
    int forced;
    if (interval == -2) {
        const char *s = SDL_getenv("GE_SHOT");
        interval = s ? SDL_atoi(s) : -1;
    }
    framenum++;
    forced = ge_shot_force;
    ge_shot_force = 0;
    if (!forced && (interval <= 0 || framenum % interval != 0)) {
        return;
    }
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(wnd, &w, &h);
    if (w <= 0 || h <= 0) {
        return;
    }
    size_t stride = (size_t)w * 3;
    size_t padded = (stride + 3) & ~3u; /* BMP rows are 4-byte aligned */
    uint8_t *px = (uint8_t *)SDL_calloc(1, padded * h);
    if (!px) return;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    /* repack in place bottom-up rows into padded stride, RGB->BGR */
    for (int y = h - 1; y >= 0; y--) {
        uint8_t *src = px + (size_t)y * stride;
        uint8_t row[4096 * 3];
        memcpy(row, src, stride);
        uint8_t *dst = px + (size_t)y * padded;
        for (int x = 0; x < w; x++) {
            dst[x * 3 + 0] = row[x * 3 + 2];
            dst[x * 3 + 1] = row[x * 3 + 1];
            dst[x * 3 + 2] = row[x * 3 + 0];
        }
    }
    char name[64];
    SDL_snprintf(name, sizeof(name), "shot_%05d.bmp", framenum);
    FILE *f = fopen(name, "wb");
    if (f) {
        uint32_t imgsize = (uint32_t)(padded * h);
        uint32_t fsize = 54 + imgsize;
        uint8_t hdr[54] = { 'B', 'M' };
        memcpy(hdr + 2, &fsize, 4);
        uint32_t off = 54; memcpy(hdr + 10, &off, 4);
        uint32_t bisz = 40; memcpy(hdr + 14, &bisz, 4);
        memcpy(hdr + 18, &w, 4);
        memcpy(hdr + 22, &h, 4); /* positive height = bottom-up, matches GL */
        uint16_t planes = 1; memcpy(hdr + 26, &planes, 2);
        uint16_t bpp = 24; memcpy(hdr + 28, &bpp, 2);
        memcpy(hdr + 34, &imgsize, 4);
        fwrite(hdr, 1, 54, f);
        fwrite(px, 1, imgsize, f);
        fclose(f);
        fprintf(stderr, "gfx: wrote %s (%dx%d)\n", name, w, h);
    }
    SDL_free(px);
}

/* Title-bar stats: presented fps, the ticks/sec g_GlobalTickCount advances at,
 * the FIELDS the sim is credited per second, and the fields each tick absorbed
 * + the GE_TICKHZ configuration and the actual pump rate. Updated twice a
 * second; cheap enough to leave always on.
 *
 * WARNING: g_GlobalTickCount is NOT a field counter. lv.c increments it by ONE
 * per tick that advanced the clock, however many fields that tick absorbed, so
 * its rate is min(tick rate, 60) - it falls with fps below 60 and reads 60 at
 * any raised tick rate. It used to be labelled "engine FIELDS/sec" here, which
 * was only true while frames fitted inside one field. The real field counter is
 * g_PortFieldsTicked (frametiming.c), shown as f/s: THAT is the one that reads
 * 60.0 whenever simulated time is keeping up, and it is the only one that
 * exposes a sim losing time under `tickhz=hw`. See
 * docs/port/tick-and-field-accounting.md.
 *
 * The numbers exist to separate failure modes that all read "60 fps | 60 tk"
 * from the outside:
 *   tick 60          -> GE_TICKHZ never reached the process
 *   tick 120 pump 60 -> the pacer isn't honouring subdiv (GE_NOSLEEP?)
 *   tick 120 pump120, fps 60 -> the blocking swap / gfx backpressure is
 *                               pinning the loop (vsync=1 on a 60Hz panel)
 *   tick 120 pump120, fps120 f/s 60, f/t 0.50 -> working as designed
 *   f/s below 60 -> the sim is losing wall-clock time: a stall the credit path
 *                   truncated (GE_TICKMAXSTEP), never a frame-rate symptom */
extern "C" int32_t g_GlobalTickCount;
extern "C" unsigned int g_PortFramesPumped;
extern "C" unsigned long long g_PortPumpSleepUs;
extern "C" unsigned long long g_PortAudioUs;
extern "C" unsigned long long g_PortJoyUs;
extern "C" unsigned long long g_PortPumpUs;
extern "C" unsigned long long g_PortGfxRunUs;
extern "C" unsigned int g_PortGfxDraws;
extern "C" unsigned int g_PortRaphnetPolls;
extern "C" int32_t portTickHz(void);
extern "C" int32_t g_PortStatFps; /* frametiming.c; show_fps overlay reads */
extern "C" int32_t g_PortStatTps;
extern "C" int32_t g_PortStatTicks;             /* the honest tick rate */
extern "C" int32_t g_PortStatFieldsX10;          /* fields/s x10: game speed */
extern "C" int32_t g_PortStatFieldsPerTickX100;  /* fields per tick x100 */
extern "C" uint32_t g_PortFieldsTicked;          /* monotonic, frametiming.c */
/* The SIM STEP counter, not boss.c's admitted-tick counter: g_PortTicksAdmitted
 * is incremented before the `pendingGfx < 2` backpressure guard, so under gfx
 * backpressure it counts ticks that never reached lvlTick. g_PortTickSerial
 * increments inside lvlTick itself (lv.c), so it is the number of ticks that
 * actually stepped the sim - which is the quantity f/t must divide by, or
 * f/t reads below 1.00 at tickhz=60 and looks like sub-ticking. */
extern "C" int32_t g_PortTickSerial;             /* monotonic, lv.c */
/* swap_buffers_begin: time in the maxfps limiter / in SDL_GL_SwapWindow */
static unsigned long long stat_limit_us = 0;
static unsigned long long stat_swap_us = 0;

#include "tick_stats.h"   /* the tested rate maths this pass publishes */

static void gfx_sdl_update_title_stats(void) {
    static uint32_t stat_last_ms = 0;
    static uint32_t stat_frames = 0;
    static uint32_t stat_last_tick = 0;
    static uint32_t stat_last_fields = 0;
    static uint32_t stat_last_ticks = 0;
    static unsigned int stat_last_pump = 0;
    static unsigned long long stat_last_idle = 0;
    static unsigned long long stat_last_glus = 0;
    static unsigned long long stat_last_audus = 0;
    static unsigned long long stat_last_joyus = 0;
    static unsigned long long stat_last_pmpus = 0;
    static unsigned int stat_last_draws = 0;
    static unsigned int stat_last_rn = 0;
    static unsigned long long stat_last_limus = 0;
    static unsigned long long stat_last_swpus = 0;
    uint32_t now = SDL_GetTicks();

    /* The log line lands every ~10s but the averages above it cover only the
     * last 500ms, so a hitch in between would vanish: carry the worst single
     * frame and the lowest 500ms fps across the whole log interval. */
    static uint64_t stat_prev_qpc = 0;
    static uint64_t stat_worst_us = 0;
    static int stat_min_fps = 0x7fffffff;
    {
        const uint64_t q = SDL_GetPerformanceCounter();
        if (stat_prev_qpc != 0) {
            const uint64_t us = (q - stat_prev_qpc) * 1000000ull / qpc_freq;
            if (us > stat_worst_us) { stat_worst_us = us; }
        }
        stat_prev_qpc = q;
    }

    stat_frames++;
    if (stat_last_ms == 0) {
        stat_last_ms = now;
        stat_last_tick = __atomic_load_n(&g_GlobalTickCount, __ATOMIC_RELAXED);
        stat_last_fields = __atomic_load_n(&g_PortFieldsTicked, __ATOMIC_RELAXED);
        stat_last_ticks = (uint32_t) __atomic_load_n(&g_PortTickSerial, __ATOMIC_RELAXED);
        stat_last_pump = g_PortFramesPumped;
        stat_last_idle = g_PortPumpSleepUs;
        stat_last_glus = g_PortGfxRunUs;
        stat_last_audus = g_PortAudioUs;
        stat_last_joyus = g_PortJoyUs;
        stat_last_pmpus = g_PortPumpUs;
        stat_last_draws = g_PortGfxDraws;
        stat_last_rn = g_PortRaphnetPolls;
        stat_last_limus = stat_limit_us;
        stat_last_swpus = stat_swap_us;
        return;
    }
    if (now - stat_last_ms >= 500) {
        char title[200];
        uint32_t dt = now - stat_last_ms;
        int fps = (int)((stat_frames * 1000 + dt / 2) / dt);
        /* D4: lvlStageLoad zeroes g_GlobalTickCount (lv.c), and this static is in
         * another translation unit, so it cannot see the reset. Without this the
         * first window after a load computed 0 - stat_last_tick and published
         * -2x the pre-load tick count (TPS -600 after 5s of play, for up to
         * 500ms) into both the overlay and audio_pump.log. A reset is not a
         * measurement: re-seat the baseline, so this one window reads 0 and the
         * next one is correct. (fps and the field figures are unaffected - they
         * come from counters that a stage load does not reset.) */
        /* Relaxed atomic loads throughout: this pass runs on the render thread
         * when render_thread=1 and the game thread writes these counters, so a
         * plain read is a data race by the letter of C11. Relaxed is enough -
         * each is an independent sample, not a set that must agree. */
        const int32_t tickNow = __atomic_load_n(&g_GlobalTickCount, __ATOMIC_RELAXED);

        stat_last_tick = portStatsReseatTickBaseline(stat_last_tick, tickNow);
        int tps = (int) portStatsRatePerSec((uint32_t) (tickNow - stat_last_tick), dt, 1);
        /* Option C: the two numbers that say whether simulated time is keeping
         * up. Both counters are monotonic since boot (neither is reset by a
         * stage load), so a load cannot make them go backwards.
         *   fields/s   x10  - 600 = 60.0 fields/s = game speed correct.
         *   fields/tick x100 - 100 = one field per tick; >100 = frames are
         *                      overrunning a field; <100 = sub-ticking.
         * The arithmetic lives in port/src/tick_stats.c so it is tested without
         * a window; see port/tests/tick_stats.c. */
        const uint32_t dfields = (uint32_t) (__atomic_load_n(&g_PortFieldsTicked, __ATOMIC_RELAXED) - stat_last_fields);
        const uint32_t dticks = (uint32_t) ((uint32_t) __atomic_load_n(&g_PortTickSerial, __ATOMIC_RELAXED) - stat_last_ticks);
        int fields10 = (int) portStatsRatePerSec(dfields, dt, 10);
        int ticks = (int) portStatsRatePerSec(dticks, dt, 1);
        int ft100 = (int) portStatsFieldsPerTickX100(dfields, dticks);
        int pump = (int)(((g_PortFramesPumped - stat_last_pump) * 1000 + dt / 2) / dt);
        /* idle% = share of wall time parked in the pump's pacing wait.
         * ~0 = no slack anywhere (real work); high = the pacer is waiting
         * and something else is setting the rate. */
        int idle = (int)(((g_PortPumpSleepUs - stat_last_idle) * 100) / ((unsigned long long)dt * 1000));
        /* gl = ms per frame in gfx_run excluding the limiter wait (lim) and
         * SDL_GL_SwapWindow (swp); frame time minus all three is game tick +
         * display-list build, and draws = triangle batches per frame. */
        unsigned int frames = stat_frames ? stat_frames : 1;
        unsigned long long waitus = (stat_limit_us - stat_last_limus) + (stat_swap_us - stat_last_swpus);
        unsigned long long glus = g_PortGfxRunUs - stat_last_glus;
        int glms10 = (int)(((glus > waitus ? glus - waitus : 0) * 10) / (1000ull * frames));
        int limms10 = (int)(((stat_limit_us - stat_last_limus) * 10) / (1000ull * frames));
        int swpms10 = (int)(((stat_swap_us - stat_last_swpus) * 10) / (1000ull * frames));
        int draws = (int)((g_PortGfxDraws - stat_last_draws) / frames);
        int rnhz = (int)(((g_PortRaphnetPolls - stat_last_rn) * 1000 + dt / 2) / dt);
        g_PortStatFps = fps;
        g_PortStatTps = tps;
        g_PortStatTicks = ticks;
        g_PortStatFieldsX10 = fields10;
        g_PortStatFieldsPerTickX100 = ft100;
        int audms10 = (int)(((g_PortAudioUs - stat_last_audus) * 10) / (1000ull * frames));
        int joyms10 = (int)(((g_PortJoyUs - stat_last_joyus) * 10) / (1000ull * frames));
        int pmpms10 = (int)(((g_PortPumpUs - stat_last_pmpus) * 10) / (1000ull * frames));
        SDL_snprintf(title, sizeof(title),
                     "ge007 | %dfps %dtk %d.%df/s %d.%02df/t | tick%d pump%d | idle%d%% gl%d.%d lim%d.%d swp%d.%d | pmp%d.%d joy%d.%d aud%d.%d | draws %d rn%dHz",
                     fps, ticks, fields10 / 10, fields10 % 10, ft100 / 100, ft100 % 100,
                     (int)portTickHz(), pump, idle,
                     glms10 / 10, glms10 % 10, limms10 / 10, limms10 % 10,
                     swpms10 / 10, swpms10 % 10, pmpms10 / 10, pmpms10 % 10,
                     joyms10 / 10, joyms10 % 10, audms10 / 10, audms10 % 10, draws, rnhz);
        /* Title-bar stats are opt-in (GE_TITLESTATS=1) to keep the title clean
         * for recording. The numbers still feed the in-game
         * show_fps digits and the cl_audio_pump block via the g_PortStat*
         * globals above. */
        {
            static int title_stats = -1;
            if (title_stats < 0) { const char *e = SDL_getenv("GE_TITLESTATS"); title_stats = (e && *e && *e != '0') ? 1 : 0; }
            if (title_stats) { /* posted, not set: this runs on the render thread (see title_post) */
                SDL_AtomicLock(&title_post_lock);
                SDL_strlcpy(title_post, title, sizeof(title_post));
                title_post_dirty = true;
                SDL_AtomicUnlock(&title_post_lock);
            }
        }
        /* The same line into the log every ~10s: borderless/fullscreen has no
         * title bar, and the pump heartbeat carries no clock, so without this
         * a user's log cannot answer "what fps, and who is eating the frame". */
        {
            static unsigned int stat_log_div = 0;
            static uint32_t stat_log_ms = 0;
            if (fps < stat_min_fps) { stat_min_fps = fps; }
            if ((stat_log_div++ % 20u) == 0u) {
                const uint32_t ldt = (stat_log_ms != 0 && now > stat_log_ms) ? now - stat_log_ms : dt;
                const unsigned int nev = (unsigned int) SDL_AtomicSet(&stat_events, 0);
                const unsigned int nmouse = (unsigned int) SDL_AtomicSet(&stat_mouse_events, 0);
                printf("ge: stats: %s | since last: min %dfps worst %d.%dms | ev %u/s mouse %u/s\n", title + 8,
                       stat_min_fps, (int)(stat_worst_us / 1000), (int)((stat_worst_us % 1000) / 100),
                       (unsigned)((uint64_t)nev * 1000 / ldt), (unsigned)((uint64_t)nmouse * 1000 / ldt));
                fflush(stdout);
                sysLogPowerIfChanged();
                stat_log_ms = now;
                stat_min_fps = 0x7fffffff;
                stat_worst_us = 0;
            }
        }
        stat_last_ms = now;
        stat_frames = 0;
        stat_last_fields = __atomic_load_n(&g_PortFieldsTicked, __ATOMIC_RELAXED);
        stat_last_ticks = (uint32_t) __atomic_load_n(&g_PortTickSerial, __ATOMIC_RELAXED);
        stat_last_tick = __atomic_load_n(&g_GlobalTickCount, __ATOMIC_RELAXED);
        stat_last_pump = g_PortFramesPumped;
        stat_last_idle = g_PortPumpSleepUs;
        stat_last_glus = g_PortGfxRunUs;
        stat_last_audus = g_PortAudioUs;
        stat_last_joyus = g_PortJoyUs;
        stat_last_pmpus = g_PortPumpUs;
        stat_last_draws = g_PortGfxDraws;
        stat_last_rn = g_PortRaphnetPolls;
        stat_last_limus = stat_limit_us;
        stat_last_swpus = stat_swap_us;
    }
}

extern "C" unsigned long long g_PortGfxRunT0Us;
extern "C" unsigned long long portProfNow(void);
extern "C" void portHitchFrame(unsigned long long interval_us, unsigned long long run_us,
                               unsigned long long swap_us, unsigned long long lim_us, unsigned int draws);

static void gfx_sdl_swap_buffers_begin(void) {
    gfx_sdl_maybe_screenshot();
    /* Both waits sit inside gfx_run's timer, so the stats line's gl figure
     * carries them: split out, or a limiter-paced 60fps reads as 16ms of GL. */
    const uint64_t t0 = SDL_GetPerformanceCounter();
    if (target_fps) {
        sync_framerate_with_timer();
    }
    const uint64_t t1 = SDL_GetPerformanceCounter();
    SDL_GL_SwapWindow(wnd);
    const uint64_t t2 = SDL_GetPerformanceCounter();
    stat_swap_us += (t2 - t1) * 1000000ull / qpc_freq;
    stat_limit_us += (t1 - t0) * 1000000ull / qpc_freq;
    { /* hitchlog.c: split a long frame (GE_HITCHMS) */
        static uint64_t hitch_prev = 0;
        static unsigned int hitch_draws = 0;
        const unsigned int draws = g_PortGfxDraws;
        if (hitch_prev != 0) {
            portHitchFrame((t2 - hitch_prev) * 1000000ull / qpc_freq,
                           g_PortGfxRunT0Us != 0 ? portProfNow() - g_PortGfxRunT0Us : 0,
                           (t2 - t1) * 1000000ull / qpc_freq, (t1 - t0) * 1000000ull / qpc_freq,
                           draws - hitch_draws);
        }
        hitch_prev = t2;
        hitch_draws = draws;
    }
    gfx_sdl_update_title_stats();
}

static void gfx_sdl_swap_buffers_end(void) {

}

static double gfx_sdl_get_time(void) {
    return SDL_GetPerformanceCounter() / (double)qpc_freq;
}

static int32_t gfx_sdl_get_target_fps(void) {
    return target_fps;
}

static void gfx_sdl_set_target_fps(int fps) {
    target_fps = fps;
}

static bool gfx_sdl_can_disable_vsync(void) {
    return true;
}

static void *gfx_sdl_get_window_handle(void) {
    return (void *)wnd;
}

static void gfx_sdl_set_window_title(const char *title) {
    SDL_SetWindowTitle(wnd, title);
}

static int gfx_sdl_get_swap_interval(void) {
    return SDL_GL_GetSwapInterval();
}

/* pcsched's vsync lock: is the swap waiting for vblank? Uses the flag set by set_swap_interval, not
 * SDL_GL_GetSwapInterval, which reads the calling thread's context and has none
 * on the pump thread. */
extern "C" int gfx_vsync_pacing(void) {
    return vsync_enabled;
}

/* pcsched's vsync lock: refresh rate of the window's display (0 = unknown),
 * read on the main thread. It is re-read twice a second to follow monitor moves
 * and mode switches. */
extern "C" int gfx_display_refresh_hz(void) {
    static int hz;
    static uint32_t last_ms;
    const uint32_t now = SDL_GetTicks();

    if (wnd != NULL && (last_ms == 0 || now - last_ms >= 500)) {
        SDL_DisplayMode m;
        const int di = SDL_GetWindowDisplayIndex(wnd);

        hz = (di >= 0 && SDL_GetCurrentDisplayMode(di, &m) == 0) ? m.refresh_rate : 0;
        last_ms = (now != 0) ? now : 1;
    }
    return hz;
}

/* Render thread support: the main thread creates the GL context, then releases
 * it for the render thread to bind. The window itself stays on the main thread,
 * so fullscreen, title and event-pump calls must run there. */
extern "C" void gfx_sdl_gl_release_current(void) {
    SDL_GL_MakeCurrent(wnd, NULL);
}
extern "C" void gfx_sdl_gl_make_current(void) {
    SDL_GL_MakeCurrent(wnd, ctx);
}
/* The game window, usable from any thread. Don't use SDL_GL_GetCurrentWindow()
 * instead: it returns NULL on the main thread once the render thread owns GL,
 * which breaks mouse capture and aiming. */
extern "C" SDL_Window* gfx_sdl_get_window(void) {
    return wnd;
}

static bool gfx_sdl_set_swap_interval(int interval) {
    const bool success = SDL_GL_SetSwapInterval(interval) >= 0;
    vsync_enabled = success && (interval != 0);
    if (!success) {
        sysLogPrintf(LOG_WARNING, "SDL: failed to set vsync %d: %s", interval, SDL_GetError());
    }
    return success;
}

int gfx_sdl_get_display_mode(int modenum, int *out_w, int *out_h) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode sdlmode;
    if (SDL_GetDisplayMode(display_in_use, modenum, &sdlmode) == 0) {
        *out_w = sdlmode.w;
        *out_h = sdlmode.h;
        return 1;
    }
    return 0;
}

int gfx_sdl_get_current_display_mode(int *out_w, int *out_h) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode sdlmode;
    if (SDL_GetCurrentDisplayMode(display_in_use, &sdlmode) == 0) {
        *out_w = sdlmode.w;
        *out_h = sdlmode.h;
        return 1;
    }
    return 0;
}

int gfx_sdl_get_num_display_modes(void) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    return SDL_GetNumDisplayModes(display_in_use);
}

struct GfxWindowManagerAPI gfx_sdl = {
    gfx_sdl_init,
    gfx_sdl_close,
    gfx_sdl_get_display_mode,
    gfx_sdl_get_current_display_mode,
    gfx_sdl_get_num_display_modes,
    gfx_sdl_get_fullscreen_state,
    gfx_sdl_set_fullscreen_changed_callback,
    gfx_sdl_set_fullscreen,
    gfx_sdl_set_fullscreen_exclusive,
    gfx_sdl_set_fullscreen_flag,
    gfx_sdl_get_fullscreen_flag_mode,
    gfx_sdl_get_maximized_state,
    gfx_sdl_set_maximize_window,
    gfx_sdl_get_active_window_refresh_rate,
    gfx_sdl_set_cursor_visibility,
    gfx_sdl_set_closest_resolution,
    gfx_sdl_set_dimensions,
    gfx_sdl_get_dimensions,
    gfx_sdl_get_centered_positions,
    gfx_sdl_handle_events,
    gfx_sdl_start_frame,
    gfx_sdl_swap_buffers_begin,
    gfx_sdl_swap_buffers_end,
    gfx_sdl_get_time,
    gfx_sdl_get_target_fps,
    gfx_sdl_set_target_fps,
    gfx_sdl_can_disable_vsync,
    gfx_sdl_get_window_handle,
    gfx_sdl_set_window_title,
    gfx_sdl_get_swap_interval,
    gfx_sdl_set_swap_interval,
};
