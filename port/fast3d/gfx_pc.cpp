#define NOMINMAX

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdio>

#include <map>
#include <chrono>
#include <set>
#include <unordered_map>
#include <vector>
#include <list>
#include <deque>
#include <stack>
#include <string>
#include <iostream>
#include <memory>
#include <limits>
#include <atomic>
#include <mutex>
#include <algorithm>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>
#include "gbiex_compat.h" // PD EXT opcodes/bits absent from GE's gbi.h

#include "platform.h"

#include "gfx_pc.h"
#include "gfx_cc.h"
#include "gfx_window_manager_api.h"
#include "gfx_rendering_api.h"
#include "gfx_screen_config.h"
#include "port_hostptr.h" /* PORT_IS_HOST_ADDR: the Xbox image sits below 0x10000000 */

uintptr_t gfxFramebuffer;

#define ALIGN(x, a) (((x) + (a - 1)) & ~(a - 1))

#define SUPPORT_CHECK(x) assert(x)

// SCALE_M_N: upscale/downscale M-bit integer to N-bit
#define SCALE_5_8(VAL_) (((VAL_)*0xFF) / 0x1F)
#define SCALE_8_5(VAL_) ((((VAL_) + 4) * 0x1F) / 0xFF)
#define SCALE_4_8(VAL_) ((VAL_)*0x11)
#define SCALE_8_4(VAL_) ((VAL_) / 0x11)
#define SCALE_3_8(VAL_) ((VAL_)*0x24)
#define SCALE_8_3(VAL_) ((VAL_) / 0x24)

// SCREEN_WIDTH and SCREEN_HEIGHT are defined in the headerfile
#define HALF_SCREEN_WIDTH (SCREEN_WIDTH / 2.f)
#define HALF_SCREEN_HEIGHT (SCREEN_HEIGHT / 2.f)

#define RATIO_X (gfx_current_dimensions.width / (float)SCREEN_WIDTH)
#define RATIO_Y (gfx_current_dimensions.height / (float)SCREEN_HEIGHT)

#define MAX_BUFFERED 256
#define MAX_LIGHTS 4
#define MAX_VERTICES 128
/* GE port: static room geometry cache (gfx_roomcache.inl, included before
 * gfx_sp_tri1). These live here because gfx_sp_vertex / gfx_sp_matrix /
 * gfx_texture_cache_clear_c come first in the file. */
struct RocRoom;
/* Per-triangle state hoist: gfx_sp_tri1 used to re-derive the full draw state
 * (depth mode, blend/fog/cycle options, combiner and shader lookups, texture
 * sizes, clamp modes, sampler latch, decal bias) for every triangle, which was
 * a large share of a heavy frame on slow CPUs. Every display-list command that
 * can change that state bumps ge_state_serial (gfx_run_dl, plus the out-of-band
 * writers of rendering_state), and gfx_sp_tri1 recomputes only when the serial,
 * the texture-changed flags or a fingerprint of the few non-display-list inputs
 * moved.
 * gfx_tri_hoist = 0 ([display] tri_hoist) restores the per-triangle path. */
static unsigned ge_state_serial = 1;
extern "C" uint32_t gfx_tri_hoist = 1; /* on by default; set [display] tri_hoist=0 to A/B */
static uint32_t roc_dbg_ops[256]; /* DL commands per opcode (twenty-second build: gfx_run_dl's self time did not move with the rooms cached) */
static RocRoom* roc_capturing = nullptr;
static int roc_next_kind = 0, roc_next_arg = 0; /* G_PORTCACHE_EXT: what the next G_DL is (0 = nothing named) */
static bool roc_capture_bad = false;
static float roc_model_pos[MAX_VERTICES + 4][3];
static float roc_model_nrm[MAX_VERTICES + 4][3]; /* n/127, lit parts */
static const Gfx* roc_cur_cmd = nullptr;         /* the command being dispatched (draw boundaries) */
static int roc_capturing_kind = 0;                /* kind of the list being captured (RocRoom is opaque here) */
static uint8_t roc_slot_loaded[MAX_VERTICES + 4];  /* set by G_VTX during a capture: a triangle on a slot this list did not load is foreign */
/* A vertex uses the matrix that was current at its G_VTX, not at its triangle,
 * because a part list may load several limbs' vertices under different matrices
 * before drawing any. Each vertex slot records a relative matrix generation,
 * and walk-replay snapshots MP / modelview per generation at each (skipped)
 * G_VTX. */
static uint32_t roc_capture_gen0 = 0, roc_walk_gen0 = 0;
static uint8_t roc_slot_gen[MAX_VERTICES + 4];
#define ROC_WALK_GENS 16 /* = the program's mats[] slots, c[14..77] */
static float roc_walk_mp[ROC_WALK_GENS][4][4];
static float roc_walk_mv[ROC_WALK_GENS][4][4];
static uint8_t roc_walk_gen_valid[ROC_WALK_GENS];
static uint32_t ge_texnode_serial = 0;           /* TextureCacheValue::serial source */
static uint32_t ge_texcache_delete_gen = 0;      /* bumped by the GAME's deletes only (gfx_texture_cache_delete: a pool slot reused = new content at the old address); LRU evictions leave it alone */
static uint32_t ge_texcache_gen = 0;             /* bumped whenever a texture cache node is erased: a room / part whose nodes were validated at this generation still holds live pointers */
extern "C" uint32_t gfx_xbox_part_skiptex = 1;   /* [display] xbox_part_skiptex: a walked part list skips its texture-load chain (the captured state carries the textures); 0 = bisect */
/* forty-fourth build diagnostics: which context the interpreter's triangles
 * come from (per 300 frames), and the deferred-XLU sorter's work */
static uint32_t ge_tri_origin = 0;               /* 0 other, 2 uncacheable room list, 3 uncacheable part list, 4 part entry-state miss, 5 capture */
static uint32_t roc_stat_tri_origin[16];         /* [1] = rects, [6] = passthrough walks, [7] = unregistered lists on DYNAMIC-arena vertices */
static uint32_t roc_stat_draw_origin[16];        /* gfx_flush draws by the origin of their last triangle; [8] = the XLU sorter's replay */
static uint32_t ge_flush_origin = 0;
static uint32_t roc_stat_tri_dxlu;               /* triangles deferred to the XLU sorter */
static uint32_t dxlu_stat_units, dxlu_stat_tests, dxlu_stat_moves, dxlu_stat_passes, dxlu_stat_visits, dxlu_stat_cut, dxlu_stat_merged;
extern "C" uint32_t gfx_xbox_xlu_batch = 1; /* [display] xbox_xlu_batch: 0 = no state-run merging of the sorted XLU list (bisect) */
extern "C" uint32_t gfx_xbox_xlu_visits_max = 8000; /* [display] xbox_xlu_visits_max: the XLU plane pass's pair visits per frame before it settles for the key order (0 = unlimited) */
static const char* roc_capture_bad_why = "matrix load / rect / deferred-XLU inside its list";
static uint32_t roc_mtx_gen = 0;                  /* bumped by every MTX / POPMTX: part draws split on it */
extern "C" void portRoomCacheClear(void);
#define MAX_VERTEX_COLORS 64

#ifdef GE_XBOX
#define TEXTURE_CACHE_MAX_SIZE 640 /* console: 256 thrashed in Cradle (16 uploads/frame at ~230 us each, fourteenth run); 512 thrashed under the pause screen (fifty-third run: 10 uploads a frame, every retained list recaptured); gfx_nv2a.cpp's table is sized to this + 1 */
#else
#define TEXTURE_CACHE_MAX_SIZE 1024
#endif

#define C0(pos, width) ((cmd->words.w0 >> (pos)) & ((1U << width) - 1))
#define C1(pos, width) ((cmd->words.w1 >> (pos)) & ((1U << width) - 1))

struct RGBA {
    uint8_t r, g, b, a;
};

struct NormalColor {
    union {
        struct { uint8_t r, g, b, a; };
        struct { int8_t x, y, z, w; };
    };
};

struct LoadedVertex {
    float x, y, z, w;
    float u, v;
    struct RGBA color;
    uint8_t fog;
    uint8_t clip_rej;
};

/* 8-bit -> float, bit-identical to `x / 255.0f` (twenty-seventh build: the
 * emission loop did that divide ~10 times per vertex; divss is 18 cycles
 * and unpipelined on the Xbox CPU) */
static float ge_b2f[256];
static struct GeB2fInit {
    GeB2fInit() {
        for (int i = 0; i < 256; i++) {
            ge_b2f[i] = (float)i / 255.0f;
        }
    }
} ge_b2f_init;

/* Freed GL texture ids, stamped with the frame (num_dls) they were freed on.
 * Redefining an id an in-flight frame still samples can make the driver wait
 * for the GPU: after the per-upload glGetError went (2026-10-16), a tester's
 * remaining hitches were swap stalls on frames that uploaded 17-160 textures,
 * and every slow upload in murkantor's log had reused a just-freed id. So an
 * id is handed out again only GE_TEXIDAGE frames (default 3, 0 = at once,
 * newest first, as before) after it was freed; until then a new id is made. */
extern uint32_t num_dls;
struct GeFreeTextureIds {
    std::deque<std::pair<uint32_t, uint32_t>> q; /* id, frame freed */
    void push_back(uint32_t id) {
        q.push_back(std::make_pair(id, num_dls));
    }
    bool take(uint32_t* id) {
        static int age = -1;
        if (age < 0) {
            const char* e = getenv("GE_TEXIDAGE");
            age = (e != NULL && *e != '\0') ? atoi(e) : 3;
            if (age < 0) {
                age = 0;
            }
        }
        if (q.empty()) {
            return false;
        }
        if (age == 0) {
            *id = q.back().first;
            q.pop_back();
            return true;
        }
        if (num_dls - q.front().second < (uint32_t)age) {
            return false;
        }
        *id = q.front().first;
        q.pop_front();
        return true;
    }
};

static struct {
    TextureCacheMap map;
    std::list<TextureCacheMapIter> lru;
    GeFreeTextureIds free_texture_ids;
} gfx_texture_cache;

static bool ge_churn_reset; /* gfx_texture_cache_clear -> the texchurn history starts again */

struct ColorCombiner {
    uint64_t shader_id0;
    uint32_t shader_id1;
    bool used_textures[2];
    struct ShaderProgram* prg[16];
    uint8_t shader_input_mapping[2][7];
};

static std::map<ColorCombinerKey, struct ColorCombiner> color_combiner_pool;
static std::map<ColorCombinerKey, struct ColorCombiner>::iterator prev_combiner = color_combiner_pool.end();

static uint8_t* tex_upload_buffer = nullptr;

/* Grows tex_upload_buffer to hold `texels` RGBA8 texels; false = out of memory and
 * the import is skipped. A fixed 8192x8192 buffer was a 256 MB malloc that a 32-bit
 * process can be refused at boot, and the NULL crashed the first upload. */
static bool ge_tex_upload_reserve(uint64_t texels) {
    static size_t cap;
    const uint64_t need = (texels != 0 ? texels : 1) * 4;

    if (need > cap) {
        uint8_t* nb = need <= 0x7fffffffu ? (uint8_t*)realloc(tex_upload_buffer, (size_t)need) : nullptr;
        if (nb == nullptr) {
            static int said;
            if (said < 8) {
                said++;
                fprintf(stderr, "ge: gfx: no memory for a %llu-texel texture upload - skipped\n",
                        (unsigned long long)texels);
            }
            return false;
        }
        tex_upload_buffer = nb;
        cap = (size_t)need;
    }
    return true;
}

static struct RSP {
    float modelview_matrix_stack[11][4][4];
    uint8_t modelview_matrix_stack_size;

    float MP_matrix[4][4];
    float P_matrix[4][4];

    Light_t lookat[2];
    bool lookat_enabled;

    Light_t current_lights[MAX_LIGHTS + 1];
    float current_lights_coeffs[MAX_LIGHTS][3];
    float current_lookat_coeffs[2][3]; // lookat_x, lookat_y
    uint8_t current_num_lights;        // includes ambient light
    bool lights_changed;

    uint32_t geometry_mode;
    int16_t fog_mul, fog_offset;

    uint32_t extra_geometry_mode;

    uint32_t aspect_mode;
    float aspect_ofs;
    float aspect_scale;

    struct {
        // U0.16
        uint16_t s, t;
    } texture_scaling_factor;

    struct LoadedVertex loaded_vertices[MAX_VERTICES + 4];

    // PD-only vertex-color palette (G_COL): GE's stock Vtx carries RGBA
    // inline, and GE's 0x07 is G_RESERVED2 (never emitted). Dead-gated.
    // const struct NormalColor *vertex_colors; //[MAX_VERTEX_COLORS];
} rsp;

struct RawTexMetadata {
    uint16_t width, height;
    float h_byte_scale = 1, v_pixel_scale = 1;
};

struct LoadedTexture {
    const uint8_t* addr;
    uint32_t orig_size_bytes;
    uint32_t full_size_bytes; // full_image_line_size_bytes * height
    uint32_t size_bytes; // line_size_bytes * height
    uint32_t full_image_line_size_bytes;
    uint32_t line_size_bytes;
    uint32_t tex_flags;
    struct RawTexMetadata raw_tex_metadata;
};

static struct RDP {
    uint16_t palette[256];
    /* Content fingerprint of the loaded TLUT: the texture
     * cache keys CI textures on the palette ADDRESS, but GE reloads
     * different palettes into the same buffer - a cached CI texture then
     * kept the colours it was first imported with (the Control mainframe's
     * green/purple garbage, and only on the racks that lost the race).
     * Stamped at LOADTLUT (cheap - once per palette load, not per bind)
     * and folded into the cache key. */
    uint32_t palette_sum;
    /* the same fingerprint per 16-entry block, content only (no block
     * number): a CI4 texture reads one block, so it is keyed on that block
     * alone (import_texture) */
    uint32_t palette_block_hash[16];
    uint32_t palette_count256; /* entries the last TLUT load at TMEM 256 wrote */
    uint32_t palette_ci8_hash; /* entries [0, palette_count256), made on first use after a load */
    bool palette_ci8_hash_ok;
    const uint8_t* palette_addrs[2];
    uint32_t palette_fmt;
    struct {
        const uint8_t* addr;
        uint8_t siz;
        uint32_t width;
        uint32_t tex_flags;
        struct RawTexMetadata raw_tex_metadata;
        uint16_t tmem; // TMEM word the load tile deposited this data at
    } texture_to_load;
    struct {
        uint8_t fmt;
        uint8_t siz;
        uint8_t cms, cmt;
        uint8_t masks, maskt;        // wrap masks (0 = none)
        uint8_t shifts, shiftt;
        uint16_t uls, ult, lrs, lrt; // U10.2
        uint16_t width, height;      // in texels
        uint16_t tmem;               // 0-511, in 64-bit word units
        uint32_t line_size_bytes;
        uint8_t palette;
    } texture_tile[8];
    LoadedTexture loaded_texture[512]; // for each tmem location
    bool textures_changed[2];

    uint8_t first_tile_index;
    uint8_t tex_min_lod;
    uint8_t tex_max_lod;

    uint32_t other_mode_l, other_mode_h;
    uint64_t combine_mode;
    bool grayscale;
    bool tex_lod;
    bool tex_detail;

    uint8_t prim_lod_fraction;
    uint8_t prim_min_level;   /* SETPRIMCOLOR m: the detail fraction's floor (gfx_ge_detail_frac) */
    struct RGBA env_color, prim_color, fog_color, fill_color, grayscale_color;
    struct XYWidthHeight viewport, scissor;
    bool viewport_or_scissor_changed;
    void* z_buf_address;
    void* color_image_address;

    int16_t subpixel_ofs_x;
    int16_t subpixel_ofs_y;
} rdp;

static struct RenderingState {
    uint8_t depth_mode;
    bool alpha_blend;
    bool modulate;
    struct XYWidthHeight viewport, scissor;
    struct ShaderProgram* shader_program;
    TextureCacheNode* textures[SHADER_MAX_TEXTURES];
} rendering_state;

struct GfxDimensions gfx_current_window_dimensions;
int32_t gfx_current_window_position_x;
int32_t gfx_current_window_position_y;
struct GfxDimensions gfx_current_dimensions;
static struct GfxDimensions gfx_prev_dimensions;
struct XYWidthHeight gfx_current_game_window_viewport;
struct XYWidthHeight gfx_current_native_viewport;
float gfx_current_native_aspect = 4.f / 3.f;
bool gfx_framebuffers_enabled = true;
bool gfx_detail_textures_enabled = true;

static bool game_renders_to_framebuffer;
static int game_framebuffer;
static int game_framebuffer_msaa_resolved;

uint32_t gfx_msaa_level = 1;
uint32_t gfx_n64_aa = 0; /* GE N64 AA: VI-style resolve of the MSAA level frame (video.c [display] n64_aa) */
/* Line mode cheat: the two values above are what the frame uses, these are what
 * the player set. While the cheat is on the frame uses at least 4x MSAA and the
 * N64 AA resolve, and turning it off restores the set values (recomputed every
 * gfx_start_frame). */
static uint32_t gfx_msaa_user = 1, gfx_n64_aa_user = 0;
static std::atomic<int> gfx_line_mode_req{0}; /* boss.c, every frame, sim thread */
extern "C" { uint32_t gfx_line_mode = 0; }    /* this frame's, render thread (gfx_opengl.cpp reads it) */
uint32_t gfx_anisotropy = 1;
uint32_t gfx_filter_2d = 1;
/* set by G_MIRRORPILLAR_EXT around an overlay wanting mirrored side bands -
 * blood_animation.c's death / gunbarrel blood, and title.c's untextured fade.
 * The sampler latch reads it as well: see the filter_2d rule there. */
static bool gfx_ge_mirror_pillars = false;
/* G_CONSOLE2D_EXT brackets the console, the last thing drawn in a frame
 * (boss.c). Inside it the native space is the console's own (480 lines, as wide
 * as the window), the 4:3 pin is off, and if the frame was rendered at a lower
 * resolution it is presented first so the console is drawn straight onto the
 * window instead of being squashed into 240 lines. */
static bool gfx_ge_con2d = false;
static uint32_t gfx_ge_con2d_saved_w, gfx_ge_con2d_saved_h;
static float gfx_ge_con2d_saved_aspect;
static void gfx_ge_con2d_set(uint32_t w, uint32_t h);
static void gfx_present_game_fb(void);
uint32_t gfx_low_res = 0; /* GE "Low Res": render the whole game at 240 lines
                           * and scale to the window at present; video.c
                           * [display] low_res drives it, gfx_start_frame
                           * applies it. */
uint32_t gfx_mipmaps = 1; /* GE N64 mipmaps: GE draws mipmapped textures with
                           * G_TL_LOD + G_CC_TRILERP and the RDP picks the level
                           * per pixel, but this interpreter only binds level 0,
                           * which shimmers badly when minified. 1 (default)
                           * enables mipmapped minification on those draws, 0 =
                           * off; video.c [display] hw_mipmaps / GE_HWMIPMAPS
                           * drive it. */
#ifdef GE_XBOX
extern "C" void portXboxTrace(const char *fmt, ...);
extern "C" void portXboxWhere(const char *where, unsigned n);
static unsigned ge_xbox_cmds;      /* commands interpreted this frame (first-frame trace) */
static unsigned ge_xbox_trace_dl;  /* trace the first frames' DLs (set in gfx_run) */
#endif
extern "C" int ge_mip_active; /* GL backend side channel: the sampler being set
                               * serves a G_TL_LOD draw - minify through mips */
int ge_mip_active;

/* GE widescreen presentation (video.c decides per submitted frame):
 *   0 = off - everything maps game space -> window like always (4:3 window,
 *       or the classic stretch when widescreen=0)
 *   1 = wide gameplay frame - 3D fills the window (the game's projection
 *       aspect was widened), textured rectangles (HUD/menus/text) pin to a
 *       centered 4:3 band; color fills still span (fades, letterbox bars)
 *   2 = pin the whole frame to centered 4:3 (frontend, watch menu - their
 *       3D is authored 4:3 and must stay proportional with their 2D) */
extern "C" uint32_t gfx_ge_aspect_pin = 0; /* C linkage: video.c reads it (MSVC-ABI clang mangles C++ globals) */
extern "C" unsigned int g_PortFramesPumped;  /* pcsched.c; file scope so the block-scope externs below keep C linkage */

/* Inside gfx_draw_rectangle (texture rectangles = HUD/menus/text). With
 * gfx_filter_2d=0 the sampler latch below forces point sampling for these
 * draws so upscaled 2D stays crisp while 3D keeps its filter. */
static bool s_DrawingRect = false;
/* The textured rect being drawn: its UV span in S10.5, set by
 * gfx_dp_texture_rectangle around the draw. Lets the sampler tell a rect that
 * shows ONE copy of its texture (an icon, a glyph) from one that tiles it. */
static bool s_RectUvValid = false;
static int32_t s_RectUvS0, s_RectUvS1, s_RectUvT0, s_RectUvT1;
/* textrelated.c PORT_TEXT_GLYPHS: the texrects being drawn are FONT GLYPHS.
 * s_RectGlyphBorder = this one was grown half a texel and wants a transparent
 * border from the sampler (see gfx_dp_texture_rectangle). */
static bool s_GeTextGlyphs = false;
static bool s_RectGlyphBorder = false;
/* options.c portHudScaleBegin (midas.ini hud_scale): G_NOOP tag 0x58 brackets
 * a HUD element, whose 2D rects then draw at half size about the pivot (U10.2),
 * as on a 640x480 canvas. 0x58FFFFFF ends the bracket. */
static bool s_GeHudScale = false;
static float s_GeHudPivotX = 0.f, s_GeHudPivotY = 0.f;
/* bondwalk2.c portRectPreciseTag: G_NOOP tag 0x59 gives the NEXT rect sub-quarter
 * edge fractions (1/64 of a U10.2 unit: ulx, uly, lrx, lry) and no lr round-up,
 * so the crosshair is not snapped to the 10.2 grid. */
static bool s_GeRectFracPending = false, s_GeRectFracOn = false;
static float s_GeRectFrac[4];
/* a wrap mode of our own, beside G_TX_WRAP / MIRROR / CLAMP: outside the
 * texture is TRANSPARENT (gfx_opengl.cpp maps it to GL_CLAMP_TO_BORDER) */
#define GE_TX_BORDER 0x10

static bool dropped_frame;

static float buf_vbo[MAX_BUFFERED * (32 * 3)]; // 3 vertices in a triangle and 32 floats per vtx
static size_t buf_vbo_len;
static size_t buf_vbo_num_tris;

static struct GfxWindowManagerAPI* gfx_wapi;
static struct GfxRenderingAPI* gfx_rapi;

static uintptr_t segmentPointers[16];

struct FBInfo {
    uint32_t orig_width, orig_height;
    uint32_t applied_width, applied_height;
    bool upscale, autoresize;
};

static bool fbActive = 0;
static std::map<int, FBInfo>::iterator active_fb;
static std::map<int, FBInfo> framebuffers;

static constexpr float clampf(const float x, const float min, const float max) {
    return (x < min) ? min : (x > max) ? max : x;
}

/* Title-bar phase split (see gfx_sdl2.cpp): microseconds spent inside
 * gfx_run - i.e. DL translation plus GL submission - and the number of
 * draw_triangles batches that took. Frame time minus this is the sim tick
 * plus display-list build. Two numbers instead of a full profiler. */
extern "C" {
    unsigned long long g_PortGfxRunUs = 0;
    unsigned int g_PortGfxDraws = 0;
    struct PortHwLoad g_PortHwLoadLast = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
}

static struct PortHwLoad g_PortHwLoad; /* the frame in progress (gfx_api.h) */
/* ZMODE_DEC projection intercept + pull distance of the current decal state,
 * for the GL backend's decal clip (gfx_opengl_draw_triangles) */
float gfx_ge_dec_b = 0.0f, gfx_ge_dec_d = 0.0f;
static void gfx_flush(void) {
    if (buf_vbo_len > 0) {
        roc_stat_draw_origin[ge_flush_origin & 15u]++;
        gfx_rapi->draw_triangles(buf_vbo, buf_vbo_len, buf_vbo_num_tris);
        buf_vbo_len = 0;
        buf_vbo_num_tris = 0;
        g_PortGfxDraws++;
    }
}

/* The GPU was reset under us (Xbox output mode switch): every cached
 * assumption about what it holds is void. */
extern "C" void gfx_ge_gpu_reset_c(void) {
    buf_vbo_len = 0;
    buf_vbo_num_tris = 0;
    rendering_state = {};
    rendering_state.shader_program = nullptr;
    rendering_state.depth_mode = 0xFF; /* no valid encoding: the next triangle re-sends */
    rdp.textures_changed[0] = rdp.textures_changed[1] = true;
    rdp.viewport_or_scissor_changed = true;
    ge_state_serial++;
}

/* ---- GE port: deferred sorted transparency --------------------------------
 * GE mostly does not order its translucent surfaces (rooms only by portal-hop
 * depth - zero on open levels; nothing within a room's XLU list; props only
 * coarsely). On N64 the RDP coverage system (CLR_ON_CVG / CVG_DST_WRAP)
 * self-limits out-of-order XLU overlap so it doesn't matter; this renderer
 * has no coverage emulation, so out-of-order XLU alpha-blends inverted
 * ("back sorted" gantry panels on Cradle, and it shifts with every camera
 * move). Universal fix: capture every world-transparent triangle (depth
 * tested, no z-write, blended) instead of drawing it, then draw them all
 * back-to-front by view depth (clip w) before 2D/HUD rendering starts.
 * GE_NOXLUSORT=1 restores the old immediate path. */
#ifdef GE_XBOX
#define DXLU_MAX_TRIS 4096 /* 64 MB console: the capture buffers below scale with this (16384 = 9 MB of .bss) */
#else
#define DXLU_MAX_TRIS 16384
#endif
#define DXLU_MAX_VERT_FLOATS 32
#define DXLU_MAX_FLOATS (DXLU_MAX_TRIS * 3 * DXLU_MAX_VERT_FLOATS)
#define DXLU_MAX_STATES 1024

struct DXluState {
    struct ShaderProgram* prg;
    bool use_alpha;
    bool use_modulate;
    bool depth_test, depth_update, depth_compare, depth_source_prim;
    uint16_t zmode;
    uint8_t depth_mode_enc;
    struct XYWidthHeight viewport, scissor;
    bool tex_used[2];
    uint32_t tex_id[2];
    bool tex_linear[2];
    bool tex_mip[2]; /* GE N64 mipmaps: replay must re-assert the node's mip
                      * sampling, or a shared texture object is left un-mipped
                      * while its cache node still says mipped (latch stale) */
    uint32_t tex_cms[2], tex_cmt[2];
};

struct DXluTri {
    float key;      /* secondary sort key (order within a group): max eye
                     * distance of the 3 vertices, see gfx_dxlu_vertex_dist. */
    float gkey;     /* primary sort key: the group's key (mean member distance for
                     * props, 0 = nearest for debris), = key for stray tris */
    float mind;     /* min / max eye distance of the 3 vertices (depth range) */
    float maxd;
    float bb[4];    /* screen bbox: xmin, xmax, ymin, ymax (x/w, y/w) */
    float e[9];     /* the 3 vertices in eye space (gfx_dxlu_vertex_eye) */
    uint32_t grp;   /* 0 = stray tri, else the unit id gfx_dxlu_group() closed over it */
    uint32_t scope; /* 0 = none, else the room-block scope id open at capture
                     * (bg.c room loop B: the room's XLU list + that room's
                     * prop alpha passes share one scope - authored order) */
    uint32_t state; /* index into dxlu_states */
    uint32_t offset;
    uint16_t nfloats;
    uint8_t gmode;  /* group mode of grp (1 prop, 2 debris, 3 room, 4 glued decals) */
    uint8_t eye_ok; /* e[]/bb[] valid (perspective P, w > 0) */
    int32_t ret;    /* fiftieth build: >= 0 = a retained-buffer triangle (dxlu_ret_units index), no floats */
    uint32_t ret_first; /* its first vertex index in the retained buffer (3 consecutive) */
};

/* a draw unit of the sorted pass: one prop/effect group (submission order
 * inside), one room group (GE_XLU_ROOMUNITS=1 only; per-tri inside) or one
 * stray tri. Built by gfx_dxlu_draw from the capture order. */
struct DXluUnit {
    uint32_t first, count; /* member tris: dxlu_tris[first .. first+count) */
    float key;             /* sort key (= members' gkey) */
    float mind, maxd;      /* depth range over the members */
    float bb[4];           /* screen bbox over the members */
    bool plane_ok;         /* all members coplanar: n.p + d = 0, eye side d > 0 */
    float n[3], d;
    uint32_t scope;        /* room-block scope id of the members (0 = none) */
    uint8_t mode;          /* 0 stray, 1 prop, 2 debris (pinned last), 3 room,
                            * 4 glued decals (pinned first) */
};

static float dxlu_vbo[DXLU_MAX_FLOATS];
static uint32_t dxlu_vbo_len;
static struct DXluState dxlu_states[DXLU_MAX_STATES];
static uint32_t dxlu_num_states;
static struct DXluTri dxlu_tris[DXLU_MAX_TRIS];
static uint32_t dxlu_num_tris;
static uint32_t dxlu_sort_idx[DXLU_MAX_TRIS];
static struct DXluUnit dxlu_units[DXLU_MAX_TRIS];
static uint32_t dxlu_unit_order[DXLU_MAX_TRIS];
/* Translucent triangles of a captured list keep object-space vertices in its
 * retained buffer and are enqueued to the sorter at replay (keys, depth range,
 * eye-space corners and screen box come from a bare matrix multiply). The
 * sorted pass then draws each run by an index list (retained_draw_indexed), so
 * there is no interpreter transform and the walked list skips its G_VTX. */
#define DXLU_MAX_RET_UNITS 256
struct DXluRetUnit {
    int desc;
    float mvp[16];
    float fx[4], fogp[4], fogsel[4], fog_rgb[3], decal[4];
    float lights[20];
    bool has_lights;
    float prim_env[12];
    uint32_t cull_mode;
    uint8_t nmats;
    uint8_t mat_slot[16];
    float mats[16][16];
};
static struct DXluRetUnit dxlu_ret_units[DXLU_MAX_RET_UNITS];
static uint32_t dxlu_num_ret_units;
static uint32_t roc_stat_tri_xluret;
static uint32_t roc_stat_xlu_planar_units;
extern "C" uint32_t gfx_xbox_xlu_planar_units = 1; /* [display] xbox_xlu_planar_units: 0 = retained XLU triangles stay one sorter unit each (bisect) */
extern "C" uint32_t gfx_xbox_xlu_retained = 1; /* [display] xbox_xlu_retained: 0 = XLU triangles of retained lists stay on the interpreter (passthrough walk; bisect) */
static void roc_xlu_replay_unit(int32_t unit, const uint16_t* idx, uint32_t n); /* gfx_roomcache.inl */
static uint8_t dxlu_unit_moved[DXLU_MAX_TRIS];
static uint32_t dxlu_group_count; /* unit ids handed out this capture */
/* capture window: true from the moment the deferred set is flushed for a
 * view until the next view begins - the gun/HUD overlay regime draws
 * z-comparing sub-parts (sights, watch glass) that must stay immediate
 * and in submission order, not join the next world's sorted set. */
static bool dxlu_closed;
/* G_DXLU_GROUP_EXT: a run of captured tris that must keep their SUBMISSION
 * order (the N64 layering) while still sorting as one unit against the rest
 * of the scene. GE's explosion renderer draws its puffs oldest-first so the
 * freshest, brightest ones land on top regardless of depth, and lv.c draws
 * the flying debris after every prop so it is always over the fireball;
 * per-tri depth sorting broke both (a dim old puff nearer the camera buried
 * the bright core, chunks vanished inside the ball, and the look changed
 * with every step around it). On group end every member tri gets the same
 * key - the mean of its members (mode 1) or 0 = nearest (mode 2, for the
 * debris pass that vanilla draws last) - and std::stable_sort keeps their
 * order within it. */
static bool dxlu_group_open;
static uint32_t dxlu_group_start;
static uint32_t dxlu_group_mode;
static uint32_t dxlu_group_depth;
/* room-block scope (marker mode 5 begin / 6 end, bg.c room loop B): tris
 * captured inside share a scope id. Vanilla draws a room's XLU list, then
 * THAT room's props' alpha passes - the props must paint over their own
 * room's XLU whatever the depths say (Jungle: the shrub prop sits BEHIND
 * the vine-curtain tri by eye distance, yet the console shows it in front
 * purely by submission order). Scopes are orthogonal to groups. */
static uint32_t dxlu_scope_cur;
static uint32_t dxlu_scope_count;
static uint32_t dxlu_trace_mark[DXLU_MAX_TRIS / 32]; /* GE_DXLU_TRACE bitmap */

static bool gfx_dxlu_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        enabled = getenv("GE_NOXLUSORT") == NULL;
    }
    return enabled != 0;
}

static void gfx_dxlu_reset(void) {
    dxlu_vbo_len = 0;
    dxlu_num_ret_units = 0;
    dxlu_num_states = 0;
    dxlu_num_tris = 0;
    dxlu_group_open = false;
    dxlu_group_depth = 0;
    dxlu_group_count = 0;
    dxlu_scope_cur = 0;
    dxlu_scope_count = 0;
    /* capture opens ONLY at the explicit new-view marker (bgLevelRender);
     * menus/cutscene paths without world rendering never capture. */
    dxlu_closed = true;
}

static void gfx_dxlu_draw(void);

/* Translucent (XLU) draw ordering. The N64 never sorted translucent geometry:
 * it drew XLU in game submission order, which Rare authored (display-list part
 * order, BSP nodes, portal-hop room depth). The port therefore sorts by draw
 * unit (room XLU pass = mode 3, prop model = mode 1, explosion/smoke = mode 1,
 * debris = mode 2): each unit keeps its submission order inside and is sorted
 * as a whole by mean depth against the others. Stray triangles outside any unit
 * (decals, shards, sparks) are sorted per triangle, and rooms are also sorted
 * per triangle because their panels need depth ordering inside the room.
 *
 * No single depth key orders every case (stacked water layers drawn by two
 * rooms, window panes inside a room's railings), so gfx_dxlu_plane_pass
 * (Newell) then moves a unit in front of another only when the key order is
 * provably wrong and the reverse is provably right. A unit is behind another if
 * all its vertices are on the far side of the other's plane, or the other's are
 * all on the eye side of its own.
 *
 * Debug knobs for A/B: GE_XLU_ROOMUNITS=1 treats rooms as units again (mode-3
 * markers honoured), GE_XLU_NOPLANE=1 skips the plane pass, GE_NOXLUSORT=1
 * disables the whole pass. */
static bool gfx_dxlu_room_units(void) {
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("GE_XLU_ROOMUNITS");
        v = (e != NULL && e[0] != '0') ? 1 : 0;
    }
    return v != 0;
}

static bool gfx_dxlu_plane_pass_enabled(void) {
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("GE_XLU_NOPLANE");
        v = (e != NULL && e[0] != '0') ? 0 : 1;
    }
    return v != 0;
}

/* DECAL PIN: a decal glued to an opaque, z-written surface can always be
 * painted first among the captured XLU, because anything behind its surface
 * fails the depth test and anything in front paints over it correctly. Pinned
 * units share
 * DXLU_KEY_PINNED, draw before everything else in submission order
 * (stable_sort) and skip the plane pass; they are the bg.c mode-4 group (scorch
 * and bullet impacts) and every stray ZMODE_DEC triangle (wall sign overlays).
 * GE_XLU_NOPIN=1 turns this off for A/B. */
#define DXLU_KEY_PINNED 1e30f

static bool gfx_dxlu_pin_enabled(void) {
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("GE_XLU_NOPIN");
        v = (e != NULL && e[0] != '0') ? 0 : 1;
    }
    return v != 0;
}

static void gfx_dxlu_group(uint32_t mode) {
    /* room-block scope channel (orthogonal to groups): 5 begin, 6 end */
    if (mode == 5) {
        dxlu_scope_cur = ++dxlu_scope_count;
        return;
    }
    if (mode == 6) {
        dxlu_scope_cur = 0;
        return;
    }
    /* groups nest (a prop's model render can bracket a child prop's):
     * only the outermost begin/end pair defines the group. */
    if (mode == 3 && !gfx_dxlu_room_units()) {
        return; /* room units off (default): rooms stay per-tri */
    }
    if (mode == 4 && !gfx_dxlu_pin_enabled()) {
        return; /* GE_XLU_NOPIN=1: decals stay stray per-tri (pre-pin form) */
    }
    if (mode != 0) {
        if (dxlu_group_depth++ == 0) {
            dxlu_group_open = true;
            dxlu_group_start = dxlu_num_tris;
            dxlu_group_mode = mode;
        }
        return;
    }
    if (dxlu_group_depth == 0 || --dxlu_group_depth != 0) {
        return;
    }
    if (dxlu_group_open && dxlu_num_tris > dxlu_group_start) {
        float gkey = 0.0f;
        if (dxlu_group_mode == 4) {
            gkey = DXLU_KEY_PINNED; /* glued decals: drawn first (see DECAL PIN) */
        } else if (dxlu_group_mode == 1 || dxlu_group_mode == 3) {
            /* Props (mode 1) use the mean member key; rooms (mode 3) use the
             * farthest vertex, since a clipping-dependent mean makes stacked
             * water layers swap order. GE_XLU_ROOMMEAN=1 selects the mean. */
            static int roommean = -1;
            if (roommean < 0) { const char* e = getenv("GE_XLU_ROOMMEAN"); roommean = (e && e[0] != '0') ? 1 : 0; }
            if (dxlu_group_mode == 3 && !roommean) {
                for (uint32_t i = dxlu_group_start; i < dxlu_num_tris; i++) {
                    if (dxlu_tris[i].key > gkey) gkey = dxlu_tris[i].key;
                }
            } else {
                double sum = 0.0;
                for (uint32_t i = dxlu_group_start; i < dxlu_num_tris; i++) {
                    sum += dxlu_tris[i].key;
                }
                gkey = (float)(sum / (double)(dxlu_num_tris - dxlu_group_start));
            }
        }
        dxlu_group_count++;
        for (uint32_t i = dxlu_group_start; i < dxlu_num_tris; i++) {
            dxlu_tris[i].gkey = gkey;
            dxlu_tris[i].grp = dxlu_group_count;
            dxlu_tris[i].gmode = (uint8_t)dxlu_group_mode;
            if (dxlu_group_mode != 3) {
                dxlu_tris[i].key = 0.0f; /* equal -> stable_sort keeps submission order */
            }
        }
        {
            /* GE_DXLU_GROUPLOG=1 (diag): one line per closed unit per frame -
             * mode, member count, group key, min/max member key - to watch
             * two units' keys cross as the camera sways (Egypt pool). */
            static int glog = -1;
            if (glog < 0) { const char* e = getenv("GE_DXLU_GROUPLOG"); glog = (e && e[0] != '0') ? 1 : 0; }
            if (glog) {
                extern uint32_t num_dls;
                float mn = 1e30f, mx = -1e30f;
                for (uint32_t i = dxlu_group_start; i < dxlu_num_tris; i++) {
                    if (dxlu_tris[i].key < mn) mn = dxlu_tris[i].key;
                    if (dxlu_tris[i].key > mx) mx = dxlu_tris[i].key;
                }
                fprintf(stderr, "dxlugrp task=%u mode=%u n=%u gkey=%.2f min=%.1f max=%.1f\n", (unsigned)num_dls,
                        (unsigned)dxlu_group_mode, (unsigned)(dxlu_num_tris - dxlu_group_start), gkey, mn, mx);
            }
        }
    }
    dxlu_group_open = false;
}

/* Game-side hook: a player's 3D world render is starting (bgLevelRender).
 * Reopen the capture window for this view. Viewport/scissor heuristics are
 * NOT reliable here - the gun's own projection setup, sniper zoom and the
 * watch all touch them mid-overlay, and reopening there sent weapon parts
 * to the end-of-frame sorted pass (weapons "clipping out"). */
extern "C" void gfx_dxlu_new_view(void) {
    gfx_dxlu_draw(); /* flush any un-flushed set from the previous view */
    dxlu_closed = false;
}

/* Sort key of one captured vertex = its Euclidean distance from the eye, not
 * its view depth (clip w). View depth depends on view direction, so stacked or
 * coplanar layers (such as two water layers drawn by two rooms) would reorder
 * as the head turns. Eye distance is rotation-invariant, so those layers keep
 * their painter order at any pitch; it is computed from the VBO vertex
 * (aspect-adjusted clip x, clip y, z, w) using the column norms of the camera's
 * P, and falls back to w when the projection is not perspective (ortho/HUD). */
/* The VBO vertex in EYE space (eye at the origin, axes = the view's, z
 * mirrored - a reflection, which keeps every plane/half-space relation the
 * plane pass needs). false = P is not perspective (ortho/HUD). */
static uint32_t rsp_p_gen = 1; /* bumped by every projection matrix load (gfx_sp_matrix) */

static bool gfx_dxlu_vertex_eye(const float* v, float out[3]) {
    /* Twenty-sixth build: the three P column norms (3 sqrtf + 3 divides) and
     * the aspect undo (1 divide) are constants of the view, recomputed only
     * when P or the aspect state moves - they were paid per vertex, 12
     * sqrtf per captured triangle, ~2.5% of the twenty-fifth run. */
    static struct {
        uint32_t pgen;
        float aspect_scale, aspect_ratio;
        float inv_sx, inv_sy, inv_sw, aspect_undo;
        bool ok;
    } k = { 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false };
    if (k.pgen != rsp_p_gen || k.aspect_scale != rsp.aspect_scale ||
        k.aspect_ratio != gfx_current_dimensions.aspect_ratio) {
        const float (*P)[4] = rsp.P_matrix;
        const float sx2 = P[0][0] * P[0][0] + P[1][0] * P[1][0] + P[2][0] * P[2][0];
        const float sy2 = P[0][1] * P[0][1] + P[1][1] * P[1][1] + P[2][1] * P[2][1];
        const float sw2 = P[0][3] * P[0][3] + P[1][3] * P[1][3] + P[2][3] * P[2][3];
        k.pgen = rsp_p_gen;
        k.aspect_scale = rsp.aspect_scale;
        k.aspect_ratio = gfx_current_dimensions.aspect_ratio;
        k.ok = !(sx2 < 1e-12f || sy2 < 1e-12f || sw2 < 1e-12f);
        if (k.ok) {
            k.inv_sx = 1.0f / sqrtf(sx2);
            k.inv_sy = 1.0f / sqrtf(sy2);
            k.inv_sw = 1.0f / sqrtf(sw2);
            k.aspect_undo = rsp.aspect_scale != 0.0f ? gfx_current_dimensions.aspect_ratio / rsp.aspect_scale : 0.0f;
        }
    }
    if (!k.ok) {
        return false;
    }
    const float w = v[3];
    float xc = v[0];
    if (!fbActive) {
        /* undo gfx_adjust_x_for_aspect_ratio */
        xc = xc * k.aspect_undo - rsp.aspect_ofs * w;
    }
    out[0] = xc * k.inv_sx;
    out[1] = v[1] * k.inv_sy;
    out[2] = w * k.inv_sw;
    return true;
}

static float gfx_dxlu_vertex_dist(const float* v, float eye[3], bool* eye_ok) {
    static int depthkey = -1;
    if (depthkey < 0) {
        const char* e = getenv("GE_XLU_DEPTHKEY");
        depthkey = (e != NULL && e[0] != '0') ? 1 : 0;
    }
    const float w = v[3];
    const bool ok = gfx_dxlu_vertex_eye(v, eye);
    if (!ok) {
        eye[0] = eye[1] = 0.0f;
        eye[2] = w;
        *eye_ok = false;
        return w;
    }
    if (depthkey) {
        return w;
    }
    return sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
}

static void gfx_dxlu_apply_state(const struct DXluState* s) {
    gfx_flush();
    ge_state_serial++;

    if (s->prg != rendering_state.shader_program) {
        gfx_rapi->unload_shader(rendering_state.shader_program);
        gfx_rapi->load_shader(s->prg);
        rendering_state.shader_program = s->prg;
        ge_state_serial++;
    }
    if (s->use_alpha != rendering_state.alpha_blend || s->use_modulate != rendering_state.modulate) {
        gfx_rapi->set_use_alpha(s->use_alpha, s->use_modulate);
        rendering_state.alpha_blend = s->use_alpha;
        rendering_state.modulate = s->use_modulate;
    }
    if (s->depth_mode_enc != rendering_state.depth_mode) {
        gfx_rapi->set_depth_mode(s->depth_test, s->depth_update, s->depth_compare, s->depth_source_prim, s->zmode);
        rendering_state.depth_mode = s->depth_mode_enc;
    }
    if (memcmp(&s->viewport, &rendering_state.viewport, sizeof(s->viewport)) != 0) {
        gfx_rapi->set_viewport(s->viewport.x, s->viewport.y, s->viewport.width, s->viewport.height);
        rendering_state.viewport = s->viewport;
        ge_state_serial++;
    }
    if (memcmp(&s->scissor, &rendering_state.scissor, sizeof(s->scissor)) != 0) {
        gfx_rapi->set_scissor(s->scissor.x, s->scissor.y, s->scissor.width, s->scissor.height);
        rendering_state.scissor = s->scissor;
        ge_state_serial++;
    }
    for (int i = 0; i < 2; i++) {
        if (s->tex_used[i]) {
            gfx_rapi->select_texture(i, s->tex_id[i], s->tex_linear[i]);
            ge_mip_active = s->tex_mip[i] ? 1 : 0;
            gfx_rapi->set_sampler_parameters(i, s->tex_linear[i], s->tex_cms[i], s->tex_cmt[i]);
            ge_mip_active = 0;
            /* the cached binding in rendering_state.textures no longer
             * matches what GL has - force re-selection on the next
             * immediate-path draw */
            rendering_state.textures[i] = NULL;
            ge_state_serial++;
            rdp.textures_changed[i] = true;
        }
    }
}

/* ---- plane pass (Newell) ------------------------------------------------
 * Tolerance in world units: vertices within it of a plane count as ON it
 * (coplanar surfaces, decals on their wall, float noise in the P-derived
 * eye coordinates at 1000+ units). */
#define DXLU_PLANE_EPS 1.0f

/* depth range, screen bbox and - if every member vertex lies on one plane -
 * that plane, normalised so the eye (origin) is on the positive side. */
static void gfx_dxlu_unit_bounds(struct DXluUnit* u) {
    u->mind = 1e30f;
    u->maxd = -1e30f;
    u->bb[0] = u->bb[2] = 1e30f;
    u->bb[1] = u->bb[3] = -1e30f;
    u->plane_ok = false;
    bool eye_ok = true;
    for (uint32_t k = 0; k < u->count; k++) {
        const struct DXluTri* t = &dxlu_tris[u->first + k];
        if (t->mind < u->mind) u->mind = t->mind;
        if (t->maxd > u->maxd) u->maxd = t->maxd;
        if (t->bb[0] < u->bb[0]) u->bb[0] = t->bb[0];
        if (t->bb[1] > u->bb[1]) u->bb[1] = t->bb[1];
        if (t->bb[2] < u->bb[2]) u->bb[2] = t->bb[2];
        if (t->bb[3] > u->bb[3]) u->bb[3] = t->bb[3];
        if (!t->eye_ok) eye_ok = false;
    }
    if (!eye_ok) {
        return;
    }
    /* plane of the first non-degenerate member tri */
    float n[3] = {0, 0, 0}, d = 0.0f;
    bool have = false;
    for (uint32_t k = 0; k < u->count && !have; k++) {
        const float* e = dxlu_tris[u->first + k].e;
        const float ax = e[3] - e[0], ay = e[4] - e[1], az = e[5] - e[2];
        const float bx = e[6] - e[0], by = e[7] - e[1], bz = e[8] - e[2];
        n[0] = ay * bz - az * by;
        n[1] = az * bx - ax * bz;
        n[2] = ax * by - ay * bx;
        const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-6f) {
            n[0] /= len;
            n[1] /= len;
            n[2] /= len;
            d = -(n[0] * e[0] + n[1] * e[1] + n[2] * e[2]);
            have = true;
        }
    }
    if (!have || fabsf(d) < DXLU_PLANE_EPS) {
        return; /* degenerate, or the plane passes through the eye */
    }
    if (d < 0.0f) {
        n[0] = -n[0];
        n[1] = -n[1];
        n[2] = -n[2];
        d = -d;
    }
    /* every member vertex must be on that plane */
    for (uint32_t k = 0; k < u->count; k++) {
        const float* e = dxlu_tris[u->first + k].e;
        for (int v = 0; v < 3; v++) {
            const float s = n[0] * e[v * 3] + n[1] * e[v * 3 + 1] + n[2] * e[v * 3 + 2] + d;
            if (fabsf(s) > DXLU_PLANE_EPS) {
                return;
            }
        }
    }
    u->n[0] = n[0];
    u->n[1] = n[1];
    u->n[2] = n[2];
    u->d = d;
    u->plane_ok = true;
}

/* every vertex of u on the eye side (side > 0) / far side (side < 0) of
 * plane (n, d), with tolerance */
static bool gfx_dxlu_unit_on_side(const struct DXluUnit* u, const float n[3], float d, int side) {
    for (uint32_t k = 0; k < u->count; k++) {
        const float* e = dxlu_tris[u->first + k].e;
        for (int v = 0; v < 3; v++) {
            const float s = n[0] * e[v * 3] + n[1] * e[v * 3 + 1] + n[2] * e[v * 3 + 2] + d;
            if (side > 0 ? (s < -DXLU_PLANE_EPS) : (s > DXLU_PLANE_EPS)) {
                return false;
            }
        }
    }
    return true;
}

/* AUTHORED pair: a room-XLU stray triangle and a prop group captured in the
 * same room block (bg.c room loop: room secondary list, then that room's prop
 * alpha passes). Vanilla layering there is pure submission order, because props
 * draw after their room's XLU even when they are farther away, so no depth key
 * can reproduce it and the scope tag restores the authored order.
 * Stray-vs-stray stays per triangle, group-vs-group stays depth, and cross-room
 * stays depth plus planes. */
static bool gfx_dxlu_authored_pair(const struct DXluUnit* a, const struct DXluUnit* b) {
    static int noscope = -1;
    if (noscope < 0) {
        const char* e = getenv("GE_XLU_NOSCOPE");
        noscope = (e != NULL && e[0] != '0') ? 1 : 0;
    }
    if (noscope) {
        return false; /* A/B: fifth-face room-block rule off */
    }
    return a->scope != 0 && a->scope == b->scope &&
           ((a->mode == 1) != (b->mode == 1)) && a->mode != 2 && b->mode != 2;
}

/* may a be painted before b (a behind b)? true when provably harmless:
 * screen bboxes disjoint, a entirely on the far side of b's plane, or b
 * entirely on the eye side of a's plane. false = unknown OR wrong. For an
 * authored same-room-block pair the game's submission order IS the truth. */
static bool gfx_dxlu_can_precede(const struct DXluUnit* a, const struct DXluUnit* b) {
    if (a->bb[1] < b->bb[0] || b->bb[1] < a->bb[0] || a->bb[3] < b->bb[2] || b->bb[3] < a->bb[2]) {
        return true;
    }
    /* Authored vs geometry: the same-room-block submission-order rule and a
     * clean plane separation can contradict each other, and the prop's shape
     * decides which is right. A planar prop (a glass pane) is governed by
     * geometry, while a non-planar prop (shrub, chairs, explosions) has no
     * usable plane relation, so authored order applies only when the pair's
     * prop unit has no plane. */
    if (gfx_dxlu_authored_pair(a, b)) {
        const struct DXluUnit* propunit = (a->mode == 1) ? a : b;
        if (!propunit->plane_ok) {
            return a->first < b->first;
        }
    }
    if (b->plane_ok && gfx_dxlu_unit_on_side(a, b->n, b->d, -1)) {
        return true;
    }
    if (a->plane_ok && gfx_dxlu_unit_on_side(b, a->n, a->d, +1)) {
        return true;
    }
    if (b->plane_ok && gfx_dxlu_unit_on_side(a, b->n, b->d, +1)) {
        return false; /* a entirely eye-side of b's plane: a first is provably wrong */
    }
    if (a->plane_ok && gfx_dxlu_unit_on_side(b, a->n, a->d, -1)) {
        return false; /* b entirely behind a's plane: a first is provably wrong */
    }
    return false;
}

/* Newell's painter reorder over dxlu_unit_order (far to near by key). For
 * each unit A in turn, every later unit B whose depth range overlaps A's is
 * checked; if A may not precede B but B may precede A, B is moved in front
 * of A (once per unit - the moved flag breaks cycles) and A's scan restarts
 * with B. Pairs the key order already gets right (disjoint ranges - every
 * unit key lies inside its range, debris pinned at 0 excepted) are never
 * touched; pairs nobody can prove stay in key order. */
static void gfx_dxlu_plane_pass(uint32_t nunits) {
    uint32_t tests = 0, moves = 0, visits = 0;
    bool cut = false;
    if (nunits < 2) {
        return;
    }
    const auto t_start = std::chrono::steady_clock::now();
    memset(dxlu_unit_moved, 0, nunits);
    /* Shortcut for the O(n^2) pair loop: once no later unit's maxd exceeds a's
     * mind, every remaining pair is range-disjoint and would be skipped anyway,
     * except authored pairs which bypass the range test (those keep the full
     * scan). dxlu_sufmax[j] = max maxd over order[j..], rebuilt after a move. */
    static float dxlu_sufmax[DXLU_MAX_TRIS + 1];
    static uint8_t dxlu_has_partner[DXLU_MAX_TRIS];
    {
        /* authored partner = same nonzero scope, opposite prop-ness, no debris */
        uint32_t scope_prop[64], scope_other[64]; /* small open table: scope -> counts */
        uint32_t scope_id[64];
        uint32_t nscopes = 0;
        for (uint32_t u = 0; u < nunits; u++) {
            const struct DXluUnit* x = &dxlu_units[u];
            dxlu_has_partner[u] = 0;
            if (x->scope == 0 || x->mode == 2) continue;
            uint32_t s;
            for (s = 0; s < nscopes; s++) if (scope_id[s] == x->scope) break;
            if (s == nscopes) {
                if (nscopes == 64) { s = 0; scope_id[0] = 0; scope_prop[0] = scope_other[0] = 1; break; }
                scope_id[nscopes] = x->scope; scope_prop[nscopes] = 0; scope_other[nscopes] = 0; nscopes++;
            }
            if (x->mode == 1) scope_prop[s]++; else scope_other[s]++;
        }
        const bool overflow = nscopes > 0 && scope_id[0] == 0;
        for (uint32_t u = 0; u < nunits; u++) {
            const struct DXluUnit* x = &dxlu_units[u];
            if (x->scope == 0 || x->mode == 2) continue;
            if (overflow) { dxlu_has_partner[u] = 1; continue; }
            for (uint32_t s = 0; s < nscopes; s++) {
                if (scope_id[s] == x->scope) {
                    dxlu_has_partner[u] = (x->mode == 1) ? (scope_other[s] > 0) : (scope_prop[s] > 0);
                    break;
                }
            }
        }
    }
    auto build_sufmax = [&](uint32_t from) {
        dxlu_sufmax[nunits] = -1e30f;
        for (uint32_t j = nunits; j-- > from;) {
            const float m = dxlu_units[dxlu_unit_order[j]].maxd;
            dxlu_sufmax[j] = m > dxlu_sufmax[j + 1] ? m : dxlu_sufmax[j + 1];
        }
    };
    build_sufmax(0);
    for (uint32_t i = 0; i + 1 < nunits; i++) {
        const struct DXluUnit* a = &dxlu_units[dxlu_unit_order[i]];
        if (a->mode == 2 || a->key >= DXLU_KEY_PINNED) {
            continue; /* debris pinned last, glued decals pinned first */
        }
        bool full_scan = dxlu_has_partner[dxlu_unit_order[i]] != 0;
        if (cut) {
            break;
        }
        for (uint32_t j = i + 1; j < nunits; j++) {
            if (!full_scan && dxlu_sufmax[j] <= a->mind) {
                break; /* every remaining pair is range-disjoint */
            }
            /* The pair visits themselves were costly; past the budget the rest
             * keeps the key order (far to near), as the N64's draw order
             * effectively did. */
            if (gfx_xbox_xlu_visits_max != 0 && ++visits > gfx_xbox_xlu_visits_max) {
                cut = true;
                break;
            }
            const uint32_t bi = dxlu_unit_order[j];
            const struct DXluUnit* b = &dxlu_units[bi];
            if (b->mode == 2 || b->key >= DXLU_KEY_PINNED) {
                continue;
            }
            /* disjoint depth ranges are settled by the key order - EXCEPT
             * for an authored pair, whose truth is submission order and can
             * contradict depth outright (the Jungle shrub/curtain case IS
             * range-disjoint the wrong way round). */
            if (!gfx_dxlu_authored_pair(a, b) && (b->maxd <= a->mind || a->maxd <= b->mind)) {
                continue;
            }
            tests++;
            if (gfx_dxlu_can_precede(a, b)) {
                continue;
            }
            if (!dxlu_unit_moved[bi] && gfx_dxlu_can_precede(b, a)) {
                memmove(dxlu_unit_order + i + 1, dxlu_unit_order + i, (j - i) * sizeof(dxlu_unit_order[0]));
                dxlu_unit_order[i] = bi;
                dxlu_unit_moved[bi] = 1;
                moves++;
                a = b;
                full_scan = dxlu_has_partner[bi] != 0;
                build_sufmax(i);
                j = i; /* rescan the later units against the new front */
            }
        }
    }
    dxlu_stat_units += nunits; dxlu_stat_tests += tests; dxlu_stat_moves += moves; dxlu_stat_passes++;
    dxlu_stat_visits += visits; dxlu_stat_cut += cut ? 1u : 0u;
    {
        /* GE_DXLU_GROUPLOG=1: one line per sorted pass - unit count, how
         * many overlapping-range pairs were plane-tested, how many moved */
        static int glog = -1;
        if (glog < 0) { const char* e = getenv("GE_DXLU_GROUPLOG"); glog = (e && e[0] != '0') ? 1 : 0; }
        if (glog) {
            extern uint32_t num_dls;
            const long us = (long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t_start).count();
            fprintf(stderr, "dxluplane task=%u units=%u tests=%u moves=%u us=%ld\n", (unsigned)num_dls, (unsigned)nunits,
                    (unsigned)tests, (unsigned)moves, us);
        }
    }
}

/* GE port (PC stats, GE_ROCSTATS=1): where the frame's render time goes -
 * the sorter's sort / plane pass / batching, its replay, the whole gfx_run */
static unsigned long long dxlu_stat_sort_us, dxlu_stat_replay_us;
static uint32_t dxlu_stat_frame_tris, dxlu_stat_frame_draws;
static void gfx_dxlu_draw(void) {
    if (dxlu_num_tris == 0) {
        return;
    }
    const std::chrono::steady_clock::time_point dxlu_t0 = std::chrono::steady_clock::now();
    dxlu_stat_frame_tris += dxlu_num_tris;

    gfx_flush();

    /* 1. units: a group's members are contiguous in capture order */
    uint32_t nunits = 0;
    for (uint32_t i = 0; i < dxlu_num_tris;) {
        struct DXluUnit* u = &dxlu_units[nunits];
        const uint32_t grp = dxlu_tris[i].grp;
        u->first = i;
        u->key = dxlu_tris[i].gkey;
        u->mode = grp ? dxlu_tris[i].gmode : 0;
        u->scope = dxlu_tris[i].scope;
        do {
            i++;
        } while (grp != 0 && i < dxlu_num_tris && dxlu_tris[i].grp == grp);
        u->count = i - u->first;
        gfx_dxlu_unit_bounds(u);
        dxlu_unit_order[nunits] = nunits;
        nunits++;
    }

    /* 2. key order: far to near by unit key (props: mean member distance,
     * debris: 0 = last, stray tris: the tri's farthest vertex) */
    std::stable_sort(dxlu_unit_order, dxlu_unit_order + nunits,
                     [](uint32_t a, uint32_t b) { return dxlu_units[a].key > dxlu_units[b].key; });

    /* 3. the plane pass: fix the provable mistakes of the key order */
    if (gfx_dxlu_plane_pass_enabled()) {
        gfx_dxlu_plane_pass(nunits);
    }

    /* 4. flatten to tris: submission order inside prop/debris units, per-tri
     * far-to-near inside room units (GE_XLU_ROOMUNITS=1 only) */
    uint32_t ntris = 0;
    for (uint32_t ui = 0; ui < nunits; ui++) {
        const struct DXluUnit* u = &dxlu_units[dxlu_unit_order[ui]];
        const uint32_t base = ntris;
        for (uint32_t k = 0; k < u->count; k++) {
            dxlu_sort_idx[ntris++] = u->first + k;
        }
        if (u->mode == 3 && u->count > 1) {
            std::stable_sort(dxlu_sort_idx + base, dxlu_sort_idx + ntris,
                             [](uint32_t a, uint32_t b) { return dxlu_tris[a].key > dxlu_tris[b].key; });
        }
    }

    /* 4b, state-run merging: a run of same-state triangles moves earlier to
     * join the previous run of its state if it overlaps none of the triangles
     * it jumps over on screen (eye_ok == 0 counts as overlapping everything).
     * Translucent draws do not write depth, so this reordering is invisible
     * and cuts the draw count. */
    if (gfx_xbox_xlu_batch && dxlu_num_tris > 1) {
        static uint32_t run_start[DXLU_MAX_TRIS], run_count[DXLU_MAX_TRIS], run_state[DXLU_MAX_TRIS];
        static int32_t run_ret[DXLU_MAX_TRIS];
        static float run_bb[DXLU_MAX_TRIS][4];
        static uint8_t run_ok[DXLU_MAX_TRIS];
        static uint32_t order[DXLU_MAX_TRIS];     /* run ids in output order */
        static uint32_t out_idx[DXLU_MAX_TRIS];
        uint32_t nruns = 0;
        for (uint32_t i = 0; i < dxlu_num_tris;) {
            const uint32_t st = dxlu_tris[dxlu_sort_idx[i]].state;
            const int32_t rt = dxlu_tris[dxlu_sort_idx[i]].ret;
            run_start[nruns] = i;
            run_state[nruns] = st;
            run_ret[nruns] = rt;
            float* bb = run_bb[nruns];
            bb[0] = bb[2] = 1e30f; bb[1] = bb[3] = -1e30f;
            uint8_t ok = 1;
            uint32_t j = i;
            while (j < dxlu_num_tris && dxlu_tris[dxlu_sort_idx[j]].state == st && dxlu_tris[dxlu_sort_idx[j]].ret == rt) {
                const struct DXluTri* t = &dxlu_tris[dxlu_sort_idx[j]];
                if (!t->eye_ok) ok = 0;
                if (t->bb[0] < bb[0]) bb[0] = t->bb[0];
                if (t->bb[1] > bb[1]) bb[1] = t->bb[1];
                if (t->bb[2] < bb[2]) bb[2] = t->bb[2];
                if (t->bb[3] > bb[3]) bb[3] = t->bb[3];
                j++;
            }
            run_count[nruns] = j - i;
            run_ok[nruns] = ok;
            nruns++;
            i = j;
        }
        uint32_t nout = 0;
        uint32_t merged = 0;
        for (uint32_t r = 0; r < nruns; r++) {
            uint32_t pos = nout; /* insert position in order[] (default: the end) */
            if (run_ok[r]) {
                /* walk back over runs this one shares no pixel with */
                uint32_t k = nout;
                uint32_t looked = 0;
                while (k > 0 && looked < 24) {
                    const uint32_t q = order[k - 1];
                    if (run_state[q] == run_state[r] && run_ret[q] == run_ret[r]) {
                        pos = k; /* right after its own state's run: one draw */
                        merged++;
                        break;
                    }
                    if (!run_ok[q]) break;
                    const float* a = run_bb[r];
                    const float* b = run_bb[q];
                    bool disjoint = a[1] < b[0] || b[1] < a[0] || a[3] < b[2] || b[3] < a[2];
                    if (!disjoint) {
                        /* the union boxes touch: per-triangle, bounded */
                        if (run_count[r] * run_count[q] > 96) break;
                        disjoint = true;
                        for (uint32_t x = 0; x < run_count[r] && disjoint; x++) {
                            const float* ta = dxlu_tris[dxlu_sort_idx[run_start[r] + x]].bb;
                            for (uint32_t y = 0; y < run_count[q]; y++) {
                                const float* tb = dxlu_tris[dxlu_sort_idx[run_start[q] + y]].bb;
                                if (!(ta[1] < tb[0] || tb[1] < ta[0] || ta[3] < tb[2] || tb[3] < ta[2])) { disjoint = false; break; }
                            }
                        }
                        if (!disjoint) break;
                    }
                    k--;
                    looked++;
                }
            }
            if (pos < nout) memmove(order + pos + 1, order + pos, (nout - pos) * sizeof(order[0]));
            order[pos] = r;
            nout++;
        }
        uint32_t n = 0;
        for (uint32_t o = 0; o < nout; o++) {
            const uint32_t r = order[o];
            memcpy(out_idx + n, dxlu_sort_idx + run_start[r], run_count[r] * sizeof(uint32_t));
            n += run_count[r];
        }
        memcpy(dxlu_sort_idx, out_idx, n * sizeof(uint32_t));
        dxlu_stat_merged += merged;
    }

    uint32_t cur_state = 0xFFFFFFFFu;
    const std::chrono::steady_clock::time_point dxlu_t1 = std::chrono::steady_clock::now();
    dxlu_stat_sort_us += (unsigned long long)std::chrono::duration_cast<std::chrono::microseconds>(dxlu_t1 - dxlu_t0).count();
    struct DxluReplayTimer {
        const std::chrono::steady_clock::time_point& t1;
        ~DxluReplayTimer() {
            dxlu_stat_replay_us += (unsigned long long)std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t1).count();
        }
    } dxlu_replay_timer{ dxlu_t1 };
    const uint32_t dxlu_draws_before = g_PortGfxDraws;
    ge_flush_origin = 8; /* the replay's draws (a draw per state change in sorted order) */
    static uint16_t ridx[DXLU_MAX_TRIS * 3];
    uint32_t nridx = 0;
    int32_t cur_ret = -1;
    for (uint32_t i = 0; i < dxlu_num_tris; i++) {
        const struct DXluTri* t = &dxlu_tris[dxlu_sort_idx[i]];
        if (dxlu_trace_mark[dxlu_sort_idx[i] >> 5] & (1u << (dxlu_sort_idx[i] & 31))) {
            fprintf(stderr, "dxlutrace: draw slot=%u/%u tri=%u gkey=%.1f key=%.1f\n", i, dxlu_num_tris, dxlu_sort_idx[i], t->gkey, t->key);
        }
        if (t->state != cur_state || t->ret != cur_ret) {
            if (nridx != 0) { /* the pending indexed run draws before the state moves */
                roc_xlu_replay_unit(cur_ret, ridx, nridx);
                nridx = 0;
            }
            if (t->state != cur_state) {
                gfx_dxlu_apply_state(&dxlu_states[t->state]);
                cur_state = t->state;
            }
            cur_ret = t->ret;
        }
        if (t->ret >= 0) {
            ridx[nridx++] = (uint16_t)t->ret_first;
            ridx[nridx++] = (uint16_t)(t->ret_first + 1u);
            ridx[nridx++] = (uint16_t)(t->ret_first + 2u);
            continue;
        }
        if (buf_vbo_len + t->nfloats > MAX_BUFFERED * (32 * 3) || buf_vbo_num_tris + 1 >= MAX_BUFFERED) {
            gfx_flush();
        }
        memcpy(buf_vbo + buf_vbo_len, dxlu_vbo + t->offset, t->nfloats * sizeof(float));
        buf_vbo_len += t->nfloats;
        buf_vbo_num_tris++;
    }
    if (nridx != 0) {
        roc_xlu_replay_unit(cur_ret, ridx, nridx);
    }
    gfx_flush();

    /* The replay applied captured viewport/scissor rects directly, and those
     * two are flag-gated in the normal path, so force a re-compare or the next
     * draw (the gun) inherits the last sorted room's scissor and is clipped to
     * it. */
    rdp.viewport_or_scissor_changed = true;

    gfx_dxlu_reset();
    dxlu_stat_frame_draws += g_PortGfxDraws - dxlu_draws_before;
}

/* Shader prewarm (GE_SHADERPREWARM=0 turns it off). A colour combiner's GL
 * program is compiled the first time a draw needs it, and that frame stalls
 * (one compile measured 42-66 ms on an RTX 5070 Ti). Every program compiled
 * is appended to Data/shaders.txt as "<id0> <id1>" in hex; from the next run
 * on, gfx_shader_prewarm_step compiles the listed ones a few ms per frame
 * from boot, so the front end pays for them instead of gameplay. A filter
 * change clears the pool and restarts the walk. */
extern "C" const char* portRootPath(const char* name); /* main.c */
extern "C" unsigned long long portProfNow(void);        /* pcsched.c */
static std::vector<std::pair<uint64_t, uint32_t>> ge_prewarm_ids;
static std::set<std::pair<uint64_t, uint32_t>> ge_prewarm_known;
static size_t ge_prewarm_next;
static int ge_prewarm_on = -1;

static void gfx_shader_prewarm_load(void) {
    const char* e = getenv("GE_SHADERPREWARM");
    ge_prewarm_on = (e == NULL || atoi(e) != 0);
    if (!ge_prewarm_on) {
        return;
    }
    /* shaders_base.txt ships with the release (every stage, MP and the
     * attract demos, recorded by scripts/port/record_shaders.sh) and is never
     * written; shaders.txt is this install's own list */
    static const char* const files[] = { "Data/shaders_base.txt", "Data/shaders.txt" };
    unsigned counts[2] = { 0, 0 };
    for (int k = 0; k < 2; k++) {
        FILE* f = fopen(portRootPath(files[k]), "r");
        if (f == NULL) {
            continue;
        }
        unsigned long long id0;
        unsigned id1;
        while (fscanf(f, "%llx %x", &id0, &id1) == 2) {
            const std::pair<uint64_t, uint32_t> id((uint64_t)id0, (uint32_t)id1);
            if (ge_prewarm_known.insert(id).second) {
                ge_prewarm_ids.push_back(id);
                counts[k]++;
            }
        }
        fclose(f);
    }
    if (!ge_prewarm_ids.empty()) {
        printf("ge: shaders: %u from shaders_base.txt + %u from shaders.txt, compiling them ahead of use\n",
               counts[0], counts[1]);
    }
}

static void gfx_shader_prewarm_record(uint64_t shader_id0, uint32_t shader_id1) {
    if (ge_prewarm_on != 1 || !ge_prewarm_known.insert(std::make_pair(shader_id0, shader_id1)).second) {
        return;
    }
    FILE* f = fopen(portRootPath("Data/shaders.txt"), "a");
    if (f != NULL) {
        fprintf(f, "%016llx %08x\n", (unsigned long long)shader_id0, (unsigned)shader_id1);
        fclose(f);
    }
}

static struct ShaderProgram* gfx_lookup_or_create_shader_program(uint64_t shader_id0, uint32_t shader_id1) {
    struct ShaderProgram* prg = gfx_rapi->lookup_shader(shader_id0, shader_id1);
    if (prg == NULL) {
        gfx_rapi->unload_shader(rendering_state.shader_program);
        prg = gfx_rapi->create_and_load_new_shader(shader_id0, shader_id1);
        rendering_state.shader_program = prg;
        ge_state_serial++;
        gfx_shader_prewarm_record(shader_id0, shader_id1);
    }
    return prg;
}

/* render thread, top of gfx_run: compile listed programs for up to 3 ms */
static void gfx_shader_prewarm_step(void) {
    if (ge_prewarm_on < 0) {
        gfx_shader_prewarm_load();
    }
    if (ge_prewarm_on != 1 || ge_prewarm_next >= ge_prewarm_ids.size()) {
        return;
    }
    const unsigned long long t0 = portProfNow();
    while (ge_prewarm_next < ge_prewarm_ids.size() && portProfNow() - t0 < 3000) {
        const std::pair<uint64_t, uint32_t>& id = ge_prewarm_ids[ge_prewarm_next++];
        gfx_lookup_or_create_shader_program(id.first, id.second);
    }
    if (ge_prewarm_next >= ge_prewarm_ids.size()) {
        printf("ge: shaders: prewarm done (%u programs)\n", (unsigned)ge_prewarm_ids.size());
    }
}

static const char* ccmux_to_string(uint32_t ccmux) {
    static const char* const tbl[] = {
        "G_CCMUX_COMBINED",
        "G_CCMUX_TEXEL0",
        "G_CCMUX_TEXEL1",
        "G_CCMUX_PRIMITIVE",
        "G_CCMUX_SHADE",
        "G_CCMUX_ENVIRONMENT",
        "G_CCMUX_1",
        "G_CCMUX_COMBINED_ALPHA",
        "G_CCMUX_TEXEL0_ALPHA",
        "G_CCMUX_TEXEL1_ALPHA",
        "G_CCMUX_PRIMITIVE_ALPHA",
        "G_CCMUX_SHADE_ALPHA",
        "G_CCMUX_ENV_ALPHA",
        "G_CCMUX_LOD_FRACTION",
        "G_CCMUX_PRIM_LOD_FRAC",
        "G_CCMUX_K5",
    };
    if (ccmux > 15) {
        return "G_CCMUX_0";

    } else {
        return tbl[ccmux];
    }
}

static const char* acmux_to_string(uint32_t acmux) {
    static const char* const tbl[] = {
        "G_ACMUX_COMBINED or G_ACMUX_LOD_FRACTION",
        "G_ACMUX_TEXEL0",
        "G_ACMUX_TEXEL1",
        "G_ACMUX_PRIMITIVE",
        "G_ACMUX_SHADE",
        "G_ACMUX_ENVIRONMENT",
        "G_ACMUX_1 or G_ACMUX_PRIM_LOD_FRAC",
        "G_ACMUX_0",
    };
    return tbl[acmux];
}

static void gfx_generate_cc(struct ColorCombiner* comb, const ColorCombinerKey& key) {
    bool is_2cyc = (key.options & (uint64_t)SHADER_OPT_2CYC) != 0;

    uint8_t c[2][2][4] = { { { 0 } } };
    uint64_t shader_id0 = 0;
    uint32_t shader_id1 = key.options;
    uint8_t shader_input_mapping[2][7] = { { 0 } };
    bool used_textures[2] = { false, false };
    for (int i = 0; i < 2 && (i == 0 || is_2cyc); i++) {
        uint32_t rgb_a = (key.combine_mode >> (i * 28)) & 0xf;
        uint32_t rgb_b = (key.combine_mode >> (i * 28 + 4)) & 0xf;
        uint32_t rgb_c = (key.combine_mode >> (i * 28 + 8)) & 0x1f;
        uint32_t rgb_d = (key.combine_mode >> (i * 28 + 13)) & 7;
        uint32_t alpha_a = (key.combine_mode >> (i * 28 + 16)) & 7;
        uint32_t alpha_b = (key.combine_mode >> (i * 28 + 16 + 3)) & 7;
        uint32_t alpha_c = (key.combine_mode >> (i * 28 + 16 + 6)) & 7;
        uint32_t alpha_d = (key.combine_mode >> (i * 28 + 16 + 9)) & 7;

        if (rgb_a >= 8) {
            rgb_a = G_CCMUX_0;
        }
        if (rgb_b >= 8) {
            rgb_b = G_CCMUX_0;
        }
        if (rgb_c >= 16) {
            rgb_c = G_CCMUX_0;
        }
        if (rgb_d == 7) {
            rgb_d = G_CCMUX_0;
        }

        if (rgb_a == rgb_b || rgb_c == G_CCMUX_0) {
            // Normalize
            rgb_a = G_CCMUX_0;
            rgb_b = G_CCMUX_0;
            rgb_c = G_CCMUX_0;
        }
        if (alpha_a == alpha_b || alpha_c == G_ACMUX_0) {
            // Normalize
            alpha_a = G_ACMUX_0;
            alpha_b = G_ACMUX_0;
            alpha_c = G_ACMUX_0;
        }
        if (i == 1) {
            if (rgb_a != G_CCMUX_COMBINED && rgb_b != G_CCMUX_COMBINED && rgb_c != G_CCMUX_COMBINED &&
                rgb_d != G_CCMUX_COMBINED) {
                // First cycle RGB not used, so clear it away
                c[0][0][0] = c[0][0][1] = c[0][0][2] = c[0][0][3] = G_CCMUX_0;
            }
            if (rgb_c != G_CCMUX_COMBINED_ALPHA && alpha_a != G_ACMUX_COMBINED && alpha_b != G_ACMUX_COMBINED &&
                alpha_d != G_ACMUX_COMBINED) {
                // First cycle ALPHA not used, so clear it away
                c[0][1][0] = c[0][1][1] = c[0][1][2] = c[0][1][3] = G_ACMUX_0;
            }
        }

        c[i][0][0] = rgb_a;
        c[i][0][1] = rgb_b;
        c[i][0][2] = rgb_c;
        c[i][0][3] = rgb_d;
        c[i][1][0] = alpha_a;
        c[i][1][1] = alpha_b;
        c[i][1][2] = alpha_c;
        c[i][1][3] = alpha_d;
    }
    if (!is_2cyc) {
        for (int i = 0; i < 2; i++) {
            for (int k = 0; k < 4; k++) {
                c[1][i][k] = i == 0 ? G_CCMUX_0 : G_ACMUX_0;
            }
        }
    }
    {
        uint8_t input_number[32] = { 0 };
        int next_input_number = SHADER_INPUT_1;
        for (int i = 0; i < 2 && (i == 0 || is_2cyc); i++) {
            for (int j = 0; j < 4; j++) {
                uint32_t val = 0;
                switch (c[i][0][j]) {
                    case G_CCMUX_0:
                        val = SHADER_0;
                        break;
                    case G_CCMUX_1:
                        val = SHADER_1;
                        break;
                    case G_CCMUX_TEXEL0:
                        val = SHADER_TEXEL0;
                        used_textures[0] = true;
                        break;
                    case G_CCMUX_TEXEL1:
                        val = SHADER_TEXEL1;
                        used_textures[1] = true;
                        break;
                    case G_CCMUX_TEXEL0_ALPHA:
                        val = SHADER_TEXEL0A;
                        used_textures[0] = true;
                        break;
                    case G_CCMUX_TEXEL1_ALPHA:
                        val = SHADER_TEXEL1A;
                        used_textures[1] = true;
                        break;
                    case G_CCMUX_NOISE:
                        val = SHADER_NOISE;
                        break;
                    case G_CCMUX_PRIMITIVE:
                    case G_CCMUX_PRIMITIVE_ALPHA:
                    case G_CCMUX_PRIM_LOD_FRAC:
                    case G_CCMUX_SHADE:
                    case G_CCMUX_SHADE_ALPHA:
                    case G_CCMUX_ENVIRONMENT:
                    case G_CCMUX_ENV_ALPHA:
                    case G_CCMUX_LOD_FRACTION:
                        if (input_number[c[i][0][j]] == 0) {
                            shader_input_mapping[0][next_input_number - 1] = c[i][0][j];
                            input_number[c[i][0][j]] = next_input_number++;
                        }
                        val = input_number[c[i][0][j]];
                        break;
                    case G_CCMUX_COMBINED:
                        val = SHADER_COMBINED;
                        break;
                    default:
                        sysLogPrintf(LOG_WARNING, "Unsupported ccmux: %d", c[i][0][j]);
                        break;
                }
                shader_id0 |= (uint64_t)val << (i * 32 + j * 4);
            }
        }
    }
    {
        uint8_t input_number[16] = { 0 };
        int next_input_number = SHADER_INPUT_1;
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 4; j++) {
                uint32_t val = 0;
                switch (c[i][1][j]) {
                    case G_ACMUX_0:
                        val = SHADER_0;
                        break;
                    case G_ACMUX_TEXEL0:
                        val = SHADER_TEXEL0;
                        used_textures[0] = true;
                        break;
                    case G_ACMUX_TEXEL1:
                        val = SHADER_TEXEL1;
                        used_textures[1] = true;
                        break;
                    case G_ACMUX_LOD_FRACTION:
                        // case G_ACMUX_COMBINED: same numerical value
                        if (j != 2) {
                            val = SHADER_COMBINED;
                            break;
                        }
                        c[i][1][j] = G_CCMUX_LOD_FRACTION;
                        [[fallthrough]]; // for G_ACMUX_LOD_FRACTION
                    case G_ACMUX_1:
                        // case G_ACMUX_PRIM_LOD_FRAC: same numerical value
                        if (j != 2) {
                            val = SHADER_1;
                            break;
                        }
                        [[fallthrough]]; // for G_ACMUX_PRIM_LOD_FRAC
                    case G_ACMUX_PRIMITIVE:
                    case G_ACMUX_SHADE:
                    case G_ACMUX_ENVIRONMENT:
                        if (input_number[c[i][1][j]] == 0) {
                            shader_input_mapping[1][next_input_number - 1] = c[i][1][j];
                            input_number[c[i][1][j]] = next_input_number++;
                        }
                        val = input_number[c[i][1][j]];
                        break;
                }
                shader_id0 |= (uint64_t)val << (i * 32 + 16 + j * 4);
            }
        }
    }
    comb->shader_id0 = shader_id0;
    comb->shader_id1 = shader_id1;
    comb->used_textures[0] = used_textures[0];
    comb->used_textures[1] = used_textures[1];
    // comb->prg = gfx_lookup_or_create_shader_program(shader_id0, shader_id1);
    memcpy(comb->shader_input_mapping, shader_input_mapping, sizeof(shader_input_mapping));
}

static struct ColorCombiner* gfx_lookup_or_create_color_combiner(const ColorCombinerKey& key) {
    if (prev_combiner != color_combiner_pool.end() && prev_combiner->first == key) {
        return &prev_combiner->second;
    }

    prev_combiner = color_combiner_pool.find(key);
    if (prev_combiner != color_combiner_pool.end()) {
        return &prev_combiner->second;
    }
    gfx_flush();
    prev_combiner = color_combiner_pool.insert(std::make_pair(key, ColorCombiner())).first;
    gfx_generate_cc(&prev_combiner->second, key);
    return &prev_combiner->second;
}

/* ---- GE port: render-submit pipelining hooks (the render thread itself lives
 * in video.c). When async mode is on:
 *  - the SDL event pump moves out of gfx_start_frame to the main thread
 *  (gfx_poll_events, called from portPumpEvents),
 *    because SDL needs window events on the window-owning thread;
 *  - texture-cache invalidations from the sim thread (texReset,
 *  blood_animation.c) are deferred and applied by the render thread
 *    at its next frame start, so the in-flight frame keeps drawing the cached
 *    texels and does not tear;
 *  - gfx_set_vsync and gfx_set_texture_filter are latched and applied on the
 *  render thread (they need the GL context);
 *  - gfx_set_aa's MSAA / N64 AA pair is latched too, since gfx_start_frame
 *  sizes the framebuffer from it and the
 *    gfx_run tail picks the resolve from it (a write in between resolved the
 *    frame wrongly: a black frame). */
static bool gfx_async_mode = false;
static std::mutex gfx_async_inval_mtx;
static std::vector<const uint8_t*> gfx_async_pending_deletes;
static bool gfx_async_pending_clear = false;
static std::atomic<int> gfx_async_pending_swapint{-1};
static std::atomic<int> gfx_async_pending_filter{-1}; /* FilteringMode latched by gfx_set_texture_filter; -1 = none */
static std::atomic<int> gfx_async_pending_aa{-1};     /* (msaa level << 1) | n64_aa latched by gfx_set_aa; -1 = none */

static void gfx_texture_cache_delete_now(const uint8_t* orig_addr);
static void gfx_set_texture_filter_now(enum FilteringMode mode);

/* ---- GE port: room / part cache hooks from the sim thread ----
 * The game registers and voids cached lists (bg.c, model.c) on the sim thread,
 * but the cache tables (gfx_roomcache.inl) belong to the render thread.
 * Registrations travel inside the display list (G_PORTCACHE_EXT, emitted by
 * roc_hooks.c right before the G_DL they describe), while voids are queued here
 * and applied at the next frame start, so any DL that still references the old
 * data has already been submitted. Synchronous mode applies them at once. */
extern "C" void portPartCacheForgetVertices(const void* vtx); /* gfx_roomcache.inl */
extern "C" void portRoomCacheForgetRoom(int room);
extern "C" void portPartCacheStage(int stage);
extern "C" uint32_t gfx_room_cache;
enum { ROC_OP_FORGET_VTX = 1, ROC_OP_FORGET_ROOM = 2, ROC_OP_STAGE = 3, ROC_OP_MODE = 4 };
struct RocPendingOp { int op; int arg; const void* ptr; };
static std::vector<RocPendingOp> gfx_roc_pending_ops;
static void gfx_roc_apply_op(const RocPendingOp& o) {
    switch (o.op) {
        case ROC_OP_FORGET_VTX: portPartCacheForgetVertices(o.ptr); break;
        case ROC_OP_FORGET_ROOM: portRoomCacheForgetRoom(o.arg); break;
        case ROC_OP_STAGE: portPartCacheStage(o.arg); break;
        case ROC_OP_MODE: portRoomCacheClear(); gfx_room_cache = (uint32_t)o.arg; break;
        default: break;
    }
}
extern "C" void gfx_roc_queue_op(int op, int arg, const void* ptr) {
    RocPendingOp o = { op, arg, ptr };
    if (gfx_async_mode) {
        std::lock_guard<std::mutex> lk(gfx_async_inval_mtx);
        gfx_roc_pending_ops.push_back(o);
    } else {
        gfx_roc_apply_op(o);
    }
}

extern "C" void gfx_set_async_mode(int on) {
    gfx_async_mode = on != 0;
}

/* main thread, once per pump: the event half of gfx_start_frame */
extern "C" void gfx_poll_events(void) {
    if (gfx_async_mode) {
        gfx_wapi->handle_events();
    }
}

/* render thread, at frame start, before gfx_run */
extern "C" void gfx_async_apply_pending(void) {
    int si = gfx_async_pending_swapint.exchange(-1);
    if (si >= 0) {
        gfx_wapi->set_swap_interval(si);
    }
    {
        std::vector<const uint8_t*> dels;
        bool clr;
        {
            std::lock_guard<std::mutex> lk(gfx_async_inval_mtx);
            dels.swap(gfx_async_pending_deletes);
            clr = gfx_async_pending_clear;
            gfx_async_pending_clear = false;
        }
        if (clr) {
            ge_state_serial++; /* gfx_texture_cache_clear_c's bump, made here on the owning thread */
            gfx_texture_cache_clear();
        } else {
            for (const uint8_t* p : dels) {
                gfx_texture_cache_delete_now(p);
            }
        }
    }
    { /* the sim thread's texture filter change (gfx_set_texture_filter) */
        int fm = gfx_async_pending_filter.exchange(-1);
        if (fm >= 0) {
            gfx_set_texture_filter_now((enum FilteringMode)fm);
        }
    }
    { /* the sim thread's MSAA / N64 AA change (gfx_set_aa): before gfx_start_frame,
       * so the framebuffer it sizes and the gfx_run tail's resolve see one value */
        int aa = gfx_async_pending_aa.exchange(-1);
        if (aa >= 0) {
            gfx_msaa_level = gfx_msaa_user = (uint32_t)aa >> 1;
            gfx_n64_aa = gfx_n64_aa_user = (uint32_t)aa & 1u;
        }
    }
    { /* the sim thread's room / part cache voids (gfx_roc_queue_op) */
        std::vector<RocPendingOp> ops;
        {
            std::lock_guard<std::mutex> lk(gfx_async_inval_mtx);
            ops.swap(gfx_roc_pending_ops);
        }
        for (const RocPendingOp& o : ops) {
            gfx_roc_apply_op(o);
        }
    }
}

void gfx_texture_cache_clear() {
    portRoomCacheClear(); /* cached texture ids die with the cache (render thread: the tables are its) */
    gfx_flush();
    for (const auto& entry : gfx_texture_cache.map) {
        gfx_texture_cache.free_texture_ids.push_back(entry.second.texture_id);
    }
    gfx_texture_cache.map.clear();
    ge_texcache_gen++;
    gfx_texture_cache.lru.clear();
    ge_churn_reset = true;
    rdp.textures_changed[0] = rdp.textures_changed[1] = true;
    memset(rendering_state.textures, 0, sizeof(rendering_state.textures));
}

/* C-callable stage-load hook (texReset): the texture cache is keyed by address
 * (+fmt/siz/palette) with no content hash, and no game display list emits
 * G_INVALTEXCACHE_EXT. Pool addresses get recycled with different pixels across
 * stages, so every stage load clears the cache wholesale (a load-time reimport
 * cost only). */
extern "C" void gfx_texture_cache_clear_c(void) {
    if (gfx_async_mode) { /* sim thread: no GL context here - defer to the render thread
                           * (ge_state_serial is render-thread state: bumped at the apply) */
        std::lock_guard<std::mutex> lk(gfx_async_inval_mtx);
        gfx_async_pending_clear = true;
        gfx_async_pending_deletes.clear(); /* subsumed by the clear */
        return;
    }
    ge_state_serial++;
    gfx_texture_cache_clear();
}

/* slow-upload line (import_texture): this miss evicted a node / reused a freed id */
static bool ge_up_evicted, ge_up_recycled;

static bool gfx_texture_cache_lookup(int i, const TextureCacheKey& key) {
    ge_up_evicted = false;
    ge_up_recycled = false;
    TextureCacheMap::iterator it = gfx_texture_cache.map.find(key);
    TextureCacheNode** n = &rendering_state.textures[i];

    if (it != gfx_texture_cache.map.end()) {
        gfx_rapi->select_texture(i, it->second.texture_id, it->second.linear_filter);
        *n = &*it;
        gfx_texture_cache.lru.splice(gfx_texture_cache.lru.end(), gfx_texture_cache.lru,
                                     it->second.lru_location); // move to back
        return true;
    }

    if (gfx_texture_cache.map.size() >= TEXTURE_CACHE_MAX_SIZE) {
        // Evict the least recently used texture, but never a bound one: a
        // re-statement of the bound texture skips the LRU touch, so it can
        // drift to the front, and evicting it would leave
        // rendering_state.textures dangling.
        /* fifty-fifth build: nodes a retained list holds (pins) are evicted
         * last - under the pause screen's texture load the LRU was throwing
         * out scene textures every frame and every list recaptured */
        /* If all 24 candidates are pinned, the victim's own LRU entry must be
         * erased along with its map node (otherwise a stale iterator crashes at
         * the next eviction); a pinned node is evicted when nothing else is
         * left and its retained list recaptures. */
        TextureCacheMap::iterator victim = gfx_texture_cache.map.end();
        for (int guard = 0; guard < 24 && victim == gfx_texture_cache.map.end(); guard++) {
            it = gfx_texture_cache.lru.front().it;
            bool bound = false;
            for (int t = 0; t < 2; t++) {
                if (rendering_state.textures[t] == &*it) bound = true;
            }
            if (!bound && it->second.pins == 0) {
                victim = it;
            } else {
                gfx_texture_cache.lru.splice(gfx_texture_cache.lru.end(), gfx_texture_cache.lru,
                                             it->second.lru_location);
            }
        }
        for (int guard = 0; guard < 4 && victim == gfx_texture_cache.map.end(); guard++) {
            /* everything in reach is pinned: the front goes, pinned or not - only a BOUND node never */
            it = gfx_texture_cache.lru.front().it;
            bool bound = false;
            for (int t = 0; t < 2; t++) {
                if (rendering_state.textures[t] == &*it) bound = true;
            }
            if (!bound) {
                victim = it;
            } else {
                gfx_texture_cache.lru.splice(gfx_texture_cache.lru.end(), gfx_texture_cache.lru,
                                             it->second.lru_location);
            }
        }
        if (victim != gfx_texture_cache.map.end()) {
            gfx_texture_cache.free_texture_ids.push_back(victim->second.texture_id);
            ge_texcache_gen++;
            gfx_texture_cache.lru.erase(victim->second.lru_location);
            gfx_texture_cache.map.erase(victim);
            ge_up_evicted = true;
        }
    }

    uint32_t texture_id;
    if (gfx_texture_cache.free_texture_ids.take(&texture_id)) {
        ge_up_recycled = true;
    } else {
        texture_id = gfx_rapi->new_texture();
    }

    it = gfx_texture_cache.map.insert(std::make_pair(key, TextureCacheValue())).first;
    TextureCacheNode* node = &*it;
    node->second.texture_id = texture_id;
    node->second.serial = ++ge_texnode_serial;
    node->second.lru_location = gfx_texture_cache.lru.insert(gfx_texture_cache.lru.end(), { it });

    gfx_rapi->select_texture(i, texture_id, false);
    gfx_rapi->set_sampler_parameters(i, false, 0, 0);
    *n = node;
    return false;
}

void gfx_texture_cache_delete(const uint8_t* orig_addr) {
    if (gfx_async_mode) { /* sim thread (blood_animation.c): defer to the render thread */
        std::lock_guard<std::mutex> lk(gfx_async_inval_mtx);
        gfx_async_pending_deletes.push_back(orig_addr);
        return;
    }
    gfx_texture_cache_delete_now(orig_addr);
}

static void gfx_texture_cache_delete_now(const uint8_t* orig_addr) {
    gfx_flush();

    for (int i = 0; i < 2; ++i) {
        if (rendering_state.textures[i] && rendering_state.textures[i]->first.texture_addr == orig_addr) {
            rdp.textures_changed[i] = true;
            rendering_state.textures[i] = nullptr;
            ge_state_serial++;
        }
    }

    while (gfx_texture_cache.map.bucket_count() > 0) {
        TextureCacheKey key = { orig_addr, { 0 }, 0, 0, 0, 0, 0, 0 }; // bucket index only depends on the address
        size_t bucket = gfx_texture_cache.map.bucket(key);
        bool again = false;
        for (auto it = gfx_texture_cache.map.begin(bucket); it != gfx_texture_cache.map.end(bucket); ++it) {
            if (it->first.texture_addr == orig_addr) {
                gfx_texture_cache.lru.erase(it->second.lru_location);
                gfx_texture_cache.free_texture_ids.push_back(it->second.texture_id);
                ge_texcache_gen++;
                ge_texcache_delete_gen++;
                gfx_texture_cache.map.erase(it->first);
                again = true;
                break;
            }
        }
        if (!again) {
            break;
        }
    }
}

/* GE: NARROW tiles. Texture loads are 8-byte-line granular, so a texture
 * narrower than one line (CI4 < 16 texels, CI8/IA8/I8 < 8, 16-bit < 4) is
 * decoded with padding columns of whatever follows it in memory. The RDP
 * never samples those: a wrap mask wraps the coordinate at 1<<masks, clamp
 * stops at the tile size. GL_REPEAT over the padded width wrapped at the
 * wrong place, so every UV past the real width sampled garbage/black - the
 * RC-P90/Moonraker world pickups' rail is a 4x16 CI4 rib strip (tex 874,
 * masks=2) and drew black. Crop the upload to the mask
 * size whenever the mask is NARROWER than the decoded image; gfx_sp_tri1
 * normalises UVs with the same rule (tex_width/tex_height) and the mask is
 * part of the cache key. The opposite case (mask WIDER than the line,
 * Rare's water shimmer) keeps its RGBA16 special case and is unaffected. */
static int ge_texnum_of(const void* addr); /* defined with the import trace below */
static int ge_maskcrop_enabled(void) { /* GE_NOMASKCROP=1 = old behaviour, for A/B */
    static int on = -1;
    if (on < 0) on = getenv("GE_NOMASKCROP") == NULL;
    return on;
}
/* Mask-wrapped tiles, any format: a tile whose S wrap mask is wider than its
 * line (e.g. Dam's water redeclares a 32x32 CI8 with a 16-texel line and
 * masks=5) addresses TMEM as t*line + s, so s past the line runs into the
 * following rows, and Rare's water shimmer relies on that. This applies the
 * same addressing as import_texture_rgba16 does to every importer, only when S
 * is wider; gfx_sp_tri1 sizes UVs by the same rule (ge_maskwrap_tile).
 * GE_NOMASKWRAP=1 restores the old linear decode. */
static int ge_maskwrap_enabled(void) {
    static int on = -1;
    if (on < 0) on = getenv("GE_NOMASKWRAP") == NULL;
    return on;
}
static bool ge_maskwrap_tile(int tile, uint32_t width) {
    return ge_maskwrap_enabled() && rdp.texture_tile[tile].masks != 0 && rdp.texture_tile[tile].maskt != 0
           && (1u << rdp.texture_tile[tile].masks) > width;
}
static void ge_upload_masked(uint8_t* rgba, uint32_t width, uint32_t height, int tile) {
    if (ge_maskwrap_tile(tile, width) && width != 0 && height != 0) {
        /* plain malloc, never freed: no static with a destructor in render-thread code */
        static uint8_t* wrapbuf;
        static size_t wrapcap;
        const uint32_t w = 1u << rdp.texture_tile[tile].masks;
        const uint32_t h = 1u << rdp.texture_tile[tile].maskt;
        const uint32_t n = width * height;
        const size_t need = (size_t) w * h * 4;

        if (need > wrapcap) {
            uint8_t* nb = (uint8_t*) realloc(wrapbuf, need);
            if (nb == NULL) {
                gfx_rapi->upload_texture(rgba, width, height);
                return;
            }
            wrapbuf = nb;
            wrapcap = need;
        }
        { /* GE_TRACE_TEX: each texture this rebuilds, once (by address) */
            static int trace = -1;
            static const void* seen[256]; static int nseen;
            if (trace < 0) trace = getenv("GE_TRACE_TEX") != NULL;
            if (trace) {
                const void* a = rdp.loaded_texture[rdp.texture_tile[tile].tmem].addr;
                int k; for (k = 0; k < nseen; k++) if (seen[k] == a) break;
                if (k == nseen && nseen < 256) {
                    seen[nseen++] = a;
                    fprintf(stderr, "tex: MASKWRAP num=%d fmt=%d siz=%d decoded=%ux%u -> %ux%u line=%u addr=%p\n",
                            ge_texnum_of(a), rdp.texture_tile[tile].fmt, rdp.texture_tile[tile].siz, width, height,
                            w, h, rdp.texture_tile[tile].line_size_bytes, a);
                }
            }
        }
        for (uint32_t t = 0; t < h; t++) {
            for (uint32_t s2 = 0; s2 < w; s2++) {
                const uint32_t src = (t * width + s2) % n;
                memcpy(wrapbuf + ((size_t) t * w + s2) * 4, rgba + (size_t) src * 4, 4);
            }
        }
        gfx_rapi->upload_texture(wrapbuf, w, h);
        return;
    }
    const uint8_t masks = ge_maskcrop_enabled() ? rdp.texture_tile[tile].masks : 0;
    const uint8_t maskt = ge_maskcrop_enabled() ? rdp.texture_tile[tile].maskt : 0;
    uint32_t w = width, h = height;
    { /* GE_TRACE_TEX census of the OPPOSITE case: a wrap mask WIDER than the
       * decoded image - the hardware wraps the address through TMEM there
       * (the RGBA16 importer rebuilds that for Frigate's water; the other
       * formats decode linearly). Once per texture. */
        static int trace = -1;
        static const void* seen[256]; static int nseen;   /* by ADDRESS: unpooled textures all read num=-1 */
        if (trace < 0) trace = getenv("GE_TRACE_TEX") != NULL;
        if (trace && ((rdp.texture_tile[tile].masks != 0 && (1u << rdp.texture_tile[tile].masks) > width)
                      || (rdp.texture_tile[tile].maskt != 0 && (1u << rdp.texture_tile[tile].maskt) > height))) {
            const void* a = rdp.loaded_texture[rdp.texture_tile[tile].tmem].addr;
            const int num = ge_texnum_of(a);
            int k; for (k = 0; k < nseen; k++) if (seen[k] == a) break;
            if (k == nseen && nseen < 256) {
                seen[nseen++] = a;
                fprintf(stderr, "tex: MASKWIDE num=%d fmt=%d siz=%d decoded=%ux%u masks=%d maskt=%d line=%u addr=%p\n",
                        num, rdp.texture_tile[tile].fmt, rdp.texture_tile[tile].siz, width, height,
                        rdp.texture_tile[tile].masks, rdp.texture_tile[tile].maskt,
                        rdp.texture_tile[tile].line_size_bytes,
                        (const void*)rdp.loaded_texture[rdp.texture_tile[tile].tmem].addr);
            }
        }
    }
    if (masks != 0 && (1u << masks) < w) w = 1u << masks;
    if (maskt != 0 && (1u << maskt) < h) h = 1u << maskt;
    if (w != width || h != height) {
        { /* GE_TRACE_TEX census: which textures the crop touches, once each */
            static int trace = -1;
            static int seen[256]; static int nseen;
            if (trace < 0) trace = getenv("GE_TRACE_TEX") != NULL;
            if (trace) {
                const int num = ge_texnum_of(rdp.loaded_texture[rdp.texture_tile[tile].tmem].addr);
                int k; for (k = 0; k < nseen; k++) if (seen[k] == num) break;
                if (k == nseen && nseen < 256) {
                    seen[nseen++] = num;
                    fprintf(stderr, "tex: MASKCROP num=%d fmt=%d siz=%d decoded=%ux%u -> %ux%u (masks=%d maskt=%d) addr=%p\n",
                            num, rdp.texture_tile[tile].fmt, rdp.texture_tile[tile].siz, width, height, w, h,
                            masks, maskt, (const void*)rdp.loaded_texture[rdp.texture_tile[tile].tmem].addr);
                }
            }
        }
        /* in-place crop: destination index never exceeds source index */
        uint8_t* dst = rgba;
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t* src = rgba + (size_t)y * width * 4;
            memmove(dst, src, (size_t)w * 4);
            dst += (size_t)w * 4;
        }
    }
    gfx_rapi->upload_texture(rgba, w, h);
}

static void import_texture_rgba16(int tile, const LoadedTexture& loaded_texture, bool importReplacement) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    // SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);
    // TODO: this trips in some places with a garbage size in full_image_line_size_bytes
    // probably wherever framebuffer effects are used

    /* GE texel convention: NATIVE u16s (settled empirically via the
     * gold-vs-purple GoldenEye logo A/B; see image.c TEX16 note). The
     * compiled-in asset words and the CPU texture pipeline both hold
     * native values on PC. */

    /* Mask-driven tiles (GE sky water): the tile declares wrap masks
     * WIDER than its line stride (masks 5 = 32 texels vs line 32 bytes =
     * 16 texels) and a degenerate SetTileSize - on hardware the sampler
     * simply computes TMEM addr = t*line + s*2, so rows of the sampled
     * space interleave through the stored image (Rare's water shimmer
     * leans on this). Rebuild the texture with that exact addressing;
     * a linear decode at line width produced the green noise. */
    const uint8_t masks = rdp.texture_tile[tile].masks;
    const uint8_t maskt = rdp.texture_tile[tile].maskt;
    const uint32_t line_bytes = rdp.texture_tile[tile].line_size_bytes;
    if (masks != 0 && maskt != 0 && line_bytes != 0 && size_bytes >= 2 &&
        (2u << masks) != line_bytes) {
        const uint32_t w = 1u << masks;
        const uint32_t h = 1u << maskt;
        { /* GE bring-up: confirm the mask-addressed path fires (water) */
            static int logged;
            if (logged < 4 && getenv("GE_DUMP_BOND") != NULL) {
                logged++;
                fprintf(stderr, "tex: rgba16 MASK import %ux%u line=%u size=%u addr=%p texels:", w, h,
                        line_bytes, size_bytes, (const void*)addr);
                for (int k = 0; k < 8; k++) {
                    const uint16_t c = ((const uint16_t *)addr)[k * 37];
                    fprintf(stderr, " %04x(r%d,g%d,b%d)", c, c >> 11, (c >> 6) & 0x1f, (c >> 1) & 0x1f);
                }
                fprintf(stderr, "\n");
            }
        }
        if (!ge_tex_upload_reserve((uint64_t)w * h)) {
            return;
        }
        uint8_t *dest = tex_upload_buffer;
        for (uint32_t t = 0; t < h; t++) {
            for (uint32_t s = 0; s < w; s++, dest += 4) {
                const uint32_t off = (t * line_bytes + s * 2) % size_bytes;
                const uint16_t col16 = *(const uint16_t *)(addr + off);
                const uint8_t a = col16 & 1;
                dest[0] = SCALE_5_8(col16 >> 11);
                dest[1] = SCALE_5_8((col16 >> 6) & 0x1f);
                dest[2] = SCALE_5_8((col16 >> 1) & 0x1f);
                dest[3] = a ? 255 : 0;
            }
        }
        gfx_rapi->upload_texture(tex_upload_buffer, w, h);
        return;
    }

    if (!ge_tex_upload_reserve(size_bytes / 2)) {
        return;
    }
    uint8_t *dest = tex_upload_buffer;
    for (uint32_t i = 0; i < size_bytes / 2; i++, dest += 4) {
        const uint16_t col16 = ((const uint16_t *)addr)[i];
        const uint8_t a = col16 & 1;
        const uint8_t r = col16 >> 11;
        const uint8_t g = (col16 >> 6) & 0x1f;
        const uint8_t b = (col16 >> 1) & 0x1f;
        dest[0] = SCALE_5_8(r);
        dest[1] = SCALE_5_8(g);
        dest[2] = SCALE_5_8(b);
        dest[3] = a ? 255 : 0;
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes / 2;
    const uint32_t height = size_bytes / rdp.texture_tile[tile].line_size_bytes;

    ge_upload_masked(tex_upload_buffer, width, height, tile);
    // DumpTexture(loaded_texture.otr_path, rgba32_buf, width, height);
}

static void import_texture_rgba32(int tile, const LoadedTexture& loaded_texture, bool importReplacement) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    if (!ge_tex_upload_reserve((size_bytes + 3) / 4)) {
        return;
    }
    uint32_t *dest = (uint32_t *)tex_upload_buffer;
    const uint32_t *src = (const uint32_t *)addr;
    for (uint32_t i = 0; i < size_bytes; i += 4, ++dest, ++src) {
        *dest = PD_BE32(*src);
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes / 2;
    const uint32_t height = (size_bytes / 2) / rdp.texture_tile[tile].line_size_bytes;
    ge_upload_masked(tex_upload_buffer, width, height, tile);
    // DumpTexture(loaded_texture.otr_path, addr, width, height);
}

static void import_texture_ia4(int tile, const LoadedTexture& loaded_texture, bool importReplacement) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    if (!ge_tex_upload_reserve((uint64_t)size_bytes * 2)) {
        return;
    }
    uint8_t *dest = tex_upload_buffer;
    for (uint32_t i = 0; i < size_bytes * 2; i++, dest += 4) {
        const uint8_t byte = addr[i / 2];
        const uint8_t part = (byte >> (4 - (i % 2) * 4)) & 0xf;
        const uint8_t intensity = part >> 1;
        const uint8_t alpha = part & 1;
        const uint8_t c = SCALE_3_8(intensity);
        dest[0] = c;
        dest[1] = c;
        dest[2] = c;
        dest[3] = alpha ? 255 : 0;
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes * 2;
    const uint32_t height = size_bytes / rdp.texture_tile[tile].line_size_bytes;

    ge_upload_masked(tex_upload_buffer, width, height, tile);
    // DumpTexture(loaded_texture.otr_path, rgba32_buf, width, height);
}

static void import_texture_ia8(int tile, const LoadedTexture& loaded_texture, bool importReplacement) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    if (!ge_tex_upload_reserve(size_bytes)) {
        return;
    }
    uint8_t *dest = tex_upload_buffer;
    for (uint32_t i = 0; i < size_bytes; i++, dest += 4) {
        const uint8_t intensity = SCALE_4_8(addr[i] >> 4);
        const uint8_t alpha = SCALE_4_8(addr[i] & 0xf);
        dest[0] = intensity;
        dest[1] = intensity;
        dest[2] = intensity;
        dest[3] = alpha;
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes;
    const uint32_t height = size_bytes / rdp.texture_tile[tile].line_size_bytes;

    ge_upload_masked(tex_upload_buffer, width, height, tile);
    // DumpTexture(loaded_texture.otr_path, rgba32_buf, width, height);
}

static void import_texture_ia16(int tile, const LoadedTexture& loaded_texture, bool importReplacement) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    if (!ge_tex_upload_reserve(size_bytes / 2)) {
        return;
    }
    uint8_t *dest = tex_upload_buffer;
    for (uint32_t i = 0; i < size_bytes / 2; i++, dest += 4) {
        /* native u16 IA16 (see rgba16 note): high byte = intensity */
        const uint16_t ia16 = ((const uint16_t *)addr)[i];
        const uint8_t intensity = (uint8_t)(ia16 >> 8);
        const uint8_t alpha = (uint8_t)ia16;
        dest[0] = intensity;
        dest[1] = intensity;
        dest[2] = intensity;
        dest[3] = alpha;
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes / 2;
    const uint32_t height = size_bytes / rdp.texture_tile[tile].line_size_bytes;

    ge_upload_masked(tex_upload_buffer, width, height, tile);
    // DumpTexture(loaded_texture.otr_path, rgba32_buf, width, height);
}

static void import_texture_i4(int tile, const LoadedTexture& loaded_texture, bool importReplacement) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    if (!ge_tex_upload_reserve((uint64_t)size_bytes * 2)) {
        return;
    }
    uint8_t *dest = tex_upload_buffer;
    for (uint32_t i = 0; i < size_bytes * 2; i++, dest += 4) {
        const uint8_t byte = addr[i / 2];
        const uint8_t part = (byte >> (4 - (i % 2) * 4)) & 0xf;
        const uint8_t intensity = SCALE_4_8(part);
        dest[0] = intensity;
        dest[1] = intensity;
        dest[2] = intensity;
        dest[3] = intensity;
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes * 2;
    const uint32_t height = size_bytes / rdp.texture_tile[tile].line_size_bytes;

    ge_upload_masked(tex_upload_buffer, width, height, tile);
    // DumpTexture(loaded_texture.otr_path, rgba32_buf, width, height);
}

static void import_texture_i8(int tile, const LoadedTexture& loaded_texture, bool importReplacement) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    if (!ge_tex_upload_reserve(size_bytes)) {
        return;
    }
    uint8_t *dest = tex_upload_buffer;
    for (uint32_t i = 0; i < size_bytes; i++, dest += 4) {
        const uint8_t intensity = addr[i];
        dest[0] = intensity;
        dest[1] = intensity;
        dest[2] = intensity;
        dest[3] = intensity;
    }

    const uint32_t width = rdp.texture_tile[tile].line_size_bytes;
    const uint32_t height = size_bytes / rdp.texture_tile[tile].line_size_bytes;

    ge_upload_masked(tex_upload_buffer, width, height, tile);
    // DumpTexture(loaded_texture.otr_path, rgba32_buf, width, height);
}

static inline void palette_to_rgba32(const uint16_t palentry, uint8_t *rgba32_buf) {
    if (rdp.palette_fmt == G_TT_IA16) {
        /* Native u16 IA16, as in import_texture_ia16: high byte = intensity,
         * low byte = alpha, since the inflater stores palettes natively.
         * Swapping them turns IA16-TLUT textures white. */
        const uint8_t intensity = palentry >> 8;
        const uint8_t alpha = (palentry & 0xff);
        rgba32_buf[0] = intensity;
        rgba32_buf[1] = intensity;
        rgba32_buf[2] = intensity;
        rgba32_buf[3] = alpha;
    } else {
        // assume G_TT_RGBA16
        const uint8_t a = palentry & 1;
        const uint8_t r = palentry >> 11;
        const uint8_t g = (palentry >> 6) & 0x1f;
        const uint8_t b = (palentry >> 1) & 0x1f;
        rgba32_buf[0] = SCALE_5_8(r);
        rgba32_buf[1] = SCALE_5_8(g);
        rgba32_buf[2] = SCALE_5_8(b);
        rgba32_buf[3] = a ? 255 : 0;
    }
}

static void import_texture_ci4(int tile, const LoadedTexture& loaded_texture, bool importReplacement) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;
    const uint32_t pal_idx = rdp.texture_tile[tile].palette; // 0-15
    const uint16_t* palette = (const uint16_t *)(rdp.palette + pal_idx * 16); // 16 pixel entries, 16 bits each
    SUPPORT_CHECK(full_image_line_size_bytes == line_size_bytes);

    if (!ge_tex_upload_reserve((uint64_t)size_bytes * 2)) {
        return;
    }
    for (uint32_t i = 0; i < size_bytes * 2; i++) {
        const uint8_t byte = addr[i / 2];
        const uint8_t idx = (byte >> (4 - (i % 2) * 4)) & 0xf;
        palette_to_rgba32(palette[idx], tex_upload_buffer +4 * i);
    }

    uint32_t result_line_size = rdp.texture_tile[tile].line_size_bytes;
    if (metadata->h_byte_scale != 1) {
        result_line_size *= metadata->h_byte_scale;
    }

    const uint32_t width = result_line_size * 2;
    const uint32_t height = size_bytes / result_line_size;

    ge_upload_masked(tex_upload_buffer, width, height, tile);
}

static void import_texture_ci8(int tile, const LoadedTexture& loaded_texture, bool importReplacement) {
    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* addr = loaded_texture.addr;
    const uint32_t size_bytes = loaded_texture.size_bytes;
    const uint32_t full_image_line_size_bytes =
        loaded_texture.full_image_line_size_bytes;
    const uint32_t line_size_bytes = loaded_texture.line_size_bytes;

    /* the loop writes whole lines, so up to one line past size_bytes */
    if (!ge_tex_upload_reserve((uint64_t)size_bytes + line_size_bytes)) {
        return;
    }
    for (uint32_t i = 0, j = 0; i < size_bytes; j += full_image_line_size_bytes - line_size_bytes) {
        for (uint32_t k = 0; k < line_size_bytes; i++, k++, j++) {
            const uint8_t idx = addr[j];
            palette_to_rgba32(rdp.palette[idx], tex_upload_buffer + 4 * i);
        }
    }

    uint32_t result_line_size = rdp.texture_tile[tile].line_size_bytes;
    if (metadata->h_byte_scale != 1) {
        result_line_size *= metadata->h_byte_scale;
    }

    const uint32_t width = result_line_size;
    const uint32_t height = size_bytes / result_line_size;

    { /* diagnostic (GE_TEXDUMP_ADDR=hex): write the RGBA32 upload for one
       * source address to texgl_<addr>.bin - ground truth of what a
       * texture looks like on screen. */
        static const char* want = getenv("GE_TEXDUMP_ADDR");
        if (want && want[0]) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%08lx", (unsigned long)(uintptr_t)addr);
            char wantl[32];
            size_t wi;
            for (wi = 0; wi < sizeof(wantl) - 1 && want[wi]; wi++) {
                wantl[wi] = (char)tolower((unsigned char)want[wi]);
            }
            wantl[wi] = 0;
            if (strstr(buf, wantl)) {
                char name[64];
                snprintf(name, sizeof(name), "texgl_%p_%ux%u.bin", (const void*)addr, width, height);
                FILE* f = fopen(name, "wb");
                if (f) {
                    fwrite(tex_upload_buffer, 4, width * height, f);
                    fclose(f);
                    fprintf(stderr, "texgl: dumped %s (pal[0..3]=%04x %04x %04x %04x)\n",
                            name, rdp.palette[0], rdp.palette[1], rdp.palette[2], rdp.palette[3]);
                }
            }
        }
    }

    ge_upload_masked(tex_upload_buffer, width, height, tile);
    // DumpTexture(loaded_texture.otr_path, rgba32_buf, width, height);
}

/* diagnostic (GE_TEXLOGF=<frame>): one-frame texture event sequencer -
 * SETTIMG / LOADBLOCK / LOADTLUT / draw-bind order */
static long ge_texlogf(void) {
    static long f = -2;
    if (f == -2) {
        const char* e = getenv("GE_TEXLOGF");
        f = (e && e[0]) ? atol(e) : -1;
    }
    return f;
}
static bool ge_texlog_now(void) {
    /* g_PortFramesPumped: extern "C" at file scope */
    return ge_texlogf() >= 0 && (long)g_PortFramesPumped == ge_texlogf();
}

/* GE_TEXDUMP_ALL metadata sidechannel: import_texture stamps these, the
 * gfx backend's upload dump names files with them (diagnostic only) */
extern "C" {
const void* ge_dbg_import_addr;
int ge_dbg_import_fmt, ge_dbg_import_siz, ge_dbg_import_tile, ge_dbg_import_tmem;
unsigned ge_dbg_import_palsum;
}

/* TMEM block registry (fixes the blue Depot floor): GE draws often pair a BASE
 * texture's mip chain (LOADBLOCK at tmem 0) with a DETAIL texture's chain
 * (a second LOADBLOCK at a higher tmem) in one draw. The mip-address
 * derivation below used to resolve every mid-chain tile against the MOST
 * RECENT load (rdp.texture_to_load) and clamp negative deltas to zero, so
 * a base-mip tile pointing BELOW the detail's tmem got the detail's bytes
 * - I-format grain sampled as CI indices through whatever TLUT was
 * current (the blue floor mottle). Track which block owns which TMEM
 * range so each tile derives from ITS OWN load. */
struct GeTmemBlock {
    const uint8_t* addr;
    uint16_t tmem;
    uint16_t words;
    /* frag: the part of an earlier load that a later load did not cover. Its
     * bytes are still in TMEM so the composite path needs them, but no tile
     * resolves against it and it never counts as "another block" in the
     * crossing test; keeping it as a record avoids a memcpy per LOADBLOCK. */
    uint8_t frag;
};
#define GE_TMEM_BLOCKS 16
static GeTmemBlock ge_tmem_blocks[GE_TMEM_BLOCKS];

/* Shadow TMEM: GE's dual-texture ground draws configure a
 * render tile whose TMEM range CROSSES two LOADBLOCKs (e.g. a 16x16 CI8
 * overlay at tmem 19 reading words 19..50 while a second texture loaded
 * at tmem 32) - on hardware TMEM is one 4KB memory, so the tile
 * legitimately samples the composite. Mirror every LOADBLOCK's bytes
 * here; tiles that cross block boundaries import from the composite. */
static uint8_t ge_shadow_tmem[4096];
static uint32_t ge_composite_hash; /* nonzero only for the current composite import */

/* Mirroring every LOADBLOCK into the shadow TMEM was costly, so the copy is
 * deferred: a block is mirrored when a later load overlaps it, and live blocks
 * are mirrored on demand by ge_shadow_tmem_materialize (the shadow is only read
 * by the composite path in import_texture). */
static void ge_shadow_tmem_mirror(const GeTmemBlock& blk) {
    uint32_t off = (uint32_t)blk.tmem * 8;
    uint32_t n = (uint32_t)blk.words * 8;
    if (blk.addr && off < sizeof(ge_shadow_tmem)) {
        if (n > sizeof(ge_shadow_tmem) - off) {
            n = sizeof(ge_shadow_tmem) - off;
        }
        memcpy(ge_shadow_tmem + off, blk.addr, n);
    }
}

static void ge_shadow_tmem_materialize(void) {
    /* frags and live blocks never overlap each other (every load carves
     * what it covers out of the others), so the order does not matter */
    for (int b = 0; b < GE_TMEM_BLOCKS; b++) {
        ge_shadow_tmem_mirror(ge_tmem_blocks[b]);
    }
}

/* a free slot, making one by mirroring a frag into the shadow if needed
 * (the old eager behaviour, now only under table pressure) */
static int ge_tmem_free_slot(void) {
    for (int b = 0; b < GE_TMEM_BLOCKS; b++) {
        if (!ge_tmem_blocks[b].addr) return b;
    }
    for (int b = 0; b < GE_TMEM_BLOCKS; b++) {
        if (ge_tmem_blocks[b].frag) {
            ge_shadow_tmem_mirror(ge_tmem_blocks[b]);
            ge_tmem_blocks[b].addr = NULL;
            return b;
        }
    }
    return -1;
}

static void ge_tmem_register_block(const uint8_t* addr, uint16_t tmem, uint32_t size_bytes) {
    uint16_t words = (uint16_t)((size_bytes + 7) / 8);
    if (words == 0) words = 1;
    const uint32_t end = (uint32_t)tmem + words;
    for (int b = 0; b < GE_TMEM_BLOCKS; b++) {
        GeTmemBlock& blk = ge_tmem_blocks[b];
        if (!blk.addr || blk.tmem >= end || tmem >= (uint32_t)blk.tmem + blk.words) {
            continue;
        }
        /* overlapped by the new load: keep the uncovered remainder(s) as
         * frags - the bytes are still in TMEM (no copy) */
        const uint32_t blk_end = (uint32_t)blk.tmem + blk.words;
        GeTmemBlock after = {};
        if (blk_end > end) {
            after.addr = blk.addr + (end - blk.tmem) * 8u;
            after.tmem = (uint16_t)end;
            after.words = (uint16_t)(blk_end - end);
            after.frag = 1;
        }
        if (blk.tmem < tmem) {
            blk.words = (uint16_t)(tmem - blk.tmem); /* the part before the new load */
            blk.frag = 1;
        } else {
            blk.addr = NULL;
        }
        if (after.addr) {
            const int s = ge_tmem_free_slot();
            if (s >= 0) {
                ge_tmem_blocks[s] = after;
            } else {
                ge_shadow_tmem_mirror(after); /* table full: fall back to the copy */
            }
        }
    }
    int slot = ge_tmem_free_slot();
    if (slot < 0) slot = 0; /* lossy, as before: the table is all live blocks */
    ge_tmem_blocks[slot].addr = addr;
    ge_tmem_blocks[slot].tmem = tmem;
    ge_tmem_blocks[slot].words = words;
    ge_tmem_blocks[slot].frag = 0;
}

/* the load already on record with exactly this address / TMEM range
 * (gfx_dp_load_block's identical-reload early-out) */
static bool ge_tmem_block_is_registered(const uint8_t* addr, uint16_t tmem, uint32_t size_bytes) {
    uint16_t words = (uint16_t)((size_bytes + 7) / 8);
    if (words == 0) words = 1;
    for (int b = 0; b < GE_TMEM_BLOCKS; b++) {
        const GeTmemBlock& blk = ge_tmem_blocks[b];
        if (blk.addr == addr && !blk.frag && blk.tmem == tmem && blk.words == words) {
            return true;
        }
    }
    return false;
}

static const GeTmemBlock* ge_tmem_find_block(uint16_t tmem) {
    for (int b = 0; b < GE_TMEM_BLOCKS; b++) {
        const GeTmemBlock& blk = ge_tmem_blocks[b];
        if (blk.addr && !blk.frag && tmem >= blk.tmem && tmem < blk.tmem + blk.words) {
            return &blk;
        }
    }
    return NULL;
}

/* GE: recover the pool texture NUMBER of a loaded texture address. texLoad
 * leaves a record header in front of the pixel data (texture number as s16
 * at -8, see white-weapon-textures memory); valid for the persistent model
 * pool (portModelTexPool) and the per-stage level pool. Guarded to the
 * model-pool arena so the read is always safe; -1 otherwise. */
struct GeTexPoolView { unsigned char* start; unsigned char* end; unsigned char* leftpos; unsigned char* rightpos; }; /* mirrors struct texpool (image.h) */
struct GeTexRecView { uint16_t numbits; uint16_t pad; const unsigned char* data; uint8_t w, h, unk0a, bits0b; uint32_t bits0c; }; /* mirrors struct tex (image.h, 16 bytes) */
extern "C" struct GeTexPoolView* portModelTexPool(void);
/* GE: recover the pool texture NUMBER of a loaded texture address by walking
 * the model pool's struct tex records (rightpos..end); lod levels sit after
 * data, so take the record with the greatest data <= addr. -1 if not in
 * the model pool. Diagnostics only (GE_TRACE_TEX / GE_TRACE_TEXNUM). */
static const struct GeTexRecView* ge_texrec_of(const void* addr) {
    const unsigned char* a = (const unsigned char*)addr;
    const struct GeTexPoolView* pool = portModelTexPool();
    if (pool == NULL || a < pool->start || a >= pool->end) return NULL;
    const struct GeTexRecView* best = NULL;
    for (const struct GeTexRecView* r = (const struct GeTexRecView*)pool->rightpos; (const unsigned char*)(r + 1) <= pool->end; r++) {
        if (r->data != NULL && r->data <= a && (best == NULL || r->data > best->data)) best = r;
    }
    return best;
}
static int ge_texnum_of(const void* addr) {
    const unsigned char* a = (const unsigned char*)addr;
    const struct GeTexPoolView* pool = portModelTexPool();
    if (pool == NULL || a < pool->start || a >= pool->end) return -1;
    const struct GeTexRecView* best = NULL;
    for (const struct GeTexRecView* r = (const struct GeTexRecView*)pool->rightpos; (const unsigned char*)(r + 1) <= pool->end; r++) {
        if (r->data != NULL && r->data <= a && (best == NULL || r->data > best->data)) best = r;
    }
    return best ? (int)(best->numbits & 0xfff) : -1;
}

/* upload-failure side channel from gfx_opengl.cpp (see import_texture tail) */
extern "C" unsigned g_GfxUploadError;
extern "C" unsigned g_GfxUploadErrorCount;
extern "C" unsigned long long portProfNow(void);
extern "C" void portHitchNote(int cat, unsigned long long us);
extern "C" unsigned long long g_GfxUpDrainUs, g_GfxUpTexUs, g_GfxUpMipUs, g_GfxUpErrUs; /* gfx_opengl.cpp */
extern "C" unsigned g_GfxUpW, g_GfxUpH;

/* One texture import (decode + GL upload) took GE_SLOWUPLOADMS (default 5,
 * 0 off) or longer: say which step ate it, the texture size, and whether
 * the cache evicted a node or reused a freed GL id for it (a driver may wait
 * for the GPU before redefining an id an in-flight frame still samples).
 * Capped at 200 lines a run. */
static void ge_slow_upload_log(unsigned long long us, unsigned fmt, unsigned siz) {
    static long long thresh = -1;
    static int lines;
    if (thresh < 0) {
        const char* e = getenv("GE_SLOWUPLOADMS");
        thresh = (e != NULL && *e != '\0') ? atoll(e) * 1000 : 5000;
    }
    if (thresh <= 0 || (long long)us < thresh || lines >= 200) {
        return;
    }
    lines++;
    if (g_GfxUpW == 0) {
        fprintf(stdout, "ge: slow upload: %llu.%llu ms, no GL upload (decode only) fmt %u siz %u%s%s\n",
                us / 1000, (us / 100) % 10, fmt, siz,
                ge_up_evicted ? " | evicted a node" : "", ge_up_recycled ? " | reused a freed id" : "");
        return;
    }
    const unsigned long long gl = g_GfxUpDrainUs + g_GfxUpTexUs + g_GfxUpMipUs + g_GfxUpErrUs;
    const unsigned long long dec = us > gl ? us - gl : 0;
    fprintf(stdout,
            "ge: slow upload: %llu.%llu ms %ux%u fmt %u siz %u | decode %llu.%llu drain-geterror %llu.%llu teximage %llu.%llu mips %llu.%llu geterror %llu.%llu | cache %u/%u%s%s\n",
            us / 1000, (us / 100) % 10, g_GfxUpW, g_GfxUpH, fmt, siz,
            dec / 1000, (dec / 100) % 10, g_GfxUpDrainUs / 1000, (g_GfxUpDrainUs / 100) % 10,
            g_GfxUpTexUs / 1000, (g_GfxUpTexUs / 100) % 10, g_GfxUpMipUs / 1000, (g_GfxUpMipUs / 100) % 10,
            g_GfxUpErrUs / 1000, (g_GfxUpErrUs / 100) % 10,
            (unsigned)gfx_texture_cache.map.size(), (unsigned)TEXTURE_CACHE_MAX_SIZE,
            ge_up_evicted ? " | evicted a node" : "", ge_up_recycled ? " | reused a freed id" : "");
}

/* Texture churn (GE_TEXCHURN=0 turns it off): murkantor's Dam filled all
 * 1024 cache entries within seconds and a tester's Surface 2 uploaded 17-170
 * textures a frame in bursts, though a stage has a few hundred textures.
 * Every import (cache miss) is sorted by what the cache last held for the
 * same address since the last stage load:
 *   new      - the address was never imported
 *   again    - the same key was imported before, so it was evicted
 *   variant  - same address, different key; the fields that differ are counted
 * One 'ge: texchurn:' line per 10 s with imports, with the three busiest
 * addresses. Render thread only. */
static std::unordered_map<const uint8_t*, TextureCacheKey> ge_churn_last;
static std::unordered_map<const uint8_t*, unsigned> ge_churn_addr;
static unsigned ge_churn_new, ge_churn_again, ge_churn_variant, ge_churn_evict;
static unsigned ge_churn_field[8]; /* palette addr, palette index, fmt/siz, line, size, palette sum/composite, masks, - */
static unsigned long long ge_churn_next_us;

static void ge_texchurn_note(const TextureCacheKey& key, bool evicted) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("GE_TEXCHURN");
        on = (e == NULL || atoi(e) != 0);
    }
    if (!on) {
        return;
    }
    if (ge_churn_reset) {
        ge_churn_reset = false;
        ge_churn_last.clear();
    }
    if (evicted) {
        ge_churn_evict++;
    }
    ge_churn_addr[key.texture_addr]++;
    auto it = ge_churn_last.find(key.texture_addr);
    if (it == ge_churn_last.end()) {
        ge_churn_new++;
        ge_churn_last.emplace(key.texture_addr, key);
        return;
    }
    const TextureCacheKey& o = it->second;
    if (o == key) {
        ge_churn_again++;
        return;
    }
    ge_churn_variant++;
    if (o.palette_addrs[0] != key.palette_addrs[0] || o.palette_addrs[1] != key.palette_addrs[1]) ge_churn_field[0]++;
    if (o.palette_index != key.palette_index) ge_churn_field[1]++;
    if (o.fmt != key.fmt || o.siz != key.siz) ge_churn_field[2]++;
    if (o.line_size_bytes != key.line_size_bytes) ge_churn_field[3]++;
    if (o.size_bytes != key.size_bytes) ge_churn_field[4]++;
    if (o.palette_sum != key.palette_sum) ge_churn_field[5]++;
    if (o.masks != key.masks || o.maskt != key.maskt) ge_churn_field[6]++;
    it->second = key;
}

/* gfx_run, once a frame: print and reset the 10 s window */
static void ge_texchurn_report(void) {
    const unsigned long long now = portProfNow();
    if (now < ge_churn_next_us) {
        return;
    }
    ge_churn_next_us = now + 10000000;
    const unsigned total = ge_churn_new + ge_churn_again + ge_churn_variant;
    if (total == 0) {
        return;
    }
    std::pair<unsigned, const uint8_t*> top[3] = {};
    for (const auto& a : ge_churn_addr) {
        for (int k = 0; k < 3; k++) {
            if (a.second > top[k].first) {
                for (int m = 2; m > k; m--) top[m] = top[m - 1];
                top[k] = std::make_pair(a.second, a.first);
                break;
            }
        }
    }
    printf("ge: texchurn: %u imports in 10 s (new %u, again after eviction %u, variant %u) evictions %u cache %u/%u"
           " | variant fields: paladdr %u palidx %u fmt %u line %u size %u palsum %u masks %u"
           " | top %p x%u, %p x%u, %p x%u\n",
           total, ge_churn_new, ge_churn_again, ge_churn_variant, ge_churn_evict,
           (unsigned)gfx_texture_cache.map.size(), (unsigned)TEXTURE_CACHE_MAX_SIZE,
           ge_churn_field[0], ge_churn_field[1], ge_churn_field[2], ge_churn_field[3], ge_churn_field[4],
           ge_churn_field[5], ge_churn_field[6],
           (const void*)top[0].second, top[0].first, (const void*)top[1].second, top[1].first,
           (const void*)top[2].second, top[2].first);
    ge_churn_new = ge_churn_again = ge_churn_variant = ge_churn_evict = 0;
    memset(ge_churn_field, 0, sizeof(ge_churn_field));
    ge_churn_addr.clear();
}

static void import_texture(int i, int tile, bool importReplacement) {
    LoadedTexture& loaded_texture = rdp.loaded_texture[rdp.texture_tile[tile].tmem];
    const uint8_t fmt = rdp.texture_tile[tile].fmt;
    const uint8_t siz = rdp.texture_tile[tile].siz;
    ge_dbg_import_addr = loaded_texture.addr;
    ge_dbg_import_fmt = fmt;
    ge_dbg_import_siz = siz;
    ge_dbg_import_tile = tile;
    ge_dbg_import_tmem = rdp.texture_tile[tile].tmem;
    ge_dbg_import_palsum = rdp.palette_sum;
    const uint32_t tex_flags = loaded_texture.tex_flags;
    const uint8_t palette_index = rdp.texture_tile[tile].palette;

    if ((rdp.tex_lod && tile >= rdp.first_tile_index + rdp.tex_detail) || !loaded_texture.addr) {
        // set up this mip level; also acts as a catch-all for when .addr is NULL because my texture loader sucks
        /* GE rooms load a whole mip chain with one LOADBLOCK and each tile
         * points into it by its tmem field. Address each level at its real
         * TMEM offset, or every level shares tile 0's cache entry. */
        const GeTmemBlock* blk = ge_tmem_find_block(rdp.texture_tile[tile].tmem);
        if (blk != NULL) {
            /* resolve against the block that actually LOADED this TMEM
             * range - not necessarily the most recent load (base+detail
             * dual-texture draws) */
            loaded_texture.addr = blk->addr + (uint32_t)(rdp.texture_tile[tile].tmem - blk->tmem) * 8;
        } else {
            uint16_t tmem_delta = (rdp.tex_lod && rdp.texture_tile[tile].tmem > rdp.texture_to_load.tmem)
                                      ? (uint16_t)(rdp.texture_tile[tile].tmem - rdp.texture_to_load.tmem)
                                      : 0;
            loaded_texture.addr = rdp.texture_to_load.addr + (uint32_t)tmem_delta * 8;
        }
        loaded_texture.line_size_bytes = rdp.texture_tile[tile].line_size_bytes;
        loaded_texture.full_image_line_size_bytes = rdp.texture_tile[tile].line_size_bytes;
        loaded_texture.full_size_bytes = loaded_texture.full_image_line_size_bytes * rdp.texture_tile[tile].height;
        loaded_texture.size_bytes = loaded_texture.line_size_bytes * rdp.texture_tile[tile].height;
        if (siz == G_IM_SIZ_32b) {
            // HACK: fixup 32-bit LODed texture height
            loaded_texture.size_bytes <<= 1;
            loaded_texture.full_size_bytes <<= 1;
        }
        loaded_texture.orig_size_bytes = loaded_texture.size_bytes;

        if (blk != NULL) {
            /* hardware-composite tile (see ge_shadow_tmem): the tile's
             * TMEM range crosses INTO ANOTHER LOADED BLOCK, so its lower
             * rows come from that other texture - read the shadow TMEM
             * composite exactly like the RDP would. (Merely overrunning
             * the owning block's own tail - mip-chain TMEM padding - is
             * NOT composite; the round-1 derivation handles that.) */
            uint32_t tile_words = (loaded_texture.size_bytes + 7) / 8;
            bool crosses_other = false;
            for (int cb = 0; cb < GE_TMEM_BLOCKS; cb++) {
                const GeTmemBlock& other = ge_tmem_blocks[cb];
                if (other.addr && !other.frag && &other != blk &&
                    (uint32_t)rdp.texture_tile[tile].tmem < (uint32_t)other.tmem + other.words &&
                    (uint32_t)other.tmem < (uint32_t)rdp.texture_tile[tile].tmem + tile_words) {
                    crosses_other = true;
                    break;
                }
            }
            if (crosses_other) {
                static uint8_t composite[4096];
                uint32_t off = (uint32_t)rdp.texture_tile[tile].tmem * 8;
                uint32_t n = loaded_texture.size_bytes;
                if (off < sizeof(ge_shadow_tmem)) {
                    if (n > sizeof(ge_shadow_tmem) - off) {
                        n = sizeof(ge_shadow_tmem) - off;
                    }
                    ge_shadow_tmem_materialize();
                    memcpy(composite, ge_shadow_tmem + off, n);
                    { /* GE_COMPSWIZ: A/B the TMEM word-swizzle phase for
                       * composite tiles. 1 = swap 32-bit halves on ODD
                       * TILE LINES (raw RDP read swizzle); 2 = swap the
                       * word at odd ABSOLUTE TMEM word index (odd-start
                       * phase correction). */
                        static int mode = -1;
                        if (mode < 0) {
                            const char* e = getenv("GE_COMPSWIZ");
                            mode = (e && e[0]) ? atoi(e) : 0;
                        }
                        uint32_t lb = rdp.texture_tile[tile].line_size_bytes;
                        if (mode && lb >= 8) {
                            for (uint32_t wi = 0; wi * 8 + 8 <= n; wi++) {
                                uint32_t swap;
                                if (mode == 1) {
                                    swap = ((wi * 8) / lb) & 1;      /* tile line parity */
                                } else {
                                    swap = (off / 8 + wi) & 1;       /* absolute word parity */
                                }
                                if (swap) {
                                    uint32_t a, b2;
                                    memcpy(&a, composite + wi * 8, 4);
                                    memcpy(&b2, composite + wi * 8 + 4, 4);
                                    memcpy(composite + wi * 8, &b2, 4);
                                    memcpy(composite + wi * 8 + 4, &a, 4);
                                }
                            }
                        }
                    }
                    loaded_texture.addr = composite;
                    /* content-hash the composite so the address-keyed
                     * cache cannot alias two different composites */
                    uint32_t chash = 2166136261u;
                    for (uint32_t cb = 0; cb < n; cb++) {
                        chash = (chash ^ composite[cb]) * 16777619u;
                    }
                    ge_composite_hash = chash;
                }
            }
        }
    }

    const RawTexMetadata* metadata = &loaded_texture.raw_tex_metadata;
    const uint8_t* orig_addr = loaded_texture.addr;
    SUPPORT_CHECK(orig_addr);

    // GE bring-up: every import_* helper computes height as
    // size_bytes / line_size_bytes; a degenerate tile (line 0, seen on the
    // frontend's i8 images) is a hard SIGFPE. Skip the import - the draw
    // uses whatever texture is bound, which is no worse than the pending
    // texture wave's output.
    if (rdp.texture_tile[tile].line_size_bytes == 0 || loaded_texture.size_bytes == 0) {
        static bool warned;
        if (!warned) {
            warned = true;
            fprintf(stderr, "gfx: skipping degenerate texture import (tile %d, fmt %d siz %d line 0)\n",
                    tile, fmt, siz);
        }
        return;
    }

    TextureCacheKey key;
    const uint32_t key_line = loaded_texture.line_size_bytes;
    const uint32_t key_size = loaded_texture.size_bytes;
    if (fmt == G_IM_FMT_CI) {
        /* the TLUT decode FORMAT is part of the identity too (seen on
         * the Depot ground overlay): the same palette bytes decode as greys
         * under G_TT_IA16 but as saturated blues under G_TT_RGBA16 - an
         * entry imported under the wrong draw's mode must not be reused */
        /* Key on only the palette entries the decode reads (all 256 for CI8,
         * block palette_index for CI4), so reloading an unrelated block does
         * not force a re-import. GE_PALKEYOLD=1 keys on the whole palette. */
        static int oldkey = -1;
        if (oldkey < 0) {
            const char* e = getenv("GE_PALKEYOLD");
            oldkey = (e != NULL && atoi(e) != 0);
        }
        if (oldkey) {
            key = { orig_addr, { rdp.palette_addrs[0], rdp.palette_addrs[1] }, fmt, siz, palette_index,
                    key_line, key_size, rdp.palette_sum ^ ge_composite_hash ^ (rdp.palette_fmt * 0x9E3779B1u) };
        } else {
            /* CI8: GE loads partial TLUTs (19-239 entries seen on Dam) and
             * the tail keeps whatever the last other load left, so only the
             * entries the last TMEM-256 load wrote are hashed (indices past
             * them read stale TMEM on the N64 too) */
            if (siz != G_IM_SIZ_4b && !rdp.palette_ci8_hash_ok) {
                uint32_t n = rdp.palette_count256 != 0 ? rdp.palette_count256 : 256u;
                uint32_t h = 2166136261u ^ n;
                for (uint32_t k = 0; k < n; k++) {
                    h = (h ^ rdp.palette[k]) * 16777619u;
                }
                rdp.palette_ci8_hash = h;
                rdp.palette_ci8_hash_ok = true;
            }
            const uint32_t pal = (siz == G_IM_SIZ_4b) ? rdp.palette_block_hash[palette_index & 15u] : rdp.palette_ci8_hash;
            key = { orig_addr, {}, fmt, siz, palette_index,
                    key_line, key_size, pal ^ ge_composite_hash ^ (rdp.palette_fmt * 0x9E3779B1u) };
        }
    } else {
        key = { orig_addr, {}, fmt, siz, palette_index, key_line, key_size, ge_composite_hash };
    }
    ge_composite_hash = 0;
    key.masks = rdp.texture_tile[tile].masks;
    key.maskt = rdp.texture_tile[tile].maskt;

    { /* GE_TEXDBG=1: report address collisions between
       * DIFFERENT geometries - the class that used to silently return
       * another texture's GL image (Control mainframe corruption). With
       * geometry in the key these are now correct separate entries; the
       * log proves the mechanism fired in a given session. */
        static int texdbg = -1;
        if (texdbg < 0) {
            texdbg = getenv("GE_TEXDBG") != NULL;
        }
        if (texdbg) {
            for (const auto& e : gfx_texture_cache.map) {
                if (e.first.texture_addr == orig_addr &&
                    (e.first.line_size_bytes != key_line || e.first.size_bytes != key_size)) {
                    fprintf(stderr, "gfx: texture addr %p reused: cached line %u size %u -> new line %u size %u (fmt %d siz %d)\n",
                            (const void *)orig_addr, e.first.line_size_bytes, e.first.size_bytes,
                            key_line, key_size, fmt, siz);
                    break;
                }
            }
        }
    }

    { /* GE_NOTEXCACHE=1 (diagnostic): evict this key before the lookup so
       * every bind re-decodes from current memory, which tells cache staleness
       * apart from import bugs. It re-uploads every texture every frame. */
        static int nocache = -1;
        if (nocache < 0) {
            nocache = getenv("GE_NOTEXCACHE") != NULL;
        }
        if (nocache) {
            TextureCacheMap::iterator ev = gfx_texture_cache.map.find(key);
            if (ev != gfx_texture_cache.map.end()) {
                for (int t = 0; t < 2; ++t) {
                    if (rendering_state.textures[t] == &*ev) {
                        rendering_state.textures[t] = nullptr;
                        ge_state_serial++;
                    }
                }
                gfx_texture_cache.free_texture_ids.push_back(ev->second.texture_id);
                gfx_texture_cache.lru.erase(ev->second.lru_location);
                ge_texcache_gen++;
                gfx_texture_cache.map.erase(ev);
            }
        }
    }

    if (rendering_state.textures[i] != nullptr && rendering_state.textures[i]->first == key) {
        /* ALREADY BOUND (Xbox twenty-fifth build): a re-statement of the
         * texture the slot holds - no batch split, no lookup, no LRU
         * splice. Before this the flag alone ended the draw: 520 draws for
         * 3,700 triangles. */
        return;
    }
    gfx_flush(); /* the batch so far was drawn with the previous binding */
    if (gfx_texture_cache_lookup(i, key)) {
        return;
    }
    ge_texchurn_note(key, ge_up_evicted);
    struct GeHitchUpload { /* hitchlog.c: a real decode + upload, every exit below */
        const unsigned long long t0 = portProfNow();
        unsigned fmt, siz;
        GeHitchUpload(unsigned f, unsigned z) : fmt(f), siz(z) { g_GfxUpW = 0; }
        ~GeHitchUpload() {
            const unsigned long long us = portProfNow() - t0;
            portHitchNote(4 /* uploads */, us);
            ge_slow_upload_log(us, fmt, siz);
        }
    } ge_hitch_upload(fmt, siz);

    { /* GE bring-up (GE_TRACE_TEX=1): one line per unique texture import -
       * names what each surface actually samples (the flat-world hunt). */
        static int trace = -1;
        if (trace < 0) trace = getenv("GE_TRACE_TEX") != NULL;
        if (trace) {
            fprintf(stderr,
                    "tex: import slot=%d tile=%d fmt=%d siz=%d tmem=%d line=%u size=%u addr=%p num=%d rec=%dx%d lods=%d rfmt=%d rdepth=%d lut=%d off=%d lod=%d ldtmem=%d ldaddr=%p first=%d det=%d h=%d\n",
                    i, tile, fmt, siz, rdp.texture_tile[tile].tmem,
                    rdp.texture_tile[tile].line_size_bytes, loaded_texture.size_bytes,
                    (const void*)loaded_texture.addr, ge_texnum_of(loaded_texture.addr),
                    ge_texrec_of(loaded_texture.addr) ? ge_texrec_of(loaded_texture.addr)->w : -1,
                    ge_texrec_of(loaded_texture.addr) ? ge_texrec_of(loaded_texture.addr)->h : -1,
                    ge_texrec_of(loaded_texture.addr) ? (ge_texrec_of(loaded_texture.addr)->bits0b & 7) : -1,
                    ge_texrec_of(loaded_texture.addr) ? ((ge_texrec_of(loaded_texture.addr)->bits0b >> 3) & 7) : -1,
                    ge_texrec_of(loaded_texture.addr) ? ((ge_texrec_of(loaded_texture.addr)->bits0b >> 6) & 3) : -1,
                    ge_texrec_of(loaded_texture.addr) ? (int)(ge_texrec_of(loaded_texture.addr)->bits0c & 3) : -1,
                    ge_texrec_of(loaded_texture.addr) ? (int)((const unsigned char*)loaded_texture.addr - ge_texrec_of(loaded_texture.addr)->data) : -1,
                    rdp.tex_lod ? 1 : 0,
                    rdp.texture_to_load.tmem, (const void*)rdp.texture_to_load.addr,
                    rdp.first_tile_index, rdp.tex_detail ? 1 : 0,
                    rdp.texture_tile[tile].height);
        }
    }

    if (fmt == G_IM_FMT_RGBA && siz == G_IM_SIZ_16b && rdp.palette_fmt != 0) {
        /* GE water: an RGBA16 tile with the TLUT still enabled; the RDP
         * applies the TLUT to every fetch, using the texel's upper byte as
         * the palette index. TMEM addressing matches import_texture_rgba16. */
        const uint8_t* addr2 = loaded_texture.addr;
        const uint32_t size2 = loaded_texture.size_bytes;
        const uint32_t line2 = rdp.texture_tile[tile].line_size_bytes;
        uint32_t w = line2 / 2;
        uint32_t h = (line2 != 0) ? size2 / line2 : 0;
        if (rdp.texture_tile[tile].masks != 0 && rdp.texture_tile[tile].maskt != 0 &&
            (2u << rdp.texture_tile[tile].masks) != line2) {
            w = 1u << rdp.texture_tile[tile].masks;
            h = 1u << rdp.texture_tile[tile].maskt;
        }
        if (w != 0 && h != 0 && size2 >= 2 && ge_tex_upload_reserve((uint64_t)w * h)) {
            uint8_t *dest = tex_upload_buffer;
            for (uint32_t t = 0; t < h; t++) {
                for (uint32_t s = 0; s < w; s++, dest += 4) {
                    const uint32_t off = (t * line2 + s * 2) % size2;
                    palette_to_rgba32(rdp.palette[addr2[off]], dest);
                }
            }
            gfx_rapi->upload_texture(tex_upload_buffer, w, h);
        }
        return;
    }

    if (fmt == G_IM_FMT_RGBA) {
        if (siz == G_IM_SIZ_16b) {
            import_texture_rgba16(tile, loaded_texture, importReplacement);
        } else if (siz == G_IM_SIZ_32b) {
            import_texture_rgba32(tile, loaded_texture, importReplacement);
        } else {
            sysFatalError("Bad size for RGBA texture in tile %d: %02x", tile, siz);
        }
    } else if (fmt == G_IM_FMT_IA) {
        if (siz == G_IM_SIZ_4b) {
            import_texture_ia4(tile, loaded_texture, importReplacement);
        } else if (siz == G_IM_SIZ_8b) {
            import_texture_ia8(tile, loaded_texture, importReplacement);
        } else if (siz == G_IM_SIZ_16b) {
            import_texture_ia16(tile, loaded_texture, importReplacement);
        } else {
            sysFatalError("Bad size for IA texture in tile %d: %02x", tile, siz);
        }
    } else if (fmt == G_IM_FMT_CI) {
        if (rdp.palette_fmt == G_TT_NONE) {
            /* HARDWARE RULE: a CI tile with the TLUT disabled samples its
             * index bytes as I4/I8 intensity. GE reuses some ground textures
             * this way, so they must not be palette-decoded. */
            if (siz == G_IM_SIZ_4b) {
                import_texture_i4(tile, loaded_texture, importReplacement);
            } else if (siz == G_IM_SIZ_8b) {
                import_texture_i8(tile, loaded_texture, importReplacement);
            } else {
                sysFatalError("Bad size for CI texture in tile %d: %02x", tile, siz);
            }
        } else if (siz == G_IM_SIZ_4b) {
            import_texture_ci4(tile, loaded_texture, importReplacement);
        } else if (siz == G_IM_SIZ_8b) {
            import_texture_ci8(tile, loaded_texture, importReplacement);
        } else {
            sysFatalError("Bad size for CI texture in tile %d: %02x", tile, siz);
        }
    } else if (fmt == G_IM_FMT_I) {
        if (siz == G_IM_SIZ_4b) {
            import_texture_i4(tile, loaded_texture, importReplacement);
        } else if (siz == G_IM_SIZ_8b) {
            import_texture_i8(tile, loaded_texture, importReplacement);
        } else {
            sysFatalError("Bad size for I texture in tile %d: %02x", tile, siz);
        }
    } else {
        sysFatalError("Bad texture format in tile %d: %02x %02x", tile, fmt, siz);
    }

    { /* GE hardening: if the backend flags a failed upload (such as
       * GL_OUT_OF_MEMORY), evict the entry gfx_texture_cache_lookup just made
       * so the next draw re-imports it. The failure is logged, rate-limited. */
        if (g_GfxUploadError != 0) {
            static unsigned logged;
            if (logged < 8 || (g_GfxUploadErrorCount % 256) == 0) {
                logged++;
                fprintf(stderr,
                        "gfx: texture upload FAILED (GL 0x%04x, #%u) fmt=%d siz=%d tile=%d addr=%p - evicting, will retry;"
                        " GPU memory pressure? (another 3D app running?)\n",
                        g_GfxUploadError, g_GfxUploadErrorCount, fmt, siz, tile, loaded_texture.addr);
                fflush(stderr);
            }
            g_GfxUploadError = 0;
            TextureCacheNode* node = rendering_state.textures[i];
            if (node != NULL) {
                gfx_texture_cache.free_texture_ids.push_back(node->second.texture_id);
                gfx_texture_cache.lru.erase(node->second.lru_location);
                ge_texcache_gen++;
                gfx_texture_cache.map.erase(node->first);
                rendering_state.textures[i] = nullptr;
                ge_state_serial++;
                rdp.textures_changed[i] = true;
            }
        }
    }
}

static void gfx_normalize_vector(float v[3]) {
    float s = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    v[0] /= s;
    v[1] /= s;
    v[2] /= s;
}

static void gfx_transposed_matrix_mul(float res[3], const float a[3], const float b[4][4]) {
    res[0] = a[0] * b[0][0] + a[1] * b[0][1] + a[2] * b[0][2];
    res[1] = a[0] * b[1][0] + a[1] * b[1][1] + a[2] * b[1][2];
    res[2] = a[0] * b[2][0] + a[1] * b[2][1] + a[2] * b[2][2];
}

static void calculate_normal_dir(const Light_t* light, float coeffs[3]) {
    const float light_dir[3] = { light->dir[0] / 127.f, light->dir[1] / 127.f, light->dir[2] / 127.f };

    gfx_transposed_matrix_mul(coeffs, light_dir, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1]);
    gfx_normalize_vector(coeffs);
}

static void calculate_normal_dir(const struct NormalColor *vcn, float coeffs[3]) {
    const float light_dir[3] = { vcn->x / 127.f, vcn->y / 127.f, vcn->z / 127.f };

    gfx_transposed_matrix_mul(coeffs, light_dir, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1]);
    gfx_normalize_vector(coeffs);
}

static void gfx_matrix_mul(float res[4][4], const float a[4][4], const float b[4][4]) {
    float tmp[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    memcpy(res, tmp, sizeof(tmp));
}

static void gfx_sp_matrix(uint8_t parameters, const int32_t* addr) {
    if (roc_capturing != nullptr && roc_capturing_kind == 0) {
        roc_capture_bad = true; /* room cache: one MP matrix per list (parts re-run their MTX in the walk) */
        roc_capture_bad_why = "matrix load in a room list";
    }
    roc_mtx_gen++; /* parts: a draw never spans two matrices (thirty-eighth build) */
    if (parameters & G_MTX_PROJECTION) {
        ge_state_serial++; /* the decal pull derives from P */
        rsp_p_gen++;       /* the deferred-XLU eye-space constants derive from P */
    }
    float matrix[4][4];
    extern uint32_t num_dls;

#ifndef GBI_FLOATS
    // Original GBI where fixed point matrices are used
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            int32_t int_part = addr[i * 2 + j / 2];
            uint32_t frac_part = addr[8 + i * 2 + j / 2];
            matrix[i][j] = (int32_t)((int_part & 0xffff0000) | (frac_part >> 16)) / 65536.0f;
            matrix[i][j + 1] = (int32_t)((int_part << 16) | (frac_part & 0xffff)) / 65536.0f;
        }
    }
#else
    // For a modified GBI where fixed point values are replaced with floats
    memcpy(matrix, addr, sizeof(matrix));
#endif

    {
        /* GE bring-up (GE_DUMP_MTX=<task>): print every matrix load for a
         * two-task window starting at <task> - diagnosing garbage joint
         * transforms (e.g. drawjointlist models clipping off-screen). */
        static long dumpmtx = -2;
        if (dumpmtx == -2) {
            const char* e = getenv("GE_DUMP_MTX");
            dumpmtx = e ? atol(e) : -1;
        }
        if (dumpmtx >= 0 && num_dls >= (uint32_t)dumpmtx && num_dls <= (uint32_t)dumpmtx + 1) {
            fprintf(stderr,
                    "gfx: mtx task=%u param=%02x diag=(%.3f,%.3f,%.3f,%.3f) trans=(%.1f,%.1f,%.1f,%.1f)\n",
                    (unsigned)num_dls, parameters,
                    matrix[0][0], matrix[1][1], matrix[2][2], matrix[3][3],
                    matrix[3][0], matrix[3][1], matrix[3][2], matrix[3][3]);
        }
    }

    if (parameters & G_MTX_PROJECTION) {
        if (parameters & G_MTX_LOAD) {
            memcpy(rsp.P_matrix, matrix, sizeof(matrix));
        } else {
            gfx_matrix_mul(rsp.P_matrix, matrix, rsp.P_matrix);
        }
    } else { // G_MTX_MODELVIEW
        if ((parameters & G_MTX_PUSH) && rsp.modelview_matrix_stack_size < 11) {
            ++rsp.modelview_matrix_stack_size;
            memcpy(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1],
                   rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 2], sizeof(matrix));
        }
        if (parameters & G_MTX_LOAD) {
            memcpy(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], matrix, sizeof(matrix));
        } else {
            gfx_matrix_mul(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], matrix,
                           rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1]);
        }
        rsp.lights_changed = 1;
    }
    gfx_matrix_mul(rsp.MP_matrix, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], rsp.P_matrix);
}

static void gfx_sp_pop_matrix(uint32_t count) {
    roc_mtx_gen++;
    while (count--) {
        if (rsp.modelview_matrix_stack_size > 0) {
            --rsp.modelview_matrix_stack_size;
            if (rsp.modelview_matrix_stack_size > 0) {
                gfx_matrix_mul(rsp.MP_matrix, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1],
                               rsp.P_matrix);
            }
        }
    }
}

static float gfx_adjust_x_for_aspect_ratio(float x, float w = 1.f) {
    if (fbActive) {
        return x;
    } else {
        /* twenty-seventh build: one multiply instead of a multiply and a
         * divide per vertex; the ratio moves only with the aspect mode
         * (ulp-level difference from the two-step form) */
        static float k_scale = 0.0f, k_ratio = 0.0f, k = 1.0f;
        if (k_scale != rsp.aspect_scale || k_ratio != gfx_current_dimensions.aspect_ratio) {
            k_scale = rsp.aspect_scale;
            k_ratio = gfx_current_dimensions.aspect_ratio;
            k = k_ratio != 0.0f ? k_scale / k_ratio : 0.0f;
        }
        return (rsp.aspect_ofs * w + x) * k;
    }
}

static void gfx_adjust_width_height_for_scale(uint32_t& width, uint32_t& height) {
    width = std::round(width * RATIO_Y);
    height = std::round(height * RATIO_Y);
    if (width == 0) {
        width = 1;
    }
    if (height == 0) {
        height = 1;
    }
}

static void gfx_sp_vertex(size_t n_vertices, size_t dest_index, const Vtx* vertices) {
    SUPPORT_CHECK(n_vertices <= MAX_VERTICES);

    for (size_t i = 0; i < n_vertices; i++, dest_index++) {
        // GE uses the stock 16-byte F3D Vtx union: Vtx_t (colors) / Vtx_tn
        // (normals) share the position/flag/texcoord layout; bytes 12..15 are
        // RGBA when unlit and normal[3]+alpha when lit. No PD vertex-color
        // palette indirection.
        const Vtx_t* v = &vertices[i].v;
        const Vtx_tn* vn = &vertices[i].n;
        struct LoadedVertex* d = &rsp.loaded_vertices[dest_index];
        if (roc_capturing != nullptr) { /* retained cache: model space + normal survive */
            roc_slot_loaded[dest_index] = 1;
            {
                const uint32_t rel = roc_mtx_gen - roc_capture_gen0;
                roc_slot_gen[dest_index] = rel < 255u ? (uint8_t)rel : 255u;
            }
            roc_model_pos[dest_index][0] = v->ob[0];
            roc_model_pos[dest_index][1] = v->ob[1];
            roc_model_pos[dest_index][2] = v->ob[2];
            roc_model_nrm[dest_index][0] = vn->n[0] / 127.0f;
            roc_model_nrm[dest_index][1] = vn->n[1] / 127.0f;
            roc_model_nrm[dest_index][2] = vn->n[2] / 127.0f;
        }

        float x = v->ob[0] * rsp.MP_matrix[0][0] + v->ob[1] * rsp.MP_matrix[1][0] + v->ob[2] * rsp.MP_matrix[2][0] + rsp.MP_matrix[3][0];
        float y = v->ob[0] * rsp.MP_matrix[0][1] + v->ob[1] * rsp.MP_matrix[1][1] + v->ob[2] * rsp.MP_matrix[2][1] + rsp.MP_matrix[3][1];
        float z = v->ob[0] * rsp.MP_matrix[0][2] + v->ob[1] * rsp.MP_matrix[1][2] + v->ob[2] * rsp.MP_matrix[2][2] + rsp.MP_matrix[3][2];
        float w = v->ob[0] * rsp.MP_matrix[0][3] + v->ob[1] * rsp.MP_matrix[1][3] + v->ob[2] * rsp.MP_matrix[2][3] + rsp.MP_matrix[3][3];

        x = gfx_adjust_x_for_aspect_ratio(x, w);

        short U = v->tc[0] * rsp.texture_scaling_factor.s >> 16;
        short V = v->tc[1] * rsp.texture_scaling_factor.t >> 16;

        if (rsp.geometry_mode & G_LIGHTING) {
            if (rsp.lights_changed) {
                for (int i = 0; i < rsp.current_num_lights - 1; i++) {
                    calculate_normal_dir(&rsp.current_lights[i], rsp.current_lights_coeffs[i]);
                }
                if (rsp.lookat_enabled) {
                    calculate_normal_dir(&rsp.lookat[0], rsp.current_lookat_coeffs[0]);
                    calculate_normal_dir(&rsp.lookat[1], rsp.current_lookat_coeffs[1]);
                }
                rsp.lights_changed = false;
            }

            int r = rsp.current_lights[rsp.current_num_lights - 1].col[0];
            int g = rsp.current_lights[rsp.current_num_lights - 1].col[1];
            int b = rsp.current_lights[rsp.current_num_lights - 1].col[2];

            for (int i = 0; i < rsp.current_num_lights - 1; i++) {
                float intensity = 0;
                intensity += vn->n[0] * rsp.current_lights_coeffs[i][0];
                intensity += vn->n[1] * rsp.current_lights_coeffs[i][1];
                intensity += vn->n[2] * rsp.current_lights_coeffs[i][2];
                intensity /= 127.0f;
                if (intensity > 0.0f) {
                    r += intensity * rsp.current_lights[i].col[0];
                    g += intensity * rsp.current_lights[i].col[1];
                    b += intensity * rsp.current_lights[i].col[2];
                }
            }

            d->color.r = r > 255 ? 255 : r;
            d->color.g = g > 255 ? 255 : g;
            d->color.b = b > 255 ? 255 : b;

            {
                /* GE bring-up (GE_SHADE_WHITE=1): force lit shade to full
                 * white - A/B splits "chr models render black" between
                 * broken lighting (turn textured) and broken texture
                 * binding (stay black). */
                static int shadewhite = -1;
                if (shadewhite < 0) shadewhite = getenv("GE_SHADE_WHITE") != NULL;
                if (shadewhite) {
                    d->color.r = d->color.g = d->color.b = 255;
                }
            }

            if (rsp.geometry_mode & G_TEXTURE_GEN) {
                float dotx = 0, doty = 0;
                if (rsp.lookat_enabled) {
                    dotx += vn->n[0] * rsp.current_lookat_coeffs[0][0];
                    dotx += vn->n[1] * rsp.current_lookat_coeffs[0][1];
                    dotx += vn->n[2] * rsp.current_lookat_coeffs[0][2];
                    doty += vn->n[0] * rsp.current_lookat_coeffs[1][0];
                    doty += vn->n[1] * rsp.current_lookat_coeffs[1][1];
                    doty += vn->n[2] * rsp.current_lookat_coeffs[1][2];
                    dotx /= 127.0f;
                    doty /= 127.0f;
                } else {
                    struct NormalColor nvec;
                    float tvcn[3];
                    nvec.x = vn->n[0];
                    nvec.y = vn->n[1];
                    nvec.z = vn->n[2];
                    nvec.w = 0;
                    calculate_normal_dir(&nvec, tvcn);
                    dotx = tvcn[0];
                    doty = tvcn[1];
                }

                dotx = clampf(dotx, -1.0f, 1.0f);
                doty = clampf(doty, -1.0f, 1.0f);

                if (rsp.geometry_mode & G_TEXTURE_GEN_LINEAR) {
                    // Not sure exactly what formula we should use to get accurate values
                    /*dotx = (2.906921f * dotx * dotx + 1.36114f) * dotx;
                    doty = (2.906921f * doty * doty + 1.36114f) * doty;
                    dotx = (dotx + 1.0f) / 4.0f;
                    doty = (doty + 1.0f) / 4.0f;*/
                    dotx = acosf(-dotx) /* M_PI */ / 4.0f;
                    doty = acosf(-doty) /* M_PI */ / 4.0f;
                } else {
                    dotx = (dotx + 1.0f) / 4.0f;
                    doty = (doty + 1.0f) / 4.0f;
                }

                U = (int32_t)(dotx * rsp.texture_scaling_factor.s);
                V = (int32_t)(doty * rsp.texture_scaling_factor.t);
            }
        } else {
            d->color.r = v->cn[0];
            d->color.g = v->cn[1];
            d->color.b = v->cn[2];
            { /* diagnostic (GE_VTXWHITE=1): force unlit vertex colors to
               * white - splits "texture is tinted" from "shade is tinted"
               * (used to find the blue Depot floor bug) */
                static int vtxwhite = -1;
                if (vtxwhite < 0) vtxwhite = getenv("GE_VTXWHITE") != NULL;
                if (vtxwhite) {
                    d->color.r = d->color.g = d->color.b = 255;
                }
            }
        }

        d->u = U;
        d->v = V;

        // trivial clip rejection
        d->clip_rej = 0;
        if (x < -w) {
            d->clip_rej |= 1; // CLIP_LEFT
        }
        if (x > w) {
            d->clip_rej |= 2; // CLIP_RIGHT
        }
        if (y < -w) {
            d->clip_rej |= 4; // CLIP_BOTTOM
        }
        if (y > w) {
            d->clip_rej |= 8; // CLIP_TOP
        }
        // if (z < -w) d->clip_rej |= 16; // CLIP_NEAR
        /* FAR PLANE: the flag is always computed, but whether an
         * all-three-past-far triangle is rejected is decided per draw in
         * gfx_sp_tri1, depending on the blender mode. On the GL side the
         * triangle draws with GL_DEPTH_CLAMP and a depth range capped just
         * inside the cleared 1.0, so clamped far fragments survive the GL_LESS
         * opaque test (gfx_opengl_set_depth_range). */
        if (z > w) {
            d->clip_rej |= 32; // CLIP_FAR (conditional - masked in gfx_sp_tri1)
        }

        d->x = x;
        d->y = y;
        d->z = z;
        d->w = w;

        if (rsp.geometry_mode & G_FOG) {
            if (fabsf(w) < 0.001f) {
                // To avoid division by zero
                w = 0.001f;
            }

            float winv = 1.0f / w;
            if (winv < 0.0f) {
                winv = std::numeric_limits<int16_t>::max();
            }

            /* GE fog is the raw z/w register form: alpha = clamp(z_ndc *
             * fog_mul + fog_offset) with z_ndc in [-1,1], the documented
             * gSPFogPosition contract given the standard 0x1FF viewport z GE
             * uses (player.c). Props fog separately via the CPU shade tint
             * (fogGetPropDistColor), which evaluates the same curve; see
             * docs/port/fog-and-distance.md. */
            float fog_z = z * winv * rsp.fog_mul + rsp.fog_offset;

            d->fog = clampf(fog_z, 0.f, 255.f);

            { /* GE bring-up (GE_FOG_STATS=1): 1Hz fog-curve probe - prints
               * the farthest fogged vertex (max z/w) and its pre-clamp fog
               * so the fog-vs-cull-radius calibration can be read off a run
               * instead of derived. */
                static int fogstats = -1;
                static float maxzw = -2.0f, fogatmax;
                static uint32_t fogverts;
                if (fogstats < 0) fogstats = getenv("GE_FOG_STATS") != NULL;
                if (fogstats) {
                    float zw = z * winv;
                    static float watmax;
                    if (w > 0.001f && zw > maxzw) {
                        maxzw = zw;
                        fogatmax = fog_z;
                        watmax = w;
                    }
                    if (++fogverts >= 20000) {
                        fprintf(stderr, "fog: mul=%d off=%d maxzw=%f fog@max=%f w@max=%f\n",
                                rsp.fog_mul, rsp.fog_offset, maxzw, fogatmax, watmax);
                        fflush(stderr);
                        fogverts = 0;
                        maxzw = -2.0f;
                    }
                }
            }
        } else {
            d->fog = rdp.fog_color.a;
        }

        d->color.a = v->cn[3]; // aliases n.a on the lit path; can be required for SHADE_ALPHA even if fog is enabled
    }
}

static void gfx_sp_modify_vertex(uint16_t vtx_idx, uint8_t where, uint32_t val) {
    SUPPORT_CHECK(where == G_MWO_POINT_ST);

    int16_t s = (int16_t)(val >> 16);
    int16_t t = (int16_t)val;

    struct LoadedVertex* v = &rsp.loaded_vertices[vtx_idx];
    v->u = s;
    v->v = t;
}

static inline int gfx_lod_tile_offset(const int i) {
    if (gfx_detail_textures_enabled)
        return ((rdp.tex_lod && !rdp.tex_detail) ? 0 : i);
    return (rdp.tex_lod ? rdp.tex_detail : i);
}

/* GE bring-up (GE_GFX_STATS=1): per-frame triangle accounting so an
 * invisible model can be classified: submitted vs clip-rejected vs
 * backface-culled vs handed to the rasterizer. Printed from gfx_run.
 * ge_dbg0..2 are free-form counters game code can bump (PC-gated) to
 * trace where a draw path dies without stdio access. */
extern "C" {
unsigned ge_tris_submitted, ge_tris_cliprej, ge_tris_culled, ge_tris_drawn;
unsigned ge_dbg0, ge_dbg1, ge_dbg2;
}

extern "C" int ge_sky_tri_active;  /* defined with gfx_rdp_triangle below */
static int ge_sky_persw_active;     /* gfx_rdp_triangle: cells carry real clip W -> shade must stay screen-linear (SHADER_OPT_NOPERSP_SHADE) */
static int ge_sky_pixdiv_active;    /* gfx_rdp_triangle: the fragment divides by 1/W (SHADER_OPT_GE_SKY_PIXDIV); the vertex u,v arrive as u*q, v*q */
static float ge_sky_q[3];           /* ...and this is q for the three vertices of the triangle being submitted, in argument order */

/* GE_FARCLIP mode: 2 (unset, default) = console far behaviour gated on fog:
 * fogless draws far-clip (all-out triangles rejected here, spanning triangles
 * cut by the SHADER_OPT_GE_FARCLIP fragment discard), while fog-blended draws
 * rasterise past far at saturated fog. 1 = always reject all-out triangles, 0 =
 * never reject and never discard. */
static int gfx_ge_farclip_mode(void) {
    static int mode = -1;
    if (mode < 0) {
        const char* e = getenv("GE_FARCLIP");
        mode = (e == NULL) ? 2 : ((e[0] != '0') ? 1 : 0);
    }
    return mode;
}

#include "gfx_roomcache.inl"

static void gfx_sp_tri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect) {
    ge_flush_origin = is_rect ? 1u : (ge_tri_origin & 15u);
    roc_stat_tri_origin[ge_flush_origin]++;
    struct LoadedVertex* v1 = &rsp.loaded_vertices[vtx1_idx];
    struct LoadedVertex* v2 = &rsp.loaded_vertices[vtx2_idx];
    struct LoadedVertex* v3 = &rsp.loaded_vertices[vtx3_idx];
    struct LoadedVertex* v_arr[3] = { v1, v2, v3 };
    /* room cache capture: a rejected / culled triangle still runs the state
     * and emission path so it can be recorded, then its floats are dropped */
    bool roc_drop = false;

    ge_tris_submitted++;

    /* deferred-XLU window is controlled ONLY by the explicit DL markers
     * (G_DXLU_NEWVIEW_EXT opens at bgLevelRender, G_DXLU_WORLDEND_EXT
     * flushes+closes before the gun). Render-state heuristics proved
     * unreliable: the gun z-compares (a Z_CMP trigger fires too late and
     * near glass paints over the weapon) and no-z world effects like guard
     * muzzle flashes drop Z_CMP mid-world (fires too early, unsorting the
     * remaining rooms). */

    if ((rsp.extra_geometry_mode & G_NO_CLIPPING_EXT) == 0) {
        const uint8_t rej = v1->clip_rej & v2->clip_rej & v3->clip_rej;
        bool reject = (rej & ~32u) != 0;
        if (!reject && (rej & 32u)) {
            /* All three verts are past the far plane: reject only when the
             * blender takes no fog, since the N64 still draws fogged far
             * geometry in pure fog colour. GE_FARCLIP=1 always rejects and
             * GE_FARCLIP=0 never does. */
            const int farclip = gfx_ge_farclip_mode();
            if (farclip == 1) {
                reject = true;
            } else if (farclip == 2) {
                const bool blender_fog = ((rdp.other_mode_l >> 30) == G_BL_CLR_FOG) ||
                                         ((rdp.other_mode_l >> 26) == G_BL_A_FOG);
                reject = !blender_fog;
            }
        }
        if (reject) {
            // The whole triangle lies outside the visible area
            ge_tris_cliprej++;
            {
                /* GE bring-up (GE_GFX_STATS): sample the first rejected tris
                 * per task - which plane(s) and where the verts actually
                 * land. Diagnosing whole-model clip rejection. */
                static int dump = -1;
                if (dump < 0) dump = getenv("GE_GFX_STATS") != NULL;
                if (dump && (ge_tris_cliprej <= 2 ||
                             (ge_tris_cliprej >= 100 && ge_tris_cliprej <= 102))) {
                    fprintf(stderr,
                            "gfx: cliprej flags=%x/%x/%x v1=(%.1f,%.1f,%.1f,%.1f) v2=(%.1f,%.1f,%.1f,%.1f) v3=(%.1f,%.1f,%.1f,%.1f)\n",
                            v1->clip_rej, v2->clip_rej, v3->clip_rej,
                            v1->x, v1->y, v1->z, v1->w, v2->x, v2->y, v2->z, v2->w,
                            v3->x, v3->y, v3->z, v3->w);
                }
            }
            if (roc_capturing == nullptr) {
                return;
            }
            roc_drop = true;
        }
    }

    if (!roc_drop && (rsp.geometry_mode & G_CULL_BOTH) != 0) {
        /* Only the sign of the projected cross product matters, so evaluate it
         * without the six divides: cross = N / (w1 w2^2 w3) with N the
         * determinant below, hence sign(cross) = sign(N) * sign(w1 w3). The
         * parity flip that follows is applied on top as before. */
        const float w1 = v1->w, w2 = v2->w, w3 = v3->w;
        const float ax = v1->x * w2 - v2->x * w1, ay = v1->y * w2 - v2->y * w1;
        const float bx = v3->x * w2 - v2->x * w3, by = v3->y * w2 - v2->y * w3;
        float cross = ax * by - ay * bx;
        if (w1 * w3 < 0.0f) {
            cross = -cross;
        }

        if ((v1->w < 0) ^ (v2->w < 0) ^ (v3->w < 0)) {
            // If one vertex lies behind the eye, negating cross will give the correct result.
            // If all vertices lie behind the eye, the triangle will be rejected anyway.
            cross = -cross;
        }

        // If inverted culling is requested, negate the cross
        // if ((rsp.extra_geometry_mode & G_EX_INVERT_CULLING) == 1) {
        //     cross = -cross;
        // }

        switch (rsp.geometry_mode & G_CULL_BOTH) {
            case G_CULL_FRONT:
                if (cross <= 0) {
                    ge_tris_culled++;
                    if (roc_capturing == nullptr) {
                        return;
                    }
                    roc_drop = true;
                    break;
                }
                break;
            case G_CULL_BACK:
                if (cross >= 0) {
                    ge_tris_culled++;
                    if (roc_capturing == nullptr) {
                        return;
                    }
                    roc_drop = true;
                    break;
                }
                break;
            case G_CULL_BOTH:
                // Why is this even an option?
                ge_tris_culled++;
                if (roc_capturing == nullptr) {
                    return;
                }
                roc_drop = true;
                break;
        }
    }
    ge_tris_drawn++;

    { /* GE HW pacing: this triangle's RDP fill at the console's 320x240 (the
       * emitted x carries the port's aspect pin: undone; the visible share of
       * the bounding box stands in for clipping). Rects come through here too
       * (is_rect) and go to their own bucket. */
        const float w1 = v1->w, w2 = v2->w, w3 = v3->w;
        if (w1 > 0.0f && w2 > 0.0f && w3 > 0.0f) {
            const float x1 = v1->x / w1, y1 = v1->y / w1, x2 = v2->x / w2, y2 = v2->y / w2, x3 = v3->x / w3, y3 = v3->y / w3;
            float area = 0.5f * fabsf((x2 - x1) * (y3 - y1) - (y2 - y1) * (x3 - x1));
            const float bx0 = fminf(fminf(x1, x2), x3), bx1 = fmaxf(fmaxf(x1, x2), x3);
            const float by0 = fminf(fminf(y1, y2), y3), by1 = fmaxf(fmaxf(y1, y2), y3);
            const float bb = (bx1 - bx0) * (by1 - by0);
            if (bb > 1e-9f) {
                const float cx0 = fmaxf(bx0, -1.0f), cx1 = fminf(bx1, 1.0f), cy0 = fmaxf(by0, -1.0f), cy1 = fminf(by1, 1.0f);
                const float cb = (cx1 > cx0 && cy1 > cy0) ? (cx1 - cx0) * (cy1 - cy0) : 0.0f;
                area *= cb / bb;
            }
            const float undo = (!fbActive && rsp.aspect_scale != 0.0f) ? gfx_current_dimensions.aspect_ratio / rsp.aspect_scale : 1.0f;
            const uint32_t px = (uint32_t)(area * undo * 19200.0f); /* NDC 2x2 = 320x240 */
            if (is_rect) {
                g_PortHwLoad.fillr += px;
            } else {
                if ((rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_2CYCLE) g_PortHwLoad.fill2 += px;
                else g_PortHwLoad.fill1 += px;
                if (((rdp.other_mode_l >> 10) & 3u) == 2u) g_PortHwLoad.fillx += px; /* ZMODE_XLU */
                g_PortHwLoad.tris++;
            }
        }
    }

    { /* GE_FARDBG=1: census of triangles drawn entirely past the far plane (z/w
       * > 1 on all three verts), which are the ones the far-clip change now
       * rasterises. It prints one line per unique state (fog inputs and
       * per-vertex fog alphas) to catch beyond-far draws that are not saturated
       * to the fog colour. */
        static int fardbg = -1;
        if (fardbg < 0) fardbg = getenv("GE_FARDBG") != NULL;
        if (fardbg && v1->w > 0.001f && v2->w > 0.001f && v3->w > 0.001f &&
            v1->z > v1->w && v2->z > v2->w && v3->z > v3->w) {
            static uint64_t seen[32];
            static int nseen;
            uint64_t sig = ((uint64_t)rdp.other_mode_l << 32) |
                           ((rsp.geometry_mode & G_FOG) ? 1u : 0u) |
                           ((uint64_t)rdp.fog_color.a << 8) | ((uint64_t)rdp.fog_color.r << 16);
            int k;
            for (k = 0; k < nseen; k++) if (seen[k] == sig) break;
            if (k == nseen && nseen < 32) {
                seen[nseen++] = sig;
                fprintf(stderr,
                        "fardbg: omode_l=%08x omode_h=%08x geomfog=%d fogmul=%d fogoff=%d fogcol=(%d,%d,%d,%d) cc=%016llx zw=(%.3f,%.3f,%.3f) va=(%d,%d,%d) ndc=(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f)\n",
                        rdp.other_mode_l, rdp.other_mode_h, (rsp.geometry_mode & G_FOG) ? 1 : 0,
                        (int)rsp.fog_mul, (int)rsp.fog_offset,
                        rdp.fog_color.r, rdp.fog_color.g, rdp.fog_color.b, rdp.fog_color.a,
                        (unsigned long long)rdp.combine_mode,
                        v1->z / v1->w, v2->z / v2->w, v3->z / v3->w,
                        (int)v1->fog, (int)v2->fog, (int)v3->fog,
                        v1->x / v1->w, v1->y / v1->w, v2->x / v2->w, v2->y / v2->w, v3->x / v3->w, v3->y / v3->w);
                fflush(stderr);
            }
        }
    }

    { /* GE_NDCPROBE=x0,x1,y0,y1 (diag): log EVERY rasterised tri whose NDC
       * bbox lies inside the region - identify a mystery surface on screen
       * (Jungle olive band hunt). Opaque and XLU alike, once per unique
       * state+texture per run. */
        static float pr[4];
        static int probe = -1;
        if (probe < 0) {
            const char* e = getenv("GE_NDCPROBE");
            probe = 0;
            if (e && sscanf(e, "%f,%f,%f,%f", &pr[0], &pr[1], &pr[2], &pr[3]) == 4) {
                probe = 1;
            }
        }
        static long probefrom = -1;
        if (probefrom < 0) {
            const char* e = getenv("GE_NDCPROBE_FROM");
            probefrom = (e && e[0]) ? atol(e) : 0;
        }
        {
            extern uint32_t num_dls;
            if (probe && (long)num_dls < probefrom) {
                goto ndcprobe_done;
            }
        }
        if (probe && v1->w > 0.001f && v2->w > 0.001f && v3->w > 0.001f) {
            float bx0 = 1e30f, bx1 = -1e30f, by0 = 1e30f, by1 = -1e30f;
            const struct LoadedVertex* vv[3] = { v1, v2, v3 };
            for (int k = 0; k < 3; k++) {
                const float sx = vv[k]->x / vv[k]->w, sy = vv[k]->y / vv[k]->w;
                if (sx < bx0) bx0 = sx;
                if (sx > bx1) bx1 = sx;
                if (sy < by0) by0 = sy;
                if (sy > by1) by1 = sy;
            }
            static int loose = -1;
            static float wmax = 0.0f;
            if (loose < 0) {
                const char* e = getenv("GE_NDCPROBE_LOOSE");
                loose = (e && e[0] != '0') ? 1 : 0;
                e = getenv("GE_NDCPROBE_WMAX");
                wmax = e ? (float)atof(e) : 0.0f;
            }
            bool hit;
            if (loose) {
                hit = !(bx1 < pr[0] || bx0 > pr[1] || by1 < pr[2] || by0 > pr[3]);
            } else {
                hit = bx0 >= pr[0] && bx1 <= pr[1] && by0 >= pr[2] && by1 <= pr[3];
            }
            if (hit && wmax > 0.0f && (v1->w > wmax || v2->w > wmax || v3->w > wmax)) {
                hit = false;
            }
            if (hit) {
                static uint64_t seen[64];
                static int nseen;
                const void* ta = rendering_state.textures[0] ? (void*)rendering_state.textures[0]->first.texture_addr : NULL;
                uint64_t sig = ((uint64_t)rdp.other_mode_l << 32) ^ (uint64_t)(uintptr_t)ta;
                int k;
                for (k = 0; k < nseen; k++) if (seen[k] == sig) break;
                if (k == nseen && nseen < 64) {
                    seen[nseen++] = sig;
                    fprintf(stderr,
                            "ndcprobe: omode_l=%08x omode_h=%08x cc=%016llx geom=%08x tex=%p fmt=%d siz=%d tlut=%d w=(%.1f %.1f %.1f) z/w=(%.3f %.3f %.3f) ndc=(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f) rgba0=%02x%02x%02x%02x\n",
                            rdp.other_mode_l, rdp.other_mode_h, (unsigned long long)rdp.combine_mode, rsp.geometry_mode, ta,
                            rdp.texture_tile[rdp.first_tile_index].fmt, rdp.texture_tile[rdp.first_tile_index].siz,
                            (int)((rdp.other_mode_h >> G_MDSFT_TEXTLUT) & 3),
                            v1->w, v2->w, v3->w, v1->z / v1->w, v2->z / v2->w, v3->z / v3->w,
                            v1->x / v1->w, v1->y / v1->w, v2->x / v2->w, v2->y / v2->w, v3->x / v3->w, v3->y / v3->w,
                            v1->color.r, v1->color.g, v1->color.b, v1->color.a);
                    fflush(stderr);
                }
            }
        }
ndcprobe_done:;
    }

    if (rdp.viewport_or_scissor_changed) {
        if (memcmp(&rdp.viewport, &rendering_state.viewport, sizeof(rdp.viewport)) != 0) {
            gfx_flush();
            gfx_rapi->set_viewport(rdp.viewport.x, rdp.viewport.y, rdp.viewport.width, rdp.viewport.height);
            rendering_state.viewport = rdp.viewport;
        }
        if (memcmp(&rdp.scissor, &rendering_state.scissor, sizeof(rdp.scissor)) != 0) {
            gfx_flush();
            gfx_rapi->set_scissor(rdp.scissor.x, rdp.scissor.y, rdp.scissor.width, rdp.scissor.height);
            rendering_state.scissor = rdp.scissor;
        }
        rdp.viewport_or_scissor_changed = false;
    }
    static float ge_dec_pull = -1.0f;
    if (ge_dec_pull < 0.0f) {
        const char* e = getenv("GE_DEC_PULL");
        ge_dec_pull = e ? (float)atof(e) : 2.0f;
        if (ge_dec_pull < 0.0f) ge_dec_pull = 0.0f;
    }

    /* ---- per-triangle state hoist (ge_state_serial, gfx_tri_hoist) ---- */
    static struct {
        unsigned serial;
        uint32_t fp;
        bool depth_test, depth_update, depth_compare, depth_source_prim;
        uint16_t zmode;
        uint8_t depth_mode;
        bool use_alpha, use_fog, use_pixfog, use_grayscale, use_modulate;
        ColorCombiner* comb;
        uint32_t tm;
        uint32_t tex_width[2], tex_height[2], tex_width2[2], tex_height2[2];
        struct ShaderProgram* prg;
        uint8_t num_inputs;
        bool used_textures[2];
        struct GfxClipParameters clip_parameters;
        bool dec_pull;
        float dec_pull_b;
    } ts;
    const uint32_t ts_fp = (is_rect ? 1u : 0u) | (s_DrawingRect ? 2u : 0u) | (ge_sky_tri_active ? 4u : 0u)
        | (ge_sky_persw_active ? 8u : 0u) | (fbActive ? 16u : 0u) | (ge_sky_pixdiv_active ? 32u : 0u)
        /* TWO bits: mode 2 ("trilinear") mips draws mode 1 does not, so
         * collapsing 1 and 2 to one bit would let a hoisted per-triangle
         * state survive a bilinear<->trilinear switch unchanged. */
        | ((gfx_mipmaps & 3u) << 7) | (gfx_filter_2d ? 64u : 0u)
        /* the texrect's UV span decides its wrap mode (the "one copy of its
         * texture" override below), so two rects on one texture and one tile
         * - a tiling strip, then an icon - must not share a hoisted state */
        | (s_RectGlyphBorder ? 256u << 23 : 0u)
        | (s_RectUvValid
               ? ((((((uint32_t) s_RectUvS0 * 31u + (uint32_t) s_RectUvS1) * 31u + (uint32_t) s_RectUvT0) * 31u
                    + (uint32_t) s_RectUvT1) << 9) | 512u)
               : 0u);
    const bool ts_recompute = gfx_tri_hoist == 0 || ts.prg == NULL || ts.serial != ge_state_serial || ts.fp != ts_fp
        || rdp.textures_changed[0] || rdp.textures_changed[1];
    bool depth_test, depth_update, depth_compare, depth_source_prim;
    uint16_t zmode;
    uint8_t depth_mode;
    bool use_alpha, use_fog, use_pixfog, use_grayscale, use_modulate;
    ColorCombiner* comb;
    uint32_t tm;
    uint32_t tex_width[2], tex_height[2], tex_width2[2], tex_height2[2];
    struct ShaderProgram* prg;
    uint8_t num_inputs;
    bool used_textures[2];
    struct GfxClipParameters clip_parameters;
    bool dec_pull;
    float dec_pull_b;
    if (!ts_recompute) {
        depth_test = ts.depth_test; depth_update = ts.depth_update; depth_compare = ts.depth_compare;
        depth_source_prim = ts.depth_source_prim; zmode = ts.zmode; depth_mode = ts.depth_mode;
        use_alpha = ts.use_alpha; use_fog = ts.use_fog; use_pixfog = ts.use_pixfog;
        use_grayscale = ts.use_grayscale; use_modulate = ts.use_modulate;
        comb = ts.comb; tm = ts.tm;
        for (int i = 0; i < 2; i++) {
            tex_width[i] = ts.tex_width[i]; tex_height[i] = ts.tex_height[i];
            tex_width2[i] = ts.tex_width2[i]; tex_height2[i] = ts.tex_height2[i];
            used_textures[i] = ts.used_textures[i];
        }
        prg = ts.prg; num_inputs = ts.num_inputs; clip_parameters = ts.clip_parameters;
        dec_pull = ts.dec_pull; dec_pull_b = ts.dec_pull_b;
    } else {
    depth_test = ((rsp.geometry_mode & G_ZBUFFER) == G_ZBUFFER || (rdp.other_mode_l & G_ZS_PRIM) == G_ZS_PRIM) &&
                      ((rdp.other_mode_h & G_CYC_1CYCLE) == G_CYC_1CYCLE || (rdp.other_mode_h & G_CYC_2CYCLE) == G_CYC_2CYCLE);
    depth_update = (rdp.other_mode_l & Z_UPD) == Z_UPD;
    depth_compare = (rdp.other_mode_l & Z_CMP) == Z_CMP;
    depth_source_prim = (rdp.other_mode_l & G_ZS_PRIM) == G_ZS_PRIM /* && gDP.primDepth.z == 1.0f */;
    zmode = rdp.other_mode_l & ZMODE_DEC;
    depth_mode = (depth_test ? 1 : 0) | (depth_update ? 2 : 0) | (depth_compare ? 4 : 0) | (depth_source_prim ? 8 : 0) | (zmode >> 6);

    if (depth_mode != rendering_state.depth_mode) {
        gfx_flush();
        gfx_rapi->set_depth_mode(depth_test, depth_update, depth_compare, depth_source_prim, zmode);
        rendering_state.depth_mode = depth_mode;
    }


    uint64_t cc_options = 0;
    use_alpha =
        (rdp.other_mode_l & (3 << 20)) == (G_BL_CLR_MEM << 20) && (rdp.other_mode_l & (3 << 16)) == (G_BL_1MA << 16);
    use_fog = ((rdp.other_mode_l >> 30) == G_BL_CLR_FOG) || ((rdp.other_mode_l >> 26) == G_BL_A_FOG);
    /* GE per-pixel fog applies only to G_FOG draws, whose fog curve the
     * fragment can recompute; prop draws carry a CPU-made constant alpha that
     * must be left alone. GE_NOPIXFOG=1 uses vertex fog instead. */
    static int s_NoPixFog = -1;
    if (s_NoPixFog < 0) {
        const char* e = getenv("GE_NOPIXFOG");
        s_NoPixFog = (e != NULL && e[0] != '0') ? 1 : 0;
    }
    use_pixfog = use_fog && !s_NoPixFog && !fbActive && !ge_sky_tri_active &&
                            (rsp.geometry_mode & G_FOG) != 0;

    { /* GE_FOG_STATS=1: fog-coverage census of draws where the RSP computed fog
       * (G_FOG geometry) but the blender check drops it, and vice versa. It
       * logs each unique other_mode_l once per direction. */
        static int fogstats = -1;
        if (fogstats < 0) fogstats = getenv("GE_FOG_STATS") != NULL;
        if (fogstats) {
            static uint32_t seen[16];
            static int seencount = 0;
            uint32_t sig = (rdp.other_mode_l & 0xFFFF0000u) | ((rsp.geometry_mode & G_FOG) ? 1 : 0) | (use_fog ? 2 : 0);
            bool logged = false;
            for (int si = 0; si < seencount; si++) {
                if (seen[si] == sig) { logged = true; break; }
            }
            if (!logged && seencount < 16) {
                seen[seencount++] = sig;
                if (((sig & 1) && !(sig & 2)) || (!(sig & 1) && (sig & 2))) {
                    fprintf(stderr, "fogcov: other_mode_l=%08x geomfog=%d blenderfog=%d\n",
                            rdp.other_mode_l, (sig & 1) ? 1 : 0, (sig & 2) ? 1 : 0);
                    fflush(stderr);
                }
            }
        }
    }
    const bool texture_edge = (rdp.other_mode_l & CVG_X_ALPHA) == CVG_X_ALPHA;
    const bool use_noise = (rdp.other_mode_l & (3U << G_MDSFT_ALPHACOMPARE)) == G_AC_DITHER;
    const bool use_2cyc = (rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_2CYCLE;
    const bool alpha_threshold = (rdp.other_mode_l & (3U << G_MDSFT_ALPHACOMPARE)) == G_AC_THRESHOLD;
    const bool invisible = (rdp.other_mode_l & (3 << 24)) == (G_BL_0 << 24) && (rdp.other_mode_l & (3 << 20)) == (G_BL_CLR_MEM << 20);
    use_grayscale = rdp.grayscale;
    use_modulate = use_alpha && (rsp.extra_geometry_mode & G_MODULATE_EXT) != 0;
    const bool use_blur = (rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) == G_TF_BLUR_EXT;

    if (texture_edge) {
        use_alpha = true;
    }

    /* GE: ALPHA_CVG_SEL without CVG_X_ALPHA makes the blender alpha coverage
     * (see gfx_opengl.cpp), which matters when the A mux is A_IN and we would
     * otherwise alpha-blend. GE_NOCVGSEL=1 blends by combiner alpha instead. */
    static int s_NoCvgSel = -1;
    if (s_NoCvgSel < 0) s_NoCvgSel = getenv("GE_NOCVGSEL") != NULL;
    const bool alpha_cvg_sel = use_alpha && !texture_edge && !s_NoCvgSel &&
                               (rdp.other_mode_l & ALPHA_CVG_SEL) != 0 &&
                               ((rdp.other_mode_l >> 24) & 3) == G_BL_A_IN;
    { /* GE_LIGHTDBG: census of the modes the rule touches, each once */
        static int ldbg2 = -1;
        if (ldbg2 < 0) ldbg2 = getenv("GE_LIGHTDBG") != NULL;
        if (ldbg2 && use_alpha && !texture_edge && (rdp.other_mode_l & ALPHA_CVG_SEL) != 0) {
            static uint32_t seen[64];
            static int nseen;
            int k;
            for (k = 0; k < nseen; k++) if (seen[k] == rdp.other_mode_l) break;
            if (k == nseen && nseen < 64) {
                /* g_PortFramesPumped: extern "C" at file scope */
                seen[nseen++] = rdp.other_mode_l;
                fprintf(stderr, "lightdbg: cvgsel-mode omode_l=%08x omode_h=%08x cc=%016llx amux=%d geom=%08x tile0fmt=%d siz=%d -> %s frame=%u ndc=(%.2f,%.2f) c1a=%02x\n",
                        rdp.other_mode_l, rdp.other_mode_h, (unsigned long long)rdp.combine_mode,
                        (int)((rdp.other_mode_l >> 24) & 3), rsp.geometry_mode,
                        rdp.texture_tile[rdp.first_tile_index].fmt, rdp.texture_tile[rdp.first_tile_index].siz,
                        alpha_cvg_sel ? "OPAQUE" : "blend", g_PortFramesPumped,
                        v1->w != 0.0f ? v1->x / v1->w : 0.0f, v1->w != 0.0f ? v1->y / v1->w : 0.0f, v1->color.a);
            }
        }
    }

    if (use_alpha) {
        cc_options |= (uint64_t)SHADER_OPT_ALPHA;
    }
    if (use_fog) {
        cc_options |= (uint64_t)SHADER_OPT_FOG;
    }
    if (texture_edge) {
        cc_options |= (uint64_t)SHADER_OPT_TEXTURE_EDGE;
    }
    if (use_noise) {
        cc_options |= (uint64_t)SHADER_OPT_NOISE;
    }
    if (use_2cyc) {
        cc_options |= (uint64_t)SHADER_OPT_2CYC;
    }
    if (alpha_threshold) {
        cc_options |= (uint64_t)SHADER_OPT_ALPHA_THRESHOLD;
    }
    if (invisible) {
        cc_options |= (uint64_t)SHADER_OPT_INVISIBLE;
    }
    if (use_grayscale) {
        cc_options |= (uint64_t)SHADER_OPT_GRAYSCALE;
    }
    if (use_blur) {
        cc_options |= (uint64_t)SHADER_OPT_BLUR;
    }
    if (alpha_cvg_sel) {
        cc_options |= (uint64_t)SHADER_OPT_ALPHA_CVG_SEL;
    }
    if (ge_sky_tri_active && ge_sky_persw_active) {
        /* Persw cells have real W, so the GPU would perspective-correct the
         * shade too and the fade-to-fog would collapse into flat-then-snap
         * bands per cell. The RDP interpolates shade screen-linearly, so ask
         * the GL backend for noperspective colour varyings. */
        cc_options |= (uint64_t)SHADER_OPT_NOPERSP_SHADE;
    }
    if (ge_sky_tri_active && ge_sky_pixdiv_active) {
        cc_options |= (uint64_t)SHADER_OPT_GE_SKY_PIXDIV;
    }
    if (ge_sky_tri_active) {
        /* RGBA5551 truncation for the sky/water draws: the RDP writes into a
         * 16-bit framebuffer, which truncates 8-bit colour to 5 bits, so the
         * faint near-horizon cloud gradient (under 8/255) collapses to the sky
         * colour on hardware. This quantises the sky output the same way; it is
         * opt-in via GE_SKY_RGB5=1 because truncation without the RDP's dither
         * bands bright skies. */
        static int rgb5 = -1;
        if (rgb5 < 0) {
            const char* e = getenv("GE_SKY_RGB5");
            rgb5 = (e != NULL && e[0] != '0') ? 1 : 0; /* opt-in: truncation without the RDP's dither bands bright skies */
        }
        if (rgb5) {
            cc_options |= (uint64_t)SHADER_OPT_SKY_RGB5;
        }
    }
    if (!use_fog && !ge_sky_tri_active && !fbActive &&
        (rsp.extra_geometry_mode & G_NO_CLIPPING_EXT) == 0 && gfx_ge_farclip_mode() != 0) {
        /* Far clip for fogless draws: the all-out reject above cannot touch a
         * triangle that spans the far plane, whose beyond-far part would
         * rasterise depth-clamped and visibly pale without fog in the blender.
         * The console clips such geometry at the far plane, so this emulates it
         * with a fragment discard where clip z > w; fog-blended draws never
         * take this option, and it is a no-op for 2D. */
        cc_options |= (uint64_t)SHADER_OPT_GE_FARCLIP;
    }
    if (use_pixfog) {
        cc_options |= (uint64_t)SHADER_OPT_GE_PIXFOG;
    }

    { /* GE_LIGHTDBG: trace the RDP state applied to darkened light-fixture
       * vertices (cn = colour>>2, alpha 0x3f after one break). Keyed on the
       * exact post-darken alpha so only the broken fixture's tris log. */
        static int ldbg = -1;
        static int ldbg_n = 0;
        if (ldbg < 0) ldbg = getenv("GE_LIGHTDBG") != NULL;
        if (ldbg && ldbg_n < 40 &&
            (v1->color.a == 0x3f || v2->color.a == 0x3f || v3->color.a == 0x3f)) {
            ldbg_n++;
            fprintf(stderr,
                    "lightdbg: tri omode_l=%08x omode_h=%08x cc=%016llx use_alpha=%d texedge=%d athresh=%d fog=%d noise=%d 2cyc=%d "
                    "c1=(%02x %02x %02x %02x) c2=(%02x %02x %02x %02x) c3=(%02x %02x %02x %02x) fog1=%d geom=%08x tile0fmt=%d siz=%d\n",
                    rdp.other_mode_l, rdp.other_mode_h, (unsigned long long)rdp.combine_mode,
                    (int)use_alpha, (int)texture_edge, (int)alpha_threshold, (int)use_fog, (int)use_noise, (int)use_2cyc,
                    v1->color.r, v1->color.g, v1->color.b, v1->color.a,
                    v2->color.r, v2->color.g, v2->color.b, v2->color.a,
                    v3->color.r, v3->color.g, v3->color.b, v3->color.a,
                    v1->fog, rsp.geometry_mode, rdp.texture_tile[rdp.first_tile_index].fmt, rdp.texture_tile[rdp.first_tile_index].siz);
        }
    }
    // If we are not using alpha, clear the alpha components of the combiner as they have no effect
    if (!use_alpha) {
        cc_options &= ~((0xfff << 16) | ((uint64_t)0xfff << 44));
    }

    ColorCombinerKey key;
    key.combine_mode = rdp.combine_mode;
    key.options = cc_options;

    comb = gfx_lookup_or_create_color_combiner(key);

    tm = 0;

    for (int i = 0; i < 2; i++) {
        // TODO: fix this; for now just ignore smaller mips
        const uint32_t tile = rdp.first_tile_index + gfx_lod_tile_offset(i);
        if (comb->used_textures[i]) {
            if (ge_texlog_now()) {
                fprintf(stderr, "texlog: BIND cc=%016llx primm=%u priml=%u i=%d tile=%u fmt=%u siz=%u tmem=%u line=%u h=%u addr=%p changed=%d palsum=%08x lod=%d det=%d lut=%u\n",
                        (unsigned long long) rdp.combine_mode, (unsigned) rdp.prim_min_level, (unsigned) rdp.prim_lod_fraction,
                        i, tile, rdp.texture_tile[tile].fmt, rdp.texture_tile[tile].siz,
                        rdp.texture_tile[tile].tmem, rdp.texture_tile[tile].line_size_bytes,
                        rdp.texture_tile[tile].height,
                        rdp.loaded_texture[rdp.texture_tile[tile].tmem].addr,
                        rdp.textures_changed[i] ? 1 : 0, rdp.palette_sum,
                        rdp.tex_lod ? 1 : 0, rdp.tex_detail ? 1 : 0,
                        rdp.palette_fmt >> G_MDSFT_TEXTLUT);
            }
            if (rdp.textures_changed[i]) {
                import_texture(i, tile, false); /* flushes itself, only when the binding really changes */
                rdp.textures_changed[i] = false;
            }

            uint8_t cms = rdp.texture_tile[tile].cms;
            uint8_t cmt = rdp.texture_tile[tile].cmt;

            uint32_t tex_size_bytes = rdp.loaded_texture[rdp.texture_tile[tile].tmem].orig_size_bytes;
            uint32_t line_size = rdp.texture_tile[tile].line_size_bytes;

            if (line_size == 0) {
                line_size = 1;
            }

            tex_height[i] = tex_size_bytes / line_size;
            switch (rdp.texture_tile[tile].siz) {
                case G_IM_SIZ_4b:
                    line_size <<= 1;
                    break;
                case G_IM_SIZ_8b:
                    break;
                case G_IM_SIZ_16b:
                    line_size /= G_IM_SIZ_16b_LINE_BYTES;
                    break;
                case G_IM_SIZ_32b:
                    line_size /= G_IM_SIZ_32b_LINE_BYTES; // this is 2!
                    tex_height[i] /= 2;
                    break;
            }
            tex_width[i] = line_size;

            /* GE narrow tiles: match ge_upload_masked - a wrap mask narrower
             * than the decoded image is the texture's real size */
            if (ge_maskcrop_enabled()) {
                if (rdp.texture_tile[tile].masks != 0 && (1u << rdp.texture_tile[tile].masks) < tex_width[i]) {
                    tex_width[i] = 1u << rdp.texture_tile[tile].masks;
                }
                if (rdp.texture_tile[tile].maskt != 0 && (1u << rdp.texture_tile[tile].maskt) < tex_height[i]) {
                    tex_height[i] = 1u << rdp.texture_tile[tile].maskt;
                }
            }

            /* mask-wrapped tiles, any format: ge_upload_masked rebuilt the
             * texture at mask size (Dam's water) - the UVs must match */
            if (ge_maskwrap_tile(tile, tex_width[i])) {
                tex_width[i] = 1u << rdp.texture_tile[tile].masks;
                tex_height[i] = 1u << rdp.texture_tile[tile].maskt;
            }

            /* mask-driven RGBA16 tiles (GE water): the importer rebuilt the
             * texture at mask dimensions via TMEM addressing - the UV
             * normalization must match (see import_texture_rgba16). */
            if (rdp.texture_tile[tile].fmt == G_IM_FMT_RGBA &&
                rdp.texture_tile[tile].siz == G_IM_SIZ_16b &&
                rdp.texture_tile[tile].masks != 0 && rdp.texture_tile[tile].maskt != 0 &&
                (2u << rdp.texture_tile[tile].masks) != rdp.texture_tile[tile].line_size_bytes) {
                tex_width[i] = 1u << rdp.texture_tile[tile].masks;
                tex_height[i] = 1u << rdp.texture_tile[tile].maskt;
            }

            tex_width2[i] = (rdp.texture_tile[tile].lrs - rdp.texture_tile[tile].uls + 4) / 4;
            tex_height2[i] = (rdp.texture_tile[tile].lrt - rdp.texture_tile[tile].ult + 4) / 4;

            uint32_t tex_width1 = tex_width[i] << (cms & G_TX_MIRROR);
            uint32_t tex_height1 = tex_height[i] << (cmt & G_TX_MIRROR);

            if ((cms & G_TX_CLAMP) && ((cms & G_TX_MIRROR) || tex_width1 != tex_width2[i])) {
                tm |= 1 << 2 * i;
                cms &= ~G_TX_CLAMP;
            }
            if ((cmt & G_TX_CLAMP) && ((cmt & G_TX_MIRROR) || tex_height1 != tex_height2[i])) {
                tm |= 1 << (2 * i + 1);
                cmt &= ~G_TX_CLAMP;
            }

            /* A 2D rect whose UVs stay inside one tile is clamped to edge even
             * if declared G_TX_WRAP, since scaled sampling would otherwise pull
             * in the opposite edge as a stray line. GE_NORECTCLAMP=1 turns it
             * off. */
            if (s_DrawingRect && s_RectUvValid) {
                static int rectclamp = -1;
                if (rectclamp < 0) {
                    rectclamp = getenv("GE_NORECTCLAMP") == NULL;
                }
                { /* GE_RECTCLAMPDBG=1: what each textured rect asked for, once
                   * per distinct (texture, span) - the stray-row hunt */
                    static int dbg = -1;
                    static uintptr_t lastaddr;
                    static int32_t last[4];
                    if (dbg < 0) {
                        dbg = getenv("GE_RECTCLAMPDBG") != NULL;
                    }
                    if (dbg && rendering_state.textures[i]
                        && ((uintptr_t) rendering_state.textures[i]->first.texture_addr != lastaddr
                            || last[0] != s_RectUvS0 || last[1] != s_RectUvS1 || last[2] != s_RectUvT0
                            || last[3] != s_RectUvT1)) {
                        lastaddr = (uintptr_t) rendering_state.textures[i]->first.texture_addr;
                        last[0] = s_RectUvS0; last[1] = s_RectUvS1; last[2] = s_RectUvT0; last[3] = s_RectUvT1;
                        fprintf(stderr, "gfx: rectclamp tex=%ux%u span s=%d..%d t=%d..%d (texels s=%.2f..%.2f t=%.2f..%.2f) "
                                        "tile uls=%d ult=%d cms=%u cmt=%u\n",
                                (unsigned) tex_width[i], (unsigned) tex_height[i], (int) s_RectUvS0, (int) s_RectUvS1,
                                (int) s_RectUvT0, (int) s_RectUvT1, s_RectUvS0 / 32.0f, s_RectUvS1 / 32.0f,
                                s_RectUvT0 / 32.0f, s_RectUvT1 / 32.0f, (int) rdp.texture_tile[tile].uls,
                                (int) rdp.texture_tile[tile].ult, (unsigned) cms, (unsigned) cmt);
                    }
                }
                if (rectclamp) {
                    const int32_t s_ofs = (int32_t) rdp.texture_tile[tile].uls * 8; /* U10.2 -> S10.5 */
                    const int32_t t_ofs = (int32_t) rdp.texture_tile[tile].ult * 8;
                    /* One texel of slack is needed: a scaled sprite's span (uls
                     * + dsdx * width, dsdx in S5.10) never lands exactly on the
                     * texture end, and the overshoot is column 0 again under
                     * G_TX_WRAP. Anything that tiles on purpose overshoots by
                     * far more than a texel. */
                    if (s_RectUvS0 - s_ofs >= -32 && s_RectUvS1 - s_ofs <= (int32_t) tex_width[i] * 32 + 32) {
                        cms = G_TX_CLAMP;
                    }
                    if (s_RectUvT0 - t_ofs >= -32 && s_RectUvT1 - t_ofs <= (int32_t) tex_height[i] * 32 + 32) {
                        cmt = G_TX_CLAMP;
                    }
                }
                if (s_RectGlyphBorder) {
                    /* a grown font glyph: transparent outside the texture, and
                     * no in-shader clamp pulling the UVs back inside it */
                    cms = GE_TX_BORDER;
                    cmt = GE_TX_BORDER;
                    tm &= ~(3u << (2 * i));
                }
            }

            if (rendering_state.textures[i]) {
                bool linear_filter = (rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT;
                if (s_DrawingRect && !gfx_ge_mirror_pillars) {
                    /* filter_2d owns the whole 2D layer: 1 smooths every 2D
                     * draw, 0 keeps every one crisp, regardless of the game's
                     * per-draw request. The exception is the death / gunbarrel
                     * blood, the only textured rects drawn inside
                     * G_MIRRORPILLAR_EXT; they skip this and follow the texture
                     * filter setting (point, bilinear or 3-point) via the 3D
                     * path below. */
                    linear_filter = gfx_filter_2d != 0;
                }
                /* GE N64 mipmaps: when the game asked for per-pixel LOD over a
                 * mip chain (G_TL_LOD), use GL's own chain on the level-0
                 * upload so minification stops aliasing. Texrects and the sky
                 * keep their own sampling; this is part of the sampler latch so
                 * one texture can flip between a lod and a plain draw. */
                /* gfx_mipmaps: 1 = only draws that asked (rdp.tex_lod), 2 =
                 * every textured draw, except alpha cut-outs that did not ask
                 * for LOD (mipmapping averages alpha and would make distant
                 * fences solid or vanish). 2D rects and the sky keep their own
                 * sampling either way. */
                const bool mip_filter = !s_DrawingRect && !ge_sky_tri_active && gfx_mipmaps != 0
                                        && (rdp.tex_lod || (gfx_mipmaps >= 2 && !texture_edge));
                { /* GE_XHAIRDBG=1: debug print of what decides a 2D sprite's
                   * filtering, logged only when it changes, to track down the
                   * crosshair blur. */
                    static int dbg = -1;
                    if (dbg < 0) { const char *e = getenv("GE_XHAIRDBG"); dbg = (e == NULL) ? 0 : (e[0] == '2' ? 2 : (e[0] != '0')); }
                    /* =2 also reports non-rect textured draws, in case the
                     * crosshair is not going through the texture-rectangle path
                     * at all. */
                    if (dbg && (s_DrawingRect || dbg == 2)) {
                        /* The texture address is part of the change key so each
                         * texture's draw prints separately and can be matched
                         * against the game side. */
                        static int lastlin = -1, lastmip = -1, lastmode = -1, lastlod = -1;
                        static const void *lastaddr = NULL;
                        const int tf = (int)((rdp.other_mode_h >> G_MDSFT_TEXTFILT) & 3);
                        const int lod = (rdp.other_mode_h & G_TL_LOD) ? 1 : 0;
                        if ((int)linear_filter != lastlin || (int)mip_filter != lastmip
                            || tf != lastmode || lod != lastlod
                            || (const void *) rendering_state.textures[i]->first.texture_addr != lastaddr) {
                            lastlin = linear_filter; lastmip = mip_filter; lastmode = tf; lastlod = lod;
                            lastaddr = (const void *) rendering_state.textures[i]->first.texture_addr;
                            printf("ge: xhairdbg: %s linear=%d mip=%d rdp_textfilt=%d G_TL_LOD=%d "
                                   "filter_2d=%d texid=%u cached_linear=%d %ux%u fmt=%u\n",
                                   s_DrawingRect ? "RECT" : "TRI ",
                                   (int) linear_filter, (int) mip_filter, tf, lod, (int) gfx_filter_2d,
                                   (unsigned) rendering_state.textures[i]->second.texture_id,
                                   (int) rendering_state.textures[i]->second.linear_filter,
                                   (unsigned) tex_width[i], (unsigned) tex_height[i],
                                   (unsigned) rendering_state.textures[i]->first.fmt);
                        }
                    }
                    /* The crosshair's own draw, in full: a texture rect with a
                     * 32x32 fmt=0 texture, logged on any change that could
                     * alter how it looks (it only blurred with decals such as
                     * bullet holes or smoke on screen). */
                    if (dbg && s_DrawingRect && i == 0
                        && tex_width[0] == 32 && tex_height[0] == 32
                        && rendering_state.textures[0]->first.fmt == 0) {
                        static uint32_t lh = 0xFFFFFFFFu, ll = 0xFFFFFFFFu;
                        static int ldm = -1, lcc = -1, ltm = -1;
                        const int cc = comb ? (int)(comb->shader_id1 & 0xFFFF) : -1;
                        if (rdp.other_mode_h != lh || rdp.other_mode_l != ll
                            || (int)depth_mode != ldm || cc != lcc || (int)tm != ltm) {
                            lh = rdp.other_mode_h; ll = rdp.other_mode_l;
                            ldm = depth_mode; lcc = cc; ltm = tm;
                            printf("ge: xhairdbg: XHAIR omode_h=%08x omode_l=%08x depth=%d cc=%d tm=%d "
                                   "cyc=%d zmode=%d aa=%d linear=%d" "\n",
                                   (unsigned) rdp.other_mode_h, (unsigned) rdp.other_mode_l,
                                   (int) depth_mode, cc, (int) tm,
                                   (int) ((rdp.other_mode_h >> 20) & 3),
                                   (int) ((rdp.other_mode_l & ZMODE_DEC) >> 10),
                                   (int) ((rdp.other_mode_l & 8) ? 1 : 0),
                                   (int) linear_filter);
                        }
                    }
                }
                if (linear_filter != rendering_state.textures[i]->second.linear_filter ||
                    mip_filter != rendering_state.textures[i]->second.mip_filter ||
                    cms != rendering_state.textures[i]->second.cms || cmt != rendering_state.textures[i]->second.cmt) {
                    gfx_flush();
                    ge_mip_active = mip_filter ? 1 : 0;
                    gfx_rapi->set_sampler_parameters(i, linear_filter, cms, cmt);
                    ge_mip_active = 0;
                    rendering_state.textures[i]->second.linear_filter = linear_filter;
                    rendering_state.textures[i]->second.mip_filter = mip_filter;
                    rendering_state.textures[i]->second.cms = cms;
                    rendering_state.textures[i]->second.cmt = cmt;
                }
            }
        }
    }

    prg = comb->prg[tm];
    if (prg == NULL) {
        comb->prg[tm] = prg =
            gfx_lookup_or_create_shader_program(comb->shader_id0, comb->shader_id1 | (tm * SHADER_OPT_TEXEL0_CLAMP_S));
    }
    if (prg != rendering_state.shader_program) {
        gfx_flush();
        gfx_rapi->unload_shader(rendering_state.shader_program);
        gfx_rapi->load_shader(prg);
        rendering_state.shader_program = prg;
    }
    if (use_alpha != rendering_state.alpha_blend || use_modulate != rendering_state.modulate) {
        gfx_flush();
        gfx_rapi->set_use_alpha(use_alpha, use_modulate);
        rendering_state.alpha_blend = use_alpha;
        rendering_state.modulate = use_modulate;
    }
    gfx_rapi->shader_get_info(prg, &num_inputs, used_textures);

    clip_parameters = gfx_rapi->get_clip_parameters();

    { /* GE_TRACE_TEXADDR=hex (diagnostic): trace the RDP state of every
       * tri that samples the texture imported from that source address -
       * combiner, env/prim, geometry mode, vertex colours - so a texture that
       * decodes fine in the upload dump but draws wrong can be read off. */
        static const char* taddr = NULL;
        static int tinit = 0;
        static int tn = 0;
        static long tnum = -1;
        if (!tinit) { tinit = 1; taddr = getenv("GE_TRACE_TEXADDR"); const char* e = getenv("GE_TRACE_TEXNUM"); if (e && e[0]) { tnum = atol(e); taddr = "*"; } }
        if (taddr && taddr[0] && tn < 24) {
            char buf[32];
            const TextureCacheNode* tex0 = rendering_state.textures[0];
            snprintf(buf, sizeof(buf), "%08lx", (unsigned long)(uintptr_t)(tex0 ? tex0->first.texture_addr : NULL));
            if (tex0 && (tnum >= 0 ? ge_texnum_of(tex0->first.texture_addr) == tnum : strstr(buf, taddr) != NULL)) {
                tn++;
                fprintf(stderr,
                        "textrace: addr=%s num=%d omode_l=%08x omode_h=%08x cc=%016llx env=%02x%02x%02x%02x prim=%02x%02x%02x%02x fog=%02x%02x%02x%02x "
                        "geom=%08x use_alpha=%d texedge=%d fogon=%d c1=(%02x %02x %02x %02x) c2=(%02x %02x %02x %02x) c3=(%02x %02x %02x %02x) "
                        "tile0 fmt=%d siz=%d pal=%d lut=%d line=%u masks=%d maskt=%d cms=%d cmt=%d uls=%d lrs=%d ult=%d lrt=%d shifts=%d shiftt=%d\n",
                        buf, ge_texnum_of(tex0->first.texture_addr), rdp.other_mode_l, rdp.other_mode_h, (unsigned long long)rdp.combine_mode,
                        rdp.env_color.r, rdp.env_color.g, rdp.env_color.b, rdp.env_color.a,
                        rdp.prim_color.r, rdp.prim_color.g, rdp.prim_color.b, rdp.prim_color.a,
                        rdp.fog_color.r, rdp.fog_color.g, rdp.fog_color.b, rdp.fog_color.a,
                        rsp.geometry_mode, (int)use_alpha, (int)texture_edge, (int)use_fog,
                        v1->color.r, v1->color.g, v1->color.b, v1->color.a,
                        v2->color.r, v2->color.g, v2->color.b, v2->color.a,
                        v3->color.r, v3->color.g, v3->color.b, v3->color.a,
                        rdp.texture_tile[rdp.first_tile_index].fmt, rdp.texture_tile[rdp.first_tile_index].siz,
                        rdp.texture_tile[rdp.first_tile_index].palette, (int)((rdp.other_mode_h >> 14) & 3),
                        rdp.texture_tile[rdp.first_tile_index].line_size_bytes,
                        rdp.texture_tile[rdp.first_tile_index].masks, rdp.texture_tile[rdp.first_tile_index].maskt,
                        rdp.texture_tile[rdp.first_tile_index].cms, rdp.texture_tile[rdp.first_tile_index].cmt,
                        rdp.texture_tile[rdp.first_tile_index].uls, rdp.texture_tile[rdp.first_tile_index].lrs,
                        rdp.texture_tile[rdp.first_tile_index].ult, rdp.texture_tile[rdp.first_tile_index].lrt,
                        rdp.texture_tile[rdp.first_tile_index].shifts, rdp.texture_tile[rdp.first_tile_index].shiftt);
            }
        }
    }

    /* GE enhancement: ZMODE_DEC decals sit slightly off the wall, a gap no
     * constant glPolygonOffset covers, so each decal vertex is pulled toward
     * the eye by a fixed world distance (GE_DEC_PULL=d, default 2, 0 = off). */
    /* GE's projection matrix includes the view, so P[3][2] varies with the
     * camera; the pull needs the pure projection term b, recovered from the
     * z and w columns via z = a*w + b. The pull applies only when the
     * projection is perspective (b < 0). */
    dec_pull_b = 0.0f;
    dec_pull = zmode == ZMODE_DEC && depth_test && depth_compare;
    if (dec_pull) {
        const float (*P)[4] = rsp.P_matrix;
        const float ww = P[0][3] * P[0][3] + P[1][3] * P[1][3] + P[2][3] * P[2][3];
        if (ww > 1e-12f) {
            const float a = (P[0][2] * P[0][3] + P[1][2] * P[1][3] + P[2][2] * P[2][3]) / ww;
            dec_pull_b = P[3][2] - a * P[3][3];
        }
        /* the GL backend's decal clip pushes by the same term
         * (gfx_opengl_draw_triangles); b >= 0 (not perspective) turns it off */
        gfx_ge_dec_b = dec_pull_b;
        gfx_ge_dec_d = ge_dec_pull;
        dec_pull = ge_dec_pull > 0.0f && dec_pull_b < 0.0f;
    }
    /* ---- store the derived state for the next triangles ---- */
    ts.serial = ge_state_serial;
    ts.fp = ts_fp;
    ts.depth_test = depth_test; ts.depth_update = depth_update; ts.depth_compare = depth_compare;
    ts.depth_source_prim = depth_source_prim; ts.zmode = zmode; ts.depth_mode = depth_mode;
    ts.use_alpha = use_alpha; ts.use_fog = use_fog; ts.use_pixfog = use_pixfog;
    ts.use_grayscale = use_grayscale; ts.use_modulate = use_modulate;
    ts.comb = comb; ts.tm = tm;
    for (int i = 0; i < 2; i++) {
        ts.tex_width[i] = tex_width[i]; ts.tex_height[i] = tex_height[i];
        ts.tex_width2[i] = tex_width2[i]; ts.tex_height2[i] = tex_height2[i];
        ts.used_textures[i] = used_textures[i];
    }
    ts.prg = prg; ts.num_inputs = num_inputs; ts.clip_parameters = clip_parameters;
    ts.dec_pull = dec_pull; ts.dec_pull_b = dec_pull_b;
    } /* ts_recompute */
    const bool dxlu_capture = gfx_dxlu_enabled() && !dxlu_closed
        && use_alpha && depth_test && depth_compare && !depth_update
        && dxlu_num_tris < DXLU_MAX_TRIS && dxlu_num_states < DXLU_MAX_STATES;
    const uint32_t dxlu_start = buf_vbo_len;

    /* GE_DEC_TRACE=<frame> (diag): at that frame print every ZMODE_DEC
     * triangle's clip verts (NDC x,y, window z, w) and remember their NDC
     * bboxes, then at frame+1 print every depth-writing non-DEC triangle
     * overlapping one of them (the wall under the decal). The camera must be
     * still (GE_NOINPUT). */
    {
        static long dec_trace = -2;
        if (dec_trace == -2) { const char* e = getenv("GE_DEC_TRACE"); dec_trace = e ? atol(e) : -1; }
        if (dec_trace >= 0) {
            /* g_PortFramesPumped: extern "C" at file scope */
            static float dec_bb[256][4]; static int dec_nbb = 0;
            long fr = (long)g_PortFramesPumped;
            if (fr == dec_trace && zmode == ZMODE_DEC) {
                float bx0 = 1e9f, bx1 = -1e9f, by0 = 1e9f, by1 = -1e9f;
                fprintf(stderr, "dectrace f%ld DEC om_l=%08x pull=%d b=%g gm=%08x P=[%g %g %g %g|%g %g %g %g|%g %g %g %g|%g %g %g %g]", fr, rdp.other_mode_l, (int)dec_pull, dec_pull_b, rsp.geometry_mode,
                        rsp.P_matrix[0][0], rsp.P_matrix[0][1], rsp.P_matrix[0][2], rsp.P_matrix[0][3], rsp.P_matrix[1][0], rsp.P_matrix[1][1], rsp.P_matrix[1][2], rsp.P_matrix[1][3],
                        rsp.P_matrix[2][0], rsp.P_matrix[2][1], rsp.P_matrix[2][2], rsp.P_matrix[2][3], rsp.P_matrix[3][0], rsp.P_matrix[3][1], rsp.P_matrix[3][2], rsp.P_matrix[3][3]);
                for (int k = 0; k < 3; k++) {
                    float w = v_arr[k]->w, nx = v_arr[k]->x / w, ny = v_arr[k]->y / w, nz = v_arr[k]->z / w;
                    fprintf(stderr, " [%.4f,%.4f z=%.7f w=%.2f]", nx, ny, nz, w);
                    if (nx < bx0) bx0 = nx; if (nx > bx1) bx1 = nx; if (ny < by0) by0 = ny; if (ny > by1) by1 = ny;
                }
                fprintf(stderr, "\n");
                if (dec_nbb < 256) { dec_bb[dec_nbb][0] = bx0; dec_bb[dec_nbb][1] = bx1; dec_bb[dec_nbb][2] = by0; dec_bb[dec_nbb][3] = by1; dec_nbb++; }
            } else if (fr == dec_trace + 1 && zmode != ZMODE_DEC && depth_update) {
                float bx0 = 1e9f, bx1 = -1e9f, by0 = 1e9f, by1 = -1e9f;
                for (int k = 0; k < 3; k++) {
                    float w = v_arr[k]->w; if (w <= 0.0f) { bx0 = -1e9f; bx1 = 1e9f; by0 = -1e9f; by1 = 1e9f; break; }
                    float nx = v_arr[k]->x / w, ny = v_arr[k]->y / w;
                    if (nx < bx0) bx0 = nx; if (nx > bx1) bx1 = nx; if (ny < by0) by0 = ny; if (ny > by1) by1 = ny;
                }
                for (int b = 0; b < dec_nbb; b++) {
                    if (bx0 <= dec_bb[b][1] && bx1 >= dec_bb[b][0] && by0 <= dec_bb[b][3] && by1 >= dec_bb[b][2]) {
                        fprintf(stderr, "dectrace f%ld OPA(bb%d) om_l=%08x", fr, b, rdp.other_mode_l);
                        for (int k = 0; k < 3; k++) {
                            float w = v_arr[k]->w;
                            fprintf(stderr, " [%.4f,%.4f z=%.7f w=%.2f]", v_arr[k]->x / w, v_arr[k]->y / w, v_arr[k]->z / w, w);
                        }
                        fprintf(stderr, "\n");
                        break;
                    }
                }
            }
        }
    }
    const uint32_t roc_vstart = buf_vbo_len;
    int roc_fog_ofs = -1, roc_pixfog_ofs = -1;
    /* Everything constant for the triangle (tile parameters, fog / grayscale /
     * combiner constants, float divides) is derived once here instead of per
     * vertex. 8-bit to float goes through ge_b2f (bit-identical to the
     * division), and only the final tex-size divide became a reciprocal
     * multiply. */
    float tc_su[2], tc_sv[2], tc_bu[2], tc_bv[2], tc_p[2], tc_c[2], tc_iw[2], tc_ih[2], tc_cs[2], tc_ct[2];
    bool tc_clamps[2], tc_clampt[2];
    for (int t = 0; t < 2; t++) {
        if (!used_textures[t]) {
            continue;
        }
        // TODO: fix this; for now just ignore smaller mips
        const uint32_t tile = gfx_lod_tile_offset(t);
        const auto& tt = rdp.texture_tile[rdp.first_tile_index + tile];
        float su = 1.0f / 32.0f, sv = 1.0f / 32.0f;
        if (tt.shifts != 0) {
            if (tt.shifts <= 10) su /= (float)(1 << tt.shifts); else su *= (float)(1 << (16 - tt.shifts));
        }
        if (tt.shiftt != 0) {
            if (tt.shiftt <= 10) sv /= (float)(1 << tt.shiftt); else sv *= (float)(1 << (16 - tt.shiftt));
        }
        tc_su[t] = su;
        tc_sv[t] = sv;
        tc_bu[t] = tt.uls / 4.0f;
        tc_bv[t] = tt.ult / 4.0f;
        tc_p[t] = 1.0f;
        tc_c[t] = 0.0f;
        if (!is_rect) {
            if (!(rdp.other_mode_h & G_TP_PERSP)) {
                tc_p[t] = 0.5f;
            }
            if ((rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT) {
                tc_c[t] = 0.5f; // Linear filter adds 0.5f to the coordinates
            }
        }
        tc_iw[t] = 1.0f / (float)tex_width[t];
        tc_ih[t] = 1.0f / (float)tex_height[t];
        tc_clamps[t] = (tm & (1 << 2 * t)) != 0;
        tc_clampt[t] = (tm & (1 << (2 * t + 1))) != 0;
        tc_cs[t] = ((float)tex_width2[t] - 0.5f) * tc_iw[t];
        tc_ct[t] = ((float)tex_height2[t] - 0.5f) * tc_ih[t];
    }
    float fog_rgb[3] = { 0.0f, 0.0f, 0.0f };
    if (use_fog) {
        fog_rgb[0] = ge_b2f[rdp.fog_color.r];
        fog_rgb[1] = ge_b2f[rdp.fog_color.g];
        fog_rgb[2] = ge_b2f[rdp.fog_color.b];
    }
    const float pixfog_mul = (float)rsp.fog_mul;
    const float pixfog_ofs = (float)rsp.fog_offset;
    float gray[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (use_grayscale) {
        gray[0] = ge_b2f[rdp.grayscale_color.r];
        gray[1] = ge_b2f[rdp.grayscale_color.g];
        gray[2] = ge_b2f[rdp.grayscale_color.b];
        gray[3] = ge_b2f[rdp.grayscale_color.a]; // lerp interpolation factor (not alpha)
    }
    /* combiner inputs: kind 0 = constant (c), 1 = shade, 2 = shade alpha
     * (rgb = a, a = 0), 3 = LOD fraction from w */
    struct { uint8_t kind; struct RGBA c; } cin[8][2];
    const int nk = 1 + (use_alpha ? 1 : 0);
    for (int j = 0; j < num_inputs && j < 8; j++) {
        for (int k = 0; k < nk; k++) {
            uint8_t kind = 0;
            struct RGBA c = { 0, 0, 0, 0 };
            switch (comb->shader_input_mapping[k][j]) {
                    // Note: CCMUX constants and ACMUX constants used here have same value, which is why this works
                    // (except LOD fraction).
                case G_CCMUX_PRIMITIVE:
                    c = rdp.prim_color;
                    break;
                case G_CCMUX_SHADE:
                    kind = 1;
                    break;
                case G_CCMUX_SHADE_ALPHA:
                    kind = 2;
                    break;
                case G_CCMUX_ENVIRONMENT:
                    c = rdp.env_color;
                    break;
                case G_CCMUX_PRIMITIVE_ALPHA:
                    c.r = c.g = c.b = rdp.prim_color.a;
                    break;
                case G_CCMUX_ENV_ALPHA:
                    c.r = c.g = c.b = rdp.env_color.a;
                    break;
                case G_CCMUX_PRIM_LOD_FRAC:
                    c.r = c.g = c.b = rdp.prim_lod_fraction;
                    break;
                case G_CCMUX_LOD_FRACTION:
                    if (rdp.other_mode_h & G_TL_LOD) {
                        kind = 3;
                    } else {
                        c.r = c.g = c.b = c.a = 255;
                    }
                    break;
                case G_ACMUX_PRIM_LOD_FRAC:
                    c.a = rdp.prim_lod_fraction;
                    break;
                default:
                    break;
            }
            cin[j][k].kind = kind;
            cin[j][k].c = c;
        }
    }

    /* Detail texture LOD fraction, as the RDP computes it for G_TD_DETAIL: the
     * base texture's texel density at the N64's 240 lines, scaled per vertex
     * by depth and clamped to [min level, 1]. GE_DETAILHACK=1 uses a simple
     * depth-based fraction instead. */
    float ge_detf[3] = { 1.0f, 1.0f, 1.0f };
    bool ge_detf_on = false;
    {
        static int hack = -1;
        if (hack < 0) {
            const char* e = getenv("GE_DETAILHACK");
            hack = (e != NULL && e[0] != '0') ? 1 : 0;
        }
        if (!hack && rdp.tex_lod && rdp.tex_detail && used_textures[1] && gfx_current_dimensions.height > 0) {
            const float sc = (float) SCREEN_HEIGHT / (float) gfx_current_dimensions.height;
            const float hw = 0.5f * (float) rdp.viewport.width * sc;
            const float hh = 0.5f * (float) rdp.viewport.height * sc;
            const float minf = (float) (rdp.prim_min_level & 0x1f) / 32.0f;
            float px[3], py[3], tu[3], tv[3], wsum = 0.0f;
            bool behind = false;

            for (int i = 0; i < 3; i++) {
                const float w = v_arr[i]->w;

                if (w < 1e-3f) {
                    behind = true;
                    px[i] = py[i] = 0.0f;
                } else {
                    px[i] = v_arr[i]->x / w * hw;
                    py[i] = v_arr[i]->y / w * hh;
                }
                wsum += w;
                tu[i] = (v_arr[i]->u * tc_su[1] - tc_bu[1]) * tc_p[1] + tc_c[1];   /* base texels */
                tv[i] = (v_arr[i]->v * tc_sv[1] - tc_bv[1]) * tc_p[1] + tc_c[1];
            }
            ge_detf_on = true;
            if (behind) {
                /* spans the eye plane: right on top of it, as magnified as it gets */
                ge_detf[0] = ge_detf[1] = ge_detf[2] = minf;
            } else {
                const float as = fabsf((px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]));
                const float at = fabsf((tu[1] - tu[0]) * (tv[2] - tv[0]) - (tu[2] - tu[0]) * (tv[1] - tv[0]));

                if (as > 1e-6f && wsum > 0.0f) {
                    const float tpp = sqrtf(at / as);   /* base texels per N64 pixel, at the centroid */
                    const float wc = wsum / 3.0f;

                    for (int i = 0; i < 3; i++) {
                        const float f = tpp * v_arr[i]->w / wc;
                        ge_detf[i] = (f < minf) ? minf : (f > 1.0f) ? 1.0f : f;
                    }
                }
                /* else edge-on: no pixels to magnify into - the base alone (1) */
            }
        }
    }

    for (int i = 0; i < 3; i++) {
        float z = v_arr[i]->z, w = v_arr[i]->w;
        if (dec_pull && w > 4.0f * ge_dec_pull) {
            z += dec_pull_b * ge_dec_pull / (w - ge_dec_pull);
        }
        if (clip_parameters.z_is_from_0_to_1) {
            z = (z + w) / 2.0f;
        }

        buf_vbo[buf_vbo_len++] = v_arr[i]->x;
        buf_vbo[buf_vbo_len++] = clip_parameters.invert_y ? -v_arr[i]->y : v_arr[i]->y;
        buf_vbo[buf_vbo_len++] = z;
        buf_vbo[buf_vbo_len++] = w;

        for (int t = 0; t < 2; t++) {
            if (!used_textures[t]) {
                continue;
            }
            float u, v;
            if (ge_sky_pixdiv_active) {
                /* the vertex holds u*q, v*q: the same affine map times q,
                 * with no divide anywhere (q is 0 on the horizon) */
                const float q = ge_sky_q[i];
                u = v_arr[i]->u * tc_su[t] * tc_p[t] + (tc_c[t] - tc_bu[t] * tc_p[t]) * q;
                v = v_arr[i]->v * tc_sv[t] * tc_p[t] + (tc_c[t] - tc_bv[t] * tc_p[t]) * q;
            } else {
                u = (v_arr[i]->u * tc_su[t] - tc_bu[t]) * tc_p[t] + tc_c[t];
                v = (v_arr[i]->v * tc_sv[t] - tc_bv[t]) * tc_p[t] + tc_c[t];
            }
            buf_vbo[buf_vbo_len++] = u * tc_iw[t];
            buf_vbo[buf_vbo_len++] = v * tc_ih[t];
            if (tc_clamps[t]) {
                buf_vbo[buf_vbo_len++] = tc_cs[t];
            }
            if (tc_clampt[t]) {
                buf_vbo[buf_vbo_len++] = tc_ct[t];
            }
        }

        if (use_fog) {
            if (i == 0) roc_fog_ofs = (int)(buf_vbo_len - roc_vstart);
            buf_vbo[buf_vbo_len++] = fog_rgb[0];
            buf_vbo[buf_vbo_len++] = fog_rgb[1];
            buf_vbo[buf_vbo_len++] = fog_rgb[2];
            buf_vbo[buf_vbo_len++] = ge_b2f[v_arr[i]->fog]; // fog factor
        }

        if (use_pixfog) {
            if (i == 0) roc_pixfog_ofs = (int)(buf_vbo_len - roc_vstart);
            buf_vbo[buf_vbo_len++] = pixfog_mul;
            buf_vbo[buf_vbo_len++] = pixfog_ofs;
        }

        if (ge_sky_pixdiv_active) {
            buf_vbo[buf_vbo_len++] = ge_sky_q[i];
        }

        if (use_grayscale) {
            buf_vbo[buf_vbo_len++] = gray[0];
            buf_vbo[buf_vbo_len++] = gray[1];
            buf_vbo[buf_vbo_len++] = gray[2];
            buf_vbo[buf_vbo_len++] = gray[3];
        }

        for (int j = 0; j < num_inputs; j++) {
            for (int k = 0; k < nk; k++) {
                const struct RGBA* color;
                struct RGBA tmp;
                switch (cin[j][k].kind) {
                    case 1:
                        color = &v_arr[i]->color;
                        break;
                    case 2:
                        tmp.r = tmp.g = tmp.b = v_arr[i]->color.a;
                        tmp.a = 0;
                        color = &tmp;
                        break;
                    case 3: {
                        if (ge_detf_on) {
                            /* a detail draw: the RDP's fraction (above). GE_DETAILF=<0..1>
                             * forces it (diagnostic: 0 = the detail texture alone, 1 = the base) */
                            static float force = -2.0f;
                            if (force < -1.5f) {
                                const char* e = getenv("GE_DETAILF");
                                force = (e != NULL && e[0] != '\0') ? (float) atof(e) : -1.0f;
                            }
                            tmp.r = tmp.g = tmp.b = tmp.a = (uint8_t) ((force >= 0.0f ? force : ge_detf[i]) * 255.f + 0.5f);
                            color = &tmp;
                            break;
                        }
                        // HACK: very roughly eyeballed based on the carpets in
                        // Defection; this should be calculated per pixel. (GE:
                        // only non-detail LOD draws reach this now, and those
                        // bind one tile to both slots, so the fraction cannot
                        // show.)
                        const float distance_frac = std::max(0.f, std::min(w / 1024.f, 1.f));
                        tmp.r = tmp.g = tmp.b = tmp.a = (0.7f + distance_frac * 0.3f) * 255.f;
                        color = &tmp;
                        break;
                    }
                    default:
                        color = &cin[j][k].c;
                        break;
                }
                if (k == 0) {
                    buf_vbo[buf_vbo_len++] = ge_b2f[color->r];
                    buf_vbo[buf_vbo_len++] = ge_b2f[color->g];
                    buf_vbo[buf_vbo_len++] = ge_b2f[color->b];
                } else {
                    buf_vbo[buf_vbo_len++] = ge_b2f[color->a];
                }
            }
        }
    }

    if (roc_capturing != nullptr && !roc_capture_bad) {
        if (is_rect) {
            roc_capture_bad = true;
            roc_capture_bad_why = "rect in the list";
        } else if (dxlu_capture && !gfx_xbox_xlu_retained) {
            roc_capture_passthrough(roc_cur_cmd); /* stays on the interpreter, the list is walked */
        } else {
            roc_record_tri(prg, comb, num_inputs, use_alpha, use_modulate, depth_test, depth_update, depth_compare, depth_source_prim,
                           zmode, depth_mode, used_textures, roc_vstart, roc_fog_ofs, roc_pixfog_ofs,
                           clip_parameters.invert_y, clip_parameters.z_is_from_0_to_1,
                           dec_pull, dec_pull_b, ge_dec_pull, v_arr, dxlu_capture);
        }
    }
    if (roc_drop) {
        buf_vbo_len = roc_vstart; /* recorded above; not drawn this frame */
        return;
    }

    if (dxlu_capture) {
        const uint32_t nf = buf_vbo_len - dxlu_start;
        if (nf <= 3 * DXLU_MAX_VERT_FLOATS && dxlu_vbo_len + nf <= DXLU_MAX_FLOATS) {
            const uint32_t stride = nf / 3;
            struct DXluState s;
            memset(&s, 0, sizeof(s)); /* zero padding so memcmp dedup is sound */
            s.prg = prg;
            s.use_alpha = use_alpha;
            s.use_modulate = use_modulate;
            s.depth_test = depth_test;
            s.depth_update = depth_update;
            s.depth_compare = depth_compare;
            s.depth_source_prim = depth_source_prim;
            s.zmode = zmode;
            s.depth_mode_enc = depth_mode;
            s.viewport = rendering_state.viewport;
            s.scissor = rendering_state.scissor;
            for (int t = 0; t < 2; t++) {
                if (used_textures[t] && rendering_state.textures[t] != NULL) {
                    s.tex_used[t] = true;
                    s.tex_id[t] = rendering_state.textures[t]->second.texture_id;
                    s.tex_linear[t] = rendering_state.textures[t]->second.linear_filter;
                    s.tex_mip[t] = rendering_state.textures[t]->second.mip_filter;
                    s.tex_cms[t] = rendering_state.textures[t]->second.cms;
                    s.tex_cmt[t] = rendering_state.textures[t]->second.cmt;
                }
            }

            uint32_t si;
            if (dxlu_num_states > 0
                && memcmp(&dxlu_states[dxlu_num_states - 1], &s, sizeof(s)) == 0) {
                si = dxlu_num_states - 1;
            } else {
                si = dxlu_num_states++;
                dxlu_states[si] = s;
            }

            {
                struct DXluTri* dt = &dxlu_tris[dxlu_num_tris];
                bool eye_ok = true;
                float w0 = gfx_dxlu_vertex_dist(buf_vbo + dxlu_start, dt->e, &eye_ok);
                float w1 = gfx_dxlu_vertex_dist(buf_vbo + dxlu_start + stride, dt->e + 3, &eye_ok);
                float w2 = gfx_dxlu_vertex_dist(buf_vbo + dxlu_start + 2 * stride, dt->e + 6, &eye_ok);
                float key = w0 > w1 ? w0 : w1;
                if (w2 > key) {
                    key = w2;
                }
                dt->maxd = key;
                dt->mind = w0 < w1 ? w0 : w1;
                if (w2 < dt->mind) {
                    dt->mind = w2;
                }
                dt->bb[0] = dt->bb[2] = 1e30f;
                dt->bb[1] = dt->bb[3] = -1e30f;
                for (int k = 0; k < 3; k++) {
                    const float* p = buf_vbo + dxlu_start + k * stride;
                    if (p[3] <= 1e-6f) {
                        /* behind/at the eye: no screen position - cover all */
                        dt->bb[0] = dt->bb[2] = -1e30f;
                        dt->bb[1] = dt->bb[3] = 1e30f;
                        break;
                    }
                    const float sx = p[0] / p[3], sy = p[1] / p[3];
                    if (sx < dt->bb[0]) dt->bb[0] = sx;
                    if (sx > dt->bb[1]) dt->bb[1] = sx;
                    if (sy < dt->bb[2]) dt->bb[2] = sy;
                    if (sy > dt->bb[3]) dt->bb[3] = sy;
                }
                dt->eye_ok = eye_ok ? 1 : 0;
                dt->ret = -1;
                dt->ret_first = 0;
                dt->grp = 0;
                dt->gmode = 0;
                dt->scope = dxlu_scope_cur;
                {
                    /* GE_XLU_CENSUS=1: once per distinct other_mode_l that
                     * reaches the deferred pass, print it (+ zmode, frame)
                     * - tells decal-vs-surface for a fighting overlay. */
                    static int census = -1;
                    static uint32_t seen[64];
                    static int nseen;
                    if (census < 0) census = getenv("GE_XLU_CENSUS") != NULL;
                    if (census) {
                        int k;
                        for (k = 0; k < nseen; k++) if (seen[k] == rdp.other_mode_l) break;
                        if (k == nseen && nseen < 64) {
                            extern uint32_t num_dls;
                            seen[nseen++] = rdp.other_mode_l;
                            fprintf(stderr, "xlucensus: omode_l=%08x zmode=%s task=%u\n", rdp.other_mode_l,
                                    zmode == ZMODE_DEC ? "DEC" : zmode == ZMODE_XLU ? "XLU" : zmode == ZMODE_INTER ? "INTER" : "OPA",
                                    (unsigned)num_dls);
                        }
                    }
                }
                {
                    /* GE_DXLU_TRACE=<texnum>: print every captured tri whose
                     * tile-0 texture is that model-pool texture (key + the
                     * three clip-w's), and its final slot in the sorted
                     * draw (gfx_dxlu_draw) - draw-order forensics for
                     * "part X shows through part Y" reports. */
                    static long dtn = -2;
                    static long dtframe = -1;
                    if (dtn == -2) {
                        const char* e = getenv("GE_DXLU_TRACE");
                        const char* tf = getenv("GE_TRACE_DL_FRAME");
                        dtn = (e && e[0]) ? (e[0] == 'a' ? -3 : atol(e)) : -1;
                        if (tf && tf[0]) dtframe = atol(tf);
                    }
                    if (dtn >= 0 && rendering_state.textures[0] != NULL
                        && ge_texnum_of(rendering_state.textures[0]->first.texture_addr) == dtn) {
                        dxlu_trace_mark[dxlu_num_tris >> 5] |= 1u << (dxlu_num_tris & 31);
                        fprintf(stderr, "dxlutrace: cap tri=%u tex=%ld key=%.1f d=(%.1f %.1f %.1f) state=%u grp=%d\n",
                                dxlu_num_tris, dtn, key, w0, w1, w2, si, dxlu_group_open ? (int)dxlu_group_mode : 0);
                    } else if (dtn == -3) {
                        /* GE_DXLU_TRACE=all (+GE_TRACE_DL_FRAME=n): every
                         * captured tri of that frame, with its screen-space
                         * corners (x/w, y/w) so it can be placed in a shot */
                        extern uint32_t num_dls;
                        if (dtframe < 0 || (long)num_dls == dtframe) {
                            const float* p0 = buf_vbo + dxlu_start;
                            const float* p1 = p0 + stride;
                            const float* p2 = p1 + stride;
                            dxlu_trace_mark[dxlu_num_tris >> 5] |= 1u << (dxlu_num_tris & 31);
                            fprintf(stderr, "dxlutrace: cap tri=%u key=%.1f d=(%.1f %.1f %.1f) ndc=(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f) state=%u grp=%d scope=%u tex=%p\n",
                                    dxlu_num_tris, key, w0, w1, w2,
                                    p0[0] / p0[3], p0[1] / p0[3], p1[0] / p1[3], p1[1] / p1[3], p2[0] / p2[3], p2[1] / p2[3],
                                    si, dxlu_group_open ? (int)dxlu_group_mode : 0, dxlu_scope_cur,
                                    rendering_state.textures[0] ? (void*)rendering_state.textures[0]->first.texture_addr : NULL);
                        } else {
                            dxlu_trace_mark[dxlu_num_tris >> 5] &= ~(1u << (dxlu_num_tris & 31));
                        }
                    } else if (dtn >= 0) {
                        dxlu_trace_mark[dxlu_num_tris >> 5] &= ~(1u << (dxlu_num_tris & 31));
                    }
                }

                memcpy(dxlu_vbo + dxlu_vbo_len, buf_vbo + dxlu_start, nf * sizeof(float));
                dxlu_tris[dxlu_num_tris].key = key;
                /* singleton unless a group closes over it (which overwrites
                 * gkey). ZMODE_DEC strays (wall sign overlays) are glued to
                 * a z-written surface: pin them first (see DECAL PIN). */
                dxlu_tris[dxlu_num_tris].gkey =
                    (zmode == ZMODE_DEC && gfx_dxlu_pin_enabled()) ? DXLU_KEY_PINNED : key;
                dxlu_tris[dxlu_num_tris].state = si;
                dxlu_tris[dxlu_num_tris].offset = dxlu_vbo_len;
                dxlu_tris[dxlu_num_tris].nfloats = (uint16_t)nf;
                dxlu_num_tris++;
                roc_stat_tri_dxlu++;
                dxlu_vbo_len += nf;
            }

            /* triangle is deferred - remove it from the immediate batch */
            buf_vbo_len = dxlu_start;
            return;
        }
    }

    if (++buf_vbo_num_tris == MAX_BUFFERED) {
        gfx_flush();
    }
}

static inline void gfx_sp_tri4(Gfx *cmd) {
    // the game issues gSPTri2 for quads, which uses G_TRI4 with 2 empty triangles
    uint8_t x = C1(0, 4);
    uint8_t y = C1(4, 4);
    uint8_t z = C0(0, 4);

    if(x || y || z) {
        gfx_sp_tri1(x, y, z, false);
    }

    x = C1(8, 4);
    y = C1(12, 4);
    z = C0(4, 4);

    if (x || y || z) {
        gfx_sp_tri1(x, y, z, false);
    }

    x = C1(16, 4);
    y = C1(20, 4);
    z = C0(8, 4);

    if (x || y || z) {
        gfx_sp_tri1(x, y, z, false);
    }

    x = C1(24, 4);
    y = C1(28, 4);
    z = C0(12, 4);

    if (x || y || z) {
        gfx_sp_tri1(x, y, z, false);
    }
}

static void gfx_sp_geometry_mode(uint32_t clear, uint32_t set) {
    const uint32_t gm = (rsp.geometry_mode & ~clear) | set;
    if (gm != rsp.geometry_mode) {
        rsp.geometry_mode = gm;
        ge_state_serial++; /* value-sensitive, see ge_state_serial */
    }
}

/* gfx_ge_mirror_pillars (set by G_MIRRORPILLAR_EXT around an overlay wanting
 * mirrored side bands) is defined up beside gfx_filter_2d: the sampler latch
 * reads it too (the blood overlay's filtering). */

/* transient override for pinned texrect draws (gfx_ge_draw_rect_pinned) */
static uint32_t s_GeAspectForceCenter = 0;

/* GE per-viewport pin: the CENTER pin compresses 2D about the current 3D
 * viewport's centre rather than the window's. s_GeViewportCenterNdc is that
 * centre in full-screen NDC (0 for full-width viewports, about +/-0.5 for the
 * 3P/4P quadrants); s_GeRawScissor is the last scissor in native units before
 * banding, so the mirror-pillar overlay can widen to the player's own viewport
 * and not leak into a neighbour's. */
static float s_GeViewportCenterNdc = 0.f;
/* The viewport's half-width in full-screen NDC (1 = full-width); with the
 * centre it gives the viewport's left/right edges, so HUD Centering edge
 * anchors sit flush to the current viewport (each quadrant in 3P/4P). */
static float s_GeViewportHalfWndc = 1.f;
static XYWidthHeight s_GeRawScissor = { 0, 240, 320, 240 };

/* GE HUD Centering = 16:9 (watch menu, options.c hudAnchorsToEdges): the
 * game brackets an edge HUD element in G_EXTRAGEOMETRYMODE_EXT with a
 * ONE-SIDED anchor (LEFT or RIGHT, never both) - the only EXT geometry
 * state GE ever emits. In a wide gameplay frame (pin 1) that anchor wins
 * over the forced CENTER pin, so the element's 4:3 band slides to the
 * window edge; pinned frames (pin 2) and MP frames never see it. */
/* The HUD slide (options.h PORT_HUD_ANCHOR): how far an edge anchor moves
 * the element from the centred band toward its edge, 0..1; 1 for an anchor
 * that carries no fraction (the pause pillars). */
static float s_GeHudAnchorFrac = 1.f;

static inline uint32_t gfx_ge_hud_anchor(void) {
    const uint32_t side = rsp.extra_geometry_mode & G_ASPECT_CENTER_EXT;
    return (gfx_ge_aspect_pin == 1 && (side == G_ASPECT_LEFT_EXT || side == G_ASPECT_RIGHT_EXT)) ? side : 0;
}

/* GE wide mouse aim (options.h PORT_HUD_ANCHOR_SPAN): the lone WIDE bit, no
 * side, brackets the aim sight when the crosshair may travel the whole
 * window. In a wide gameplay frame that texrect takes the WINDOW mapping
 * (mode 0 - native x 0..320 across the window, exactly the 3D's) instead
 * of the forced 4:3 band, so a sight drawn at unpinned coords lands on the
 * pixel the 3D aim ray passes through and can cross the side bands.
 * sky.c's backdrop fills carry it too (gfx_dp_fill_rectangle: a marked
 * fill is never pinned), so a 3P/4P quadrant's sky reaches its edges.
 * Pinned frames (pin 2) never see it. */
static inline bool gfx_ge_span_marker(void) {
    return gfx_ge_aspect_pin == 1 && (rsp.extra_geometry_mode & G_ASPECT_MODE_EXT) == G_ASPECT_WIDE_EXT;
}

static inline void gfx_update_aspect_mode(void) {
    if (gfx_ge_span_marker() || gfx_ge_con2d) {   /* the console spans the window (G_CONSOLE2D_EXT) */
        rsp.aspect_mode = 0;
    } else if (gfx_ge_aspect_pin == 2 || s_GeAspectForceCenter) {
        /* pinned frame (or pinned 2D draw): render in a centered
         * native-aspect band regardless of DL geometry-mode state -
         * unless the DL anchored this HUD element to an edge */
        const uint32_t anchor = gfx_ge_hud_anchor();
        rsp.aspect_mode = anchor ? anchor : G_ASPECT_CENTER_EXT;
    } else {
        /* ALWAYS re-derive from the DL state: rsp.aspect_mode is
         * persistent, so a forced CENTER from a pinned frame must not
         * leak into the next wide frame (GE never emits the EXT
         * geometry-mode command that would clear it). */
        rsp.aspect_mode = (rsp.extra_geometry_mode & G_ASPECT_MODE_EXT);
    }

    const uint32_t side = rsp.aspect_mode & G_ASPECT_CENTER_EXT;

    rsp.aspect_scale = rsp.aspect_mode ? gfx_current_native_aspect : gfx_current_window_dimensions.aspect_ratio;

    {
        /* Per-viewport pin/anchors: every arm compresses about a fixed point c
         * with ofs = c * (window/native - 1). CENTER uses the current 3D
         * viewport's centre and LEFT/RIGHT its edges, which equal the old
         * window-based formulas for full-width viewports. */
        const float k = gfx_current_dimensions.aspect_ratio / gfx_current_native_aspect - 1.f;

        if (side == G_ASPECT_LEFT_EXT) {
            rsp.aspect_ofs = (s_GeViewportCenterNdc - s_GeViewportHalfWndc * s_GeHudAnchorFrac) * k;
        } else if (side == G_ASPECT_RIGHT_EXT) {
            rsp.aspect_ofs = (s_GeViewportCenterNdc + s_GeViewportHalfWndc * s_GeHudAnchorFrac) * k;
        } else if (side == G_ASPECT_CENTER_EXT) {
            rsp.aspect_ofs = s_GeViewportCenterNdc * k;
        } else {
            rsp.aspect_ofs = 0.f;
        }
    }

    if (side && (rsp.aspect_mode & G_ASPECT_WIDE_EXT)) {
        constexpr float c = 16.f / 9.f;
        if (gfx_current_dimensions.aspect_ratio > c) {
            rsp.aspect_ofs *= c / gfx_current_dimensions.aspect_ratio;
        }
    }
}

static void gfx_sp_extra_geometry_mode(uint32_t clear, uint32_t set) {
    rsp.extra_geometry_mode &= ~clear;
    rsp.extra_geometry_mode |= set;
    rsp.aspect_mode = (rsp.extra_geometry_mode & G_ASPECT_MODE_EXT);
    gfx_update_aspect_mode();
}

static void gfx_adjust_viewport_or_scissor(XYWidthHeight* area, bool preserve_aspect = false) {
    /* Scale both edges and take the size as their difference, rather than
     * truncating origin and size separately. That keeps the 3D viewport on
     * the same pixel rows as the 2D layer, so no seam line appears. */
    const float nx0 = (float) area->x;
    const float nx1 = nx0 + (float) area->width;
    const float ny0 = (float) (SCREEN_HEIGHT - area->y); /* bottom edge, from the bottom */
    const float ny1 = ny0 + (float) area->height;
    const int32_t ix0 = (int32_t) std::lround(nx0 * RATIO_X);
    const int32_t ix1 = (int32_t) std::lround(nx1 * RATIO_X);
    const int32_t iy0 = (int32_t) std::lround(ny0 * RATIO_Y);
    const int32_t iy1 = (int32_t) std::lround(ny1 * RATIO_Y);

    area->x = (int16_t) ix0;
    area->width = (uint32_t) (ix1 > ix0 ? ix1 - ix0 : 1);
    area->y = (int16_t) iy0;
    area->height = (uint32_t) (iy1 > iy0 ? iy1 - iy0 : 1);
    if (preserve_aspect) {
        // preserve native aspect ratio
        const float ratio = gfx_current_native_aspect / gfx_current_dimensions.aspect_ratio;
        const float midx = gfx_current_dimensions.width * 0.5f;
        area->x = midx + (area->x - midx) * ratio;
        /* the ofs is post-compression in the vertex map ((ofs+x)*scale), so
         * its window-px shift carries the same ratio factor - keeps the
         * banded scissor on the per-viewport pin's pixels (ofs was always 0
         * here before the per-viewport centre) */
        area->x += rsp.aspect_ofs * ratio * gfx_current_dimensions.width * 0.5f;
        area->width *= ratio;
    }

    if (!game_renders_to_framebuffer ||
        (gfx_msaa_level > 1 && gfx_current_dimensions.width == gfx_current_game_window_viewport.width &&
            gfx_current_dimensions.height == gfx_current_game_window_viewport.height)) {
        if (!game_renders_to_framebuffer) {
            /* PILLARBOX: the centring offset is for drawing straight into
             * the window; an MSAA framebuffer of the window's size gets it
             * from the present blit instead (gfx_opengl present_x) */
            area->x += gfx_current_game_window_viewport.x;
        }
        area->y += gfx_current_window_dimensions.height -
                    (gfx_current_game_window_viewport.y + gfx_current_game_window_viewport.height);
    }
}

static void gfx_calc_and_set_viewport(const Vp_t* viewport) {
    // 2 bits fraction
    float width = 2.0f * viewport->vscale[0] / 4.0f;
    float height = 2.0f * viewport->vscale[1] / 4.0f;
    float x = (viewport->vtrans[0] / 4.0f) - width / 2.0f;
    float y = ((viewport->vtrans[1] / 4.0f) + height / 2.0f);

    rdp.viewport.x = x;
    rdp.viewport.y = y;
    rdp.viewport.width = width;
    rdp.viewport.height = height;

    /* per-viewport pin centre (see s_GeViewportCenterNdc): captured in
     * NATIVE units before the window adjust; snapped to 0 for full-width
     * viewports so solo/2P keep the exact old map */
    if (gfx_current_native_viewport.width > 0) {
        s_GeViewportCenterNdc =
            (x + width * 0.5f) / ((float) gfx_current_native_viewport.width * 0.5f) - 1.f;
        if (s_GeViewportCenterNdc > -0.005f && s_GeViewportCenterNdc < 0.005f) {
            s_GeViewportCenterNdc = 0.f;
        }
        s_GeViewportHalfWndc = width / (float) gfx_current_native_viewport.width;
        if (s_GeViewportHalfWndc > 0.995f) {
            s_GeViewportHalfWndc = 1.f; /* full-width: exact old anchors */
        }
    }

    gfx_adjust_viewport_or_scissor(&rdp.viewport);

    rdp.viewport_or_scissor_changed = true;

    gfx_update_aspect_mode(); /* rsp.aspect_ofs depends on the viewport centre */
}

static void gfx_sp_movemem(uint8_t index, uint8_t offset, const void* data) {
    switch (index) {
        case G_MV_VIEWPORT:
            gfx_calc_and_set_viewport((const Vp_t*)data);
            break;
        case G_MV_LOOKATY:
        case G_MV_LOOKATX:
            // I think this is only really used for guLookAtReflect
            index = !((index - G_MV_LOOKATY) / 2);
            rsp.lookat[index] = ((const Light *)data)->l;
            rsp.lookat_enabled = (index == 0) || (rsp.lookat[1].dir[0] || rsp.lookat[1].dir[1]);
            rsp.lights_changed = true;
            break;
        case G_MV_L0:
        case G_MV_L1:
        case G_MV_L2:
            // NOTE: reads out of bounds if it is an ambient light
            memcpy(rsp.current_lights + (index - G_MV_L0) / 2, data, sizeof(Light_t));
            break;
    }
}

static void gfx_sp_moveword(uint8_t index, uint16_t offset, uintptr_t data) {
    switch (index) {
        case G_MW_NUMLIGHT:
            // Ambient light is included
            // The 31th bit is a flag that lights should be recalculated
            rsp.current_num_lights = (data - 0x80000000U) / 32;
            rsp.lights_changed = 1;
            break;
        case G_MW_FOG:
            rsp.fog_mul = (int16_t)(data >> 16);
            rsp.fog_offset = (int16_t)data;
            break;
        case G_MW_SEGMENT: {
            // offset = segment*4; the array has 16 slots. The old `& 0xff`
            // mask let a misdecoded moveword write up to 1KB past the array
            // (silent corruption, surfaces later) - drop it loudly instead.
            uint32_t seg = offset >> 2;
            if (seg < 16) {
                segmentPointers[seg] = data;
#ifdef GE_XBOX
                if (ge_xbox_trace_dl) {
                    portXboxTrace("dl: segment %u = %08x", (unsigned)seg, (unsigned)data);
                }
#endif
            } else {
                fprintf(stderr, "gfx: G_MW_SEGMENT index %u out of range (dropped)\n", seg);
            }
            break;
        }
    }
}

static void gfx_sp_texture(uint16_t sc, uint16_t tc, uint8_t level, uint8_t tile, uint8_t on) {
    rsp.texture_scaling_factor.s = sc;
    rsp.texture_scaling_factor.t = tc;
    rdp.tex_max_lod = level;
    if (rdp.first_tile_index != tile) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
        rdp.first_tile_index = tile;
        ge_state_serial++; /* value-sensitive, see ge_state_serial */
    }
}

static void gfx_dp_set_scissor(uint32_t mode, uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry) {
    float x = ulx / 4.0f;
    float y = lry / 4.0f;
    float width = (lrx - ulx) / 4.0f;
    float height = (lry - uly) / 4.0f;

    rdp.scissor.x = x;
    rdp.scissor.y = y;
    rdp.scissor.width = width;
    rdp.scissor.height = height;

    /* native-units copy, pre-banding: the mirror-pillar overlay path widens
     * its scissor to this (the player's own viewport) - s_GeRawScissor */
    s_GeRawScissor.x = (int16_t) x;
    s_GeRawScissor.y = (int16_t) y;
    s_GeRawScissor.width = (uint32_t) width;
    s_GeRawScissor.height = (uint32_t) height;

    /* MP divider leak: rsp.aspect_mode is persistent and the pinned-texrect
     * transient leaves it at CENTER, so a scissor decoded between one player's
     * HUD and the next player's world inherited the looser band mapping, and GL
     * clips at the scissor, not the viewport, so edge triangles rasterised into
     * the divider. The mode is re-derived here with the same rule as draws:
     * wide frames get the full mapping, fully-pinned frames keep the band. */
    gfx_update_aspect_mode();
    /* An edge-anchored HUD bracket (gfx_ge_hud_anchor) must not band the
     * scissor: the scissor is persistent and the bracket ends before the
     * next centred element, which would then be clipped to the wrong band.
     * Wide frames keep the full-window scissor under an anchor. */
    gfx_adjust_viewport_or_scissor(&rdp.scissor, rsp.aspect_mode != 0 && gfx_ge_hud_anchor() == 0);

    rdp.viewport_or_scissor_changed = true;
}

/* A render tile's parameters changed: only the texel slot(s) currently mapped
 * to that tile need re-importing, since SETTILEs for other mip levels or the
 * load tile bind nothing. The slot-to-tile mapping itself only moves via
 * G_TEXTURE and the LOD bits of SETOTHERMODE, which flag both slots. */
static inline void ge_tile_touched(uint8_t tile) {
    for (int i = 0; i < 2; i++) {
        if (rdp.first_tile_index + gfx_lod_tile_offset(i) == tile) {
            rdp.textures_changed[i] = true;
        }
    }
    ge_state_serial++;
}

static void gfx_dp_set_texture_image(uint32_t format, uint32_t size, uint32_t width, uint32_t tex_flags, const void* addr) {
    if (ge_texlog_now()) {
        fprintf(stderr, "texlog: SETTIMG %p format/size=%u/%u width=%u\n", addr, format, size, width);
    }
    rdp.texture_to_load.addr = (const uint8_t*)addr;
    rdp.texture_to_load.siz = size;
    rdp.texture_to_load.width = width;
    rdp.texture_to_load.tex_flags = tex_flags;
}

static void gfx_dp_set_tile(uint8_t fmt, uint32_t siz, uint32_t line, uint32_t tmem, uint8_t tile, uint32_t palette,
                            uint32_t cmt, uint32_t maskt, uint32_t shiftt, uint32_t cms, uint32_t masks,
                            uint32_t shifts) {
    // OTRTODO:
    // SUPPORT_CHECK(tmem == 0 || tmem == 256);
    static uint32_t max_tmem = 0;
    if (ge_texlog_now()) {
        fprintf(stderr, "texlog: SETTILE tile=%u fmt=%u siz=%u tmem=%u line=%u pal=%u\n",
                tile, fmt, siz, tmem, line, palette);
    }
    if (cms == G_TX_WRAP && masks == G_TX_NOMASK) {
        cms = G_TX_CLAMP;
    }
    if (cmt == G_TX_WRAP && maskt == G_TX_NOMASK) {
        cmt = G_TX_CLAMP;
    }

    if (fmt == G_IM_FMT_RGBA && siz < G_IM_SIZ_16b) {
        // HACK: sometimes the game will submit G_IM_FMT_RGBA, G_IM_SIZ_8b/4b, intending it to read as CI8/CI4 with RGBA16 palette
        fmt = G_IM_FMT_CI;
    } else if (fmt == G_IM_FMT_IA && siz == G_IM_SIZ_32b) {
        // HACK: ... and sometimes it submits this, apparently intending it to be I8
        fmt = G_IM_FMT_I;
        siz = G_IM_SIZ_8b;
    }

    masks = (masks == G_TX_NOMASK) ? 0 : masks;
    maskt = (maskt == G_TX_NOMASK) ? 0 : maskt;
    line *= 8;
    {
        /* Value-sensitive: GE re-states the same tile thousands of times a
         * frame, and an unchanged SETTILE must not invalidate the hoisted draw
         * state or split the batch (see ge_state_serial). */
        const auto& t = rdp.texture_tile[tile];
        if (t.palette == palette && t.fmt == fmt && t.siz == siz && t.cms == cms && t.cmt == cmt &&
            t.masks == masks && t.maskt == maskt && t.shifts == shifts && t.shiftt == shiftt &&
            t.line_size_bytes == line && t.tmem == tmem) {
            return;
        }
    }
    rdp.texture_tile[tile].palette = palette; // palette should set upper 4 bits of color index in 4b mode
    rdp.texture_tile[tile].fmt = fmt;
    rdp.texture_tile[tile].siz = siz;
    rdp.texture_tile[tile].cms = cms;
    rdp.texture_tile[tile].cmt = cmt;
    rdp.texture_tile[tile].masks = masks;
    rdp.texture_tile[tile].maskt = maskt;
    rdp.texture_tile[tile].shifts = shifts;
    rdp.texture_tile[tile].shiftt = shiftt;
    rdp.texture_tile[tile].line_size_bytes = line;
    rdp.texture_tile[tile].tmem = tmem;

    ge_tile_touched(tile);
}

static void gfx_dp_set_tile_size(uint8_t tile, uint16_t uls, uint16_t ult, uint16_t lrs, uint16_t lrt) {
    if (ge_texlog_now()) {
        fprintf(stderr, "texlog: SETTILESIZE tile %u s=[%u,%u] t=[%u,%u] -> %ux%u texels\n",
                tile, uls, lrs, ult, lrt, (lrs - uls + 4) / 4, (lrt - ult + 4) / 4);
    }
    {
        const auto& t = rdp.texture_tile[tile];
        if (t.uls == uls && t.ult == ult && t.lrs == lrs && t.lrt == lrt) {
            return; /* value-sensitive, see gfx_dp_set_tile */
        }
    }
    rdp.texture_tile[tile].uls = uls;
    rdp.texture_tile[tile].ult = ult;
    rdp.texture_tile[tile].lrs = lrs;
    rdp.texture_tile[tile].lrt = lrt;
    rdp.texture_tile[tile].width = (lrs - uls + 4) / 4;
    rdp.texture_tile[tile].height = (lrt - ult + 4) / 4;
    ge_tile_touched(tile);
}

static void gfx_dp_load_tlut(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    g_PortHwLoad.loads++;
    g_PortHwLoad.lbytes += ((lrs - uls) + 1u) * 2u; /* GE HW pacing */
    // SUPPORT_CHECK(tile == G_TX_LOADTILE);
    SUPPORT_CHECK(rdp.texture_to_load.siz == G_IM_SIZ_16b);
    SUPPORT_CHECK(rdp.texture_tile[tile].tmem >= 256);

    // GE's gDPLoadTLUTCmd2 (gbi_extension.h, ZX Spectrum screen) ORs 0xff0
    // into both words, decoding as ult == lrt == 1020 with uls == 0. The
    // palette still starts at the SETTIMG address; without this clamp base
    // would be computed pitch*1020 entries out of bounds.
    if (uls == 0 && ult == lrt) {
        ult = 0;
        lrt = 0;
    }

    rdp.texture_tile[tile].uls = uls;
    rdp.texture_tile[tile].ult = ult;
    rdp.texture_tile[tile].lrs = lrs;
    rdp.texture_tile[tile].lrt = lrt;

    const uint32_t width = (lrs - uls + 1);
    const uint32_t height = (lrt - ult + 1);
    const uint32_t pitch = rdp.texture_to_load.width + 1;
    const uint32_t count =  width * height;
    const uint16_t *base = (const uint16_t *)rdp.texture_to_load.addr + pitch * ult + uls;

    if (rdp.texture_tile[tile].tmem == 256) {
        rdp.palette_addrs[0] = (const uint8_t *)base;
        rdp.palette_count256 = count > 256 ? 256 : count;
        if (count >= 256) {
            rdp.palette_addrs[1] = (const uint8_t *)(base + 128);
        }
    } else {
        rdp.palette_addrs[1] = (const uint8_t *)base;
    }

    const uint32_t palofs = rdp.texture_tile[tile].tmem - 256;
    SUPPORT_CHECK(palofs + count <= 256);

    uint16_t *dst = rdp.palette + palofs;
    /* (twenty-fifth build tried a memcmp early-out here; the twenty-sixth
     * run showed Cradle's TLUTs really change between loads - it was a net
     * cost, so: copy, then fingerprint) */
    memcpy(dst, base, count * 2u); /* native texel convention (see import_texture_rgba16) */

    { /* fingerprint the whole active palette (see rdp.palette_sum); it is
       * hashed per 16-entry block and only the blocks this load touched are
       * rehashed, then combined order-sensitively. */
        uint32_t* block_hash = rdp.palette_block_hash;
        static bool block_init = false;
        const uint32_t b0 = palofs >> 4;
        const uint32_t b1 = (palofs + count - 1u) >> 4;
        for (uint32_t b = block_init ? b0 : 0u; b < 16u; b++) {
            if (block_init && b > b1) break;
            uint32_t h = 2166136261u;
            const uint16_t* e = rdp.palette + b * 16u;
            for (uint32_t i = 0; i < 16u; ++i) {
                h = (h ^ e[i]) * 16777619u;
            }
            block_hash[b] = h;
        }
        block_init = true;
        uint32_t sum = 2166136261u;
        for (uint32_t b = 0; b < 16u; b++) { /* order-sensitive, so the block number still counts here */
            sum = (sum ^ block_hash[b]) * 16777619u;
        }
        rdp.palette_sum = sum;
        rdp.palette_ci8_hash_ok = false; /* any TLUT load can change the CI8 range */
    }

    if (ge_texlog_now()) {
        fprintf(stderr, "texlog: LOADTLUT tmem=%u base=%p count=%u palsum=%08x pal01=%04x,%04x\n",
                rdp.texture_tile[tile].tmem, (const void*)base, count, rdp.palette_sum,
                rdp.palette[0], rdp.palette[1]);
    }

    rdp.textures_changed[0] = rdp.textures_changed[1] = true;
    ge_state_serial++;
}

static void gfx_dp_load_block(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t dxt) {
    if (ge_texlog_now()) {
        /* GE pool records stamp their texture number 8 bytes before the
         * data (texload writes *(s16*)leftpos = texnum, then +=8) */
        int texnum = -1;
        if ((uintptr_t)rdp.texture_to_load.addr > 8) {
            texnum = *(const int16_t*)(rdp.texture_to_load.addr - 8);
        }
        fprintf(stderr, "texlog: LOADBLOCK tile=%u tmem=%u addr=%p lrs=%u dxt=%u texnum=%d\n",
                tile, rdp.texture_tile[tile].tmem, rdp.texture_to_load.addr, lrs, dxt, texnum);
    }
    // SUPPORT_CHECK(tile == G_TX_LOADTILE);
    SUPPORT_CHECK(uls == 0);
    SUPPORT_CHECK(ult == 0);

    // The lrs field rather seems to be number of pixels to load
    uint32_t orig_size_bytes = (lrs + 1) << rdp.texture_to_load.siz >> 1;
    g_PortHwLoad.loads++;
    g_PortHwLoad.lbytes += orig_size_bytes; /* GE HW pacing: the TMEM DMA the RDP waited on */
    uint32_t size_bytes = orig_size_bytes;
    if (rdp.texture_to_load.raw_tex_metadata.h_byte_scale != 1 ||
        rdp.texture_to_load.raw_tex_metadata.v_pixel_scale != 1) {
        size_bytes *= rdp.texture_to_load.raw_tex_metadata.h_byte_scale;
        size_bytes *= rdp.texture_to_load.raw_tex_metadata.v_pixel_scale;
    }

    LoadedTexture& loaded_texture = rdp.loaded_texture[rdp.texture_tile[tile].tmem];
    rdp.texture_to_load.tmem = rdp.texture_tile[tile].tmem;
    if (loaded_texture.addr == rdp.texture_to_load.addr && loaded_texture.orig_size_bytes == orig_size_bytes &&
        loaded_texture.size_bytes == size_bytes && loaded_texture.full_size_bytes == size_bytes &&
        loaded_texture.line_size_bytes == size_bytes && loaded_texture.full_image_line_size_bytes == size_bytes &&
        loaded_texture.tex_flags == rdp.texture_to_load.tex_flags &&
        memcmp(&loaded_texture.raw_tex_metadata, &rdp.texture_to_load.raw_tex_metadata,
               sizeof(loaded_texture.raw_tex_metadata)) == 0 &&
        ge_tmem_block_is_registered(rdp.texture_to_load.addr, rdp.texture_tile[tile].tmem, orig_size_bytes)) {
        /* Value-sensitive: the same texture re-loaded at the same TMEM leaves
         * the TMEM record, cache key and bound texture unchanged, so skip it.
         * An entry import_texture rewrote for a mip tile fails this compare and
         * takes the full path. */
        return;
    }
    loaded_texture.orig_size_bytes = orig_size_bytes;
    loaded_texture.size_bytes = size_bytes;
    loaded_texture.full_size_bytes = size_bytes;
    loaded_texture.line_size_bytes = size_bytes;
    loaded_texture.full_image_line_size_bytes = size_bytes;
    loaded_texture.tex_flags = rdp.texture_to_load.tex_flags;
    loaded_texture.raw_tex_metadata = rdp.texture_to_load.raw_tex_metadata;
    loaded_texture.addr = rdp.texture_to_load.addr;

    ge_tmem_register_block(rdp.texture_to_load.addr, rdp.texture_tile[tile].tmem, orig_size_bytes);

    /* (the shadow TMEM mirror is deferred: ge_shadow_tmem_materialize) */

    rdp.textures_changed[0] = rdp.textures_changed[1] = true;
    ge_state_serial++;
}

static void gfx_dp_load_tile(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    g_PortHwLoad.loads++;
    g_PortHwLoad.lbytes += (((lrs - uls) + 1u) * ((lrt - ult) + 1u)) << rdp.texture_to_load.siz >> 1; /* GE HW pacing */
    SUPPORT_CHECK(tile == G_TX_LOADTILE);

    uint32_t offset_x = uls >> G_TEXTURE_IMAGE_FRAC;
    uint32_t offset_y = ult >> G_TEXTURE_IMAGE_FRAC;
    uint32_t tile_width = ((lrs - uls) >> G_TEXTURE_IMAGE_FRAC) + 1;
    uint32_t tile_height = ((lrt - ult) >> G_TEXTURE_IMAGE_FRAC) + 1;
    uint32_t full_image_width = rdp.texture_to_load.width + 1;

    uint32_t offset_x_in_bytes = offset_x << rdp.texture_to_load.siz >> 1;
    uint32_t tile_line_size_bytes = tile_width << rdp.texture_to_load.siz >> 1;
    uint32_t full_image_line_size_bytes = full_image_width << rdp.texture_to_load.siz >> 1;

    uint32_t orig_size_bytes = tile_line_size_bytes * tile_height;
    uint32_t size_bytes = orig_size_bytes;
    uint32_t start_offset_bytes = full_image_line_size_bytes * offset_y + offset_x_in_bytes;

    float h_byte_scale = rdp.texture_to_load.raw_tex_metadata.h_byte_scale;
    float v_pixel_scale = rdp.texture_to_load.raw_tex_metadata.v_pixel_scale;

    if (h_byte_scale != 1 || v_pixel_scale != 1) {
        start_offset_bytes = h_byte_scale * (v_pixel_scale * offset_y * full_image_line_size_bytes + offset_x_in_bytes);
        size_bytes *= h_byte_scale * v_pixel_scale;
        full_image_line_size_bytes *= h_byte_scale;
        tile_line_size_bytes *= h_byte_scale;
    }

    LoadedTexture& loaded_texture = rdp.loaded_texture[rdp.texture_tile[tile].tmem];
    rdp.texture_to_load.tmem = rdp.texture_tile[tile].tmem;
    loaded_texture.orig_size_bytes = orig_size_bytes;
    loaded_texture.size_bytes = size_bytes;
    loaded_texture.full_size_bytes = full_image_line_size_bytes * tile_height;
    loaded_texture.full_image_line_size_bytes = full_image_line_size_bytes;
    loaded_texture.line_size_bytes = tile_line_size_bytes;
    loaded_texture.tex_flags = rdp.texture_to_load.tex_flags;
    loaded_texture.raw_tex_metadata = rdp.texture_to_load.raw_tex_metadata;
    loaded_texture.addr = rdp.texture_to_load.addr + start_offset_bytes;

    rdp.texture_tile[tile].uls = uls;
    rdp.texture_tile[tile].ult = ult;
    rdp.texture_tile[tile].lrs = lrs;
    rdp.texture_tile[tile].lrt = lrt;
    rdp.texture_tile[tile].width = ((lrs - uls) >> G_TEXTURE_IMAGE_FRAC) + 1;
    rdp.texture_tile[tile].height = ((lrt - ult) >> G_TEXTURE_IMAGE_FRAC) + 1;

    rdp.textures_changed[0] = rdp.textures_changed[1] = true;
}

static void gfx_dp_set_combine_mode(uint32_t rgb, uint32_t alpha, uint32_t rgb_cyc2, uint32_t alpha_cyc2) {
    const uint64_t cm = rgb | (alpha << 16) | ((uint64_t)rgb_cyc2 << 28) | ((uint64_t)alpha_cyc2 << 44);
    if (cm != rdp.combine_mode) {
        rdp.combine_mode = cm;
        ge_state_serial++; /* value-sensitive, see ge_state_serial */
    }
}

static inline uint32_t color_comb(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a & 0xf) | ((b & 0xf) << 4) | ((c & 0x1f) << 8) | ((d & 7) << 13);
}

static inline uint32_t alpha_comb(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a & 7) | ((b & 7) << 3) | ((c & 7) << 6) | ((d & 7) << 9);
}

static void gfx_dp_set_grayscale_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rdp.grayscale_color.r = r;
    rdp.grayscale_color.g = g;
    rdp.grayscale_color.b = b;
    rdp.grayscale_color.a = a;
}

static void gfx_dp_set_env_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rdp.env_color.r = r;
    rdp.env_color.g = g;
    rdp.env_color.b = b;
    rdp.env_color.a = a;
}

static void gfx_dp_set_prim_color(uint8_t m, uint8_t l, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    { /* diagnostic (GE_PRIMZERO=1): zero prim rgb - isolates ADDPRIM
       * tinting (used to find the blue Depot floor bug) */
        static int primzero = -1;
        if (primzero < 0) primzero = getenv("GE_PRIMZERO") != NULL;
        if (primzero) {
            r = g = b = 0;
        }
    }
    rdp.prim_lod_fraction = l;
    rdp.prim_min_level = m;
    rdp.prim_color.r = r;
    rdp.prim_color.g = g;
    rdp.prim_color.b = b;
    rdp.prim_color.a = a;
    rdp.fill_color.r = r;
    rdp.fill_color.g = g;
    rdp.fill_color.b = b;
    rdp.fill_color.a = a;
    rdp.tex_min_lod = m;

}

static void gfx_dp_set_fog_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rdp.fog_color.r = r;
    rdp.fog_color.g = g;
    rdp.fog_color.b = b;
    rdp.fog_color.a = a;
}

/* exact 8-bit fill colour from viSetFillColor's tagged NOOP (see fr.c):
 * valid for exactly the next SETFILLCOLOR, beating the RGBA5551 roundtrip
 * that drew the sky backdrop band a few steps off the fans' env colour */
static uint32_t s_GeExactFillRGB;
static int s_GeExactFillValid;

static void gfx_dp_set_fill_color(uint32_t packed_color) {
    uint16_t col16 = (uint16_t)packed_color;
    uint32_t r = col16 >> 11;
    uint32_t g = (col16 >> 6) & 0x1f;
    uint32_t b = (col16 >> 1) & 0x1f;
    uint32_t a = col16 & 1;
    if (s_GeExactFillValid) {
        s_GeExactFillValid = 0;
        rdp.fill_color.r = (s_GeExactFillRGB >> 16) & 0xFF;
        rdp.fill_color.g = (s_GeExactFillRGB >> 8) & 0xFF;
        rdp.fill_color.b = s_GeExactFillRGB & 0xFF;
        rdp.fill_color.a = 255;
        return;
    }
    rdp.fill_color.r = SCALE_5_8(r);
    rdp.fill_color.g = SCALE_5_8(g);
    rdp.fill_color.b = SCALE_5_8(b);
    rdp.fill_color.a = a * 255;
}

static void gfx_dp_set_subpixel_offset(int16_t x, int16_t y) {
    rdp.subpixel_ofs_x = x;
    rdp.subpixel_ofs_y = y;
}

static void gfx_draw_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    uint32_t saved_other_mode_h = rdp.other_mode_h;
    uint32_t cycle_type = (rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE));

    if (cycle_type == G_CYC_COPY) {
        rdp.other_mode_h = (rdp.other_mode_h & ~(3U << G_MDSFT_TEXTFILT)) | G_TF_POINT;
    }

    ulx += rdp.subpixel_ofs_x;
    lrx += rdp.subpixel_ofs_x;
    uly += rdp.subpixel_ofs_y;
    lry += rdp.subpixel_ofs_y;

    // U10.2 coordinates
    float ulxf = ulx;
    float ulyf = uly;
    float lrxf = lrx;
    float lryf = lry;

    if (s_GeRectFracOn) {
        ulxf += s_GeRectFrac[0];
        ulyf += s_GeRectFrac[1];
        lrxf += s_GeRectFrac[2];
        lryf += s_GeRectFrac[3];
    }
    if (s_GeHudScale) {
        ulxf = s_GeHudPivotX + (ulxf - s_GeHudPivotX) * 0.5f;
        ulyf = s_GeHudPivotY + (ulyf - s_GeHudPivotY) * 0.5f;
        lrxf = s_GeHudPivotX + (lrxf - s_GeHudPivotX) * 0.5f;
        lryf = s_GeHudPivotY + (lryf - s_GeHudPivotY) * 0.5f;
    }

    ulxf = ulxf / (4.0f * HALF_SCREEN_WIDTH) - 1.0f;
    ulyf = -(ulyf / (4.0f * HALF_SCREEN_HEIGHT)) + 1.0f;
    lrxf = lrxf / (4.0f * HALF_SCREEN_WIDTH) - 1.0f;
    lryf = -(lryf / (4.0f * HALF_SCREEN_HEIGHT)) + 1.0f;

    ulxf = gfx_adjust_x_for_aspect_ratio(ulxf);
    lrxf = gfx_adjust_x_for_aspect_ratio(lrxf);

    struct LoadedVertex* ul = &rsp.loaded_vertices[MAX_VERTICES + 0];
    struct LoadedVertex* ll = &rsp.loaded_vertices[MAX_VERTICES + 1];
    struct LoadedVertex* lr = &rsp.loaded_vertices[MAX_VERTICES + 2];
    struct LoadedVertex* ur = &rsp.loaded_vertices[MAX_VERTICES + 3];

    ul->x = ulxf;
    ul->y = ulyf;
    ul->z = -1.0f;
    ul->w = 1.0f;

    ll->x = ulxf;
    ll->y = lryf;
    ll->z = -1.0f;
    ll->w = 1.0f;

    lr->x = lrxf;
    lr->y = lryf;
    lr->z = -1.0f;
    lr->w = 1.0f;

    ur->x = lrxf;
    ur->y = ulyf;
    ur->z = -1.0f;
    ur->w = 1.0f;

    // The coordinates for texture rectangle shall bypass the viewport setting
    struct XYWidthHeight default_viewport = { 0, (int16_t)SCREEN_HEIGHT, (uint32_t)SCREEN_WIDTH, (uint32_t)SCREEN_HEIGHT };
    struct XYWidthHeight viewport_saved = rdp.viewport;
    uint32_t geometry_mode_saved = rsp.geometry_mode;

    gfx_adjust_viewport_or_scissor(&default_viewport);

    rdp.viewport = default_viewport;
    rdp.viewport_or_scissor_changed = true;
    rsp.geometry_mode = 0;

    s_DrawingRect = true;
    gfx_sp_tri1(MAX_VERTICES + 0, MAX_VERTICES + 1, MAX_VERTICES + 3, true);
    gfx_sp_tri1(MAX_VERTICES + 1, MAX_VERTICES + 2, MAX_VERTICES + 3, true);
    s_DrawingRect = false;

    rsp.geometry_mode = geometry_mode_saved;
    rdp.viewport = viewport_saved;
    rdp.viewport_or_scissor_changed = true;

    if (cycle_type == G_CYC_COPY) {
        rdp.other_mode_h = saved_other_mode_h;
    }
}

/**
 * Textured rectangles are GE's entire 2D layer (HUD, menus, text, images).
 * In widescreen modes, draw them pinned to a centered native-aspect band so
 * they never spread or stretch across a wide window. Color fills keep the
 * plain path (they must span the window: fades, letterbox, dividers).
 */
/*
 * Full-screen overlay in a WIDE window: mirror the native 4:3 image into the
 * side bands instead of stretching it across the whole window.
 *
 *      | mirrored |     native 4:3     | mirrored |
 *
 * The death-blood and gunbarrel-blood overlays are a 96x80 image with real
 * structure - drips running down the screen - so stretching it to an
 * ultrawide window smears it. Reflecting it about each seam keeps the drips
 * at their authored proportions and still fills the bands.
 *
 * Geometry: in the pinned (centered) mapping gfx_adjust_x_for_aspect_ratio
 * scales native x by native_aspect/window_aspect, so the native screen
 * occupies |x| <= r with r = native/window and each band is (1 - r) wide in
 * NDC. Feeding NATIVE coordinates outside [0, nw4] therefore lands exactly
 * in the bands - the pillar is (window/native - 1) * nw4/2 in U10.2 units.
 * gfx_draw_rectangle's viewport is the full window (it does not pass
 * preserve_aspect) and in a wide gameplay frame the scissor uses the
 * full-window mapping too, so nothing clips the out-of-band draws.
 *
 * UVs are reflected about the seam: at the seam the band u equals the
 * centre edge u and runs back INTO the image as you move outward. A band
 * narrower than the image mirrors only the part that fits, which is right.
 *
 * Returns false (caller falls back to the old stretch) when the window is
 * not wider than native, or when GE_NOBLOODMIRROR=1.
 */
static bool gfx_ge_draw_rect_mirrored(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    const int32_t nw4 = (int32_t) gfx_current_native_viewport.width * 4;
    static int disabled = -1;
    if (disabled < 0) {
        disabled = getenv("GE_NOBLOODMIRROR") != NULL;
    }
    if (disabled) {
        return false;
    }

    const float wf = gfx_current_dimensions.aspect_ratio / gfx_current_native_aspect;
    if (!(wf > 1.001f)) {
        return false;           /* 4:3 or taller: nothing to fill */
    }

    const int32_t width4 = lrx - ulx;
    if (width4 <= 0 || nw4 <= 0) {
        return false;
    }

    /* band width from the RECT's own width, not the whole screen's - for a
     * full-screen overlay they are the same (the old form), for a quadrant
     * overlay the bands fill exactly the quadrant's own side gaps */
    const int32_t pillar4 = (int32_t) ((wf - 1.0f) * (float) width4 * 0.5f + 0.5f);
    if (pillar4 <= 0) {
        return false;
    }

    struct LoadedVertex* ul = &rsp.loaded_vertices[MAX_VERTICES + 0];
    struct LoadedVertex* ll = &rsp.loaded_vertices[MAX_VERTICES + 1];
    struct LoadedVertex* lr = &rsp.loaded_vertices[MAX_VERTICES + 2];
    struct LoadedVertex* ur = &rsp.loaded_vertices[MAX_VERTICES + 3];

    /* the texrect handler left the image u range on these verts */
    const float u_left = ul->u;         /* u at native x = ulx */
    const float u_right = lr->u;        /* u at native x = lrx */
    const float du = (u_right - u_left) * ((float) pillar4 / (float) width4);

    s_GeAspectForceCenter = 1;
    gfx_update_aspect_mode();

    /* The scissor is the 4:3 band whenever the aspect mode is non-zero
     * (gfx_dp_set_scissor passes preserve_aspect = aspect_mode != 0), which
     * is exactly the pinned case we are drawing outside of - without this
     * the side bands are clipped away and you see the old pinned image with
     * bare margins. Widen to the full window for the three draws, then put
     * the DL's scissor back. */
    XYWidthHeight scissor_saved = rdp.scissor;
    {
        /* widen to the player's own viewport (raw scissor, plain-adjusted),
         * NOT the whole window - a quadrant's mirrored bands must not leak
         * into a neighbour's viewport; solo's raw scissor is the full
         * screen, so nothing changes there */
        XYWidthHeight full = s_GeRawScissor;
        gfx_adjust_viewport_or_scissor(&full);
        rdp.scissor = full;
        rdp.viewport_or_scissor_changed = true;
    }

    /* centre: the native image, untouched */
    gfx_draw_rectangle(ulx, uly, lrx, lry);

    /* left band: reflected about the ulx seam */
    ul->u = u_left + du;  ll->u = u_left + du;
    ur->u = u_left;       lr->u = u_left;
    gfx_draw_rectangle(ulx - pillar4, uly, ulx, lry);

    /* right band: reflected about the lrx seam */
    ul->u = u_right;      ll->u = u_right;
    ur->u = u_right - du; lr->u = u_right - du;
    gfx_draw_rectangle(lrx, uly, lrx + pillar4, lry);

    /* leave the verts as the caller set them */
    ul->u = u_left;  ll->u = u_left;
    ur->u = u_right; lr->u = u_right;

    rdp.scissor = scissor_saved;
    rdp.viewport_or_scissor_changed = true;

    s_GeAspectForceCenter = 0;
    gfx_update_aspect_mode();
    return true;
}

static void gfx_ge_draw_rect_pinned(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    /* marked overlay in a wide frame: mirrored side bands, either pin mode */
    if (gfx_ge_mirror_pillars && gfx_ge_aspect_pin != 0 &&
        gfx_ge_draw_rect_mirrored(ulx, uly, lrx, lry)) {
        return;
    }

    if (gfx_ge_aspect_pin == 1) {
        /* Full-screen overlay exception (death-blood clamped
         * to 4:3): in a wide gameplay frame, a texrect covering the whole
         * native screen is an overlay (death blood, damage flashes) and
         * must bleed across the entire window like the 3D behind it -
         * pinning it leaves bare side bands. 4px tolerance for the
         * (x*4)-1 idiom and view-rect insets. Pin mode 2 (frontend)
         * keeps every rect pinned. */
        const int32_t nw4 = (int32_t) gfx_current_native_viewport.width * 4;
        const int32_t nh4 = (int32_t) gfx_current_native_viewport.height * 4;
        if (ulx <= 16 && uly <= 16 && lrx >= nw4 - 16 && lry >= nh4 - 16) {
            gfx_draw_rectangle(ulx, uly, lrx, lry);
            return;
        }
    }
    if (gfx_ge_aspect_pin != 0) {
        s_GeAspectForceCenter = 1;
        gfx_update_aspect_mode();
        gfx_draw_rectangle(ulx, uly, lrx, lry);
        s_GeAspectForceCenter = 0;
        gfx_update_aspect_mode();
    } else {
        gfx_draw_rectangle(ulx, uly, lrx, lry);
    }
}

static void gfx_dp_texture_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry, uint8_t tile, int16_t uls,
                                     int16_t ult, int16_t dsdx, int16_t dtdy, bool flip) {
    const bool precise = s_GeRectFracPending; /* one-shot: this rect only */
    s_GeRectFracPending = false;

    /* GE: zero-extent texrects occur (text path with bailed textures); the
     * N64 RDP draws nothing for them, but the math below divides by the
     * extent - SIGFPE. Match hardware: draw nothing. */
    if (lrx <= ulx || lry <= uly) {
        return;
    }

    /* Font glyphs under the 2D filter: grow the rect half a texel per side
     * with a transparent border, so bilinear can finish the edge fade that
     * the tight glyph cells cut off. GE_NOGLYPHPAD=1 turns it off. */
    s_RectGlyphBorder = false;
    if (s_GeTextGlyphs && gfx_filter_2d != 0 && !flip && dsdx > 0 && dtdy > 0
        && (rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) != G_CYC_COPY) {
        static int glyphpad = -1;
        if (glyphpad < 0) {
            glyphpad = getenv("GE_NOGLYPHPAD") == NULL;
        }
        if (glyphpad) {
            const int32_t ex = (2048 + dsdx / 2) / dsdx;
            const int32_t ey = (2048 + dtdy / 2) / dtdy;
            /* A glyph that textRender clipped at a list's top or bottom starts
             * or ends inside its texture, so grow an edge only where it sits on
             * the texture's own edge (half a texel of slack); otherwise the ink
             * row past the cut would be drawn outside the clip. */
            const int32_t t0 = (int32_t) ult - (int32_t) rdp.texture_tile[tile].ult * 8;
            const int32_t t1 = t0 + (((lry - uly) * dtdy) >> 7);
            const int32_t th = ((int32_t) rdp.texture_tile[tile].lrt - (int32_t) rdp.texture_tile[tile].ult + 4) / 4;
            const bool growtop = t0 <= 16;
            const bool growbot = t1 + 16 >= th * 32;
            ulx -= ex;
            lrx += ex;
            if (growtop) {
                uly -= ey;
                ult = (int16_t) (ult - ((ey * dtdy) >> 7));
            }
            if (growbot) {
                lry += ey;
            }
            uls = (int16_t) (uls - ((ex * dsdx) >> 7));   /* rect units -> S10.5 texels */
            s_RectGlyphBorder = true;
        }
    }

    uint64_t saved_combine_mode = rdp.combine_mode;
    int32_t lrx0 = lrx, lry0 = lry;   /* pre-round-up extents (GE_RECTUV) */
    if ((rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_COPY) {
        // Per RDP Command Summary Set Tile's shift s and this dsdx should be set to 4 texels
        // Divide by 4 to get 1 instead
        dsdx >>= 2;

        // Color combiner is turned off in copy mode
        gfx_dp_set_combine_mode(color_comb(0, 0, 0, G_CCMUX_TEXEL0), alpha_comb(0, 0, 0, G_ACMUX_TEXEL0), 0, 0);

        // Per documentation one extra pixel is added in this modes to each edge
        lrx += 1 << 2;
        lry += 1 << 2;
    } else {
        /* GE: 1/2-cycle rects use the (x << 2) - 1 idiom, but the RDP still
         * fills partially covered pixels, so round the lr edge up like the
         * hardware does. Otherwise thin gaps appear between 1px-row rects. */
        lrx0 = lrx;
        lry0 = lry;
        /* Skip this rounding for a grown glyph: its lr edge is sub-pixel on
         * purpose and the UVs already match it, so rounding up would stretch
         * the glyph w+1 texels over w+1.5 pixels. */
        if (!s_RectGlyphBorder && !precise) {
            lrx = (lrx + 3) & ~3;
            lry = (lry + 3) & ~3;
        }
    }

    /* GE_RECTDBG=1: dump icon-sized texrects (mine-icon extra-row hunt). */
    {
        static int rectdbg = -1;
        if (rectdbg < 0) {
            rectdbg = getenv("GE_RECTDBG") != NULL;
        }
        if (rectdbg) {
            int32_t h4 = lry - uly;
            if (h4 >= 48 && h4 <= 80) {
                fprintf(stderr, "gfx: rect ul=(%d,%d) lr=(%d,%d) lr0=(%d,%d) st=(%d,%d) dsdt=(%d,%d)\n",
                        ulx, uly, lrx, lry, lrx0, lry0, uls, ult, dsdx, dtdy);
            }
        }
    }

    // uls and ult are S10.5
    // dsdx and dtdy are S5.10
    // lrx, lry, ulx, uly are U10.2
    // lrs, lrt are S10.5

    const int16_t width = flip ? lry - uly : lrx - ulx;
    const int16_t height = flip ? lrx - ulx : lry - uly;
    float lrs = ((uls << 7) + dsdx * width) >> 7;
    float lrt = ((ult << 7) + dtdy * height) >> 7;

    /* Texrect UV cap: the round-up above makes the rect sample past its last
     * texel, a doubled edge row when upscaled, so UVs are capped at the
     * unrounded extent (GE_NORECTUV=1 turns this off). */
    {
        static int rectuv = -1;
        if (rectuv < 0) {
            rectuv = getenv("GE_NORECTUV") == NULL;
        }
        if (rectuv) {
            const int16_t width0 = flip ? lry0 - uly : lrx0 - ulx;
            const int16_t height0 = flip ? lrx0 - ulx : lry0 - uly;
            lrs = ((uls << 7) + dsdx * width0) >> 7;
            lrt = ((ult << 7) + dtdy * height0) >> 7;
        }
    }

    struct LoadedVertex* ul = &rsp.loaded_vertices[MAX_VERTICES + 0];
    struct LoadedVertex* ll = &rsp.loaded_vertices[MAX_VERTICES + 1];
    struct LoadedVertex* lr = &rsp.loaded_vertices[MAX_VERTICES + 2];
    struct LoadedVertex* ur = &rsp.loaded_vertices[MAX_VERTICES + 3];
    ul->u = uls;
    ul->v = ult;
    lr->u = lrs;
    lr->v = lrt;
    if (!flip) {
        ll->u = uls;
        ll->v = lrt;
        ur->u = lrs;
        ur->v = ult;
    } else {
        ll->u = lrs;
        ll->v = ult;
        ur->u = uls;
        ur->v = lrt;
    }

    uint8_t saved_tile = rdp.first_tile_index;
    if (saved_tile != tile) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
    }
    rdp.first_tile_index = tile;

    /* the rect's own UV span, for the sampler's "does this rect tile at all"
     * test (s_RectUv*, see the wrap override in gfx_sp_tri1) */
    s_RectUvS0 = (int32_t) ((uls < lrs) ? uls : lrs);
    s_RectUvS1 = (int32_t) ((uls < lrs) ? lrs : uls);
    s_RectUvT0 = (int32_t) ((ult < lrt) ? ult : lrt);
    s_RectUvT1 = (int32_t) ((ult < lrt) ? lrt : ult);
    s_RectUvValid = true;
    s_GeRectFracOn = precise;
    gfx_ge_draw_rect_pinned(ulx, uly, lrx, lry);
    s_GeRectFracOn = false;
    s_RectUvValid = false;
    s_RectGlyphBorder = false;
    if (saved_tile != tile) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
    }
    rdp.first_tile_index = saved_tile;
    rdp.combine_mode = saved_combine_mode;
}

static void gfx_dp_image_rectangle(int32_t tile, int32_t w, int32_t h,
                                   int32_t ulx, int32_t uly, int16_t uls, int16_t ult,
                                   int32_t lrx, int32_t lry, int16_t lrs, int16_t lrt) {
    uint64_t saved_combine_mode = rdp.combine_mode;

    struct LoadedVertex* ul = &rsp.loaded_vertices[MAX_VERTICES + 0];
    struct LoadedVertex* ll = &rsp.loaded_vertices[MAX_VERTICES + 1];
    struct LoadedVertex* lr = &rsp.loaded_vertices[MAX_VERTICES + 2];
    struct LoadedVertex* ur = &rsp.loaded_vertices[MAX_VERTICES + 3];
    ul->u = uls * 32;
    ul->v = ult * 32;
    lr->u = lrs * 32;
    lr->v = lrt * 32;
    ll->u = uls * 32;
    ll->v = lrt * 32;
    ur->u = lrs * 32;
    ur->v = ult * 32;

    // ensure we have the correct texture size
    rdp.texture_tile[tile].line_size_bytes = w << rdp.texture_tile[tile].siz >> 1;
    rdp.texture_tile[tile].width = w;
    rdp.texture_tile[tile].height = h;
    rdp.texture_tile[tile].cms = 0;
    rdp.texture_tile[tile].cmt = 0;
    rdp.texture_tile[tile].shifts = 0;
    rdp.texture_tile[tile].shiftt = 0;
    auto& loadtex = rdp.loaded_texture[rdp.texture_tile[tile].tmem];
    loadtex.full_image_line_size_bytes = loadtex.line_size_bytes = rdp.texture_tile[tile].line_size_bytes;
    loadtex.size_bytes = loadtex.orig_size_bytes = loadtex.full_size_bytes = loadtex.line_size_bytes * h;

    uint8_t saved_tile = rdp.first_tile_index;
    if (saved_tile != tile) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
    }
    rdp.first_tile_index = tile;

    gfx_ge_draw_rect_pinned(ulx, uly, lrx, lry);
    if (saved_tile != tile) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
    }
    rdp.first_tile_index = saved_tile;

    rdp.combine_mode = saved_combine_mode;
}

static void gfx_dp_fill_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    const bool precise = s_GeRectFracPending; /* one-shot: this rect only (G_NOOP tag 0x59) */
    s_GeRectFracPending = false;
    { /* GE bring-up (GE_DUMP_BOND): the sky-wedge fill hunt */
        static int logged = -1;
        if (logged < 0) logged = getenv("GE_DUMP_BOND") != NULL ? 0 : 999;
        if (logged < 20 && (rdp.fill_color.r | rdp.fill_color.g | rdp.fill_color.b) != 0) {
            logged++;
            fprintf(stderr, "fillrect: %d,%d..%d,%d rgb=%d,%d,%d zimg=%d\n",
                    ulx / 4, uly / 4, lrx / 4, lry / 4,
                    rdp.fill_color.r, rdp.fill_color.g, rdp.fill_color.b,
                    rdp.color_image_address == rdp.z_buf_address);
        }
    }
    if (rdp.color_image_address == rdp.z_buf_address) {
        // Don't clear Z buffer here since we already did it with glClear
        return;
    }
    /* MP split-screen: each player's z-clear sets the colour image to zbuf plus
     * the viewport's row offset and fills a quadrant rect, which the
     * exact-equality test above misses, so the 0xFFFC z pattern would flash
     * near-white over the other viewports. Any fill whose colour image lands
     * inside the z-buffer (max 440x330 16-bit) is treated as a depth clear. */
    if (rdp.z_buf_address != 0 &&
        rdp.color_image_address > rdp.z_buf_address &&
        (uint8_t*)rdp.color_image_address < (uint8_t*)rdp.z_buf_address + 440u * 330u * 2u &&
        rdp.fill_color.r >= 240 && rdp.fill_color.g >= 240 && rdp.fill_color.b >= 240) {
        return;
    }
    uint32_t mode = (rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE));

    // OTRTODO: This is a bit of a hack for widescreen screen fades, but it'll work for now...
    if (ulx == 0 && uly == 0 && lrx == 319 * 4 && lry == 239 * 4) {
        ulx = -1024;
        uly = -1024;
        lrx = 2048;
        lry = 2048;
    }

    if (mode == G_CYC_COPY || mode == G_CYC_FILL) {
        // Per documentation one extra pixel is added in this modes to each edge
        lrx += 1 << 2;
        lry += 1 << 2;
    }

    for (int i = MAX_VERTICES; i < MAX_VERTICES + 4; i++) {
        struct LoadedVertex* v = &rsp.loaded_vertices[i];
        v->color = rdp.fill_color;
    }

    uint64_t saved_combine_mode = rdp.combine_mode;

    if (mode == G_CYC_FILL) {
        gfx_dp_set_combine_mode(color_comb(0, 0, 0, G_CCMUX_SHADE), alpha_comb(0, 0, 0, G_ACMUX_SHADE), 0, 0);
    }

    s_GeRectFracOn = precise;
    {
        unsigned drawn_before = ge_tris_drawn;
        /* A marked full-screen wash (the gunbarrel's red and the fade after
         * it) must cover the whole window, not the pinned 4:3 band. The
         * mirror helper already widens mapping and scissor, and for a flat
         * colour mirroring looks the same as spanning, so it is reused. */
        if (!(gfx_ge_mirror_pillars && gfx_ge_aspect_pin != 0 &&
              gfx_ge_draw_rect_mirrored(ulx, uly, lrx, lry)))
        {
            /* Pin fill rects like texrects, so a fill (such as the watch
             * highlight) stays aligned with the text beside it. Fills that span
             * the full native width (letterbox bars, fades, sky) stay full. */
            const int32_t nw4 = (int32_t) gfx_current_native_viewport.width * 4;
            /* A fill covering the current raw scissor's x-range also counts as
             * spanning, such as the MP pause darkener over one quadrant. */
            const int32_t vsx0 = (int32_t) s_GeRawScissor.x * 4;
            const int32_t vsx1 = ((int32_t) s_GeRawScissor.x + (int32_t) s_GeRawScissor.width) * 4;
            /* ...and so does a fill marked PORT_HUD_ANCHOR_SPAN (sky.c's
             * backdrop), which in 3P/4P spans its quadrant under a full-screen
             * scissor. Pinning it would leave black strips beside each quadrant. */
            if (gfx_ge_aspect_pin == 1 && !(ulx <= 16 && lrx >= nw4 - 16) &&
                !(ulx <= vsx0 + 16 && lrx >= vsx1 - 16) && !gfx_ge_span_marker()) {
                gfx_ge_draw_rect_pinned(ulx, uly, lrx, lry);
            } else {
                gfx_draw_rectangle(ulx, uly, lrx, lry);
            }
        }
        {
            static int logged = -1;
            if (logged < 0) logged = getenv("GE_DUMP_BOND") != NULL ? 0 : 999;
            if (logged < 8 && (rdp.fill_color.r | rdp.fill_color.g | rdp.fill_color.b) != 0 &&
                rdp.color_image_address != rdp.z_buf_address) {
                logged++;
                fprintf(stderr, "fillrect drawn %+d scissor=%d,%d %ux%u\n",
                        (int)(ge_tris_drawn - drawn_before),
                        rdp.scissor.x, rdp.scissor.y, rdp.scissor.width, rdp.scissor.height);
            }
        }
    }
    s_GeRectFracOn = false;
    rdp.combine_mode = saved_combine_mode;
}

/* ==== CPU-emitted RDP triangles (GE sky + water) =========================
 *
 * sky.c runs the RDP edge-walker setup on the CPU: it computes raw RDP
 * triangle commands (G_TRI_FILL / G_TRI_SHADE_TXTR families - edge, shade
 * and texture coefficient blocks) and streams them through
 * G_RDPHALF_1/G_RDPHALF_CONT words, which the F3D microcode forwards to
 * the RDP untouched. Here we reassemble that 64-bit command stream and
 * INVERT the coefficients back into three screen-space vertices with
 * per-vertex shade and S/T, then draw through the same screen-space path
 * texrects use. Attribute planes are reconstructed from the base value
 * (referenced at the major-edge start XH,YH) plus the DDx/DDy gradients;
 * the DDe edge-stepper terms are redundant for a plane and ignored.
 *
 * Command sizes (32-bit halves): edge 8, shade +16, texture +16, z +4. */

#define RDP_TRI_MAX_HALVES 44

static uint32_t s_RdpTriBuf[RDP_TRI_MAX_HALVES];
static int s_RdpTriLen;
static int s_RdpTriExpect;

static inline float rdp_fix_16_16(uint32_t hi16, uint32_t lo16) {
    return (float)(int32_t)((hi16 << 16) | (lo16 & 0xFFFFu)) / 65536.0f;
}

/* attribute plane: base + gradients, evaluated at a vertex */
struct RdpAttrPlane {
    float base, ddx, ddy;
};

static inline float rdp_plane_at(const struct RdpAttrPlane* p, float dx, float dy) {
    return p->base + p->ddx * dx + p->ddy * dy;
}

/* decode one attribute plane from a coefficient block laid out as
 * qwords [int base][int ddx][frac base][frac ddx][int dde][int ddy][frac dde][frac ddy];
 * sel picks the 16-bit lane: 0 = hi of first word, 1 = lo of first word,
 * 2 = hi of second word, 3 = lo of second word. */
static struct RdpAttrPlane rdp_decode_plane(const uint32_t* blk, int sel) {
    const int word = sel >> 1;
    const int hi = (sel & 1) == 0;
    #define LANE(q) (hi ? (blk[(q) * 2 + word] >> 16) : (blk[(q) * 2 + word] & 0xFFFFu))
    struct RdpAttrPlane p;
    p.base = rdp_fix_16_16(LANE(0), LANE(2));
    p.ddx = rdp_fix_16_16(LANE(1), LANE(3));
    p.ddy = rdp_fix_16_16(LANE(5), LANE(7));
    #undef LANE
    return p;
}

/* ---- GE sky float sidechannel -----------------------------------------
 * The sky/water renderer (sky.c skyRenderTri/skyRenderFull) computes its
 * triangles in float, then packs them into N64 RDP fixed-point coefficient
 * blocks (Y at quarter-scanline s11.2, attribute planes s15.16 referenced
 * at the quantized XH,YH). Reconstructing from those quantized fields made the
 * sky jitter against the smoothly-rendered world as the camera moved. The PC
 * arm of the two emitters pushes the exact
 * float corners here, one entry per RDP command, same thread, same order;
 * gfx_rdp_triangle prefers the entry when its Y range matches the decoded
 * command (guard against any pairing slip) and falls back to the quantized
 * reconstruction otherwise. Attributes are pushed pre-multiplied by 1/W
 * (S*stmul*invw, T*stmul*invw, invw), which interpolate linearly in screen
 * space - the same planes the RDP walks, just without the 16.16 rounding. */
extern "C" int ge_sky_tri_active; /* GL backend: mipmap+trilinear the bound texture while set */
int ge_sky_tri_active;

#define GE_SKY_SEQ_MAGIC 0x53590000u /* 'SY' - G_NOOP w1 pairing marker */
static uint32_t s_GeSkyPendingSeq;
static int s_GeSkyPendingValid;

struct GeSkyEntry {
    int full, n;
    uint32_t seq;
    float x[5], y[5], sw[5], tw[5], iw[5], r[5], g[5], b[5], a[5];
};
/* A real ring: the display list is built CPU-side in full before the gfx task
 * replays it, so a single slot only held the last sky command's corners.
 * Entries are keyed by sequence number; the decoder copies its match into
 * s_GeSkyRing (the 'current command' view the plane rebuild below reads) and
 * consumes it. */
#define GE_SKY_RING_N 64
static struct GeSkyEntry s_GeSkyRingBuf[GE_SKY_RING_N];
static struct GeSkyEntry s_GeSkyRing;

/* Returns the sequence number assigned to this entry; sky.c embeds it in
 * the G_NOOP marker it emits just before the RDP command, giving the
 * decoder EXACT command<->entry pairing (the earlier Y-extent guess could
 * flap frame to frame - the tick-tock - or mispair - the black corner). */
extern "C" uint32_t ge_sky_push(int n, const float* x, const float* y, const float* s,
                                const float* t, const float* iw, const float* r, const float* g,
                                const float* b, const float* a, float stmul) {
    static uint32_t seqgen;
    if (n < 3 || n > 4) {
        return 0;
    }
    (void)stmul;
    const uint32_t seq = (++seqgen) & 0xFFFFu;
    struct GeSkyEntry* ent = &s_GeSkyRingBuf[seq % GE_SKY_RING_N];
    ent->seq = seq;
    ent->n = n;
    for (int i = 0; i < 5; i++) {
        const int k = i < n ? i : n - 1; /* pad with the last corner: degenerate fan tris drop out */
        ent->x[i] = x[k];
        ent->y[i] = y[k];
        /* Match the encoder's ratio: skyRenderTri emits S_rdp = S*norm/w and
         * W_rdp = 32767*norm/w (norm cancels in S/W), so the decode ratio is
         * S/32767 regardless of norm. The quantized and sidechannel paths must
         * use the same units. */
        ent->sw[i] = s[k] * iw[k];
        ent->tw[i] = t[k] * iw[k];
        ent->iw[i] = iw[k] * 32767.0f;
        ent->r[i] = r[k];
        ent->g[i] = g[k];
        ent->b[i] = b[k];
        ent->a[i] = a[k];
    }
    { /* GE_SKY_RINGDBG=3: log what sky.c handed us, per push (pairs with
       * the skyaudit seq on the decode side) */
        static int pushdbg = -1;
        if (pushdbg < 0) { const char* e = getenv("GE_SKY_RINGDBG"); pushdbg = (e != NULL && atoi(e) >= 3) ? 1 : 0; }
        if (pushdbg) {
            fprintf(stderr, "skypush seq=%u n=%d c0=(%.2f,%.2f) rgba0=(%.2f,%.2f,%.2f,%.2f) rgba1=(%.2f,%.2f,%.2f,%.2f) rgba2=(%.2f,%.2f,%.2f,%.2f)\n",
                    seq, n, x[0], y[0], r[0], g[0], b[0], a[0], r[1], g[1], b[1], a[1], r[2], g[2], b[2], a[2]);
        }
    }
    ent->full = 1;
    return seq;
}

/* plane through three (x,y,attr) points; reference at origin */
static struct RdpAttrPlane rdp_plane_from_pts(float x0, float y0, float a0, float x1, float y1,
                                              float a1, float x2, float y2, float a2) {
    struct RdpAttrPlane p;
    const float d = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (d < 1e-6f && d > -1e-6f) {
        p.base = a0;
        p.ddx = 0.0f;
        p.ddy = 0.0f;
        return p;
    }
    p.ddx = ((a1 - a0) * (y2 - y0) - (a2 - a0) * (y1 - y0)) / d;
    p.ddy = ((a2 - a0) * (x1 - x0) - (a1 - a0) * (x2 - x0)) / d;
    p.base = a0 - p.ddx * x0 - p.ddy * y0;
    return p;
}

static void gfx_rdp_triangle(void) {
    const uint32_t* b = s_RdpTriBuf;
    const uint8_t op = b[0] >> 24;
    const uint8_t tile = (b[0] >> 16) & 7;
    const bool has_shade = (op & 4) != 0;
    const bool has_tex = (op & 2) != 0;

    { /* GE bring-up (GE_DUMP_BOND): first decoded RDP triangles */
        static int logged = -1;
        if (logged < 0) logged = getenv("GE_DUMP_BOND") != NULL ? 0 : 99;
        if (logged < 8) {
            logged++;
            fprintf(stderr, "rdptri: op=%02x tile=%d yh=%d ym=%d yl=%d xh=%d xl=%d xm=%d\n",
                    op, tile, (int16_t)(b[1] & 0xFFFFu) / 4, (int16_t)(b[1] >> 16) / 4,
                    ((int32_t)(b[0] << 18) >> 18) / 4, (int32_t)b[4] >> 16,
                    (int32_t)b[2] >> 16, (int32_t)b[6] >> 16);
        }
    }

    /* Edge coefficients describe a SCAN REGION, not necessarily a true
     * triangle: skyRenderFull encodes the full-width sky band as a
     * trapezoid (H = vertical left screen edge, M = vertical right edge,
     * L = the slanted horizon). Recover the general polygon - top span
     * (H@YH..M@YH), the minor-edge bend at YM, bottom span (H@YL..L@YL) -
     * and fan it; for a true triangle the extra corners coincide and the
     * degenerate sub-tris drop out. */
    const float yl = (float)((int32_t)(b[0] << 18) >> 18) / 4.0f; /* s11.2 in 14 bits */
    const float ym = (float)(int16_t)(b[1] >> 16) / 4.0f;
    const float yh = (float)(int16_t)(b[1] & 0xFFFFu) / 4.0f;
    const float xl = (float)(int32_t)b[2] / 65536.0f;
    const float dxldy = (float)(int32_t)b[3] / 65536.0f;
    const float xh = (float)(int32_t)b[4] / 65536.0f;
    const float dxhdy = (float)(int32_t)b[5] / 65536.0f;
    const float xm = (float)(int32_t)b[6] / 65536.0f;
    const float dxmdy = (float)(int32_t)b[7] / 65536.0f;

    if (yl <= yh) {
        { /* GE bring-up: a rejected command is a candidate missing sky piece */
            static int logged = -1;
            if (logged < 0) logged = getenv("GE_DUMP_BOND") != NULL ? 0 : 99;
            if (logged < 12) {
                logged++;
                fprintf(stderr, "rdptri REJECT yl<=yh: op=%02x yh=%d ym=%d yl=%d\n",
                        op, (int)(yh * 4.f), (int)(ym * 4.f), (int)(yl * 4.f));
            }
        }
        return; /* degenerate */
    }

    if (op != 0xCE) { /* GE bring-up: non-SHADE_TXTR variants are rare - log them */
        static int logged = -1;
        if (logged < 0) logged = getenv("GE_DUMP_BOND") != NULL ? 0 : 99;
        if (logged < 12) {
            logged++;
            fprintf(stderr, "rdptri variant op=%02x yh=%d ym=%d yl=%d xh=%d xm=%d xl=%d fill=%d,%d,%d\n",
                    op, (int)yh, (int)ym, (int)yl, (int)xh, (int)xm, (int)xl,
                    rdp.fill_color.r, rdp.fill_color.g, rdp.fill_color.b);
        }
    }

    { /* GE bring-up: GE_RDP_LOGF=n dumps every decoded tri of frame n */
        /* g_PortFramesPumped: extern "C" at file scope */
        static long logframe = -2;
        if (logframe == -2) {
            const char* e = getenv("GE_RDP_LOGF");
            logframe = e ? atol(e) : -1;
        }
        if ((long)g_PortFramesPumped == logframe) {
            fprintf(stderr,
                    "rdpF%u: op=%02x lft=%d yh=%.1f ym=%.1f yl=%.1f xh=%.1f dxh=%.2f xm=%.1f dxm=%.2f xl=%.1f dxl=%.2f\n",
                    g_PortFramesPumped, op, (int)((b[0] >> 23) & 1), yh, ym, yl,
                    xh, dxhdy, xm, dxmdy, xl, dxldy);
        }
    }

    /* Corners of the scan region: the minor edge is M from YH to YM, then L
     * from YM to YL, and either span may be empty. Flat-top commands take the
     * top corner from XL and flat-bottom ones the bottom from M, or one
     * triangle of every quad collapses. */
    const float eps = 0.5f; /* quarter-scanline in Y */
    const bool has_m = ym > yh + eps; /* M walks rows */
    const bool has_l = yl > ym + eps; /* L walks rows */

    /* RDP convention: XH and XM are the edge X at floor(YH), not at YH (XL is
     * at YM). Reading them at YH shifts the corners and cracks water quads
     * along the diagonal. */
    const float yhi = floorf(yh);
    const float fh = yh - yhi;
    float vx[5], vy[5];
    vx[0] = xh + dxhdy * fh;                                          /* major top */
    vy[0] = yh;
    vx[1] = has_m ? xm + dxmdy * fh : xl;                             /* minor top */
    vy[1] = yh;
    vx[2] = has_m && has_l ? xl : vx[1];                              /* bend at YM */
    vy[2] = has_m && has_l ? ym : yh;
    vx[3] = has_l ? xl + dxldy * (yl - ym) : xm + dxmdy * (yl - yhi); /* minor bottom */
    vy[3] = yl;
    vx[4] = xh + dxhdy * (yl - yhi);                                  /* major bottom */
    vy[4] = yl;

    /* Float sidechannel: the float corners from sky.c avoid the jitter of the
     * RDP's fixed-point encoding, and are used only on an exact match with the
     * G_NOOP marker before this command. GE_SKY_QUANT=1 forces the quantized
     * path. */
    static int s_SkyQuantForced = -1;
    if (s_SkyQuantForced < 0) {
        s_SkyQuantForced = getenv("GE_SKY_QUANT") != NULL;
    }
    int use_ring = 0;
    if (s_GeSkyPendingValid && !s_SkyQuantForced) {
        struct GeSkyEntry* ent = &s_GeSkyRingBuf[s_GeSkyPendingSeq % GE_SKY_RING_N];
        if (ent->full && ent->seq == s_GeSkyPendingSeq) {
            s_GeSkyRing = *ent; /* the plane rebuild below reads this view */
            ent->full = 0;      /* consumed: a stale entry can never re-pair */
            use_ring = 1;
            for (int i = 0; i < 5; i++) {
                vx[i] = s_GeSkyRing.x[i];
                vy[i] = s_GeSkyRing.y[i];
            }
        }
    }
    { /* GE_SKY_RINGDBG=1: one line per sky/water command - did the float
       * sidechannel pair (ring=1) or did it fall to the quantized corners? */
        static int ringdbg = -1;
        if (ringdbg < 0) {
            const char* e = getenv("GE_SKY_RINGDBG");
            ringdbg = (e != NULL && e[0] != '0') ? 1 : 0;
        }
        if (ringdbg) {
            /* g_PortFramesPumped: extern "C" at file scope */
            fprintf(stderr, "skyring F%u op=%02x pend=%d seq=%u ring=%d y0=%.3f y3=%.3f x3=%.3f\n",
                    g_PortFramesPumped, op, s_GeSkyPendingValid, s_GeSkyPendingSeq, use_ring, vy[0], vy[3], vx[3]);
        }
    }
    s_GeSkyPendingValid = 0;

    const uint32_t* shade_blk = b + 8;
    const uint32_t* tex_blk = b + (has_shade ? 24 : 8);

    struct RdpAttrPlane pr, pg, pb, pa;
    if (has_shade) {
        pr = rdp_decode_plane(shade_blk, 0);
        pg = rdp_decode_plane(shade_blk, 1);
        pb = rdp_decode_plane(shade_blk, 2);
        pa = rdp_decode_plane(shade_blk, 3);
    }

    struct RdpAttrPlane ps, pt, pw;
    if (has_tex) {
        ps = rdp_decode_plane(tex_blk, 0);
        pt = rdp_decode_plane(tex_blk, 1);
        pw = rdp_decode_plane(tex_blk, 2);
    }

    { /* GE_SKY_RINGDBG=2: prints the ring's shade/UV/W next to the RDP
       * planes evaluated at the same corners, one line per corner. Both
       * describe the same triangle, so a column that disagrees is the bug. */
        static int ringaudit = -1;
        if (ringaudit < 0) {
            const char* e = getenv("GE_SKY_RINGDBG");
            ringaudit = (e != NULL && atoi(e) >= 2) ? 1 : 0;
        }
        if (ringaudit && use_ring) {
            /* g_PortFramesPumped: extern "C" at file scope */
            for (int i = 0; i < 3; i++) {
                const float dx = s_GeSkyRing.x[i] - xh, dy = s_GeSkyRing.y[i] - yh;
                const float qr = has_shade ? rdp_plane_at(&pr, dx, dy) : -1.0f;
                const float qg = has_shade ? rdp_plane_at(&pg, dx, dy) : -1.0f;
                const float qb = has_shade ? rdp_plane_at(&pb, dx, dy) : -1.0f;
                const float qa = has_shade ? rdp_plane_at(&pa, dx, dy) : -1.0f;
                const float qs = has_tex ? rdp_plane_at(&ps, dx, dy) : 0.0f;
                const float qt = has_tex ? rdp_plane_at(&pt, dx, dy) : 0.0f;
                const float qw = has_tex ? rdp_plane_at(&pw, dx, dy) : 0.0f;
                const float rw = s_GeSkyRing.iw[i];
                fprintf(stderr, "skyaudit F%u seq=%u op=%02x c%d xy=(%.2f,%.2f) shade ring=(%.0f,%.0f,%.0f,%.0f) quant=(%.1f,%.1f,%.1f,%.1f) uv ring=(%.5f,%.5f) quant=(%.5f,%.5f) w ring=%.4g quant=%.4g\n",
                        g_PortFramesPumped, s_GeSkyRing.seq, op, i, s_GeSkyRing.x[i], s_GeSkyRing.y[i],
                        s_GeSkyRing.r[i], s_GeSkyRing.g[i], s_GeSkyRing.b[i], s_GeSkyRing.a[i], qr, qg, qb, qa,
                        (rw != 0.0f) ? s_GeSkyRing.sw[i] / rw : 0.0f, (rw != 0.0f) ? s_GeSkyRing.tw[i] / rw : 0.0f,
                        (qw != 0.0f) ? qs / qw : 0.0f, (qw != 0.0f) ? qt / qw : 0.0f, rw, qw);
            }
        }
    }

    /* Build one sub-vertex: attributes come from the coefficient planes
     * evaluated EXACTLY at this screen position (not lerped), so with grid
     * subdivision below the per-pixel S/W division the RDP performs is
     * approximated to sub-tile accuracy - including at the horizon where
     * W -> 0 (clip-w tricks explode there; this stays finite). */
    struct RdpTriCtx {
        const struct RdpAttrPlane *pr, *pg, *pb, *pa, *ps, *pt, *pw;
        bool has_shade, has_tex;
        float xh, yh;
        float uvmul;
        bool persw;
        bool pixdiv;
        float qscale; /* pixdiv: normalises the 1/W plane per fan triangle (cancels in the divide) */
    } ctx;
    ctx.pr = &pr; ctx.pg = &pg; ctx.pb = &pb; ctx.pa = &pa;
    ctx.ps = &ps; ctx.pt = &pt; ctx.pw = &pw;
    ctx.has_shade = has_shade;
    ctx.has_tex = has_tex;
    ctx.xh = xh;
    ctx.yh = yh;
    {
        static float uvmul = -1.0f;
        if (uvmul < 0.0f) {
            const char* e = getenv("GE_SKY_UVMUL");
            /* 1024 is the N64-faithful scale and gives correct zenith cloud
             * detail (64 smeared it white). The horizon repetition is authentic
             * flat-plane geometry that the N64 masked with 320x240 softness,
             * and on PC the sky-draw mipmaps (gfx_opengl) do that job; the knob
             * is kept for experiments only. */
            uvmul = e ? (float)atof(e) : 1024.0f;
        }
        ctx.uvmul = uvmul;
    }
    {
        /* GE_SKY_PERSW=1: each subdivision-cell vertex carries its real clip W
         * so the texture mapping is exact at every pixel. It is paired with
         * SHADER_OPT_NOPERSP_SHADE so shade stays screen-linear like the RDP
         * (GE_SKY_NOPERSP=0 disables that). */
        /* Persw is off by default on PC, because the screen-linear-cell water
         * looks best at 1080p and, with the XH/XM integer-scanline corner
         * recovery above, the adaptive-grid path is crack-free. GE_SKY_PERSW=1
         * opts in (together with SHADER_OPT_NOPERSP_SHADE). */
        static int persw = -1;
        if (persw < 0) {
            const char* e = getenv("GE_SKY_PERSW");
#ifdef GE_XBOX
            /* Console: default on. The screen-linear path's adaptive grid turns
             * one sky triangle into thousands on a slow CPU, while the real-W
             * cells stay at the 6x6 base and the GPU interpolates
             * hyperbolically anyway. */
            persw = e ? (atoi(e) != 0 ? 1 : 0) : 1;
#else
            persw = e ? (atoi(e) != 0 ? 1 : 0) : 0;
#endif
        }
        ctx.persw = persw != 0;
    }
    {
        /* GE_SKY_PIXDIV (default on): instead of approximating the RDP's
         * per-pixel divide with an adaptive grid (most of Dam's triangles and
         * CPU time), keep vertices screen-space with w = 1, pass S/W*k, T/W*k
         * and q = 1/W*k, and divide in the fragment shader
         * (SHADER_OPT_GE_SKY_PIXDIV). GE_SKY_PIXDIV=0 restores the old grid;
         * persw (the Xbox default) wins when both are on. */
        static int pixdiv = -1;
        if (pixdiv < 0) {
            const char* e = getenv("GE_SKY_PIXDIV");
            pixdiv = e ? (atoi(e) != 0 ? 1 : 0) : 1;
        }
        ctx.pixdiv = pixdiv != 0 && !ctx.persw && has_tex;
        ctx.qscale = 1.0f;
    }

    auto emit_vertex = [&ctx](struct LoadedVertex* v, float px, float py) {
        const float dx = px - ctx.xh;
        const float dy = py - ctx.yh;
        float sx = px * 4.0f + rdp.subpixel_ofs_x;
        float sy = py * 4.0f + rdp.subpixel_ofs_y;
        memset(v, 0, sizeof(*v));
        float ndcx = gfx_adjust_x_for_aspect_ratio(sx / (4.0f * HALF_SCREEN_WIDTH) - 1.0f);
        float ndcy = -(sy / (4.0f * HALF_SCREEN_HEIGHT)) + 1.0f;
        v->x = ndcx;
        v->y = ndcy;
        v->z = -1.0f;
        v->w = 1.0f;

        if (ctx.has_shade) {
            v->color.r = (uint8_t)clampf(rdp_plane_at(ctx.pr, dx, dy), 0.f, 255.f);
            v->color.g = (uint8_t)clampf(rdp_plane_at(ctx.pg, dx, dy), 0.f, 255.f);
            v->color.b = (uint8_t)clampf(rdp_plane_at(ctx.pb, dx, dy), 0.f, 255.f);
            v->color.a = (uint8_t)clampf(rdp_plane_at(ctx.pa, dx, dy), 0.f, 255.f);
        } else {
            v->color = rdp.fill_color;
        }

        if (ctx.has_tex) {
            float w = rdp_plane_at(ctx.pw, dx, dy);
            float s = rdp_plane_at(ctx.ps, dx, dy);
            float t = rdp_plane_at(ctx.pt, dx, dy);
            if (ctx.pixdiv) {
                /* u*q, v*q and q, undivided: gfx_sp_tri1 carries them to
                 * the fragment stage, which does the divide */
                v->u = s * ctx.qscale * ctx.uvmul * 32.0f;
                v->v = t * ctx.qscale * ctx.uvmul * 32.0f;
                ge_sky_q[v - &rsp.loaded_vertices[MAX_VERTICES]] = w * ctx.qscale;
                return;
            }
            if (w > 1e-9f || w < -1e-9f) {
                s /= w;
                t /= w;
            }
            /* GE pre-scales S,T,W into 16.16 before packing, so the S/W ratio
             * needs one global factor restored. W stays 1 so shade
             * interpolates screen-linearly like the RDP; a real clip W would
             * squash the sky fade into a hard band at the horizon. */
            v->u = s * ctx.uvmul * 32.0f;
            v->v = t * ctx.uvmul * 32.0f;

            /* GE_SKY_PERSW: clip w is the reciprocal of the screen-linear 1/W
             * plane, and positions are premultiplied so they land on the same
             * pixel. UVs stay plain so the GPU's perspective divide gives the
             * exact texture mapping inside each cell. */
            if (ctx.persw && (w > 1e-9f)) {
                float cw = 1.0f / w;
                v->x = ndcx * cw;
                v->y = ndcy * cw;
                v->z = -1.0f * cw;
                v->w = cw;
            }
        }
    };

    { /* GE bring-up (GE_DUMP_BOND): reconstructed vertex attributes */
        static int logged = -1;
        if (logged < 0) logged = getenv("GE_DUMP_BOND") != NULL ? 0 : 99;
        if (logged < 4) {
            logged++;
            const struct LoadedVertex* v = &rsp.loaded_vertices[MAX_VERTICES];
            fprintf(stderr, "rdptri attrs: rgba=%d,%d,%d,%d uv=%.1f,%.1f / %.1f,%.1f / %.1f,%.1f\n",
                    v[0].color.r, v[0].color.g, v[0].color.b, v[0].color.a,
                    v[0].u / 32.0f, v[0].v / 32.0f, v[1].u / 32.0f, v[1].v / 32.0f,
                    v[2].u / 32.0f, v[2].v / 32.0f);
        }
    }

    uint64_t saved_combine_mode = rdp.combine_mode;
    if (!has_shade && !has_tex) {
        /* fill triangle: like FILLRECT, shade = fill colour */
        gfx_dp_set_combine_mode(color_comb(0, 0, 0, G_CCMUX_SHADE), alpha_comb(0, 0, 0, G_ACMUX_SHADE), 0, 0);
    }

    uint8_t saved_tile = rdp.first_tile_index;
    if (saved_tile != tile) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
    }
    rdp.first_tile_index = tile;

    struct XYWidthHeight default_viewport = { 0, (int16_t)SCREEN_HEIGHT, (uint32_t)SCREEN_WIDTH,
                                              (uint32_t)SCREEN_HEIGHT };
    struct XYWidthHeight viewport_saved = rdp.viewport;
    uint32_t geometry_mode_saved = rsp.geometry_mode;

    gfx_adjust_viewport_or_scissor(&default_viewport);

    rdp.viewport = default_viewport;
    rdp.viewport_or_scissor_changed = true;
    rsp.geometry_mode = 0;

    if (use_ring) {
        /* planes in absolute screen coords (rdp_plane_from_pts references
         * the origin); emit_vertex evaluates at (px - xh, py - yh) */
        ctx.xh = 0.0f;
        ctx.yh = 0.0f;
    }
    ge_sky_tri_active = 1; /* GL backend: mipmap the texture for this draw */
    {
        /* GE_SKY_NOPERSP=0: A/B kill switch for the screen-linear shade
         * varyings (leaves the persw cells' shade perspective-bent). */
        static int nopersp = -1;
        if (nopersp < 0) {
            const char* e = getenv("GE_SKY_NOPERSP");
            nopersp = e ? (atoi(e) != 0 ? 1 : 0) : 1;
        }
        ge_sky_persw_active = (ctx.persw && ctx.has_tex && nopersp) ? 1 : 0;
    }
    ge_sky_pixdiv_active = ctx.pixdiv ? 1 : 0;

    /* Fan around corner 0 (coincident corners drop as zero-area), subdividing
     * each fan triangle into an NxN barycentric grid so the plane evaluation
     * above approximates the RDP's per-pixel perspective. The density is
     * adaptive: near the horizon 1/W spans orders of magnitude across a
     * triangle, so N scales with the triangle's 1/W dynamic range (capped for
     * fans that run all the way to the horizon) to keep each cell's hyperbola
     * segment near linear. */
    #define RDP_TRI_SUBDIV 6
    for (int t = 0; t < 3; t++) {
        const int i1 = t + 1;
        const int i2 = t + 2;
        const float area = (vx[i1] - vx[0]) * (vy[i2] - vy[0]) - (vx[i2] - vx[0]) * (vy[i1] - vy[0]);
        if (area > -0.05f && area < 0.05f) {
            continue; /* sub-quarter-pixel sliver */
        }
        int nsub = RDP_TRI_SUBDIV;
        /* GE_SKY_SUBDIV=<n>: pin the grid density, bypassing the adaptive terms (diagnostic A/B).
         * If a dense grid fixes a sky artifact and a coarse one worsens it, the
         * fault is the cells' screen-linear UV approximation; if 6 and 128 look
         * identical, it is upstream of the interpolation. */
        static int forcesub = -1;
        if (forcesub < 0) {
            const char* e = getenv("GE_SKY_SUBDIV");
            forcesub = e ? atoi(e) : 0;
            if (forcesub > 128) {
                forcesub = 128;
            }
        }
        if (forcesub > 0 && ctx.has_tex) {
            nsub = forcesub;
        } else if (ctx.pixdiv) {
            nsub = 1; /* the fragment divides: one triangle is exact */
        } else if (ctx.has_tex && ctx.persw) {
            /* real-W cells make the texture mapping exact at ANY density;
             * the grid only bounds the (invisible) per-cell shade bend, so
             * the adaptive/zoom boosts below - which existed purely for
             * texture error - stay off. 6x6 keeps shade within 1/6 of the
             * fan range per cell. */
        } else if (ctx.has_tex) {
            float w0 = rdp_plane_at(ctx.pw, vx[0] - ctx.xh, vy[0] - ctx.yh);
            float w1 = rdp_plane_at(ctx.pw, vx[i1] - ctx.xh, vy[i1] - ctx.yh);
            float w2 = rdp_plane_at(ctx.pw, vx[i2] - ctx.xh, vy[i2] - ctx.yh);
            float wlo = w0 < w1 ? (w0 < w2 ? w0 : w2) : (w1 < w2 ? w1 : w2);
            float whi = w0 > w1 ? (w0 > w2 ? w0 : w2) : (w1 > w2 ? w1 : w2);
            if (wlo > 1e-9f) {
                float ratio = whi / wlo;
                while (nsub < 40 && ratio > 1.5f) {
                    nsub += 4;
                    ratio *= 0.5f;
                }

                /* Zoom magnification: the 1/W-ratio criterion above falls as
                 * the fov narrows while the on-screen texel size rises, which
                 * is backwards for visible error, so the density is boosted
                 * with the triangle's screen-area-per-uv-area magnification
                 * (about 1x at 60 degrees fov, about 3x at full sniper zoom).
                 * Without it a scoped sky drops to the coarse grid and the
                 * interior cloud field slides as the zoom changes. */
                float s0 = rdp_plane_at(ctx.ps, vx[0] - ctx.xh, vy[0] - ctx.yh) / w0;
                float t0 = rdp_plane_at(ctx.pt, vx[0] - ctx.xh, vy[0] - ctx.yh) / w0;
                float s1 = rdp_plane_at(ctx.ps, vx[i1] - ctx.xh, vy[i1] - ctx.yh) / w1;
                float t1 = rdp_plane_at(ctx.pt, vx[i1] - ctx.xh, vy[i1] - ctx.yh) / w1;
                float s2 = rdp_plane_at(ctx.ps, vx[i2] - ctx.xh, vy[i2] - ctx.yh) / w2;
                float t2 = rdp_plane_at(ctx.pt, vx[i2] - ctx.xh, vy[i2] - ctx.yh) / w2;
                float uvarea = (s1 - s0) * (t2 - t0) - (s2 - s0) * (t1 - t0);
                if (uvarea < 0.0f) {
                    uvarea = -uvarea;
                }
                if (uvarea > 1e-6f) {
                    float mag = sqrtf((area < 0.0f ? -area : area) / uvarea);
                    float boost = sqrtf(mag * (1.0f / 8.0f));
                    if (boost > 1.0f) {
                        if (boost > 4.0f) {
                            boost = 4.0f;
                        }
                        nsub = (int)(nsub * boost);
                        if (nsub > 96) {
                            nsub = 96;
                        }
                    }
                }
            } else {
                nsub = 40; /* degenerate/near-pole: maximum density */
            }
        }
        if (use_ring) {
            /* rebuild the attribute planes exactly from this fan triangle's
             * float corners - S/W, T/W, 1/W and shade are all linear in
             * screen space, so three points define each plane precisely */
            #define RINGPLANE(f) rdp_plane_from_pts(vx[0], vy[0], s_GeSkyRing.f[0], \
                vx[i1], vy[i1], s_GeSkyRing.f[i1], vx[i2], vy[i2], s_GeSkyRing.f[i2])
            pr = RINGPLANE(r);
            pg = RINGPLANE(g);
            pb = RINGPLANE(b);
            pa = RINGPLANE(a);
            ps = RINGPLANE(sw);
            pt = RINGPLANE(tw);
            pw = RINGPLANE(iw);
            #undef RINGPLANE
        }
        if (ctx.pixdiv) {
            /* q = 1 at this fan triangle's nearest corner: keeps the
             * varyings in a sane float range whatever scale sky.c packed */
            float wmax = 0.0f;
            const int ci[3] = { 0, i1, i2 };
            for (int k = 0; k < 3; k++) {
                const float wk = fabsf(rdp_plane_at(ctx.pw, vx[ci[k]] - ctx.xh, vy[ci[k]] - ctx.yh));
                if (wk > wmax) {
                    wmax = wk;
                }
            }
            ctx.qscale = wmax > 1e-30f ? 1.0f / wmax : 1.0f;
        }
        const int n = ctx.has_tex ? nsub : 1;
        for (int r = 0; r < n; r++) {
            for (int c = 0; c + r < n; c++) {
                /* barycentric corners of the two grid cells at (r, c) */
                const float f = 1.0f / n;
                float u0 = c * f, w0r = r * f;
                float px0 = vx[0] + (vx[i1] - vx[0]) * u0 + (vx[i2] - vx[0]) * w0r;
                float py0 = vy[0] + (vy[i1] - vy[0]) * u0 + (vy[i2] - vy[0]) * w0r;
                float px1 = px0 + (vx[i1] - vx[0]) * f;
                float py1 = py0 + (vy[i1] - vy[0]) * f;
                float px2 = px0 + (vx[i2] - vx[0]) * f;
                float py2 = py0 + (vy[i2] - vy[0]) * f;

                emit_vertex(&rsp.loaded_vertices[MAX_VERTICES + 0], px0, py0);
                emit_vertex(&rsp.loaded_vertices[MAX_VERTICES + 1], px1, py1);
                emit_vertex(&rsp.loaded_vertices[MAX_VERTICES + 2], px2, py2);
                gfx_sp_tri1(MAX_VERTICES + 0, MAX_VERTICES + 1, MAX_VERTICES + 2, true);

                if (c + r + 1 < n) { /* the mirrored cell completing the quad */
                    float px3 = px1 + (vx[i2] - vx[0]) * f;
                    float py3 = py1 + (vy[i2] - vy[0]) * f;
                    emit_vertex(&rsp.loaded_vertices[MAX_VERTICES + 0], px1, py1);
                    emit_vertex(&rsp.loaded_vertices[MAX_VERTICES + 1], px3, py3);
                    emit_vertex(&rsp.loaded_vertices[MAX_VERTICES + 2], px2, py2);
                    gfx_sp_tri1(MAX_VERTICES + 0, MAX_VERTICES + 1, MAX_VERTICES + 2, true);
                }
            }
        }
    }
    #undef RDP_TRI_SUBDIV
    ge_sky_tri_active = 0;
    ge_sky_persw_active = 0;
    ge_sky_pixdiv_active = 0;

    rsp.geometry_mode = geometry_mode_saved;
    rdp.viewport = viewport_saved;
    rdp.viewport_or_scissor_changed = true;

    if (saved_tile != tile) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
    }
    rdp.first_tile_index = saved_tile;
    rdp.combine_mode = saved_combine_mode;
}

static void gfx_rdp_half(uint32_t w1) {
    if (s_RdpTriExpect == 0) {
        const uint8_t op = w1 >> 24;
        if (op < 0xC8 || op > 0xCF) {
            return; /* not an RDP triangle start (e.g. a stray half) */
        }
        s_RdpTriExpect = 8 + ((op & 4) ? 16 : 0) + ((op & 2) ? 16 : 0) + ((op & 1) ? 4 : 0);
        s_RdpTriLen = 0;
    }
    s_RdpTriBuf[s_RdpTriLen++] = w1;
    if (s_RdpTriLen >= s_RdpTriExpect) {
        s_RdpTriExpect = 0;
        gfx_rdp_triangle();
    }
}

static void gfx_dp_set_z_image(void* z_buf_address) {
    rdp.z_buf_address = z_buf_address;
}

static void gfx_dp_set_color_image(uint32_t format, uint32_t size, uint32_t width, void* address) {
    rdp.color_image_address = address;
}

static void gfx_sp_set_other_mode(uint32_t shift, uint32_t num_bits, uint64_t mode) {
    uint64_t mask = (((uint64_t)1 << num_bits) - 1) << shift;
    const uint64_t om_old = rdp.other_mode_l | ((uint64_t)rdp.other_mode_h << 32);
    const uint64_t om = (om_old & ~mask) | mode;
    if (om == om_old) {
        return; /* value-sensitive (1,100 SETOTHERMODE_H a frame), see ge_state_serial */
    }
    rdp.other_mode_l = (uint32_t)om;
    rdp.other_mode_h = (uint32_t)(om >> 32);
    {
        uint32_t nf = rdp.other_mode_h & (3U << G_MDSFT_TEXTLUT);
        if (ge_texlog_now() && nf != rdp.palette_fmt) {
            fprintf(stderr, "texlog: TEXTLUT %u -> %u\n",
                    rdp.palette_fmt >> G_MDSFT_TEXTLUT, nf >> G_MDSFT_TEXTLUT);
        }
        const bool tex_lod = (rdp.other_mode_h & G_TL_LOD) != 0;
        const bool tex_detail = (rdp.other_mode_h & (2U << G_MDSFT_TEXTDETAIL)) == G_TD_DETAIL;
        if (nf != rdp.palette_fmt || tex_lod != rdp.tex_lod || tex_detail != rdp.tex_detail) {
            /* the TLUT format is part of the CI cache key, and the LOD bits
             * move the slot -> tile mapping (gfx_lod_tile_offset): both
             * slots must re-import */
            rdp.textures_changed[0] = rdp.textures_changed[1] = true;
        }
        rdp.palette_fmt = nf;
        rdp.tex_lod = tex_lod;
        rdp.tex_detail = tex_detail;
    }
    ge_state_serial++;
}

#if 0 // PD-only G_COL vertex-color palettes; GE vertices carry RGBA inline (see rsp.vertex_colors)
static void gfx_sp_set_vertex_colors(uint32_t count, const struct NormalColor *vcn) {
    // common sense dictates that we should copy the colors as the command is supposed to do,
    // but it actually doesn't seem to matter
    // SUPPORT_CHECK(count <= sizeof(rsp.vertex_colors) / sizeof(rsp.vertex_colors[0]));
    // for (uint32_t i = 0; i < count; ++i) {
    //     rsp.vertex_colors[i] = vcn[i];
    // }
    rsp.vertex_colors = vcn;
}
#endif

static void gfx_dp_set_other_mode(uint32_t h, uint32_t l) {
    rdp.other_mode_h = h;
    rdp.other_mode_l = l;
}

static inline void *seg_addr(uintptr_t w1) {
    // PD's tagged form: segmented addresses carry bit 0 (their asset
    // preprocessor stamps it).
    if (!PORT_IS_HOST_ADDR(w1) && (w1 & 1)) {
        // seg 0 is reserved and doesn't count here
        const uintptr_t seg = (w1 & 0x0f000000) >> 24;
        if (seg && segmentPointers[seg]) {
            const uintptr_t addr = (w1 & 0x00fffffe);
            return (void *)(segmentPointers[seg] + addr);
        }
    }
    /* GE form: compiled-in asset data embeds untagged segment words such as
     * 0x03000000, so any low word whose segment slot is installed is resolved.
     * This is safe only because no host pointer lives below 0x10000000 (see
     * CMakeLists and portGetMempArena). */
    /* Xbox: the image occupies the segment-0/1 word range (port_hostptr.h) */
    if (!PORT_IS_HOST_ADDR(w1)) {
        const uintptr_t seg = w1 >> 24;
        if (seg >= 1 && seg <= 15 && segmentPointers[seg]) {
            return (void *)(segmentPointers[seg] + (w1 & 0x00ffffff));
        }
    }
    return (void *)w1;
}

uintptr_t clearMtx;

/* GE bring-up diagnostics (see the unknown-opcode default case) */
extern "C" {
    extern void *gunbarrelgfxListPointer;
    extern void *barrelDisplayListPtr;
    extern int current_menu;
}
#define ge_gunbarrelgfxListPointer gunbarrelgfxListPointer
#define ge_barrelDisplayListPtr barrelDisplayListPtr
#define ge_current_menu current_menu

#define DL_JUMP_RING_LEN 512
static struct DlJumpEntry {
    const Gfx* cmd;
    uintptr_t w0, w1;
} s_DlJumpRing[DL_JUMP_RING_LEN];
static uint32_t s_DlJumpRingIdx;

static void gfx_run_dl(Gfx* cmd) {
    // puts("dl");
    int dummy = 0;
    char dlName[128];
    const char* fileName;

    Gfx* dListStart = cmd;
    uint64_t ourHash = -1;

    /* Opcodes that never change the derived draw state (see ge_state_serial),
     * plus the value-sensitive setters whose handlers bump the serial
     * themselves and only when a field actually changed (most state commands
     * just restate the current value). The colour setters feed per-vertex
     * emission straight from rdp and are not part of the hoisted state;
     * anything not listed still bumps per command. */
    static uint8_t ge_hot_op[256];
    static bool ge_hot_op_init = false;
    if (!ge_hot_op_init) {
        ge_hot_op_init = true;
        ge_hot_op[(uint8_t)G_SETTILE] = 1;          /* gfx_dp_set_tile */
        ge_hot_op[(uint8_t)G_SETTILESIZE] = 1;      /* gfx_dp_set_tile_size */
        ge_hot_op[(uint8_t)G_SETOTHERMODE_H] = 1;   /* gfx_sp_set_other_mode */
        ge_hot_op[(uint8_t)G_SETOTHERMODE_L] = 1;
        ge_hot_op[(uint8_t)G_SETCOMBINE] = 1;       /* gfx_dp_set_combine_mode */
        ge_hot_op[(uint8_t)G_SETGEOMETRYMODE] = 1;  /* gfx_sp_geometry_mode */
        ge_hot_op[(uint8_t)G_CLEARGEOMETRYMODE] = 1;
        ge_hot_op[(uint8_t)G_TEXTURE] = 1;          /* gfx_sp_texture */
        ge_hot_op[(uint8_t)G_SETTIMG] = 1;          /* only stages the next load */
        ge_hot_op[(uint8_t)G_LOADBLOCK] = 1;        /* gfx_dp_load_block */
        ge_hot_op[(uint8_t)G_LOADTLUT] = 1;         /* gfx_dp_load_tlut */
        ge_hot_op[(uint8_t)G_SETPRIMCOLOR] = 1;
        ge_hot_op[(uint8_t)G_SETENVCOLOR] = 1;
        ge_hot_op[(uint8_t)G_SETFOGCOLOR] = 1;
        ge_hot_op[(uint8_t)G_SETFILLCOLOR] = 1;
        ge_hot_op[(uint8_t)G_TRI1] = 1;
        ge_hot_op[(uint8_t)G_TRI4] = 1;
        ge_hot_op[(uint8_t)G_VTX] = 1;
        ge_hot_op[(uint8_t)G_DL] = 1;
        ge_hot_op[(uint8_t)G_PORTCACHE_EXT] = 1;    /* a registration marker, not state */
        ge_hot_op[(uint8_t)G_ENDDL] = 1;
        ge_hot_op[(uint8_t)G_MTX] = 1;
        ge_hot_op[(uint8_t)G_POPMTX] = 1;
        ge_hot_op[(uint8_t)G_MOVEMEM] = 1;   /* viewport (flag-gated), lights, lookat: per-vertex */
        ge_hot_op[(uint8_t)G_MOVEWORD] = 1;  /* fog registers, segments, numlights: per-vertex */
        ge_hot_op[(uint8_t)G_RDPPIPESYNC] = 1;
        ge_hot_op[(uint8_t)G_RDPLOADSYNC] = 1;
        ge_hot_op[(uint8_t)G_RDPTILESYNC] = 1;
        ge_hot_op[(uint8_t)G_RDPFULLSYNC] = 1;
        ge_hot_op[(uint8_t)G_NOOP] = 1;
    }
    for (;;) {
        uint32_t opcode = cmd->words.w0 >> 24;
        roc_cur_cmd = cmd;
        if (roc_walk != nullptr) {
            /* GPU part replay: the list's state and matrix commands run as
             * usual; vertex loads and triangles are skipped, and a retained
             * draw is issued where its first triangle was captured */
            if (opcode == (uint8_t)G_VTX) {
                /* the matrix these vertices would have been transformed with */
                const uint32_t rel = roc_mtx_gen - roc_walk_gen0;
                if (rel < ROC_WALK_GENS && !roc_walk_gen_valid[rel]) {
                    memcpy(roc_walk_mp[rel], rsp.MP_matrix, sizeof(roc_walk_mp[rel]));
                    memcpy(roc_walk_mv[rel], rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], sizeof(roc_walk_mv[rel]));
                    roc_walk_gen_valid[rel] = 1;
                }
                if (!roc_walk->passthrough) {
                    ++cmd;
                    continue;
                }
                /* passthrough list: its interpreter triangles need the vertices */
            } else if (opcode == (uint8_t)G_TRI1 || opcode == (uint8_t)G_TRI4) {
                if (roc_walk_tri(cmd)) {
                    ++cmd;
                    continue;
                }
                /* a passthrough triangle: the interpreter draws it */
            }
            /* The texture-load chain of every limb (SETTIMG / SETTILE /
             * LOADBLOCK / LOADTLUT / SETTILESIZE and syncs) is state the
             * captured draws already carry
             * (roc_apply_state binds their nodes), so it is skipped; the
             * interpreter's tile state stays at its pre-list values, which the
             * next interpreter draw re-states anyway. Matrices, segments,
             * colours and modes still run. */
            if (gfx_xbox_part_skiptex && !roc_walk->passthrough && opcode != (uint8_t)G_VTX
                && opcode != (uint8_t)G_TRI1 && opcode != (uint8_t)G_TRI4) {
                switch (opcode) {
                    case (uint8_t)G_SETTIMG: case (uint8_t)G_SETTILE: case (uint8_t)G_SETTILESIZE:
                    case (uint8_t)G_LOADBLOCK: case (uint8_t)G_LOADTILE: case (uint8_t)G_LOADTLUT:
                    case (uint8_t)G_RDPPIPESYNC: case (uint8_t)G_RDPLOADSYNC: case (uint8_t)G_RDPTILESYNC:
                        ++cmd;
                        continue;
                    default:
                        break;
                }
            }
        }
        roc_dbg_ops[opcode]++;
        g_PortHwLoad.cmds++;
        if (!ge_hot_op[opcode]) {
            ge_state_serial++;
        }
#ifdef GE_XBOX
        if (ge_xbox_trace_dl) {
            ++ge_xbox_cmds;
            if (ge_xbox_cmds <= 2048 || (ge_xbox_cmds % 256u) == 0u) {
                portXboxTrace("dl: cmd %u at %p op %02x w0 %08x w1 %08x", ge_xbox_cmds, (void*)cmd, (unsigned)opcode, (unsigned)cmd->words.w0, (unsigned)cmd->words.w1);
            }
        }
#endif
        // gfx_print_cmd(cmd);
        /* GE bring-up: GE_TRACE_DL=1 prints every command before it runs, so
         * a crash names its killer. Remove once the renderer is stable. */
        {
            /* GE_TRACE_DL_FRAME=n (diag): same trace, but only for the n-th
             * gfx task (num_dls), and G_VTX dumps the vertices it loads -
             * lets two runs' DLs be diffed at one frame. */
            extern uint32_t num_dls;
            static int trace = -1;
            static long traceframe = -1;
            if (trace < 0) {
                const char *tf = getenv("GE_TRACE_DL_FRAME");
                trace = getenv("GE_TRACE_DL") != NULL;
                if (tf != NULL && tf[0] != '\0') { traceframe = atol(tf); trace = 1; }
            }
            if (trace && (traceframe < 0 || (long)num_dls == traceframe)) {
                fprintf(stderr, "dl %p: %02x %08x %08x\n", (void *)cmd,
                        (unsigned)opcode, (unsigned)cmd->words.w0, (unsigned)cmd->words.w1);
                if (opcode == G_VTX) {
                    const Vtx* vv = (const Vtx*)seg_addr(cmd->words.w1);
                    size_t nv = C0(0, 16) / sizeof(Vtx);
                    for (size_t vi = 0; vv != NULL && vi < nv; vi++) {
                        const Vtx_t* v = &vv[vi].v;
                        fprintf(stderr, "  vtx[%u] %d %d %d st=%d %d rgba=%02x%02x%02x%02x\n",
                                (unsigned)(C0(16, 4) + vi), v->ob[0], v->ob[1], v->ob[2], v->tc[0], v->tc[1],
                                v->cn[0], v->cn[1], v->cn[2], v->cn[3]);
                    }
                }
                fflush(stderr);
            }
        }
        switch (opcode) {
                // RSP commands:
            case G_SPNOOP: // 0x00 in GE's plain-F3D gbi.h: a zero Gfx word must NOT pop the matrix stack
                break;
            case G_NOOP: // 0xC0; also G_SETTEX, which texLoadFromGdl expands CPU-side before we run
                if ((cmd->words.w1 & 0xFFFF0000u) == GE_SKY_SEQ_MAGIC) {
                    /* sky float-sidechannel pairing marker (sky.c emits it
                     * immediately before the RDP triangle's HALF stream) */
                    s_GeSkyPendingSeq = cmd->words.w1 & 0xFFFFu;
                    s_GeSkyPendingValid = 1;
                } else if ((cmd->words.w1 >> 24) == 0x46u) {
                    /* exact fill colour marker from viSetFillColor (fr.c) */
                    s_GeExactFillRGB = cmd->words.w1 & 0x00FFFFFFu;
                    s_GeExactFillValid = 1;
                } else if ((cmd->words.w1 >> 24) == 0x47u) {
                    /* font glyph run on / off (textrelated.c PORT_TEXT_GLYPHS) */
                    s_GeTextGlyphs = (cmd->words.w1 & 1u) != 0;
                } else if ((cmd->words.w1 >> 24) == 0x58u) {
                    /* HUD scale bracket (options.c portHudScaleBegin): pivot x, y in U10.2 */
                    s_GeHudScale = (cmd->words.w1 & 0x00FFFFFFu) != 0x00FFFFFFu;
                    s_GeHudPivotX = (float) ((cmd->words.w1 >> 12) & 0xFFFu);
                    s_GeHudPivotY = (float) (cmd->words.w1 & 0xFFFu);
                } else if ((cmd->words.w1 >> 24) == 0x59u) {
                    /* the next rect's edge fractions (bondwalk2.c portRectPreciseTag) */
                    for (int i = 0; i < 4; i++) {
                        s_GeRectFrac[i] = (float) ((cmd->words.w1 >> (18 - 6 * i)) & 0x3Fu) * (1.0f / 64.0f);
                    }
                    s_GeRectFracPending = true;
                }
                break;
            case G_MTX: {
                gfx_sp_matrix(C0(16, 8), (const int32_t*)seg_addr(cmd->words.w1));
                break;
            }
            case (uint8_t)G_POPMTX: // 0xBD via GE's gbi.h
                gfx_sp_pop_matrix(1);
                break;
            case (uint8_t)G_CULLDL: // 0xBE: defensive no-op (GE never culls CPU-side here)
                break;
            case (uint8_t)G_LINE3D: // 0xB5: defensive no-op
                break;
            case G_MOVEMEM:
                gfx_sp_movemem(C0(16, 8), 0, seg_addr(cmd->words.w1));
                break;
            case (uint8_t)G_MOVEWORD:
                gfx_sp_moveword(C0(0, 8), C0(8, 16), cmd->words.w1);
                break;
            case (uint8_t)G_TEXTURE:
                gfx_sp_texture(C1(16, 16), C1(0, 16), C0(11, 3), C0(8, 3), C0(0, 8));
                break;
            case G_VTX:
                gfx_sp_vertex(C0(0, 16) / sizeof(Vtx), C0(16, 4), (const Vtx*)seg_addr(cmd->words.w1));
                break;
            case G_DL:
                /* GE bring-up: ring-log every DL jump so the runaway fatal
                 * can name the parent that held a garbage pointer (the
                 * walker coasts megabytes through zero-fill before the
                 * unknown-opcode fatal fires - the ring survives that). */
                s_DlJumpRing[s_DlJumpRingIdx++ & (DL_JUMP_RING_LEN - 1)] = {
                    cmd, cmd->words.w0, cmd->words.w1
                };
                if (C0(16, 1) == 0) {
                    // Push return address
                    Gfx* subGFX = (Gfx*)seg_addr(cmd->words.w1);

                    if (roc_next_kind != 0) { /* G_PORTCACHE_EXT named this list */
                        if (subGFX != nullptr && gfx_room_cache != 0) {
                            if (roc_next_kind == 1) {
                                portRoomCacheRegister(roc_next_arg, subGFX);
                            } else if (roc_next_kind == 2 && gfx_room_cache == 2 && roc_next_arg >= 0 && roc_next_arg < 16) {
                                portPartCacheRegister(subGFX, (const void*)segmentPointers[roc_next_arg]);
                            }
                        }
                        roc_next_kind = 0;
                    }
                    if (subGFX != nullptr) {
                        RocRoom* rr = (gfx_room_cache != 0 && roc_capturing == nullptr && roc_walk == nullptr) ? roc_find(subGFX) : nullptr;
                        if (rr == nullptr && gfx_room_cache == 2 && roc_capturing == nullptr && roc_walk == nullptr) {
                            rr = roc_part_find(subGFX, (const void*)segmentPointers[4]); /* SPSEGMENT_MODEL_VTX */
                            if (rr == nullptr) {
                                rr = roc_part_find(subGFX, (const void*)segmentPointers[5]); /* SPSEGMENT_MODEL_COL1: gun-DL nodes (model.c modelRenderNodeGundl) */
                            }
                        }
                        /* A part list's draws were captured under the render
                         * state its model set up outside the list (model.c
                         * modelApplyRenderMode*); a chr fading out runs the
                         * same list under a translucent state, so a mismatch
                         * falls back to the interpreter, and an uncacheable
                         * part is retried under a state it has not failed
                         * under. */
                        const uint64_t sig = (rr != nullptr && rr->kind == 1) ? roc_entry_sig() : 0;
                        const uint32_t origin_saved = ge_tri_origin;
                        if (rr != nullptr && rr->kind == 1 && rr->state != 1 && sig != rr->sig) {
                            /* another entry state than this entry's capture: its own
                             * variant entry (allocated on first sight, captured next) */
                            const uint64_t vkey = roc_part_vkey(rr->dl, rr->vtx, sig);
                            RocRoom* v = roc_part_find_key(vkey);
                            if (v == nullptr) {
                                v = roc_part_alloc(rr->dl, rr->vtx, vkey);
                                if (v != nullptr) v->sig = sig;
                            }
                            if (v != nullptr) rr = v;
                        }
                        if (rr != nullptr && rr->kind == 1 && rr->state == 2 && sig != rr->sig) {
                            roc_stat_part_sigmiss++;
                            ge_tri_origin = 4;
                            gfx_run_dl(subGFX);
                        } else if (rr != nullptr && rr->state == 2) {
                            bool ok;
                            if (rr->kind == 1 || rr->passthrough) {
                                ok = roc_walk_begin(*rr);
                                if (ok) {
                                    ge_tri_origin = 6;
                                    gfx_run_dl(subGFX);
                                    roc_walk_end(*rr);
                                }
                            } else {
                                ok = rr->gpu_buffer >= 0 ? roc_replay_gpu(*rr) : roc_replay(*rr);
                            }
                            if (!ok) {
                                gfx_run_dl(subGFX);
                            }
                        } else if (rr != nullptr && (rr->state == 1 || (rr->state == 3 && rr->kind == 1 && sig != rr->sig))) {
                            roc_capture_begin(*rr);
                            rr->sig = sig;
                            ge_tri_origin = 5;
                            gfx_run_dl(subGFX);
                            roc_capture_end(*rr);
                        } else {
                            if (rr != nullptr && rr->state == 3) {
                                ge_tri_origin = rr->kind == 1 ? 3 : 2;
                            } else if (rr == nullptr && gfx_room_cache == 2 && ge_tri_origin == 0
                                       && portIsDynVertexPointer((const void*)segmentPointers[4])) {
                                ge_tri_origin = 7; /* a list on per-frame vertices: never registered */
                            }
                            gfx_run_dl(subGFX);
                        }
                        ge_tri_origin = origin_saved;
                    }
                } else {
                    cmd = (Gfx*)seg_addr(cmd->words.w1);
                    --cmd; // increase after break
                }
                break;
            case (uint8_t)G_ENDDL:
                return;
            case (uint8_t)G_SETGEOMETRYMODE:
                gfx_sp_geometry_mode(0, cmd->words.w1);
                break;
            case (uint8_t)G_CLEARGEOMETRYMODE:
                gfx_sp_geometry_mode(cmd->words.w1, 0);
                break;
            case G_EXTRAGEOMETRYMODE_EXT:
                /* GE: bit 24 = a HUD slide fraction in bits 16-23 (options.h PORT_HUD_ANCHOR) */
                s_GeHudAnchorFrac = (cmd->words.w1 & 0x01000000u) ? (float) ((cmd->words.w1 >> 16) & 0xFF) / 255.f : 1.f;
                gfx_sp_extra_geometry_mode(~C0(0, 24), cmd->words.w1 & 0x0000FFFFu);
                break;
            case (uint8_t)G_TRI1:
                gfx_sp_tri1(C1(16, 8) / 10, C1(8, 8) / 10, C1(0, 8) / 10, false);
                break;
            case (uint8_t)G_TRI4:
                gfx_sp_tri4(cmd);
                break;
            case (uint8_t)G_SETOTHERMODE_L:
                gfx_sp_set_other_mode(C0(8, 8), C0(0, 8), cmd->words.w1);
                break;
            case (uint8_t)G_SETOTHERMODE_H:
                gfx_sp_set_other_mode(C0(8, 8) + 32, C0(0, 8), (uint64_t)cmd->words.w1 << 32);
                break;
            // PD-only G_COL (0x07) removed: GE's 0x07 is G_RESERVED2, never
            // emitted; vertex colors live inline in the stock Vtx.

            // RDP Commands:
            case G_SETTIMG: {
                gfx_dp_set_texture_image(C0(21, 3), C0(19, 2), C0(0, 10), 0, seg_addr(cmd->words.w1));
                break;
            }
            case G_SETTIMG_FB_EXT:
                gfx_flush();
                gfx_rapi->select_texture_fb(cmd->words.w1);
                rdp.textures_changed[0] = false;
                rdp.textures_changed[1] = false;
                break;
            case G_SETGRAYSCALE_EXT:
                rdp.grayscale = cmd->words.w1;
                break;
            case G_LOADBLOCK:
                gfx_dp_load_block(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_LOADTILE:
                gfx_dp_load_tile(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_SETTILE:
                gfx_dp_set_tile(C0(21, 3), C0(19, 2), C0(9, 9), C0(0, 9), C1(24, 3), C1(20, 4), C1(18, 2), C1(14, 4),
                                C1(10, 4), C1(8, 2), C1(4, 4), C1(0, 4));
                break;
            case G_SETTILESIZE:
                gfx_dp_set_tile_size(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_LOADTLUT:
                gfx_dp_load_tlut(C1(24, 3), C0(14, 10), C0(2, 10), C1(14, 10), C1(2, 10));
                break;
            case G_SETENVCOLOR:
                gfx_dp_set_env_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETPRIMCOLOR:
                gfx_dp_set_prim_color(C0(8, 8), C0(0, 8), C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETFOGCOLOR:
                gfx_dp_set_fog_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETFILLCOLOR:
                gfx_dp_set_fill_color(cmd->words.w1);
                break;
            case G_SETPRIMDEPTH: // 0xEE: boss.c VisCVG debug path emits it; z-from-prim-depth unsupported
                break;
            case G_SETBLENDCOLOR: // 0xF9: boss.c debug path; blend color unused by the shader pipeline
                break;
            case G_SETINTENSITY_EXT:
                gfx_dp_set_grayscale_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETCOMBINE:
                gfx_dp_set_combine_mode(color_comb(C0(20, 4), C1(28, 4), C0(15, 5), C1(15, 3)),
                                        alpha_comb(C0(12, 3), C1(12, 3), C0(9, 3), C1(9, 3)),
                                        color_comb(C0(5, 4), C1(24, 4), C0(0, 5), C1(6, 3)),
                                        alpha_comb(C1(21, 3), C1(3, 3), C1(18, 3), C1(0, 3)));
                break;
            // G_SETPRIMCOLOR, G_CCMUX_PRIMITIVE, G_ACMUX_PRIMITIVE, is used by Goddard
            // G_CCMUX_TEXEL1, LOD_FRACTION is used in Bowser room 1
            case G_SETSUBPIXELOFFSET_EXT: {
                gfx_dp_set_subpixel_offset(C0(0, 16), C1(0, 16));
                break;
            }
            case G_TEXRECT:
            case G_TEXRECTFLIP: {
                int32_t lrx, lry, tile, ulx, uly;
                uint32_t uls, ult, dsdx, dtdy;
                lrx = C0(12, 12);
                lry = C0(0, 12);
                tile = C1(24, 3);
                ulx = C1(12, 12);
                uly = C1(0, 12);
                ++cmd;
                uls = C1(16, 16);
                ult = C1(0, 16);
                ++cmd;
                dsdx = C1(16, 16);
                dtdy = C1(0, 16);
                gfx_dp_texture_rectangle(ulx, uly, lrx, lry, tile, uls, ult, dsdx, dtdy, opcode == G_TEXRECTFLIP);
                break;
            }
            case G_FILLRECT:
                gfx_dp_fill_rectangle(C1(12, 12), C1(0, 12), C0(12, 12), C0(0, 12));
                break;
            case G_FILLRECT_WIDE_EXT: {
                int32_t lrx, lry, ulx, uly;
                lrx = (int32_t)(C0(0, 24) << 8) >> 8;
                lry = (int32_t)(C1(0, 24) << 8) >> 8;
                ++cmd;
                ulx = (int32_t)(C0(0, 24) << 8) >> 8;
                uly = (int32_t)(C1(0, 24) << 8) >> 8;
                gfx_dp_fill_rectangle(ulx, uly, lrx, lry);
                break;
            }
            case G_TEXRECT_WIDE_EXT: {
                int32_t lrx, lry, tile, ulx, uly;
                uint32_t uls, ult, dsdx, dtdy;
                bool flip;
                lrx = (int32_t)((C0(0, 24) << 8)) >> 8;
                lry = (int32_t)((C1(0, 24) << 8)) >> 8;
                tile = C1(24, 3);
                flip = C1(27, 1);
                ++cmd;
                ulx = (int32_t)((C0(0, 24) << 8)) >> 8;
                uly = (int32_t)((C1(0, 24) << 8)) >> 8;
                ++cmd;
                uls = C0(16, 16);
                ult = C0(0, 16);
                dsdx = C1(16, 16);
                dtdy = C1(0, 16);
                gfx_dp_texture_rectangle(ulx, uly, lrx, lry, tile, uls, ult, dsdx, dtdy, flip);
                break;
            }
            case G_IMAGERECT_EXT: {
                int16_t tile, iw, ih;
                int16_t x0, y0, s0, t0;
                int16_t x1, y1, s1, t1;
                tile = C0(0, 3);
                iw = C1(16, 16);
                ih = C1(0, 16);
                ++cmd;
                x0 = C0(16, 16);
                y0 = C0(0, 16);
                s0 = C1(16, 16);
                t0 = C1(0, 16);
                ++cmd;
                x1 = C0(16, 16);
                y1 = C0(0, 16);
                s1 = C1(16, 16);
                t1 = C1(0, 16);
                gfx_dp_image_rectangle(tile, iw, ih, x0, y0, s0, t0, x1, y1, s1, t1);
                break;
            }
            case G_SETSCISSOR:
                gfx_dp_set_scissor(C1(24, 2), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_SETZIMG:
                gfx_dp_set_z_image(seg_addr(cmd->words.w1));
                break;
            case G_SETCIMG:
                gfx_dp_set_color_image(C0(21, 3), C0(19, 2), C0(0, 11), seg_addr(cmd->words.w1));
                break;
            case G_SETFB_EXT:
                gfx_flush();
                if (cmd->words.w1) {
                    // don't care about noise here
                    gfx_set_framebuffer(cmd->words.w1, 1.f);
                    fbActive = true;
                } else {
                    gfx_reset_framebuffer();
                    fbActive = false;
                }
                break;
            case G_COPYFB_EXT:
                gfx_copy_framebuffer(C0(11, 11), C0(0, 11), (int16_t)C1(16, 16), (int16_t)C1(0, 16), C0(22, 1));
                break;
            case G_RDPSETOTHERMODE:
                gfx_dp_set_other_mode(C0(0, 24), cmd->words.w1);
                break;
            case G_INVALTEXCACHE_EXT:
                if (cmd->words.w1) {
                    gfx_texture_cache_delete((const uint8_t *)seg_addr(cmd->words.w1));
                } else {
                    gfx_texture_cache_clear();
                }
                break;
            case (uint8_t)G_RDPHALF_1:
            case (uint8_t)G_RDPHALF_2:
            case (uint8_t)G_RDPHALF_CONT:
                /* skyRender computes raw RDP triangle commands on the CPU
                 * (G_TRI_FILL / G_TRI_SHADE_TXTR: sky and water) and streams
                 * them as half words; reassemble and rasterize them. */
                gfx_rdp_half(cmd->words.w1);
                break;
            case G_RDPFLUSH_EXT:
                gfx_flush();
                break;
            case G_CLEAR_DEPTH_EXT:
                gfx_flush();
                gfx_rapi->clear_framebuffer(false, true);
                break;
            case G_DXLU_NEWVIEW_EXT:
                gfx_dxlu_new_view();
                break;
            case G_DXLU_GROUP_EXT:
                gfx_dxlu_group(cmd->words.w1);
                break;
            case G_PORTCACHE_EXT:
                /* GE: the next G_DL is a room list (kind 1: room index) or a model
                 * part list (kind 2: the segment its vertices load through) - the
                 * sim thread's registration, in stream order (roc_hooks.c) */
                roc_next_kind = (int)((cmd->words.w1 >> 16) & 0xffu);
                roc_next_arg = (int)(cmd->words.w1 & 0xffffu);
                break;
            case G_CONSOLE2D_EXT:
                gfx_flush();
                gfx_ge_con2d_set((uint32_t) (cmd->words.w1 >> 16), (uint32_t) (cmd->words.w1 & 0xffffu));
                gfx_update_aspect_mode();
                rdp.viewport_or_scissor_changed = true;
                break;
            case G_MIRRORPILLAR_EXT:
                /* GE: bracket around a full-screen overlay whose side bands
                 * should be MIRRORED rather than stretched or left bare
                 * (blood_animation.c). Marker-driven so the frontend's own
                 * full-screen rects - logos, legal page - stay pinned. */
                gfx_ge_mirror_pillars = (cmd->words.w1 != 0);
                break;
            case G_DXLU_WORLDEND_EXT:
                /* world scene complete for this view: draw the sorted
                 * transparent set NOW, before the gun - the gun z-compares,
                 * and near glass drawn after it would win the depth test
                 * and paint over the weapon. */
                gfx_dxlu_draw();
                dxlu_closed = true;
                break;
            case G_RDPPIPESYNC:
            case G_RDPFULLSYNC:
            case G_RDPLOADSYNC:
            case G_RDPTILESYNC:
                break;
            default:
                /* GE bring-up: name the runaway. Print the title-sequence DL
                 * pointers so a stale/never-built sub-DL is identifiable
                 * (the walker treats 0x00/0xFF fill as NOOP/FULLSYNC and can
                 * coast far from the real culprit). */
                fprintf(stderr, "gfx: runaway context: menu=%d gunbarrelgfxList=%p barrelDL=%p dlStart=%p\n",
                        ge_current_menu, ge_gunbarrelgfxListPointer, ge_barrelDisplayListPtr, (void *)dListStart);
                {
                    /* last DL jumps, oldest first - the final entries name the
                     * parent DL that carried the garbage pointer */
                    uint32_t n = s_DlJumpRingIdx < 24 ? s_DlJumpRingIdx : 24;
                    uint32_t i;
                    fprintf(stderr, "gfx: last %u DL jumps (cmdaddr: w0 w1):\n", n);
                    for (i = s_DlJumpRingIdx - n; i != s_DlJumpRingIdx; i++) {
                        const struct DlJumpEntry* e = &s_DlJumpRing[i & (DL_JUMP_RING_LEN - 1)];
                        fprintf(stderr, "  %p: %08zx %08zx\n", (const void *)e->cmd, (size_t)e->w0, (size_t)e->w1);
                    }
                    fflush(stderr);
                }
                sysFatalError("Unknown GBI opcode 0x%02x at %p.\nw0 %08x\nw1 %08x", opcode, cmd, cmd->words.w0, cmd->words.w1);
                break;
        }
        ++cmd;
    }
}

static void gfx_sp_reset() {
    rsp.modelview_matrix_stack_size = 1;
    rsp.current_num_lights = 2;
    rsp.lights_changed = true;
}

extern "C" void gfx_get_dimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) {
    gfx_wapi->get_dimensions(width, height, posX, posY);
}

extern "C" void gfx_init(const GfxInitSettings *settings) {
    gfx_wapi = settings->wapi;
    gfx_rapi = settings->rapi;
    gfx_wapi->init(&settings->window_settings);
    gfx_rapi->init();
    gfx_rapi->update_framebuffer_parameters(0, settings->window_settings.width, settings->window_settings.height, 1, false, true, true, true);
    gfx_current_dimensions.internal_mul = 1;
    gfx_current_game_window_viewport.width = gfx_current_dimensions.width = settings->window_settings.width;
    gfx_current_game_window_viewport.height = gfx_current_dimensions.height = settings->window_settings.height;
    game_framebuffer = gfx_rapi->create_framebuffer();
    game_framebuffer_msaa_resolved = gfx_rapi->create_framebuffer();

    if (gfx_msaa_level > 1 && !gfx_framebuffers_enabled) {
        sysLogPrintf(LOG_WARNING, "F3D: MSAA set to %d, but framebuffers are not available; disabling", gfx_msaa_level);
        gfx_msaa_level = gfx_msaa_user = 1;
    }

    for (int i = 0; i < 16; i++) {
        segmentPointers[i] = 0;
    }

    rsp.lookat[0].dir[0] = rsp.lookat[1].dir[1] = 0x7F;
    rsp.current_lookat_coeffs[0][0] = rsp.current_lookat_coeffs[1][1] = 1.f;
    rsp.lookat_enabled = true;
}

extern "C" void gfx_destroy(void) {
    // TODO: should also destroy rapi and wapi, and any other resources acquired in fast3d

    // Texture cache and loaded textures store references to Resources which need to be unreferenced.
    gfx_texture_cache_clear();
}

extern "C" struct GfxRenderingAPI* gfx_get_current_rendering_api(void) {
    return gfx_rapi;
}

extern "C" int portPillarboxFrame(void);   /* video.c */
extern int g_GfxPresentX, g_GfxPresentW;    /* gfx_opengl.cpp: the window rect a present lands in, 0 width = all */
extern "C" uint32_t gfx_real_window_width;  /* the real window width while a pillarbox narrows the logical one (video.c mouse maps) */
uint32_t gfx_real_window_width = 0;

extern "C" void gfx_start_frame(void) {
    gfx_line_mode = (gfx_line_mode_req.load() != 0 && gfx_framebuffers_enabled) ? 1u : 0u;
    gfx_msaa_level = gfx_line_mode ? std::max<uint32_t>(gfx_msaa_user, 4u) : gfx_msaa_user;
    gfx_n64_aa = gfx_line_mode ? 1u : gfx_n64_aa_user;
    if (!gfx_async_mode) { /* async: the main thread pumps events via gfx_poll_events */
        gfx_wapi->handle_events();
    }
    gfx_wapi->get_dimensions(&gfx_current_window_dimensions.width, &gfx_current_window_dimensions.height,
                             &gfx_current_window_position_x, &gfx_current_window_position_y);

    if (gfx_current_window_dimensions.height == 0) {
        // Avoid division by zero
        gfx_current_window_dimensions.height = 1;
    }

    gfx_current_window_dimensions.aspect_ratio = (float)gfx_current_window_dimensions.width / gfx_current_window_dimensions.height;

    /* Pillarbox (video.c portPillarboxFrame): the frame is laid out in a
     * centred 4:3 of the window, so the logical window shrinks to it, the game
     * viewport is offset into the middle (gfx_adjust_viewport_or_scissor), the
     * window framebuffer keeps its real size (its clear blacks the sides), and
     * presents land in the same rect (g_GfxPresentX/W). */
    gfx_current_game_window_viewport.x = 0;
    g_GfxPresentX = 0;
    g_GfxPresentW = 0;
    gfx_real_window_width = gfx_current_window_dimensions.width;
    if (portPillarboxFrame()) {
        const uint32_t w43 = (uint32_t) std::lround((float) gfx_current_window_dimensions.height * 4.0f / 3.0f) & ~1u;

        if (w43 >= 4 && w43 + 2 <= gfx_current_window_dimensions.width) {
            const uint32_t x0 = (gfx_current_window_dimensions.width - w43) / 2;

            gfx_current_window_dimensions.width = w43;
            gfx_current_window_dimensions.aspect_ratio = (float) w43 / (float) gfx_current_window_dimensions.height;
            gfx_current_game_window_viewport.x = (int16_t) x0;
            g_GfxPresentX = (int) x0;
            g_GfxPresentW = (int) w43;
        }
    }

    gfx_current_dimensions = gfx_current_window_dimensions;

    gfx_current_game_window_viewport.width = gfx_current_dimensions.width;
    gfx_current_game_window_viewport.height = gfx_current_dimensions.height;

    /* GE "Low Res": render at 240 lines, scaled up to the window at present.
     * The width follows the window aspect, and the game window viewport stays
     * the real window, so the different_size framebuffer path below does the
     * rest. */
    /* Low Res setting is a line count: 240 "HW" (the original setting), 480,
     * 720, 1080, or 0 for the window's own resolution. It is only ever a
     * downscale, so a window shorter than the setting renders at the window. */
    if (gfx_low_res != 0 && gfx_framebuffers_enabled && gfx_current_window_dimensions.height > gfx_low_res) {
        const uint32_t lines = gfx_low_res;
        uint32_t w = (uint32_t) std::lround((float) lines * gfx_current_window_dimensions.aspect_ratio);
        w &= ~1u; /* even width keeps the blit clean */
        if (w < lines * 4u / 3u) {
            w = lines * 4u / 3u;
        }
        gfx_current_dimensions.width = w;
        gfx_current_dimensions.height = lines;
        gfx_current_dimensions.aspect_ratio = (float) w / (float) lines;
    }

    if (gfx_current_dimensions.height != gfx_prev_dimensions.height) {
        for (auto& fb : framebuffers) {
            uint32_t width, height, msaa;
            if (fb.second.autoresize) {
                if (fb.second.upscale) {
                    width = fb.second.orig_width;
                    height = fb.second.orig_height;
                    gfx_adjust_width_height_for_scale(width, height);
                } else {
                    // assume this is a fullscreen fb
                    width = gfx_current_dimensions.width;
                    height = gfx_current_dimensions.height;
                }
                if (width != fb.second.applied_width || height != fb.second.applied_height) {
                    gfx_rapi->update_framebuffer_parameters(fb.first, width, height, 1, true, true, true, true);
                    fb.second.applied_width = width;
                    fb.second.applied_height = height;
                }
            }
        }
    }
    gfx_prev_dimensions = gfx_current_dimensions;

    bool different_size = gfx_current_dimensions.width != gfx_current_game_window_viewport.width ||
                          gfx_current_dimensions.height != gfx_current_game_window_viewport.height;
    if (gfx_framebuffers_enabled && (different_size || gfx_msaa_level > 1)) {
        game_renders_to_framebuffer = true;
        if (different_size) {
            /* opengl_invert_y = FALSE: invert_y flips rendered geometry per
             * draw but set_scissor/set_viewport pass y to GL uncompensated, so
             * the per-room portal scissors (bg.c bgScissorCurrentPlayerView)
             * clipped the wrong band at low res. Nothing samples this
             * framebuffer as a texture, so render upright and everything
             * agrees. */
            gfx_rapi->update_framebuffer_parameters(game_framebuffer, gfx_current_dimensions.width,
                                                    gfx_current_dimensions.height, gfx_msaa_level, false, true, true,
                                                    true);
        } else {
            // MSAA framebuffer needs to be resolved to an equally sized target when complete, which must therefore
            // match the window size
            gfx_rapi->update_framebuffer_parameters(game_framebuffer, gfx_current_window_dimensions.width,
                                                    gfx_current_window_dimensions.height, gfx_msaa_level, false, true,
                                                    true, true);
        }
        if (gfx_msaa_level > 1 && different_size) {
            gfx_rapi->update_framebuffer_parameters(game_framebuffer_msaa_resolved, gfx_current_dimensions.width,
                                                    gfx_current_dimensions.height, 1, false, false, false, false);
        }
    } else {
        game_renders_to_framebuffer = false;
    }

    fbActive = 0;

    // update aspect scale and offset
    gfx_update_aspect_mode();
}

uint32_t num_dls = 0;

extern "C" unsigned long long g_PortGfxRunT0Us = 0; /* hitchlog.c: this frame's gfx_run start (gfx_sdl2.cpp) */
extern "C" unsigned gfx_opengl_poll_gl_error(void); /* gfx_opengl.cpp */

extern "C" void gfx_run(Gfx* commands) {
    const std::chrono::steady_clock::time_point ge_run_t0 = std::chrono::steady_clock::now();
    g_PortGfxRunT0Us = portProfNow();
    struct GeRunTimer {
        const std::chrono::steady_clock::time_point& t0;
        ~GeRunTimer() {
            g_PortGfxRunUs += (unsigned long long) std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0).count();
        }
    } ge_run_timer{ ge_run_t0 };

    ++num_dls;
    gfx_sp_reset();
    s_GeHudScale = false;   /* a bracket never outlives its frame */
    s_GeRectFracPending = false;

    // puts("New frame");

    if (!gfx_wapi->start_frame()) {
        dropped_frame = true;
        return;
    }
    dropped_frame = false;

    gfx_rapi->update_framebuffer_parameters(0, gfx_real_window_width,   /* the REAL window: a pillarbox's logical one is narrower */
                                            gfx_current_window_dimensions.height, 1, false, true, true,
                                            !game_renders_to_framebuffer);
    gfx_rapi->start_frame();
    gfx_rapi->start_draw_to_framebuffer(game_renders_to_framebuffer ? game_framebuffer : 0,
                                        (float)gfx_current_dimensions.height / SCREEN_HEIGHT);
    gfx_shader_prewarm_step();
    ge_texchurn_report();
    /* clear DEPTH here too: gfx_dp_fill_rectangle swallows the game's own
     * z-buffer clear rects on the assumption the frame clear handled depth
     * - with color-only this left GL depth stale forever, killing the first
     * z-tested draw in menu context (the invisible cast/parade models). */
    gfx_rapi->clear_framebuffer(true, true);
    rdp.viewport_or_scissor_changed = true;
    rendering_state.viewport = {};
    ge_state_serial++;
    rendering_state.scissor = {};
    ge_state_serial++;
    gfx_dxlu_reset(); /* drop any deferred XLU leftovers from a dropped frame */
    roc_next_kind = 0;
#ifdef GE_XBOX
    ge_xbox_trace_dl = num_dls <= 2;
    ge_xbox_cmds = 0;
    if (ge_xbox_trace_dl) { portXboxTrace("dl: frame %u run begin (dl %p)", (unsigned)num_dls, (void*)commands); }
#endif
    gfx_run_dl(commands);
    roc_frame_stats((unsigned)num_dls);
#ifdef GE_XBOX
    if (ge_xbox_trace_dl) { portXboxTrace("dl: frame %u run end, %u commands", (unsigned)num_dls, ge_xbox_cmds); }
#endif
    gfx_flush();
    gfx_dxlu_draw(); /* frames with no 2D tail still need the sorted pass */
    gfxFramebuffer = 0;
    { /* GE HW pacing: the frame's RDP workload is complete - publish it */
        static unsigned hw_draws0;
        g_PortHwLoad.draws = g_PortGfxDraws - hw_draws0;
        hw_draws0 = g_PortGfxDraws;
        g_PortHwLoad.frame = num_dls;
        g_PortHwLoadLast = g_PortHwLoad;
        memset(&g_PortHwLoad, 0, sizeof(g_PortHwLoad));
    }

    {
        static int stats = -1;
        if (stats < 0) stats = getenv("GE_GFX_STATS") != NULL;
        if (stats) {
            fprintf(stderr, "gfx: task %u tris sub=%u cliprej=%u culled=%u drawn=%u dbg=%u/%u/%u\n",
                    (unsigned)num_dls, ge_tris_submitted, ge_tris_cliprej, ge_tris_culled, ge_tris_drawn,
                    ge_dbg0, ge_dbg1, ge_dbg2);
        }
        ge_tris_submitted = ge_tris_cliprej = ge_tris_culled = ge_tris_drawn = 0;
        ge_dbg0 = ge_dbg1 = ge_dbg2 = 0;
    }

    gfx_ge_con2d_set(0, 0);   /* whatever the list left: the frame's own space */
    if (game_renders_to_framebuffer) {
        gfx_present_game_fb();
    }

    gfx_rapi->end_frame();
    gfx_wapi->swap_buffers_begin();
    { /* The upload path no longer asks glGetError per texture (gfx_opengl.cpp
       * upload_texture). Every ~2 s one check, right after the swap is queued (at the top of
       * a frame it waited a whole refresh, 8.3 ms measured; here the render
       * thread is about to wait for the next frame anyway): GL_OUT_OF_MEMORY means an
       * upload may have failed, so every texture re-imports; any other error
       * (a window move raises 0x502) is only logged. */
        static unsigned long long next_us;
        static unsigned logged;
        const unsigned long long now = portProfNow();
        static int poll_on = -1;
        if (poll_on < 0) {
            const char* e = getenv("GE_GLPOLL");
            poll_on = (e == NULL || atoi(e) != 0);
        }
        if (poll_on && now >= next_us) {
            next_us = now + 2000000;
            const unsigned err = gfx_opengl_poll_gl_error();
            if (err != 0 && logged < 16) {
                logged++;
                printf("ge: gfx: GL error 0x%04x in the last 2 s%s\n", err,
                       err == 0x0505 ? " (out of memory) - re-importing every texture" : "");
            }
            if (err == 0x0505) {
                gfx_texture_cache_clear();
            }
        }
    }
}

/* the framebuffer-rendered frame onto the window: at the frame's end, or early
 * for the console (G_CONSOLE2D_EXT) */
static void gfx_present_game_fb(void) {
    {
        gfx_rapi->start_draw_to_framebuffer(0, 1);
        gfx_rapi->clear_framebuffer(true, true);

        if (gfx_msaa_level > 1 && gfx_n64_aa && gfx_rapi->resolve_n64_aa != NULL) {
            /* N64 AA: the VI-style resolve reads the samples itself
             * and presents scaled (any size) - the whole MSAA branch below in one */
            gfx_rapi->resolve_n64_aa(0, game_framebuffer);
        } else if (gfx_msaa_level > 1) {
            bool different_size = gfx_current_dimensions.width != gfx_current_game_window_viewport.width ||
                                  gfx_current_dimensions.height != gfx_current_game_window_viewport.height;

            if (different_size) {
                gfx_rapi->resolve_msaa_color_buffer(game_framebuffer_msaa_resolved, game_framebuffer);
                gfxFramebuffer = (uintptr_t)gfx_rapi->get_framebuffer_texture_id(game_framebuffer_msaa_resolved);
                /* Low Res: nothing consumes gfxFramebuffer, so present by
                 * blitting the resolved image to the window, scaled (an MSAA
                 * source cannot scale directly, hence the same-size resolve
                 * first). The framebuffer is upright (invert_y false above), so
                 * no flip is needed here. */
                gfx_rapi->resolve_msaa_color_buffer(0, game_framebuffer_msaa_resolved);
            } else {
                gfx_rapi->resolve_msaa_color_buffer(0, game_framebuffer);
            }
        } else {
            gfxFramebuffer = (uintptr_t)gfx_rapi->get_framebuffer_texture_id(game_framebuffer);
            /* Low Res with msaa off (game_renders_to_framebuffer here means
             * different_size): scaled blit of the upright 240p frame */
            gfx_rapi->resolve_msaa_color_buffer(0, game_framebuffer);
        }
    }
}

static void gfx_ge_con2d_set(uint32_t w, uint32_t h) {
    if (w != 0 && h != 0 && !gfx_ge_con2d) {
        if (game_renders_to_framebuffer) {
            /* present the frame now and draw the console onto the window at
             * its own resolution; the frame-end present is then skipped */
            gfx_present_game_fb();
            game_renders_to_framebuffer = false;
            gfx_current_dimensions = gfx_current_window_dimensions;
            gfx_rapi->start_draw_to_framebuffer(0, (float) gfx_current_dimensions.height / (float) h);
        }
        gfx_ge_con2d_saved_w = gfx_current_native_viewport.width;
        gfx_ge_con2d_saved_h = gfx_current_native_viewport.height;
        gfx_ge_con2d_saved_aspect = gfx_current_native_aspect;
        gfx_current_native_viewport.width = w;
        gfx_current_native_viewport.height = h;
        gfx_current_native_aspect = (float) w / (float) h;
        gfx_ge_con2d = true;
    } else if ((w == 0 || h == 0) && gfx_ge_con2d) {
        gfx_current_native_viewport.width = gfx_ge_con2d_saved_w;
        gfx_current_native_viewport.height = gfx_ge_con2d_saved_h;
        gfx_current_native_aspect = gfx_ge_con2d_saved_aspect;
        gfx_ge_con2d = false;
    }
}

extern "C" void gfx_end_frame(void) {
    if (!dropped_frame) {
        gfx_rapi->finish_render();
        gfx_wapi->swap_buffers_end();
    }
}

extern "C" void gfx_set_target_fps(int fps) {
    gfx_wapi->set_target_fps(fps);
}

extern "C" void gfx_set_vsync(int on) {
    if (gfx_async_mode) { /* SDL_GL_SetSwapInterval needs the context - latch for the render thread */
        gfx_async_pending_swapint.store(on ? 1 : 0);
        return;
    }
    gfx_wapi->set_swap_interval(on ? 1 : 0);
}

extern "C" void gfx_set_texture_filter(enum FilteringMode mode) {
    if (gfx_async_mode) { /* sim thread (watch rows / preset / console `set`): the texture cache,
                           * shader + combiner pools are the render thread's and the GL deletes
                           * need its context - latch for gfx_async_apply_pending, last value wins */
        gfx_async_pending_filter.store((int)mode);
        return;
    }
    gfx_set_texture_filter_now(mode);
}

extern "C" void gfx_set_aa(uint32_t msaa_level, uint32_t n64_aa) {
    if (gfx_async_mode) { /* sim thread (watch MSAA / Classic Mode rows, APPLY, console `set`): the
                           * render thread reads the pair at gfx_start_frame AND at the gfx_run tail -
                           * latch for gfx_async_apply_pending, last value wins */
        gfx_async_pending_aa.store((int)((msaa_level << 1) | (n64_aa ? 1u : 0u)));
        return;
    }
    gfx_msaa_level = gfx_msaa_user = msaa_level;
    gfx_n64_aa = gfx_n64_aa_user = n64_aa ? 1u : 0u;
}

extern "C" void gfx_set_line_mode(int32_t on) {
    gfx_line_mode_req.store(on ? 1 : 0);
}

static void gfx_set_texture_filter_now(enum FilteringMode mode) {
    gfx_texture_cache_clear();
    if (rendering_state.shader_program) {
        gfx_rapi->unload_shader(rendering_state.shader_program);
        rendering_state.shader_program = nullptr;
        ge_state_serial++;
    }
    gfx_rapi->clear_shaders();
    color_combiner_pool.clear();
    prev_combiner = color_combiner_pool.end();
    ge_prewarm_next = 0; /* the listed programs compile again under the new filter */
    gfx_rapi->set_texture_filter(mode);
}

extern "C" int gfx_create_framebuffer(uint32_t width, uint32_t height, int upscale, int autoresize) {
    int fb = gfx_rapi->create_framebuffer();
    gfx_resize_framebuffer(fb, width, height, upscale, autoresize);
    return fb;
}

extern "C" void gfx_resize_framebuffer(int fb, uint32_t width, uint32_t height, int upscale, int autoresize) {
    uint32_t orig_width, orig_height;

    if (width && height) {
        // user-specified size
        orig_width = width;
        orig_height = height;
        if (upscale) {
            gfx_adjust_width_height_for_scale(width, height);
        }
        gfx_rapi->update_framebuffer_parameters(fb, width, height, 1, true, true, true, true);
    } else {
        // same size as main fb
        orig_width = width = gfx_current_dimensions.width;
        orig_height = height = gfx_current_dimensions.height;
        upscale = false;
        autoresize = true;
        gfx_rapi->update_framebuffer_parameters(fb, width, height, 1, true, true, true, true);
    }

    framebuffers[fb] = { orig_width, orig_height, width, height, (bool)upscale, (bool)autoresize };
}

extern "C" void gfx_set_framebuffer(int fb, float noise_scale) {
    gfx_rapi->start_draw_to_framebuffer(fb, noise_scale);
    gfx_rapi->clear_framebuffer(true, true);
    active_fb = framebuffers.find(fb);
}

extern "C" void gfx_copy_framebuffer(int fb_dst, int fb_src, int left, int top, int use_back) {
    const bool is_main_fb = (fb_src == 0);

    if (is_main_fb) {
        if (left > 0 && top > 0) {
            // upscale the position
            left = left * gfx_current_dimensions.width / gfx_current_native_viewport.width;
            top = top * gfx_current_dimensions.height / gfx_current_native_viewport.height;
            // flip Y
            top = gfx_current_dimensions.height - top - 1;
        }
        if (use_back && gfx_msaa_level > 1) {
            // read from the framebuffer we've been rendering to
            fb_src = game_framebuffer;
        }
    }

    gfx_rapi->copy_framebuffer(fb_dst, fb_src, left, top, is_main_fb, (bool)use_back);
}

extern "C" void gfx_reset_framebuffer(void) {
    gfx_rapi->start_draw_to_framebuffer(0, (float)gfx_current_dimensions.height / SCREEN_HEIGHT);
    active_fb = framebuffers.end();
}
