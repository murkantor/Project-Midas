#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <unordered_map>
#include <vector>
#include <string>

#include <SDL.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "glad/glad.h"

#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_pc.h"

#include <cstdarg>
#include <cstdio>

using namespace std;

/* GE links the game's own sprintf (src/sprintf.c), whose return value is
 * written-1 by original-game quirk. This file accumulates shader text via
 * sprintf return values, so that off-by-one overlaps every appended line and
 * corrupts the GLSL ("invalid atom" driver errors). Route this file's calls
 * through libc vsnprintf semantics instead.
 *
 * A bounded scratch buffer, NOT vsnprintf straight into dst: the port is
 * -m32, and on Linux the main thread's stack sits at the top of the 4 GB
 * address space (vs_buf observed at 0xffffa5fc), so the old `dst + 0x10000`
 * size lie wrapped the address space and glibc silently dropped the LAST
 * character of every chunk - including the newline closing "#version 130",
 * which left an embedded NUL and killed every shader compile. Windows never
 * saw it: 32-bit PE stacks live below 2 GB. */
static int f3d_sprintf(char *dst, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n >= 0) {
        size_t copy = ((size_t) n < sizeof(tmp) ? (size_t) n : sizeof(tmp) - 1);
        memcpy(dst, tmp, copy);
        dst[copy] = '\0';
    }
    return n;
}
#define sprintf f3d_sprintf

struct ShaderProgram {
    GLuint opengl_program_id;
    uint8_t num_inputs;
    bool used_textures[SHADER_MAX_TEXTURES];
    uint8_t num_floats;
    GLint attrib_locations[16];
    uint8_t attrib_sizes[16];
    uint8_t num_attribs;
    GLint frame_count_location;
    GLint noise_scale_location;
    GLint three_point_filter_locations[2];
    /* GE retained geometry (below): the fragment shader object outlives the
     * link so the twin program can share it, plus the ids its features come from */
    GLuint fragment_shader;
    uint64_t shader_id0;
    uint32_t shader_id1;
};

struct Framebuffer {
    uint32_t width, height;
    bool has_depth_buffer;
    uint32_t msaa_level;
    bool invert_y;

    GLuint fbo, clrbuf, clrbuf_msaa, rbo;
    GLuint clrtex_msaa;   /* GE N64 AA: the colour target as a multisample TEXTURE (readable per sample) */
    bool msaa_is_texture; /* which of the two is attached */
};

static std::map<pair<uint64_t, uint32_t>, struct ShaderProgram> shader_program_pool;
static GLuint opengl_vbo;
static GLuint opengl_vao;
static bool current_depth_mask;

static uint32_t frame_count;

static std::vector<Framebuffer> framebuffers;
static size_t current_framebuffer;
static float current_noise_scale;
static FilteringMode current_filter_mode = FILTER_LINEAR;
static bool current_textures_linear_filter[2] = {false, false};
/* What three_point_filter value the shader currently holds, as opposed to what
 * the next draw wants: it is a uniform that used to be pushed only on program
 * change, so a filtered draw (bullet hole, smoke) left the next crosshair draw
 * on the same program filtered too. The retained path tracks this per program
 * (rp->c_three); this is the same for the interpreter, with -1 = nothing pushed
 * yet. */
static GLint gl_pushed_three[2] = {-1, -1};
/* moved up from the retained-draw section: the three-point sync below needs it */
static struct ShaderProgram* gl_current_prg = nullptr; /* gfx_opengl_load_shader's last program */
/* moved up too: the sync must not write into the retained twin program */
static bool gl_ret_active = false;           /* the twin program + a retained VBO are bound */
/* GE N64 mipmaps: which texture object each unit holds, plus a per-object
 * "has a mip chain" bit (indexed by GL name - small dense ints here) so a
 * chain is generated once per upload, on demand, never sampled incomplete. */
static GLuint current_texture_ids[2] = {0, 0};
static int last_selected_tile = 0;
static std::vector<uint8_t> gl_tex_has_mips;
static void gl_set_has_mips(GLuint id, uint8_t v) {
    if (id >= gl_tex_has_mips.size()) {
        gl_tex_has_mips.resize((size_t)id + 64, 0);
    }
    gl_tex_has_mips[id] = v;
}
static bool gl_has_mips(GLuint id) {
    return id < gl_tex_has_mips.size() && gl_tex_has_mips[id] != 0;
}

static int gl_glsl_version = 130;
static char gl_glsl_version_str[16] = "130";
static GLenum gl_mirror_clamp = GL_MIRROR_CLAMP_TO_EDGE;
static bool gl_es = false;
static bool gl_core_profile = false;

static void gl_ret_restore(void); /* GE retained geometry: back to the interpreter's VBO / program (defined below) */
static struct ShaderProgram* gl_current_prg_set(struct ShaderProgram* prg);

static int gfx_opengl_get_max_texture_size() {
    GLint max_texture_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    return max_texture_size;
}

static const char* gfx_opengl_get_name() {
    return "OpenGL";
}

static struct GfxClipParameters gfx_opengl_get_clip_parameters(void) {
    return { false, framebuffers[current_framebuffer].invert_y };
}

static void gfx_opengl_vertex_array_set_attribs(struct ShaderProgram* prg) {
    size_t num_floats = prg->num_floats;
    size_t pos = 0;

    for (int i = 0; i < prg->num_attribs; i++) {
        if (prg->attrib_locations[i] >= 0) {
            glEnableVertexAttribArray(prg->attrib_locations[i]);
            glVertexAttribPointer(prg->attrib_locations[i], prg->attrib_sizes[i], GL_FLOAT, GL_FALSE,
                                num_floats * sizeof(float), (void*)(pos * sizeof(float)));
        }
        pos += prg->attrib_sizes[i];
    }
}

static void gfx_opengl_set_uniforms(struct ShaderProgram* prg) {
    if (prg->frame_count_location >= 0) {
        glUniform1i(prg->frame_count_location, frame_count);
    }
    if (prg->noise_scale_location >= 0) {
        glUniform1f(prg->noise_scale_location, current_noise_scale);
    }
    for (int t = 0; t < 2; t++) {
        if (prg->three_point_filter_locations[t] >= 0) {
            glUniform1i(prg->three_point_filter_locations[t], current_textures_linear_filter[t]);
            gl_pushed_three[t] = (GLint) current_textures_linear_filter[t];
        } else {
            gl_pushed_three[t] = -1;
        }
    }
}

/**
 * Push three_point_filter if this draw wants something other than what the
 * shader is already carrying. Called wherever the wanted value can change
 * WITHOUT the program changing - which is the case the old code missed.
 */
static void gfx_opengl_sync_three_point(void) {
    struct ShaderProgram* prg = gl_current_prg;

    /* During a retained replay the twin program is bound, not prg, so a
     * location from prg would land in the twin. gl_ret_restore re-pushes
     * through gfx_opengl_set_uniforms before the next interpreter draw. */
    if (prg == NULL || gl_ret_active) {
        return;
    }
    for (int t = 0; t < 2; t++) {
        if (prg->three_point_filter_locations[t] >= 0
            && gl_pushed_three[t] != (GLint) current_textures_linear_filter[t]) {
            gl_pushed_three[t] = (GLint) current_textures_linear_filter[t];
            glUniform1i(prg->three_point_filter_locations[t], current_textures_linear_filter[t]);
        }
    }
}

static void gfx_opengl_unload_shader(struct ShaderProgram* old_prg) {
    gl_ret_restore(); /* a retained draw's VAO may be bound: its arrays are not ours to disable */
    if (old_prg != NULL) {
        for (int i = 0; i < old_prg->num_attribs; i++) {
            if (old_prg->attrib_locations[i] >= 0) {
                glDisableVertexAttribArray(old_prg->attrib_locations[i]);
            }
        }
    }
}

static void gfx_opengl_load_shader(struct ShaderProgram* new_prg) {
    // if (!new_prg) return;
    gl_current_prg_set(new_prg);
    gl_ret_restore(); /* a retained draw may have moved the buffer binding / attribute arrays */
    glUseProgram(new_prg->opengl_program_id);
    gfx_opengl_vertex_array_set_attribs(new_prg);
    gfx_opengl_set_uniforms(new_prg);
}

static void append_str(char* buf, size_t* len, const char* str) {
    while (*str != '\0') {
        buf[(*len)++] = *str++;
    }
}

static void append_line(char* buf, size_t* len, const char* str) {
    while (*str != '\0') {
        buf[(*len)++] = *str++;
    }
    buf[(*len)++] = '\n';
}

#define RAND_NOISE "((random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0)"

static const char* shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha,
                                      bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:
                return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
            case SHADER_1:
                return with_alpha ? "vec4(1.0, 1.0, 1.0, 1.0)" : "vec3(1.0, 1.0, 1.0)";
            case SHADER_INPUT_1:
                return with_alpha || !inputs_have_alpha ? "vInput1" : "vInput1.rgb";
            case SHADER_INPUT_2:
                return with_alpha || !inputs_have_alpha ? "vInput2" : "vInput2.rgb";
            case SHADER_INPUT_3:
                return with_alpha || !inputs_have_alpha ? "vInput3" : "vInput3.rgb";
            case SHADER_INPUT_4:
                return with_alpha || !inputs_have_alpha ? "vInput4" : "vInput4.rgb";
            case SHADER_TEXEL0:
                return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A:
                return hint_single_element ? "texVal0.a"
                                           : (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)"
                                                         : "vec3(texVal0.a, texVal0.a, texVal0.a)");
            case SHADER_TEXEL1A:
                return hint_single_element ? "texVal1.a"
                                           : (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)"
                                                         : "vec3(texVal1.a, texVal1.a, texVal1.a)");
            case SHADER_TEXEL1:
                return with_alpha ? "texVal1" : "texVal1.rgb";
            case SHADER_COMBINED:
                return with_alpha ? "texel" : "texel.rgb";
            case SHADER_NOISE:
                return with_alpha ? "vec4(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")"
                                  : "vec3(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")";
        }
    } else {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_1:
                return "1.0";
            case SHADER_INPUT_1:
                return "vInput1.a";
            case SHADER_INPUT_2:
                return "vInput2.a";
            case SHADER_INPUT_3:
                return "vInput3.a";
            case SHADER_INPUT_4:
                return "vInput4.a";
            case SHADER_TEXEL0:
                return "texVal0.a";
            case SHADER_TEXEL0A:
                return "texVal0.a";
            case SHADER_TEXEL1A:
                return "texVal1.a";
            case SHADER_TEXEL1:
                return "texVal1.a";
            case SHADER_COMBINED:
                return "texel.a";
            case SHADER_NOISE:
                return RAND_NOISE;
        }
    }
    return "";
}

#undef RAND_NOISE

static void append_formula(char* buf, size_t* len, uint8_t c[2][4], bool do_single, bool do_multiply, bool do_mix,
                           bool with_alpha, bool only_alpha, bool opt_alpha) {
    if (do_single) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    } else if (do_multiply) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
    } else if (do_mix) {
        append_str(buf, len, "mix(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, ")");
    } else {
        append_str(buf, len, "(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " - ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ") * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, " + ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    }
}

extern "C" unsigned long long portProfNow(void);
extern "C" void portHitchNote(int cat, unsigned long long us);

static struct ShaderProgram* gfx_opengl_create_and_load_new_shader(uint64_t shader_id0, uint32_t shader_id1) {
    const unsigned long long hitch_t0 = portProfNow(); /* hitchlog.c */
    struct CCFeatures cc_features = { 0 };
    gfx_cc_get_features(shader_id0, shader_id1, &cc_features);

    char vs_buf[2048];
    char fs_buf[8192];
    size_t vs_len = 0;
    size_t fs_len = 0;
    size_t num_floats = 4;

    // Vertex shader

    vs_len += sprintf(vs_buf + vs_len, "#version %s\n", gl_glsl_version_str);

    if (gl_es) {
        /* Vertex stage defaults to highp; be explicit so drivers that make
         * mediump the default still compute positions/depth at full range. */
        append_line(vs_buf, &vs_len, "precision highp float;");
    }

    if (gl_glsl_version >= 130) {
        append_line(vs_buf, &vs_len, "#define INPUT in");
        append_line(vs_buf, &vs_len, "#define OUTPUT out");
    } else {
        append_line(vs_buf, &vs_len, "#define INPUT attribute");
        append_line(vs_buf, &vs_len, "#define OUTPUT varying");
    }

    append_line(vs_buf, &vs_len, "INPUT vec4 aVtxPos;");

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            vs_len += sprintf(vs_buf + vs_len, "INPUT vec2 aTexCoord%d;\n", i);
            vs_len += sprintf(vs_buf + vs_len, "OUTPUT vec2 vTexCoord%d;\n", i);
            num_floats += 2;
            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    vs_len += sprintf(vs_buf + vs_len, "INPUT float aTexClamp%s%d;\n", j == 0 ? "S" : "T", i);
                    vs_len += sprintf(vs_buf + vs_len, "OUTPUT float vTexClamp%s%d;\n", j == 0 ? "S" : "T", i);
                    num_floats += 1;
                }
            }
        }
    }
    if (cc_features.opt_fog) {
        append_line(vs_buf, &vs_len, "INPUT vec4 aFog;");
        append_line(vs_buf, &vs_len, "OUTPUT vec4 vFog;");
        num_floats += 4;
    }
    if (cc_features.opt_ge_farclip || cc_features.opt_ge_pixfog) {
        /* GE far clip + per-pixel fog: clip z,w
         * to the fragment stage; both interpolate perspective-correct, so
         * z/w is the fragment's true NDC depth. No extra attribute. */
        append_line(vs_buf, &vs_len, "OUTPUT vec2 vGeZw;");
    }
    if (cc_features.opt_ge_pixfog) {
        /* fog register curve (mul, offset) rides as a constant attribute */
        append_line(vs_buf, &vs_len, "INPUT vec2 aGeFogP;");
        append_line(vs_buf, &vs_len, "OUTPUT vec2 vGeFogP;");
        num_floats += 2;
    }
    if (cc_features.opt_ge_sky_pixdiv) {
        /* GE sky/water: the texcoords arrive multiplied by q (the RDP's
         * 1/W plane, normalised per command); u*q, v*q and q are all linear
         * in screen space and these vertices have w = 1, so the plain
         * varyings interpolate them exactly as the RDP walks S, T and W. */
        append_line(vs_buf, &vs_len, "INPUT float aGeSkyQ;");
        append_line(vs_buf, &vs_len, "OUTPUT float vGeSkyQ;");
        num_floats += 1;
    }

    if (cc_features.opt_grayscale) {
        append_line(vs_buf, &vs_len, "INPUT vec4 aGrayscaleColor;");
        append_line(vs_buf, &vs_len, "OUTPUT vec4 vGrayscaleColor;");
        num_floats += 4;
    }

    /* GE SHADER_OPT_NOPERSP_SHADE (sky/water cells with real clip W): the
     * RDP interpolates shade screen-linearly, so the colour varyings take
     * the GLSL 1.30 'noperspective' qualifier (desktop GL only - ES has no
     * core equivalent, it silently keeps perspective there). */
    const char* shade_qual = (cc_features.opt_nopersp_shade && gl_glsl_version >= 130 && !gl_es) ? "noperspective " : "";
    for (int i = 0; i < cc_features.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "INPUT vec%d aInput%d;\n", cc_features.opt_alpha ? 4 : 3, i + 1);
        vs_len += sprintf(vs_buf + vs_len, "%sOUTPUT vec%d vInput%d;\n", shade_qual, cc_features.opt_alpha ? 4 : 3, i + 1);
        num_floats += cc_features.opt_alpha ? 4 : 3;
    }

    append_line(vs_buf, &vs_len, "void main() {");
    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            vs_len += sprintf(vs_buf + vs_len, "    vTexCoord%d = aTexCoord%d;\n", i, i);
            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    vs_len += sprintf(vs_buf + vs_len, "    vTexClamp%s%d = aTexClamp%s%d;\n", j == 0 ? "S" : "T", i,
                                      j == 0 ? "S" : "T", i);
                }
            }
        }
    }
    if (cc_features.opt_fog) {
        append_line(vs_buf, &vs_len, "    vFog = aFog;");
    }
    if (cc_features.opt_grayscale) {
        append_line(vs_buf, &vs_len, "    vGrayscaleColor = aGrayscaleColor;");
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "    vInput%d = aInput%d;\n", i + 1, i + 1);
    }

    if (cc_features.opt_ge_farclip || cc_features.opt_ge_pixfog) {
        append_line(vs_buf, &vs_len, "    vGeZw = aVtxPos.zw;");
    }
    if (cc_features.opt_ge_pixfog) {
        append_line(vs_buf, &vs_len, "    vGeFogP = aGeFogP;");
    }
    if (cc_features.opt_ge_sky_pixdiv) {
        append_line(vs_buf, &vs_len, "    vGeSkyQ = aGeSkyQ;");
    }
    append_line(vs_buf, &vs_len, "    gl_Position = aVtxPos;");
    if (!GLAD_GL_ARB_depth_clamp) {
        // HACK: workaround for no GL_DEPTH_CLAMP
        append_line(vs_buf, &vs_len, "    gl_Position.z *= 0.3f;");
    }
    append_line(vs_buf, &vs_len, "}");

    // Fragment shader

    fs_len += sprintf(fs_buf + fs_len, "#version %s\n", gl_glsl_version_str);

    if (gl_es) {
        /* Fragment highp when the GPU has it (ES 3.0 permits mediump-only
         * fragments); N64 combiner math and fog benefit from the extra range. */
        append_line(fs_buf, &fs_len, "#ifdef GL_FRAGMENT_PRECISION_HIGH");
        append_line(fs_buf, &fs_len, "precision highp float;");
        append_line(fs_buf, &fs_len, "#else");
        append_line(fs_buf, &fs_len, "precision mediump float;");
        append_line(fs_buf, &fs_len, "#endif");
    }

    if (gl_glsl_version >= 130) {
        append_line(fs_buf, &fs_len, "#define INPUT in");
        append_line(fs_buf, &fs_len, "#define OUTPUT_COLOR outColor");
        append_line(fs_buf, &fs_len, "#define SAMPLE_TEX(tex, uv) texture(tex, uv)");
    } else {
        append_line(fs_buf, &fs_len, "#define INPUT varying");
        append_line(fs_buf, &fs_len, "#define OUTPUT_COLOR gl_FragColor");
        append_line(fs_buf, &fs_len, "#define SAMPLE_TEX(tex, uv) texture2D(tex, uv)");
    }

    // Reference approach to color wrapping as per GLideN64
    // Return wrapped value of x in interval [low, high)
    append_line(fs_buf, &fs_len, "#define WRAP(x, low, high) mod((x)-(low), (high)-(low)) + (low)");

    append_line(fs_buf, &fs_len, "#define TEX_OFFSET(tex, uv, texSize, off) SAMPLE_TEX(tex, uv - (off)/texSize)");

    // append_line(fs_buf, &fs_len, "precision mediump float;");
    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            fs_len += sprintf(fs_buf + fs_len, "INPUT vec2 vTexCoord%d;\n", i);
            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    fs_len += sprintf(fs_buf + fs_len, "INPUT float vTexClamp%s%d;\n", j == 0 ? "S" : "T", i);
                }
            }
        }
    }
    if (cc_features.opt_fog) {
        append_line(fs_buf, &fs_len, "INPUT vec4 vFog;");
    }
    if (cc_features.opt_ge_farclip || cc_features.opt_ge_pixfog) {
        append_line(fs_buf, &fs_len, "INPUT vec2 vGeZw;");
    }
    if (cc_features.opt_ge_pixfog) {
        append_line(fs_buf, &fs_len, "INPUT vec2 vGeFogP;");
    }
    if (cc_features.opt_ge_sky_pixdiv) {
        append_line(fs_buf, &fs_len, "INPUT float vGeSkyQ;");
    }
    if (cc_features.opt_grayscale) {
        append_line(fs_buf, &fs_len, "INPUT vec4 vGrayscaleColor;");
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        fs_len += sprintf(fs_buf + fs_len, "%sINPUT vec%d vInput%d;\n", shade_qual, cc_features.opt_alpha ? 4 : 3, i + 1);
    }

    if (cc_features.used_textures[0]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex0;");
        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "uniform int three_point_filter0;");
    }
    if (cc_features.used_textures[1]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex1;");
        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "uniform int three_point_filter1;");
    }

    append_line(fs_buf, &fs_len, "uniform int frame_count;");
    append_line(fs_buf, &fs_len, "uniform float noise_scale;");

    append_line(fs_buf, &fs_len, "float random(in vec3 value) {");
    append_line(fs_buf, &fs_len, "    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));");
    append_line(fs_buf, &fs_len, "    return fract(sin(random) * 143758.5453);");
    append_line(fs_buf, &fs_len, "}");

    if (current_filter_mode == FILTER_THREE_POINT) {
        append_line(fs_buf, &fs_len, "vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {");
        append_line(fs_buf, &fs_len, "    vec2 offset = fract(texCoord*texSize - vec2(0.5));");
        append_line(fs_buf, &fs_len, "    offset -= step(1.0, offset.x + offset.y);");
        append_line(fs_buf, &fs_len, "    vec4 c0 = TEX_OFFSET(tex, texCoord, texSize, offset);");
        append_line(fs_buf, &fs_len, "    vec4 c1 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x - sign(offset.x), offset.y));");
        append_line(fs_buf, &fs_len, "    vec4 c2 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x, offset.y - sign(offset.y)));");
        append_line(fs_buf, &fs_len, "    return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);");
        append_line(fs_buf, &fs_len, "}");
    }

    if (cc_features.opt_blur) {
        // blur filter, used for menu backgrounds
        // used to be two for loops from 0 to 4, but apparently intel drivers crashed trying to unroll it
        // used to have a const weight array, but apparently drivers for the GT620 don't like const array initializers

        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "lowp vec4 hookTexture2D(in sampler2D t, in vec2 uv, in vec2 texSize, in int three_point_filter) {");
        else
            append_line(fs_buf, &fs_len, "lowp vec4 hookTexture2D(in sampler2D t, in vec2 uv, in vec2 texSize) {");

        append_line(fs_buf, &fs_len, "    lowp vec4 cw = vec4(0.0);");
        append_line(fs_buf, &fs_len, "    for (int i = 0; i < 16; ++i) {");
        append_line(fs_buf, &fs_len, "        vec2 xy = vec2(float(i & 3), float(i >> 2));");
        append_line(fs_buf, &fs_len, "        lowp float w = 0.009947 - length(xy) * 0.001;");
        append_line(fs_buf, &fs_len, "        vec2 scaled_uv = uv + (vec2(-1.5) + xy) / texSize;");

        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "        lowp vec4 tex = mix(SAMPLE_TEX(t, scaled_uv), filter3point(t, scaled_uv, texSize), float(three_point_filter));");
        else
            append_line(fs_buf, &fs_len, "        lowp vec4 tex = SAMPLE_TEX(t, scaled_uv);");

        append_line(fs_buf, &fs_len, "        cw += vec4(tex.rgb * w, w);");
        append_line(fs_buf, &fs_len, "    }");
        append_line(fs_buf, &fs_len, "    return vec4(cw.rgb / cw.a, 1.0);");
        append_line(fs_buf, &fs_len, "}");
    } else {
        if (current_filter_mode == FILTER_THREE_POINT) {
            append_line(fs_buf, &fs_len, "vec4 hookTexture2D(in sampler2D tex, in vec2 uv, in vec2 texSize, in int three_point_filter) {");
            append_line(fs_buf, &fs_len, "    return mix(SAMPLE_TEX(tex, uv), filter3point(tex, uv, texSize), float(three_point_filter));");
            append_line(fs_buf, &fs_len, "}");
        } else {
            append_line(fs_buf, &fs_len, "vec4 hookTexture2D(in sampler2D tex, in vec2 uv, in vec2 texSize) {");
            append_line(fs_buf, &fs_len, "    return SAMPLE_TEX(tex, uv);");
            append_line(fs_buf, &fs_len, "}");
        }
    }

    if (gl_glsl_version >= 130) {
        append_line(fs_buf, &fs_len, "out vec4 outColor;");
    }

    append_line(fs_buf, &fs_len, "void main() {");

    if (cc_features.opt_ge_farclip) {
        /* GE far clip: fragment past the projection far plane - the console
         * clips fogless geometry there. */
        append_line(fs_buf, &fs_len, "    if (vGeZw.x > vGeZw.y) discard;");
    }

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            bool s = cc_features.clamp[i][0], t = cc_features.clamp[i][1];

            fs_len += sprintf(fs_buf + fs_len, "    vec2 texSize%d = vec2(textureSize(uTex%d, 0));\n", i, i);

            /* GE sky/water per-pixel divide: S/W and T/W by 1/W at this pixel,
             * as the RDP does (the old CPU grid only approximated it). q is
             * normalised to 1 at the nearest corner; the floor only guards the
             * horizon line itself. */
            const char* tc = cc_features.opt_ge_sky_pixdiv ? "geSkyTc" : "vTexCoord";
            if (cc_features.opt_ge_sky_pixdiv) {
                fs_len += sprintf(fs_buf + fs_len, "    vec2 geSkyTc%d = vTexCoord%d / max(vGeSkyQ, 1e-7);\n", i, i);
            }

            if (!s && !t) {
                fs_len += sprintf(fs_buf + fs_len, "    vec2 vTexCoordAdj%d = %s%d;\n", i, tc, i);
            } else {
                if (s && t) {
                    fs_len += sprintf(fs_buf + fs_len,
                                      "    vec2 vTexCoordAdj%d = clamp(%s%d, 0.5 / texSize%d, "
                                      "vec2(vTexClampS%d, vTexClampT%d));\n",
                                      i, tc, i, i, i, i);
                } else if (s) {
                    fs_len += sprintf(fs_buf + fs_len,
                                      "    vec2 vTexCoordAdj%d = vec2(clamp(%s%d.s, 0.5 / "
                                      "texSize%d.s, vTexClampS%d), %s%d.t);\n",
                                      i, tc, i, i, i, tc, i);
                } else {
                    fs_len += sprintf(fs_buf + fs_len,
                                      "    vec2 vTexCoordAdj%d = vec2(%s%d.s, clamp(%s%d.t, "
                                      "0.5 / texSize%d.t, vTexClampT%d));\n",
                                      i, tc, i, tc, i, i, i);
                }
            }

            if (current_filter_mode == FILTER_THREE_POINT)
                fs_len += sprintf(fs_buf + fs_len, "    vec4 texVal%d = hookTexture2D(uTex%d, vTexCoordAdj%d, texSize%d, three_point_filter%d);\n", i, i, i, i, i);
            else
                fs_len += sprintf(fs_buf + fs_len, "    vec4 texVal%d = hookTexture2D(uTex%d, vTexCoordAdj%d, texSize%d);\n", i, i, i, i);
        }
    }

    append_line(fs_buf, &fs_len, cc_features.opt_alpha ? "    vec4 texel;" : "    vec3 texel;");
    for (int c = 0; c < (cc_features.opt_2cyc ? 2 : 1); c++) {
        append_str(fs_buf, &fs_len, "    texel = ");
        if (!cc_features.color_alpha_same[c] && cc_features.opt_alpha) {
            append_str(fs_buf, &fs_len, "vec4(");
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][0],
                           cc_features.do_multiply[c][0], cc_features.do_mix[c][0], false, false, true);
            append_str(fs_buf, &fs_len, ", ");
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][1],
                           cc_features.do_multiply[c][1], cc_features.do_mix[c][1], true, true, true);
            append_str(fs_buf, &fs_len, ")");
        } else {
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][0],
                           cc_features.do_multiply[c][0], cc_features.do_mix[c][0], cc_features.opt_alpha, false,
                           cc_features.opt_alpha);
        }
        append_line(fs_buf, &fs_len, ";");

        if (c == 0) {
            append_line(fs_buf, &fs_len, "    texel = WRAP(texel, -1.01, 1.01);");
        }
    }

    append_line(fs_buf, &fs_len, "    texel = WRAP(texel, -0.51, 1.51);");
    append_line(fs_buf, &fs_len, "    texel = clamp(texel, 0.0, 1.0);");
    // TODO discard if alpha is 0?
    if (cc_features.opt_fog) {
        static int passthru = -1;
        if (passthru < 0) { const char* e = getenv("GE_PIXFOG_PASSTHRU"); passthru = (e && e[0] != '0') ? 1 : 0; }
        if (cc_features.opt_ge_pixfog && !passthru) {
            /* GE per-pixel fog: the RDP fogs each pixel from its own depth,
             * whereas vertex fog interpolated across a huge triangle mid-fogs
             * entire slabs. This recomputes the fog register curve at the
             * fragment's true z/w, matching the vertex value at the vertices. */
            append_line(fs_buf, &fs_len,
                        "    float geFogA = clamp((vGeZw.x / vGeZw.y) * vGeFogP.x + vGeFogP.y, 0.0, 255.0) / 255.0;");
        } else {
            append_line(fs_buf, &fs_len, "    float geFogA = vFog.a;");
        }
        if (cc_features.opt_alpha) {
            append_line(fs_buf, &fs_len, "    texel = vec4(mix(texel.rgb, vFog.rgb, geFogA), texel.a);");
        } else {
            append_line(fs_buf, &fs_len, "    texel = mix(texel, vFog.rgb, geFogA);");
        }
    }

    if (cc_features.opt_texture_edge && cc_features.opt_alpha) {
        append_line(fs_buf, &fs_len, "    if (texel.a > 0.19) texel.a = 1.0; else discard;");
    }

    if (cc_features.opt_alpha && cc_features.opt_noise) {
        append_line(fs_buf, &fs_len,
                    "    texel.a *= floor(clamp(random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + "
                    "texel.a, 0.0, 1.0));");
    }

    if (cc_features.opt_grayscale) {
        append_line(fs_buf, &fs_len, "    float intensity = (texel.r + texel.g + texel.b) / 3.0;");
        append_line(fs_buf, &fs_len, "    vec3 new_texel = vGrayscaleColor.rgb * intensity;");
        append_line(fs_buf, &fs_len, "    texel.rgb = mix(texel.rgb, new_texel, vGrayscaleColor.a);");
    }

    if (cc_features.opt_alpha) {
        if (cc_features.opt_alpha_threshold) {
            append_line(fs_buf, &fs_len, "    if (texel.a < 8.0 / 256.0) discard;");
        }
        if (cc_features.opt_invisible) {
            append_line(fs_buf, &fs_len, "    texel.a = 0.0;");
        }
        if (cc_features.opt_alpha_cvg_sel) {
            /* GE: with RDP ALPHA_CVG_SEL the blender uses coverage, not the
             * combiner alpha, so interior pixels are always opaque; without
             * this, AA_ZB_OPA geometry with vertex alpha below 255 turns
             * see-through. Under G_AC_DITHER, pixels the dithered compare
             * rejected (alpha 0) are discarded before forcing opaque. */
            if (cc_features.opt_noise) {
                append_line(fs_buf, &fs_len, "    if (texel.a <= 0.0) discard;");
            }
            append_line(fs_buf, &fs_len, "    texel.a = 1.0;");
        }

        if (cc_features.opt_sky_rgb5) {
            /* GE: N64 RGBA5551 framebuffer truncation for sky/water draws */
            append_line(fs_buf, &fs_len, "    texel.rgb = floor(texel.rgb * 31.0) / 31.0;");
        }
        append_line(fs_buf, &fs_len, "    OUTPUT_COLOR = texel;");
    } else {
        if (cc_features.opt_sky_rgb5) {
            append_line(fs_buf, &fs_len, "    texel = floor(texel * 31.0) / 31.0;");
        }
        append_line(fs_buf, &fs_len, "    OUTPUT_COLOR = vec4(texel, 1.0);");
    }

    append_line(fs_buf, &fs_len, "}");

    vs_buf[vs_len] = '\0';
    fs_buf[fs_len] = '\0';

    const GLchar* sources[2] = { vs_buf, fs_buf };
    const GLint lengths[2] = { (GLint)vs_len, (GLint)fs_len };
    GLint success;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 1024;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(vertex_shader, max_length, &max_length, &error_log[0]);
        sysLogPrintf(LOG_ERROR, "Failed to compile this vertex shader (ID %llx, %x):\n%s", shader_id0, shader_id1, vs_buf);
        sysFatalError("Vertex shader compilation failed:\n%s", error_log);
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 1024;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(fragment_shader, max_length, &max_length, &error_log[0]);
        sysLogPrintf(LOG_ERROR, "Failed to compile this fragment shader (ID %llx, %x):\n%s", shader_id0, shader_id1, fs_buf);
        sysFatalError("Fragment shader compilation failed:\n%s", error_log);
    }

    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    glDetachShader(shader_program, vertex_shader);
    glDetachShader(shader_program, fragment_shader);
    glDeleteShader(vertex_shader);
    /* the fragment shader stays alive for the retained twin program (gl_ret_program_get) */

    size_t cnt = 0;

    struct ShaderProgram* prg = &shader_program_pool[make_pair(shader_id0, shader_id1)];
    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aVtxPos");
    prg->attrib_sizes[cnt] = 4;
    ++cnt;

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            char name[32];
            sprintf(name, "aTexCoord%d", i);
            prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
            prg->attrib_sizes[cnt] = 2;
            ++cnt;

            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    sprintf(name, "aTexClamp%s%d", j == 0 ? "S" : "T", i);
                    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
                    prg->attrib_sizes[cnt] = 1;
                    ++cnt;
                }
            }
        }
    }

    if (cc_features.opt_fog) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aFog");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    if (cc_features.opt_ge_pixfog) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aGeFogP");
        prg->attrib_sizes[cnt] = 2;
        ++cnt;
    }

    if (cc_features.opt_ge_sky_pixdiv) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aGeSkyQ");
        prg->attrib_sizes[cnt] = 1;
        ++cnt;
    }

    if (cc_features.opt_grayscale) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aGrayscaleColor");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    for (int i = 0; i < cc_features.num_inputs; i++) {
        char name[16];
        sprintf(name, "aInput%d", i + 1);
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
        prg->attrib_sizes[cnt] = cc_features.opt_alpha ? 4 : 3;
        ++cnt;
    }

    prg->opengl_program_id = shader_program;
    prg->num_inputs = cc_features.num_inputs;
    prg->used_textures[0] = cc_features.used_textures[0];
    prg->used_textures[1] = cc_features.used_textures[1];
    prg->num_floats = num_floats;
    prg->num_attribs = cnt;
    prg->fragment_shader = fragment_shader;
    prg->shader_id0 = shader_id0;
    prg->shader_id1 = shader_id1;

    gl_ret_restore(); /* the new program's attribute arrays are bound to the stream VBO below */
    gl_current_prg_set(prg);
    glUseProgram(shader_program);

    if (cc_features.used_textures[0]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex0");
        glUniform1i(sampler_location, 0);
    }
    if (cc_features.used_textures[1]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex1");
        glUniform1i(sampler_location, 1);
    }

    prg->frame_count_location = glGetUniformLocation(shader_program, "frame_count");
    prg->noise_scale_location = glGetUniformLocation(shader_program, "noise_scale");
    prg->three_point_filter_locations[0] = glGetUniformLocation(shader_program, "three_point_filter0");
    prg->three_point_filter_locations[1] = glGetUniformLocation(shader_program, "three_point_filter1");

    gfx_opengl_load_shader(prg);

    portHitchNote(3 /* shaders */, portProfNow() - hitch_t0);
    return prg;
}

static struct ShaderProgram* gfx_opengl_lookup_shader(uint64_t shader_id0, uint32_t shader_id1) {
    auto it = shader_program_pool.find(make_pair(shader_id0, shader_id1));
    return it == shader_program_pool.end() ? nullptr : &it->second;
}

static void gfx_opengl_shader_get_info(struct ShaderProgram* prg, uint8_t* num_inputs, bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

static void gl_ret_clear_programs(void); /* below */
static void gfx_opengl_clear_shaders(void) {
    glUseProgram(0);
    gl_ret_clear_programs();
    for (auto& pair : shader_program_pool) {
        glDeleteProgram(pair.second.opengl_program_id);
        glDeleteShader(pair.second.fragment_shader);
    }
    shader_program_pool.clear();
}

static GLuint gfx_opengl_new_texture(void) {
    GLuint ret;
    glGenTextures(1, &ret);
    return ret;
}

static void gfx_opengl_delete_texture(uint32_t texID) {
    glDeleteTextures(1, &texID);
    gl_set_has_mips(texID, 0);
}

static void gfx_opengl_select_texture(int tile, GLuint texture_id, bool linear_filter) {
    glActiveTexture(GL_TEXTURE0 + tile);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    current_textures_linear_filter[tile] = linear_filter;
    current_texture_ids[tile] = texture_id;
    last_selected_tile = tile;
    gfx_opengl_sync_three_point();
}

/* Anisotropic filtering (EXT_texture_filter_anisotropic; core-adjacent,
 * effectively universal). Needs a mip chain to act on, so anisotropy > 1
 * also switches minification to trilinear mips generated at upload. */
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
static float gl_max_anisotropy = 0.0f; /* 0 = unqueried, <=1 = unsupported */

static float gfx_opengl_anisotropy(void) {
    if (gfx_anisotropy <= 1) {
        return 0.0f;
    }
    if (gl_max_anisotropy == 0.0f) {
        glGetError(); /* clear */
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &gl_max_anisotropy);
        if (glGetError() != GL_NO_ERROR || gl_max_anisotropy < 1.0f) {
            gl_max_anisotropy = 1.0f; /* unsupported */
        }
    }
    float aniso = (float)gfx_anisotropy;
    if (aniso > gl_max_anisotropy) {
        aniso = gl_max_anisotropy;
    }
    return (aniso > 1.0f) ? aniso : 0.0f;
}

/* side channel read by gfx_pc.cpp import_texture (see comment in the upload) */
extern "C" unsigned g_GfxUploadError;
extern "C" unsigned g_GfxUploadErrorCount;
unsigned g_GfxUploadError;
unsigned g_GfxUploadErrorCount;
/* the last upload's GL steps in us, for gfx_pc.cpp's slow-upload line */
extern "C" unsigned long long g_GfxUpDrainUs, g_GfxUpTexUs, g_GfxUpMipUs, g_GfxUpErrUs;
extern "C" unsigned g_GfxUpW, g_GfxUpH;
unsigned long long g_GfxUpDrainUs, g_GfxUpTexUs, g_GfxUpMipUs, g_GfxUpErrUs;
unsigned g_GfxUpW, g_GfxUpH;

static void gfx_opengl_upload_texture(const uint8_t* rgba32_buf, uint32_t width, uint32_t height) {
    { /* diagnostic (GE_TEXDUMP_ALL=1): write every UNIQUE upload to
       * texall_<n>_<w>x<h>.bin (contact-sheet triage, F2.5 blue floor).
       * Capped; uniqueness by cheap content hash. */
        static int dumpall = -1;
        if (dumpall < 0) dumpall = getenv("GE_TEXDUMP_ALL") != NULL;
        if (dumpall) {
            static uint32_t seen[512];
            static int nseen = 0, nfile = 0;
            uint32_t h = 2166136261u;
            for (uint32_t i = 0; i < width * height * 4; i += 7) {
                h = (h ^ rgba32_buf[i]) * 16777619u;
            }
            h ^= width * 131 + height;
            int found = 0;
            for (int i = 0; i < nseen; i++) if (seen[i] == h) { found = 1; break; }
            if (!found && nseen < 512 && nfile < 400) {
                extern const void* ge_dbg_import_addr;
                extern int ge_dbg_import_fmt, ge_dbg_import_siz, ge_dbg_import_tile, ge_dbg_import_tmem;
                extern unsigned ge_dbg_import_palsum;
                seen[nseen++] = h;
                char name[128];
                snprintf(name, sizeof(name), "texall_%03d_%ux%u_a%08lx_f%d_s%d_t%d_m%d_p%08x.bin",
                         nfile++, width, height,
                         (unsigned long)(uintptr_t)ge_dbg_import_addr,
                         ge_dbg_import_fmt, ge_dbg_import_siz, ge_dbg_import_tile,
                         ge_dbg_import_tmem, ge_dbg_import_palsum);
                FILE* f = fopen(name, "wb");
                if (f) { fwrite(rgba32_buf, 4, width * height, f); fclose(f); }
            }
        }
    }
    /* A texture upload can fail (e.g. GL_OUT_OF_MEMORY), leaving the texture
     * incomplete and sampling grey forever. The error is recorded in a side
     * channel so gfx_pc's import_texture can evict the cache entry and retry on
     * the next draw. */
    /* glGetError stalls until the driver drains its queue, which made texture
     * bursts stutter, so by default gfx_pc.cpp polls only every ~2 s instead
     * (gfx_opengl_poll_gl_error). GE_UPLOADERRCHECK=1 restores the per-upload
     * check and its evict-and-retry. */
    static int errcheck = -1;
    if (errcheck < 0) {
        const char* e = getenv("GE_UPLOADERRCHECK");
        errcheck = (e != NULL && atoi(e) != 0);
    }
    unsigned long long up_t = portProfNow();
    g_GfxUpW = width;
    g_GfxUpH = height;
    if (errcheck) {
        while (glGetError() != GL_NO_ERROR) { /* drain stale errors */ }
    }
    g_GfxUpDrainUs = portProfNow() - up_t;
    up_t += g_GfxUpDrainUs;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
    g_GfxUpTexUs = portProfNow() - up_t;
    up_t += g_GfxUpTexUs;
    /* a fresh level 0 invalidates any older chain on this object; the N64
     * mipmap sampler regenerates on demand (or right here under aniso) */
    gl_set_has_mips(current_texture_ids[last_selected_tile], 0);
    if (gfx_opengl_anisotropy() > 1.0f) {
        glGenerateMipmap(GL_TEXTURE_2D); /* aniso acts on minification mips */
        gl_set_has_mips(current_texture_ids[last_selected_tile], 1);
    }
    g_GfxUpMipUs = portProfNow() - up_t;
    up_t += g_GfxUpMipUs;
    {
        GLenum err = errcheck ? glGetError() : GL_NO_ERROR;
        g_GfxUpErrUs = portProfNow() - up_t;
        /* GE_FAKEUPLOADFAIL=n (diag): report every n-th upload as failed so the
         * evict-and-retry path can be exercised without starving the GPU */
        static int fakefail = -1;
        static unsigned nup;
        if (fakefail < 0) { const char *e = getenv("GE_FAKEUPLOADFAIL"); fakefail = e ? atoi(e) : 0; }
        if (fakefail > 0 && (++nup % (unsigned)fakefail) == 0) err = GL_OUT_OF_MEMORY;
        if (err != GL_NO_ERROR) {
            g_GfxUploadError = (unsigned)err;
            g_GfxUploadErrorCount++;
        }
    }
}

/* gfx_pc.cpp's periodic check: drain the GL error flags and return the first,
 * or 0. One driver sync, so it is called every ~2 s, not per upload. */
extern "C" unsigned gfx_opengl_poll_gl_error(void) {
    GLenum first = GL_NO_ERROR;
    for (int n = 0; n < 16; n++) {
        const GLenum e = glGetError();
        if (e == GL_NO_ERROR) {
            break;
        }
        if (first == GL_NO_ERROR) {
            first = e;
        }
    }
    return (unsigned)first;
}

#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif

static uint32_t gfx_cm_to_opengl(uint32_t val) {
    switch (val) {
        case 0x10: /* GE_TX_BORDER (gfx_pc.cpp): a grown font glyph - outside the
                    * texture is GL's default border colour, transparent black */
            return GL_CLAMP_TO_BORDER;
        case G_TX_NOMIRROR | G_TX_CLAMP:
            return GL_CLAMP_TO_EDGE;
        case G_TX_MIRROR | G_TX_WRAP:
            return GL_MIRRORED_REPEAT;
        case G_TX_MIRROR | G_TX_CLAMP:
            return gl_mirror_clamp;
        case G_TX_NOMIRROR | G_TX_WRAP:
            return GL_REPEAT;
    }
    return 0;
}

extern "C" int ge_sky_tri_active; /* gfx_pc: inside an RDP-triangle (sky/water) draw */
extern "C" int ge_mip_active;     /* gfx_pc: this sampler serves a G_TL_LOD draw - minify through mips */

static void gfx_opengl_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    const GLint filter = linear_filter && (current_filter_mode == FILTER_LINEAR) ? GL_LINEAR : GL_NEAREST;
    /* THIS is the value the draw actually wants - gfx_pc computes it per draw
     * and calls here, while select_texture only ever saw the texture's CACHED
     * flag. Record it and push, or the shader keeps the previous draw's. */
    if (tile >= 0 && tile < 2) {
        current_textures_linear_filter[tile] = linear_filter;
        gfx_opengl_sync_three_point();
    }
    const float aniso = gfx_opengl_anisotropy();
    glActiveTexture(GL_TEXTURE0 + tile);
    if (aniso > 1.0f) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
    } else if (gl_max_anisotropy > 1.0f) {
        /* Anisotropy is per texture object and survives a recycled name, so OFF
         * has to be written back or the old 2x-16x stays */
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
    }
    if (aniso > 1.0f && filter == GL_LINEAR && !ge_sky_tri_active) {
        /* Mips exist (generated at upload when aniso is on) and trilinear
         * minification is what the anisotropic samples run over. If aniso was
         * turned on after this texture's upload, build the chain here or the
         * texture is incomplete. */
        const GLuint id = current_texture_ids[tile];
        if (!gl_has_mips(id)) {
            glGenerateMipmap(GL_TEXTURE_2D);
            gl_set_has_mips(id, 1);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
        return;
    }
    if (ge_mip_active && !ge_sky_tri_active) {
        /* GE N64 mipmaps: the RDP walked a mip chain for this draw, so give GL
         * one (once per upload, box-filtered like GE's own builder) and minify
         * through it to avoid moire. Bilerp becomes trilinear and point /
         * 3-point becomes nearest-within-level with levels blended;
         * magnification is untouched. */
        const GLuint id = current_texture_ids[tile];
        if (!gl_has_mips(id)) {
            glGenerateMipmap(GL_TEXTURE_2D);
            gl_set_has_mips(id, 1);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        (filter == GL_LINEAR) ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
        return;
    }
    if (ge_sky_tri_active) {
        /* unconditional for sky draws: the tile's point-sample flag must
         * not veto minification mips (the sky was bilerp-intended; crisp
         * repeats at the horizon are worse than any filter mismatch) */
        /* GE sky horizon: the cloud texture tiles thousands of times into the
         * last few scanlines, which unmipped sampling renders as a hard band of
         * repeating clouds. Trilinear mips blur it into haze, and regenerating
         * them on each sky draw keeps the shared texture correct for its other
         * uses. */
        /* GE_SKY_NOMIP=1: diagnostic kill-switch for the sniper-zoom
         * cloud-slide bisect (zoom sweeps the LOD ramp; if the slide dies
         * with mips off, the mip chain is implicated). */
        static int skynomip = -1;
        if (skynomip < 0) {
            const char* e = getenv("GE_SKY_NOMIP");
            skynomip = (e != NULL) && (e[0] != '0');
        }
        if (skynomip) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        } else {
            glGenerateMipmap(GL_TEXTURE_2D);
            gl_set_has_mips(current_texture_ids[tile], 1);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        }
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
}

/* GE enhancement: depth bias for translucent overlays. GE's wall decals
 * (hazard triangles, signs, grime) are coplanar XLU polys over opaque
 * walls and z-fight in the base game too. They are Z-tested and never Z-write,
 * so nudging them toward the camera only helps them win ties with the surface
 * they sit on. ZMODE_DEC decals needed more than fast3d's stock -2,-2 offset
 * because the scene sits compressed in the top of the depth range, so
 * interpolation error exceeds 2 units. GE_DEC_ZBIAS=factor,units sets the decal
 * bias (default -4,-8). GE_XLU_ZBIAS=factor,units adds
 * the same nudge to plain Z-tested/no-Z-write XLU (default 0,0 = off - it
 * would touch every window/pane; enable to chase XLU_SURF overlays). */
static float ge_xlu_bias_f = 0.0f, ge_xlu_bias_u = 0.0f;
static float ge_dec_bias_f = -4.0f, ge_dec_bias_u = -8.0f;
static void ge_read_zbias(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    const char* e = getenv("GE_XLU_ZBIAS");
    if (e) sscanf(e, "%f,%f", &ge_xlu_bias_f, &ge_xlu_bias_u);
    e = getenv("GE_DEC_ZBIAS");
    if (e) sscanf(e, "%f,%f", &ge_dec_bias_f, &ge_dec_bias_u);
}

/* ZMODE_DEC draw in progress (z-tested, z-compared): gfx_opengl_draw_triangles
 * gives it the RDP's two-sided depth window through the stencil (see there).
 * The last depth-mode arguments are kept so the draw can restore them. */
static bool ge_dec_draw;
static bool ge_dm_args[4];
static uint16_t ge_dm_zmode;

static void gfx_opengl_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode) {
    ge_read_zbias();
    ge_dec_draw = depth_test && depth_compare && zmode == ZMODE_DEC;
    ge_dm_args[0] = depth_test; ge_dm_args[1] = depth_update; ge_dm_args[2] = depth_compare; ge_dm_args[3] = depth_source_prim;
    ge_dm_zmode = zmode;
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(depth_update ? GL_TRUE : GL_FALSE);
        current_depth_mask = depth_update;

        if (depth_compare) {
            switch (zmode) {
                case ZMODE_INTER:
                    glDepthFunc(GL_LEQUAL);
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(0, 0);
                    break;

                case ZMODE_OPA:
                case ZMODE_XLU:
                    if (depth_source_prim) {
                        glDepthFunc(GL_LEQUAL);
                    } else {
                        glDepthFunc(GL_LESS);
                    }
                    if (!depth_update && (ge_xlu_bias_f != 0.0f || ge_xlu_bias_u != 0.0f)) {
                        glEnable(GL_POLYGON_OFFSET_FILL);
                        glPolygonOffset(ge_xlu_bias_f, ge_xlu_bias_u);
                    } else {
                        glDisable(GL_POLYGON_OFFSET_FILL);
                        glPolygonOffset(0, 0);
                    }
                    break;

                case ZMODE_DEC:
                    glDepthFunc(GL_LEQUAL);
                    glEnable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(ge_dec_bias_f, ge_dec_bias_u);
                    break;
            }
        } else {
            glDepthFunc(GL_ALWAYS);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(0, 0);
        }
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

static void gfx_opengl_set_depth_range(float znear, float zfar) {
    /* GE: far-plane geometry is depth-CLAMPED (no far clip, RSP parity - see
     * gfx_pc CLIP_FAR). Clamped fragments land on the range's far value; the
     * buffer clears to 1.0 and opaque tests GL_LESS, so keep the far end a
     * hair inside 1.0 or every clamped far triangle fails the test. */
    if (zfar > 0.9999995f) zfar = 0.9999995f;
    if (glDepthRangef) {
        glDepthRangef(znear, zfar);
    } else {
        glDepthRange(znear, zfar);
    }
}

static void gfx_opengl_set_viewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

static void gfx_opengl_set_scissor(int x, int y, int width, int height) {
    glScissor(x, y, width, height);
}

static void gfx_opengl_set_use_alpha(bool use_alpha, bool modulate) {
    if (use_alpha) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    if (modulate) {
        glBlendFunc(GL_DST_COLOR, GL_ZERO);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}

/* DECAL CLIP: the RDP's ZMODE_DEC only passes where the stored depth is
 * within a small window either side of the decal's own depth, so on N64 a
 * bullet hole on a wall's edge is cut off where the wall ends. fast3d emulates
 * DEC as a one-sided LEQUAL (plus gfx_sp_tri1's pull toward the eye), so holes
 * overhung into the air. The far side of the window is added through the
 * stencil:
 *   1. mark: no colour/depth writes, the decal pushed GE_DEC_TOL world
 *      units AWAY from the eye, GEQUAL - marks pixels whose stored surface
 *      is no farther than that;
 *   2. draw: the normal pulled LEQUAL draw, only on marked pixels;
 *   3. clear: zero the stencil under the footprint (pass 2 keeps the marks
 *      so overlapping holes in one batch still blend over each other).
 * The push uses the projection's z/w intercept b (as the pull does), so the
 * window is a world distance at every range. Immediate draws only; retained
 * room decals keep the one-sided test. GE_DEC_CLIP=0 = old behaviour,
 * GE_DEC_TOL=<world units> (default 8). */
extern float gfx_ge_dec_b, gfx_ge_dec_d;
static void gfx_opengl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    // printf("flushing %d tris\n", buf_vbo_num_tris);
    static int clip = -1;
    static float tol = 8.0f;
    if (clip < 0) {
        const char* e = getenv("GE_DEC_CLIP");
        clip = (e == NULL || e[0] != '0') ? 1 : 0;
        e = getenv("GE_DEC_TOL");
        if (e != NULL && atof(e) > 0.0) tol = (float)atof(e);
    }
    gl_ret_restore(); /* after a retained draw: the stream VBO + this program's arrays again */
    if (!clip || !ge_dec_draw || gfx_ge_dec_b >= 0.0f || buf_vbo_num_tris == 0) {
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 3 * buf_vbo_num_tris);
        return;
    }

    /* NEVER a destructible static here: its destructor would run at exit()
     * while the render thread is still drawing decals into it, corrupting the
     * heap. Leaked on purpose. */
    static std::vector<float>& pushed = *new std::vector<float>();
    const size_t nv = 3 * buf_vbo_num_tris;
    const size_t stride = buf_vbo_len / nv;
    const float b = gfx_ge_dec_b, d = gfx_ge_dec_d;
    pushed.assign(buf_vbo, buf_vbo + buf_vbo_len);
    for (size_t i = 0; i < nv; i++) {
        float* v = &pushed[i * stride];
        const float w = v[3];
        const float dv = (w > 4.0f * d) ? d : 0.0f; /* gfx_sp_tri1 leaves near-plane verts unpulled */
        if (w - dv > 1.0f) {
            v[2] -= b * tol * w / ((w - dv) * (w - dv + tol));
        }
    }

    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    /* 1. mark */
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_GEQUAL);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, pushed.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, nv);
    /* 2. draw */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gfx_opengl_set_depth_mode(ge_dm_args[0], ge_dm_args[1], ge_dm_args[2], ge_dm_args[3], ge_dm_zmode);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, nv);
    /* 3. clear */
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_ALWAYS);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
    glDrawArrays(GL_TRIANGLES, 0, nv);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_STENCIL_TEST);
    gfx_opengl_set_depth_mode(ge_dm_args[0], ge_dm_args[1], ge_dm_args[2], ge_dm_args[3], ge_dm_zmode);
}

typedef void (APIENTRY *DEBUGPROC)(GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar *message,
    const void *userParam);

static void APIENTRY gl_debug(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *msg, const void *p) {
    sysLogPrintf(LOG_WARNING, "GL: (%05x) %s", id, msg);
}

static void gfx_opengl_enable_debug(void) {
    if (GLAD_GL_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
    }
    if (glDebugMessageControl != NULL) {
        // enable everything except some specific spam messages
        const GLuint disable[] = {
            0x20061, /* "Framebuffer detailed info" */
            0x20071  /* "Buffer detailed info" */
        };
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
        glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 2, disable, GL_FALSE);
    }
    if (glDebugMessageCallback != NULL) {
        glDebugMessageCallback(gl_debug, NULL);
    }
}

static bool gfx_opengl_supports_framebuffers(void) {
    if (GLVersion.major > 2) {
        // GL3.0+ supports everything we need, but we'll still check it for sanity
        return (glad_glFramebufferRenderbuffer && glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    if (GLAD_GL_ARB_framebuffer_object) {
        // some implementations might be missing these
        return (glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    if (GLAD_GL_EXT_framebuffer_object && GLAD_GL_EXT_framebuffer_blit && GLAD_GL_EXT_framebuffer_multisample) {
        // sanity check
        return (glad_glFramebufferRenderbuffer && glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    // nothing
    return false;
}

static bool gfx_opengl_supports_shaders(void) {
    if (GLVersion.major > 2) {
        // should support GLSL130+
        return true;
    }

    // check supported GLSL version
    const char *ver = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    if (ver) {
        int maj = 0, min = 0;
        sscanf(ver, "%d.%d", &maj, &min);
        if (maj > 1 || (maj == 1 && min > 20)) {
            // above 120, should be fine
            return true;
        }
    }

    // check for extension that adds textureSize
    return GLAD_GL_EXT_gpu_shader4;
}

static void gfx_opengl_log_info(void) {
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *vendor = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *glsl_version = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    sysLogPrintf(LOG_NOTE, "GL: version: %s", version ? version : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: vendor: %s", vendor ? vendor : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: renderer: %s", renderer ? renderer : "unknown");
    /* no GPU driver at all: Windows' GL 1.1 fallback, or a CPU rasteriser */
    if (renderer && (strstr(renderer, "GDI Generic") || strstr(renderer, "llvmpipe") ||
                     strstr(renderer, "softpipe") || strstr(renderer, "Microsoft Basic Render"))) {
        sysLogPrintf(LOG_WARNING, "GL: WARNING: '%s' is a SOFTWARE renderer - no GPU driver is serving OpenGL", renderer);
    }
    sysLogPrintf(LOG_NOTE, "GL: GLSL version: %s", glsl_version ? glsl_version : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: ARB_framebuffer_object: %s", gfx_opengl_supports_framebuffers() ? "yes" : "no");
    sysLogPrintf(LOG_NOTE, "GL: ARB_depth_clamp: %s", GLAD_GL_ARB_depth_clamp ? "yes" : "no");
    sysLogPrintf(LOG_NOTE, "GL: ARB_texture_mirror_clamp_to_edge: %s", GLAD_GL_ARB_texture_mirror_clamp_to_edge ? "yes" : "no");
}

static void *gl_load_proc(const char *name) {
    void *ret = SDL_GL_GetProcAddress(name);
    if (ret) {
        return ret;
    }

    // try with postfixes
    static const char *post[] = { "ARB", "EXT" };
    char tmp[256] = { 0 };
    for (size_t i = 0; i < sizeof(post) / sizeof(*post); ++i) {
        snprintf(tmp, sizeof(tmp), "%s%s", name, post[i]);
        ret = SDL_GL_GetProcAddress(tmp);
        if (ret) {
            return ret;
        }
    }

    sysLogPrintf(LOG_ERROR, "GL: could not find function: %s", name);

    return NULL;
}

static void gfx_opengl_init_extensions(void) {
    // patch up some extension values and pointers
    if (!GLAD_GL_ARB_depth_clamp) {
        if (GLAD_GL_EXT_depth_clamp || GLAD_GL_NV_depth_clamp) {
            GLAD_GL_ARB_depth_clamp = 1;
        } else if (!gl_es && GLVersion.major >= 3) {
            // GL3.2+ should have depth_clamp as part of the spec, but some devices don't report it for some reason
            GLAD_GL_ARB_depth_clamp = (GLVersion.major > 3 || (GLVersion.major == 3 && GLVersion.minor >= 2));
        }
    }

    if (!GLAD_GL_ARB_texture_mirror_clamp_to_edge) {
        GLAD_GL_ARB_texture_mirror_clamp_to_edge = GLAD_GL_EXT_texture_mirror_clamp_to_edge;
    }

    if (GLVersion.major < 3 && !GLAD_GL_ARB_framebuffer_object) {
        if (GLAD_GL_EXT_framebuffer_object && GLAD_GL_EXT_framebuffer_blit && GLAD_GL_EXT_framebuffer_multisample) {
            // because of the way glad works we'll have to copy the pointers over
            glad_glGenFramebuffers = glad_glGenFramebuffersEXT;
            glad_glGenRenderbuffers = glad_glGenRenderbuffersEXT;
            glad_glDeleteFramebuffers = glad_glDeleteFramebuffersEXT;
            glad_glDeleteRenderbuffers = glad_glDeleteRenderbuffersEXT;
            glad_glBindFramebuffer = glad_glBindFramebufferEXT;
            glad_glBindRenderbuffer = glad_glBindRenderbufferEXT;
            glad_glFramebufferRenderbuffer = glad_glFramebufferRenderbufferEXT;
            glad_glFramebufferTexture2D = glad_glFramebufferTexture2DEXT;
            glad_glRenderbufferStorage = glad_glRenderbufferStorageEXT;
            glad_glRenderbufferStorageMultisample = glad_glRenderbufferStorageMultisampleEXT;
            glad_glBlitFramebuffer = glad_glBlitFramebufferEXT;
        }
    }
}

static void gfx_opengl_init(void) {
    // The ES and desktop loaders parse GL_VERSION differently and populate
    // different function tables (notably glDepthRangef/glClearDepthf), so pick
    // before touching glad. SDL can report the profile before the context is
    // bound through the window manager's attributes.
    int val = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &val);
    gl_core_profile = (val == SDL_GL_CONTEXT_PROFILE_CORE);
    gl_es = (val == SDL_GL_CONTEXT_PROFILE_ES);

    const bool loaded = gl_es ? (gladLoadGLES2Loader(gl_load_proc) != 0)
                              : (gladLoadGLLoader(gl_load_proc) != 0);
    if (!loaded || glGetString == NULL || glEnable == NULL) {
        sysFatalError("Could not load %s.\nReported SDL error: %s",
                      gl_es ? "OpenGL ES" : "OpenGL", SDL_GetError());
    }

    if (gl_es) {
        /* GLAD's ES 3.0 table stops before glTexImage2DMultisample (ES 3.1).
         * Load it when the driver exposes it so the N64-AA multisample texture
         * path can run; the renderbuffer path remains the ES 3.0 fallback. */
        if (glad_glTexImage2DMultisample == NULL) {
            glad_glTexImage2DMultisample =
                (PFNGLTEXIMAGE2DMULTISAMPLEPROC) SDL_GL_GetProcAddress("glTexImage2DMultisample");
        }
    }

    gfx_opengl_init_extensions();

    if (sysArgCheck("--debug-gl")) {
        gfx_opengl_enable_debug();
    }
    // dump version info as early as possible, always: a user's log has to
    // name the GPU (iGPU vs dGPU is the first question on any fps report)
    gfx_opengl_log_info();

    if (GLVersion.major < 2 || (GLVersion.major == 2 && GLVersion.minor < 1)) {
        const char *ver = (const char *)glGetString(GL_VERSION);
        sysFatalError("Could not load OpenGL 2.1.\nReported version: %d.%d (%s)",
            GLVersion.major, GLVersion.minor, ver ? ver : "unknown");
    }

    if (!gfx_opengl_supports_shaders()) {
        sysLogPrintf(LOG_WARNING, "GL: GLSL 1.30 may be unsupported");
        // maybe replace this with sysFatalError, though the GLSL compiler will cause that later anyway
    }

    if (!gfx_framebuffers_enabled) {
        sysLogPrintf(LOG_WARNING, "GL: framebuffer effects disabled by user");
    } else if (!gfx_opengl_supports_framebuffers()) {
        sysLogPrintf(LOG_WARNING, "GL: GL_ARB_framebuffer_object unsupported, framebuffer effects disabled");
        gfx_framebuffers_enabled = false;
    }

    if ((GLVersion.major < 4 || GLVersion.minor < 4) && !GLAD_GL_ARB_texture_mirror_clamp_to_edge) {
        // GL_MIRROR_CLAMP_TO_EDGE unsupported
        gl_mirror_clamp = GL_MIRRORED_REPEAT;
    }

    // determine GLSL version
    if (gl_es) {
        // ES has its own numbering scheme, but it should support 300 even on 3.1 and 3.2
        gl_glsl_version = 300;
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d es", gl_glsl_version);
    } else if (!gl_core_profile) {
        // in compat profile we can just request the lowest possible
        gl_glsl_version = 130;
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d", gl_glsl_version);
    } else {
        // otherwise we have to pick a specific version
        if (GLVersion.major == 3 && GLVersion.minor == 2) {
            // 3.2core is the earliest core version and it follows the old numbering scheme
            gl_glsl_version = 150;
        } else {
            // 3.3+ follow the new numbering scheme
            gl_glsl_version = GLVersion.major * 100 + GLVersion.minor * 10;
        }
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d core", gl_glsl_version);
    }
    sysLogPrintf(LOG_NOTE, "GL: using GLSL version %s", gl_glsl_version_str);

    glGenBuffers(1, &opengl_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);

    if (gl_core_profile || gl_es) {
        // warn user that funny things can happen
        sysLogPrintf(LOG_WARNING, "GL: using core profile or ES, watch out for errors");
        // core/es will explode if we don't use a VAO for our VBO
        glGenVertexArrays(1, &opengl_vao);
        glBindVertexArray(opengl_vao);
    }

    if (GLAD_GL_ARB_depth_clamp) {
        glEnable(GL_DEPTH_CLAMP);
    }
    gfx_opengl_set_depth_range(0.0f, 1.0f);
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    framebuffers.resize(1); // for the default screen buffer
}

static void gfx_opengl_on_resize(void) {
}

static void gfx_opengl_start_frame(void) {
    frame_count++;
}

static void gfx_opengl_end_frame(void) {
    glFlush();
}

static void gfx_opengl_finish_render(void) {
}

static int gfx_opengl_create_framebuffer() {
    size_t i = framebuffers.size();
    framebuffers.resize(i + 1);

    GLuint clrbuf;
    glGenTextures(1, &clrbuf);
    glBindTexture(GL_TEXTURE_2D, clrbuf);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    framebuffers[i].clrbuf = clrbuf;
    framebuffers[i].clrtex_msaa = 0;
    framebuffers[i].msaa_is_texture = false;

    if (!gfx_framebuffers_enabled) {
        return i;
    }

    GLuint clrbuf_msaa;
    glGenRenderbuffers(1, &clrbuf_msaa);
    framebuffers[i].clrbuf_msaa = clrbuf_msaa;

    GLuint rbo;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 1, 1);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    framebuffers[i].rbo = rbo;

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    framebuffers[i].fbo = fbo;

    return i;
}

static void gfx_opengl_update_framebuffer_parameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                                     bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                                     bool can_extract_depth) {
    Framebuffer& fb = framebuffers[fb_id];
    /* The GPU's sample limit, lowered when a driver rejects a level, so an MSAA
     * pick it cannot do falls back instead of leaving a black screen. */
    static GLint msaa_cap = -1;
    static bool msaa_tex_ok = true; /* cleared when N64 AA's multisample texture is rejected */
    bool realloc = false;

    width = max(width, 1U);
    height = max(height, 1U);

    if (gfx_framebuffers_enabled && fb_id != 0 && msaa_level > 1) {
        if (msaa_cap < 0) {
            GLint s = 0;
            glGetIntegerv(GL_MAX_SAMPLES, &s);
            if (glTexImage2DMultisample != NULL) {
                GLint t = 0;
                glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &t);
                if (t > 0 && t < s) {
                    s = t;
                }
            }
            msaa_cap = max(s, 1);
            sysLogPrintf(LOG_NOTE, "GL: up to %dx MSAA", (int)msaa_cap);
        }
        while (msaa_level > 1 && msaa_level > (uint32_t)msaa_cap) {
            msaa_level >>= 1;
        }
    }

    if (gfx_framebuffers_enabled) {
        glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

        if (fb_id != 0) {
            /* GE N64 AA: the VI resolve reads samples, so the multisampled colour
             * target is a texture while gfx_n64_aa is set (a renderbuffer otherwise) */
            const bool want_tex = msaa_level > 1 && gfx_n64_aa != 0 && glTexImage2DMultisample != NULL && msaa_tex_ok;
            if (fb.width != width || fb.height != height || fb.msaa_level != msaa_level || fb.msaa_is_texture != want_tex) {
                realloc = true;
                if (msaa_level <= 1) {
                    glBindTexture(GL_TEXTURE_2D, fb.clrbuf);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
                    glBindTexture(GL_TEXTURE_2D, 0);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.clrbuf, 0);
                } else if (want_tex) {
                    if (fb.clrtex_msaa == 0) {
                        glGenTextures(1, &fb.clrtex_msaa);
                    }
                    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, fb.clrtex_msaa);
                    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, msaa_level, GL_RGB8, width, height, GL_TRUE);
                    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, fb.clrtex_msaa, 0);
                } else {
                    glBindRenderbuffer(GL_RENDERBUFFER, fb.clrbuf_msaa);
                    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_level, GL_RGB8, width, height);
                    glBindRenderbuffer(GL_RENDERBUFFER, 0);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fb.clrbuf_msaa);
                }
                fb.msaa_is_texture = want_tex;
            }

            if (has_depth_buffer &&
                (fb.width != width || fb.height != height || fb.msaa_level != msaa_level || !fb.has_depth_buffer)) {
                realloc = true;
                glBindRenderbuffer(GL_RENDERBUFFER, fb.rbo);
                if (msaa_level <= 1) {
                    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
                } else {
                    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_level, GL_DEPTH24_STENCIL8, width, height);
                }
                glBindRenderbuffer(GL_RENDERBUFFER, 0);
            }

            if (!fb.has_depth_buffer && has_depth_buffer) {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.rbo);
            } else if (fb.has_depth_buffer && !has_depth_buffer) {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
            }

            if (realloc && msaa_level > 1) {
                const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (status != GL_FRAMEBUFFER_COMPLETE) {
                    /* record what is attached now, so the retry reallocates everything */
                    fb.width = width;
                    fb.height = height;
                    fb.has_depth_buffer = has_depth_buffer;
                    fb.msaa_level = msaa_level;
                    if (fb.msaa_is_texture) {
                        /* N64 AA's multisample texture next to the multisample depth
                         * renderbuffer: keep the level with a renderbuffer instead
                         * (N64 AA then uses the plain resolve) */
                        sysLogPrintf(LOG_WARNING, "GL: %ux MSAA texture framebuffer rejected (0x%x), "
                                     "N64 AA falls back to the plain resolve", msaa_level, (unsigned)status);
                        msaa_tex_ok = false;
                        gfx_opengl_update_framebuffer_parameters(fb_id, width, height, msaa_level, opengl_invert_y,
                                                                 render_target, has_depth_buffer, can_extract_depth);
                        return;
                    }
                    sysLogPrintf(LOG_WARNING, "GL: %ux MSAA framebuffer rejected (0x%x), trying %ux", msaa_level,
                                 (unsigned)status, msaa_level >> 1);
                    msaa_cap = (GLint)(msaa_level >> 1);
                    gfx_opengl_update_framebuffer_parameters(fb_id, width, height, msaa_level >> 1, opengl_invert_y,
                                                             render_target, has_depth_buffer, can_extract_depth);
                    return;
                }
            }
        }
    } else {
        has_depth_buffer = false;
    }

    fb.width = width;
    fb.height = height;
    fb.has_depth_buffer = has_depth_buffer;
    fb.msaa_level = msaa_level;
    fb.invert_y = opengl_invert_y;
}

bool gfx_opengl_start_draw_to_framebuffer(int fb_id, float noise_scale) {
    if (gfx_framebuffers_enabled && fb_id < (int)framebuffers.size()) {
        Framebuffer& fb = framebuffers[fb_id];
        if (noise_scale != 0.0f) {
            current_noise_scale = 1.0f / noise_scale;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
        current_framebuffer = fb_id;
        return true;
    } else {
        return false;
    }
}

void gfx_opengl_clear_framebuffer(bool clear_color, bool clear_depth) {
    glDisable(GL_SCISSOR_TEST);
    
    GLbitfield mask = 0;
    if (clear_color) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (clear_depth) {
        glDepthMask(GL_TRUE);
        mask |= GL_DEPTH_BUFFER_BIT;
        /* the decal clip's stencil marks (gfx_opengl_draw_triangles) */
        glStencilMask(0xFF);
        glClearStencil(0);
        mask |= GL_STENCIL_BUFFER_BIT;
    }
    glClear(mask);
    if (clear_depth) {
        glDepthMask(current_depth_mask ? GL_TRUE : GL_FALSE);
    }

    glEnable(GL_SCISSOR_TEST);
}

/* PILLARBOX (gfx_pc.cpp gfx_start_frame): the window rect a present into
 * framebuffer 0 lands in - x and width; 0 width = the whole window */
int g_GfxPresentX = 0;
int g_GfxPresentW = 0;

static void gfx_opengl_present_x(int fb_id, GLint full_w, GLint* x0, GLint* x1) {
    if (fb_id == 0 && g_GfxPresentW > 0) {
        *x0 = (GLint) g_GfxPresentX;
        *x1 = (GLint) (g_GfxPresentX + g_GfxPresentW);
    } else {
        *x0 = 0;
        *x1 = full_w;
    }
}

/* Blit the bound READ framebuffer's (0,0)-(sw,sh) to the rect (dx0,dy0)-(dx1,dy1)
 * of draw framebuffer `dst_fbo`. A driver's "override" antialiasing makes the
 * window multisampled, and GL refuses a scaled blit into it, which left Classic
 * Mode and Low Res black. Into such a window a scaled blit goes through a plain
 * texture of the rect's size first, so the last step is a same-size copy. */
static void gl_blit_to(GLuint dst_fbo, GLint sw, GLint sh, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLenum filter) {
    static int win_ms = -1;
    static GLuint ws_fbo = 0, ws_tex = 0;
    static GLint ws_w = 0, ws_h = 0;
    const GLint rw = dx1 - dx0, rh = dy1 - dy0;

    if (win_ms < 0) {
        GLint read_fbo = 0, sb = 0, samples = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glGetIntegerv(GL_SAMPLE_BUFFERS, &sb);
        glGetIntegerv(GL_SAMPLES, &samples);
        win_ms = (sb > 0) ? 1 : 0;
        if (win_ms) {
            sysLogPrintf(LOG_WARNING, "GL: the window is multisampled (%d samples, a driver antialiasing override?); "
                         "scaled frames reach it through a plain texture", (int)samples);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read_fbo);
    }
    if (dst_fbo != 0 || !win_ms || (rw == sw && rh == sh) || rw <= 0 || rh <= 0) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo);
        glBlitFramebuffer(0, 0, sw, sh, dx0, dy0, dx1, dy1, GL_COLOR_BUFFER_BIT, filter);
        return;
    }
    if (ws_fbo == 0) {
        glGenFramebuffers(1, &ws_fbo);
        glGenTextures(1, &ws_tex);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ws_fbo);
    if (ws_w != rw || ws_h != rh) {
        GLint had_tex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &had_tex);
        glBindTexture(GL_TEXTURE_2D, ws_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, rw, rh, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, (GLuint)had_tex);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ws_tex, 0);
        ws_w = rw;
        ws_h = rh;
    }
    glBlitFramebuffer(0, 0, sw, sh, 0, 0, rw, rh, GL_COLOR_BUFFER_BIT, filter);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, ws_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, rw, rh, dx0, dy0, dx1, dy1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void gfx_opengl_resolve_msaa_color_buffer(int fb_id_target, int fb_id_source) {
    if (!gfx_framebuffers_enabled) {
        return;
    }

    Framebuffer& fb_dst = framebuffers[fb_id_target];
    Framebuffer& fb_src = framebuffers[fb_id_source];
    GLint dx0, dx1;

    gfx_opengl_present_x(fb_id_target, (GLint) fb_dst.width, &dx0, &dx1);
    glDisable(GL_SCISSOR_TEST);
    if (fb_src.msaa_level > 1 && (fb_id_target == 0 || fb_src.width != fb_dst.width || fb_src.height != fb_dst.height)) {
        /* A multisample blit needs identical rects (and, on strict drivers, formats),
         * so the window (RGBA, maybe pillarboxed) went black on AMD/Intel. Resolve at
         * the source size into a plain RGB8 texture first, then blit that. */
        static GLuint rs_fbo = 0, rs_tex = 0;
        static uint32_t rs_w = 0, rs_h = 0;
        if (rs_fbo == 0) {
            glGenFramebuffers(1, &rs_fbo);
            glGenTextures(1, &rs_tex);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, rs_fbo);
        if (rs_w != fb_src.width || rs_h != fb_src.height) {
            GLint had_tex = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &had_tex);
            glBindTexture(GL_TEXTURE_2D, rs_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, fb_src.width, fb_src.height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
            glBindTexture(GL_TEXTURE_2D, (GLuint)had_tex);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rs_tex, 0);
            rs_w = fb_src.width;
            rs_h = fb_src.height;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_src.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rs_fbo);
        glBlitFramebuffer(0, 0, fb_src.width, fb_src.height, 0, 0, fb_src.width, fb_src.height, GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, rs_fbo);
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_src.fbo);
    }
    gl_blit_to(fb_dst.fbo, fb_src.width, fb_src.height, dx0, 0, dx1, fb_dst.height, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, current_framebuffer);
    glEnable(GL_SCISSOR_TEST);
}

void* gfx_opengl_get_framebuffer_texture_id(int fb_id) {
    return (void*)(uintptr_t)framebuffers[fb_id].clrbuf;
}

void gfx_opengl_select_texture_fb(int fb_id) {
    // glDisable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, framebuffers[fb_id].clrbuf);

    current_textures_linear_filter[0] = true;
}

void gfx_opengl_copy_framebuffer(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back) {
    if (!gfx_framebuffers_enabled || fb_dst >= (int)framebuffers.size() || fb_src >= (int)framebuffers.size()) {
        return;
    }

    const Framebuffer& src = framebuffers[fb_src];
    const Framebuffer& dst = framebuffers[fb_dst];

    int srcX0, srcY0, srcX1, srcY1;
    int dstX0, dstY0, dstX1, dstY1;

    dstX0 = dstY0 = 0;
    dstX1 = dst.width;
    dstY1 = dst.height;

    if (left >= 0 && top >= 0) {
        // unscaled rect copy
        srcX0 = left;
        srcY0 = top;
        srcX1 = left + dst.width;
        srcY1 = top + dst.height;
    } else {
        // scaled full copy
        srcX0 = 0;
        srcY0 = 0;
        srcX1 = src.width;
        srcY1 = src.height;
    }

    glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, src.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo);

    if (flip_y) {
        // flip the dst rect to mirror the image vertically
        std::swap(dstY0, dstY1);
    }

    if (fb_src == 0) {
        // GLES does not support GL_FRONT here
        glReadBuffer((use_back || gl_es) ? GL_BACK : GL_FRONT);
    } else {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[current_framebuffer].fbo);

    glReadBuffer(GL_BACK);

    glEnable(GL_SCISSOR_TEST);
}

void gfx_opengl_set_texture_filter(FilteringMode mode) {
    current_filter_mode = mode;
}

FilteringMode gfx_opengl_get_texture_filter(void) {
    return current_filter_mode;
}


/* ==== GE port: RETAINED (GPU-resident, object-space) geometry ==============
 *
 * The room / model-part cache (gfx_roomcache.inl, [display] room_cache = 2)
 * hands each captured draw to the backend once, as vertices with MODEL-space
 * positions, the matrix slot each belongs to, its normal, and the interpreter's
 * own per-vertex float stream minus the position. Every later frame it issues
 * the draw with that frame's matrix and constants, and the GPU does what
 * gfx_sp_vertex / gfx_sp_tri1 did per vertex. Ported from the NV2A version (the
 * vertex program pdx_passthrough.vs.cg is the reference for the maths below).
 *
 * Twin programs: each combiner program gets, on first use, a second GL
 * program with the SAME fragment shader object and a vertex shader that
 * transforms instead of passing through; its varyings are identical, so the
 * fragment side never knows. Layout of a retained vertex (floats):
 *   0..3  model x y z + matrix slot (0 = the draw's mvp, 1..15 = retained_matrices)
 *   4..7  model normal x y z (n/127) + 0
 *   8..   the interpreter's vertex from float 4 on (texcoords, clamps, fog,
 *         pixfog, grayscale, combiner inputs) - the twin's attribute offsets
 *         are the original program's plus 4.
 * Buffers are packed per draw (each keeps its own stride) and uploaded to a VBO
 * at the first draw; an index-list replay (the deferred XLU sorter)
 * rebases the absolute indices onto the draw's own vertex run.
 *
 * State: the caller (roc_apply_state) has already set program, textures, depth
 * / blend / viewport / scissor through the normal API, so a retained draw only
 * switches program + attribute source, and gl_ret_active tells the next
 * interpreter draw to switch back.
 *
 * Limits: cull winding is derived (see gl_ret_cull_mode) and verified with
 * screenshots, not on every driver; GE_ROCGL_CULL=0/1/2 = off / CW / CCW for
 * comparison. invert_y framebuffers are refused upstream. */
#define GL_RET_MAX_STREAM_FLOATS 64
#define GL_RET_HEADER_FLOATS 8
#define GL_RET_MAX_SLOTS 16

struct GlRetainedBuffer {
    bool used = false;
    GLuint vbo = 0;
    bool uploaded = false;
    uint32_t vertex_count = 0;
    std::vector<float> staging;  /* until the first draw */
};

struct GlRetainedDesc {
    bool used = false;
    GLuint vao = 0;              /* its attribute layout + the index buffer, built at the first draw */
    int buffer = -1;
    size_t byte_offset = 0;      /* into the buffer's VBO */
    uint32_t stride_floats = 0;
    uint32_t first = 0, count = 0; /* the caller's absolute vertex range (index rebasing) */
    struct ShaderProgram* prg = nullptr;
    uint8_t input_kind[8] = {0};
    int8_t shade_slot = -1;
};

struct GlRetainedProgram {
    GLuint program = 0;
    GLint attrib_locations[18];
    uint8_t attrib_sizes[18];
    uint8_t attrib_offsets[18]; /* floats from the vertex start */
    int num_attribs = 0;
    GLint u_mats, u_mats1, u_fixup, u_fogp, u_fogsel, u_fogrgb, u_decal, u_light, u_shade, u_kind, u_prim, u_env, u_lod;
    GLint frame_count_location, noise_scale_location, three_point_filter_locations[2];
    /* uniform value cache: a program object keeps its uniforms between binds,
     * so a retained draw uploads only what changed since this program's last
     * one (the per-draw GL call count is the PC's cost, not the triangles) */
    bool c_valid = false;
    unsigned c_mats_gen = 0;
    float c_fixup[4], c_fogp[4], c_fogsel[4], c_fogrgb[4], c_decal[4], c_lights[20], c_prim_env[12];
    GLint c_shade = -2, c_kinds[8], c_frame_count = -1, c_three[2] = { -1, -1 };
    float c_noise = -1.0f;
};

static std::vector<GlRetainedBuffer> gl_ret_buffers;
static std::vector<GlRetainedDesc> gl_ret_descs;
static std::unordered_map<struct ShaderProgram*, GlRetainedProgram> gl_ret_programs;
static float gl_ret_mats[GL_RET_MAX_SLOTS][16]; /* slots 1..15 = retained_matrices; 0 = the draw's mvp */
static unsigned gl_ret_mats_gen = 1;             /* bumped by retained_matrices: slots 1..15 changed */
static GLuint gl_ret_ebo = 0;
static std::vector<uint16_t> gl_ret_idx_scratch;
static bool gl_ret_vaos = false;             /* GL 3.0+: a VAO per descriptor (else pointers per draw) */
static int gl_ret_cull_state = -1;           /* the GL cull state in effect: -1 unknown, 0 off, else the G_CULL bits */
static GlRetainedProgram* gl_ret_bound = nullptr;
static int gl_ret_cull_mode = -1;            /* GE_ROCGL_CULL: 0 off, 1 front = CW, 2 front = CCW (default: shot-proven) */
static unsigned gl_ret_dbg_draws = 0, gl_ret_dbg_tris = 0;

/* back to the interpreter's state: its VBO, its program's attribute arrays */
static void gl_ret_restore(void) {
    if (!gl_ret_active) {
        return;
    }
    if (gl_ret_cull_state != 0) {
        glDisable(GL_CULL_FACE);
        gl_ret_cull_state = 0;
    }
    if (gl_ret_vaos) {
        /* the interpreter's arrays live in the default VAO (compat) / opengl_vao (core), untouched */
        glBindVertexArray((gl_core_profile || gl_es) ? opengl_vao : 0);
        glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);
        if (gl_current_prg != nullptr) {
            glUseProgram(gl_current_prg->opengl_program_id);
            gfx_opengl_set_uniforms(gl_current_prg);
        }
    } else {
        if (gl_ret_bound != nullptr) {
            for (int i = 0; i < gl_ret_bound->num_attribs; i++) {
                if (gl_ret_bound->attrib_locations[i] >= 0) {
                    glDisableVertexAttribArray(gl_ret_bound->attrib_locations[i]);
                }
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);
        if (gl_current_prg != nullptr) {
            glUseProgram(gl_current_prg->opengl_program_id);
            gfx_opengl_vertex_array_set_attribs(gl_current_prg);
            gfx_opengl_set_uniforms(gl_current_prg);
        }
    }
    gl_ret_active = false;
    gl_ret_bound = nullptr;
}

static GlRetainedProgram* gl_ret_program_get(struct ShaderProgram* prg) {
    auto it = gl_ret_programs.find(prg);
    if (it != gl_ret_programs.end()) {
        return &it->second;
    }
    struct CCFeatures cc = { 0 };
    gfx_cc_get_features(prg->shader_id0, prg->shader_id1, &cc);

    char vs_buf[8192];
    size_t vs_len = 0;
    vs_len += sprintf(vs_buf + vs_len, "#version %s\n", gl_glsl_version_str);
    if (gl_es) {
        append_line(vs_buf, &vs_len, "precision highp float;");
    }
    if (gl_glsl_version >= 130) {
        append_line(vs_buf, &vs_len, "#define INPUT in");
        append_line(vs_buf, &vs_len, "#define OUTPUT out");
    } else {
        append_line(vs_buf, &vs_len, "#define INPUT attribute");
        append_line(vs_buf, &vs_len, "#define OUTPUT varying");
    }
    append_line(vs_buf, &vs_len, "INPUT vec4 aRetPos;");
    append_line(vs_buf, &vs_len, "INPUT vec4 aRetNrm;");
    for (int i = 0; i < 2; i++) {
        if (cc.used_textures[i]) {
            vs_len += sprintf(vs_buf + vs_len, "INPUT vec2 aTexCoord%d;\nOUTPUT vec2 vTexCoord%d;\n", i, i);
            for (int j = 0; j < 2; j++) {
                if (cc.clamp[i][j]) {
                    vs_len += sprintf(vs_buf + vs_len, "INPUT float aTexClamp%s%d;\nOUTPUT float vTexClamp%s%d;\n",
                                      j == 0 ? "S" : "T", i, j == 0 ? "S" : "T", i);
                }
            }
        }
    }
    if (cc.opt_fog) {
        append_line(vs_buf, &vs_len, "INPUT vec4 aFog;");
        append_line(vs_buf, &vs_len, "OUTPUT vec4 vFog;");
    }
    if (cc.opt_ge_farclip || cc.opt_ge_pixfog) {
        append_line(vs_buf, &vs_len, "OUTPUT vec2 vGeZw;");
    }
    if (cc.opt_ge_pixfog) {
        append_line(vs_buf, &vs_len, "INPUT vec2 aGeFogP;");
        append_line(vs_buf, &vs_len, "OUTPUT vec2 vGeFogP;");
    }
    if (cc.opt_grayscale) {
        append_line(vs_buf, &vs_len, "INPUT vec4 aGrayscaleColor;");
        append_line(vs_buf, &vs_len, "OUTPUT vec4 vGrayscaleColor;");
    }
    const char* shade_qual = (cc.opt_nopersp_shade && gl_glsl_version >= 130 && !gl_es) ? "noperspective " : "";
    const int iw = cc.opt_alpha ? 4 : 3;
    for (int i = 0; i < cc.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "INPUT vec%d aInput%d;\n%sOUTPUT vec%d vInput%d;\n", iw, i + 1, shade_qual, iw, i + 1);
    }
    append_line(vs_buf, &vs_len, "uniform vec4 uMats[64];");
    append_line(vs_buf, &vs_len, "uniform vec4 uFixup;");   /* (x scale, x offset, z scale, z offset) in NDC */
    append_line(vs_buf, &vs_len, "uniform vec4 uFogP;");    /* (mul/255, ofs/255, 0.001, flat alpha) */
    append_line(vs_buf, &vs_len, "uniform vec4 uFogSel;");  /* weights: (gpu fog, flat alpha, vertex alpha, 0) */
    append_line(vs_buf, &vs_len, "uniform vec4 uFogRgb;");
    append_line(vs_buf, &vs_len, "uniform vec4 uDecal;");   /* (b*k, k, 4k, 0) */
    append_line(vs_buf, &vs_len, "uniform vec4 uLight[5];"); /* ambient, l0 dir, l0 col, l1 dir, l1 col */
    append_line(vs_buf, &vs_len, "uniform int uShadeSlot;"); /* the combiner input the lighting replaces, -1 = unlit */
    if (cc.num_inputs > 0) {
        vs_len += sprintf(vs_buf + vs_len, "uniform int uInputKind[%d];\n", cc.num_inputs);
    }
    append_line(vs_buf, &vs_len, "uniform vec4 uPrim;");
    append_line(vs_buf, &vs_len, "uniform vec4 uEnv;");
    append_line(vs_buf, &vs_len, "uniform float uLod;");
    append_line(vs_buf, &vs_len, "void main() {");
    /* object space -> clip space through the vertex's own matrix slot (row-vector convention, as gfx_pc) */
    append_line(vs_buf, &vs_len, "    int midx = int(aRetPos.w + 0.5) * 4;");
    append_line(vs_buf, &vs_len, "    vec4 clip = aRetPos.x * uMats[midx] + aRetPos.y * uMats[midx + 1] + aRetPos.z * uMats[midx + 2] + uMats[midx + 3];");
    /* GE decal pull (gfx_sp_tri1, ZMODE_DEC): z += b*k / (w - k) for w > 4k */
    append_line(vs_buf, &vs_len, "    float pullMask = clip.w > uDecal.z ? 1.0 : 0.0;");
    append_line(vs_buf, &vs_len, "    clip.z += (uDecal.x * pullMask) / max(clip.w - uDecal.y, 1.0);");
    /* fog from the un-remapped z/w with gfx_sp_vertex's 1/w clamps */
    append_line(vs_buf, &vs_len, "    float wf = max(clip.w, uFogP.z);");
    append_line(vs_buf, &vs_len, "    float fogW = 1.0 / wf + (clip.w < -uFogP.z ? 31767.0 : 0.0);");
    append_line(vs_buf, &vs_len, "    float fogGpu = clamp(clip.z * fogW * uFogP.x + uFogP.y, 0.0, 1.0);");
    /* the interpreter's per-vertex fixups, in clip space (x' = x*kx + ox*w: the aspect pin; z remap) */
    append_line(vs_buf, &vs_len, "    clip.x = clip.x * uFixup.x + uFixup.y * clip.w;");
    append_line(vs_buf, &vs_len, "    clip.z = clip.z * uFixup.z + uFixup.w * clip.w;");
    /* N64 lighting from the model-space normal (dirs already in model space) */
    append_line(vs_buf, &vs_len, "    vec3 lit = min(uLight[0].xyz + max(0.0, dot(aRetNrm.xyz, uLight[1].xyz)) * uLight[2].xyz + max(0.0, dot(aRetNrm.xyz, uLight[3].xyz)) * uLight[4].xyz, 1.0);");
    for (int i = 0; i < 2; i++) {
        if (cc.used_textures[i]) {
            vs_len += sprintf(vs_buf + vs_len, "    vTexCoord%d = aTexCoord%d;\n", i, i);
            for (int j = 0; j < 2; j++) {
                if (cc.clamp[i][j]) {
                    vs_len += sprintf(vs_buf + vs_len, "    vTexClamp%s%d = aTexClamp%s%d;\n", j == 0 ? "S" : "T", i, j == 0 ? "S" : "T", i);
                }
            }
        }
    }
    if (cc.opt_fog) {
        append_line(vs_buf, &vs_len, "    vFog = vec4(uFogRgb.rgb, fogGpu * uFogSel.x + uFogP.w * uFogSel.y + aFog.a * uFogSel.z);");
    }
    if (cc.opt_grayscale) {
        append_line(vs_buf, &vs_len, "    vGrayscaleColor = aGrayscaleColor;");
    }
    for (int i = 0; i < cc.num_inputs; i++) {
        /* input kinds (gfx_rendering_api retained_build): 1 prim, 2 env, 3 prim
         * alpha as rgb, 4 env alpha as rgb, 5 prim lod fraction rgb, 6 prim lod
         * fraction alpha; the shade slot takes the lighting */
        vs_len += sprintf(vs_buf + vs_len, "    { vec%d v = aInput%d; int k = uInputKind[%d];\n", iw, i + 1, i);
        if (cc.opt_alpha) {
            append_line(vs_buf, &vs_len, "      if (k == 1) v = uPrim; else if (k == 2) v = uEnv;");
            append_line(vs_buf, &vs_len, "      else if (k == 3) v.rgb = vec3(uPrim.a); else if (k == 4) v.rgb = vec3(uEnv.a);");
            append_line(vs_buf, &vs_len, "      else if (k == 5) v.rgb = vec3(uLod); else if (k == 6) v.a = uLod;");
        } else {
            append_line(vs_buf, &vs_len, "      if (k == 1) v = uPrim.rgb; else if (k == 2) v = uEnv.rgb;");
            append_line(vs_buf, &vs_len, "      else if (k == 3) v = vec3(uPrim.a); else if (k == 4) v = vec3(uEnv.a);");
            append_line(vs_buf, &vs_len, "      else if (k == 5) v = vec3(uLod);");
        }
        vs_len += sprintf(vs_buf + vs_len, "      if (uShadeSlot == %d) v.rgb = lit;\n      vInput%d = v; }\n", i, i + 1);
    }
    if (cc.opt_ge_farclip || cc.opt_ge_pixfog) {
        append_line(vs_buf, &vs_len, "    vGeZw = clip.zw;");
    }
    if (cc.opt_ge_pixfog) {
        append_line(vs_buf, &vs_len, "    vGeFogP = uFogP.xy * 255.0;"); /* the frame's registers, not the captured pair */
    }
    append_line(vs_buf, &vs_len, "    gl_Position = clip;");
    if (!GLAD_GL_ARB_depth_clamp) {
        append_line(vs_buf, &vs_len, "    gl_Position.z *= 0.3f;");
    }
    append_line(vs_buf, &vs_len, "}");
    vs_buf[vs_len] = '\0';

    const GLchar* src = vs_buf;
    const GLint len = (GLint)vs_len;
    GLint success;
    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &src, &len);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 1024;
        char error_log[1024];
        glGetShaderInfoLog(vertex_shader, max_length, &max_length, &error_log[0]);
        sysLogPrintf(LOG_ERROR, "Failed to compile this retained vertex shader (ID %llx, %x):\n%s\n%s", prg->shader_id0, prg->shader_id1, vs_buf, error_log);
        glDeleteShader(vertex_shader);
        GlRetainedProgram& bad = gl_ret_programs[prg];
        bad.program = 0; /* remembered: retained draws of this program are skipped */
        return &bad;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, prg->fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    glDetachShader(program, vertex_shader);
    glDetachShader(program, prg->fragment_shader);
    glDeleteShader(vertex_shader);
    GlRetainedProgram& rp = gl_ret_programs[prg];
    if (!success) {
        GLint max_length = 1024;
        char error_log[1024];
        glGetProgramInfoLog(program, max_length, &max_length, &error_log[0]);
        sysLogPrintf(LOG_ERROR, "Failed to link the retained program (ID %llx, %x): %s", prg->shader_id0, prg->shader_id1, error_log);
        glDeleteProgram(program);
        rp.program = 0;
        return &rp;
    }
    rp.program = program;
    int cnt = 0;
    uint8_t off = 0;
    rp.attrib_locations[cnt] = glGetAttribLocation(program, "aRetPos"); rp.attrib_sizes[cnt] = 4; rp.attrib_offsets[cnt] = off; off += 4; cnt++;
    rp.attrib_locations[cnt] = glGetAttribLocation(program, "aRetNrm"); rp.attrib_sizes[cnt] = 4; rp.attrib_offsets[cnt] = off; off += 4; cnt++;
    /* the original program's attributes after its position, at +4 floats */
    {
        uint8_t pos = 4;
        for (int i = 1; i < prg->num_attribs; i++) {
            rp.attrib_locations[cnt] = -1;
            rp.attrib_sizes[cnt] = prg->attrib_sizes[i];
            rp.attrib_offsets[cnt] = (uint8_t)(pos + 4);
            pos += prg->attrib_sizes[i];
            cnt++;
        }
        cnt = 2;
        char name[32];
        for (int i = 0; i < 2; i++) {
            if (cc.used_textures[i]) {
                sprintf(name, "aTexCoord%d", i);
                rp.attrib_locations[cnt++] = glGetAttribLocation(program, name);
                for (int j = 0; j < 2; j++) {
                    if (cc.clamp[i][j]) {
                        sprintf(name, "aTexClamp%s%d", j == 0 ? "S" : "T", i);
                        rp.attrib_locations[cnt++] = glGetAttribLocation(program, name);
                    }
                }
            }
        }
        if (cc.opt_fog) { rp.attrib_locations[cnt++] = glGetAttribLocation(program, "aFog"); }
        if (cc.opt_ge_pixfog) { rp.attrib_locations[cnt++] = glGetAttribLocation(program, "aGeFogP"); }
        if (cc.opt_grayscale) { rp.attrib_locations[cnt++] = glGetAttribLocation(program, "aGrayscaleColor"); }
        for (int i = 0; i < cc.num_inputs; i++) {
            sprintf(name, "aInput%d", i + 1);
            rp.attrib_locations[cnt++] = glGetAttribLocation(program, name);
        }
    }
    rp.num_attribs = cnt;
    glUseProgram(program);
    if (cc.used_textures[0]) { glUniform1i(glGetUniformLocation(program, "uTex0"), 0); }
    if (cc.used_textures[1]) { glUniform1i(glGetUniformLocation(program, "uTex1"), 1); }
    rp.u_mats = glGetUniformLocation(program, "uMats");
    rp.u_mats1 = glGetUniformLocation(program, "uMats[4]"); /* slots 1..15 as one upload (-1: all 64 each draw) */
    rp.u_fixup = glGetUniformLocation(program, "uFixup");
    rp.u_fogp = glGetUniformLocation(program, "uFogP");
    rp.u_fogsel = glGetUniformLocation(program, "uFogSel");
    rp.u_fogrgb = glGetUniformLocation(program, "uFogRgb");
    rp.u_decal = glGetUniformLocation(program, "uDecal");
    rp.u_light = glGetUniformLocation(program, "uLight");
    rp.u_shade = glGetUniformLocation(program, "uShadeSlot");
    rp.u_kind = glGetUniformLocation(program, "uInputKind");
    rp.u_prim = glGetUniformLocation(program, "uPrim");
    rp.u_env = glGetUniformLocation(program, "uEnv");
    rp.u_lod = glGetUniformLocation(program, "uLod");
    rp.frame_count_location = glGetUniformLocation(program, "frame_count");
    rp.noise_scale_location = glGetUniformLocation(program, "noise_scale");
    rp.three_point_filter_locations[0] = glGetUniformLocation(program, "three_point_filter0");
    rp.three_point_filter_locations[1] = glGetUniformLocation(program, "three_point_filter1");
    /* back to the interpreter's program: the caller expects it current */
    if (gl_current_prg != nullptr) {
        glUseProgram(gl_current_prg->opengl_program_id);
    }
    return &rp;
}

static int gfx_opengl_retained_create(uint32_t vertex_count) {
    if (vertex_count == 0) {
        return -1;
    }
    int id = -1;
    for (size_t i = 0; i < gl_ret_buffers.size(); i++) {
        if (!gl_ret_buffers[i].used) { id = (int)i; break; }
    }
    if (id < 0) {
        gl_ret_buffers.emplace_back();
        id = (int)gl_ret_buffers.size() - 1;
    }
    GlRetainedBuffer& b = gl_ret_buffers[id];
    b = GlRetainedBuffer();
    b.used = true;
    b.vertex_count = vertex_count;
    b.staging.reserve((size_t)vertex_count * 24);
    return id;
}

static void gfx_opengl_retained_destroy(int buffer) {
    if (buffer < 0 || (size_t)buffer >= gl_ret_buffers.size() || !gl_ret_buffers[buffer].used) {
        return;
    }
    if (gl_ret_active) {
        gl_ret_restore(); /* the deleted VBO / VAO may be the bound one */
    }
    for (size_t d = 0; d < gl_ret_descs.size(); d++) {
        if (gl_ret_descs[d].used && gl_ret_descs[d].buffer == buffer) {
            if (gl_ret_descs[d].vao != 0) {
                glDeleteVertexArrays(1, &gl_ret_descs[d].vao);
            }
            gl_ret_descs[d] = GlRetainedDesc();
        }
    }
    GlRetainedBuffer& b = gl_ret_buffers[buffer];
    if (b.vbo != 0) {
        glDeleteBuffers(1, &b.vbo);
    }
    b = GlRetainedBuffer();
}

static int gfx_opengl_retained_build(int buffer, uint32_t first_vertex, struct ShaderProgram* prg, const float* buf_vbo,
                                     size_t stride, size_t num_tris, const float* model_pos, const float* model_nrm,
                                     const uint32_t tex_id[2], const bool tex_used[2], const uint8_t input_kind[8],
                                     int shade_slot) {
    (void)tex_id; (void)tex_used; /* GL samples normalised texcoords: the stream's are final */
    if (buffer < 0 || (size_t)buffer >= gl_ret_buffers.size() || !gl_ret_buffers[buffer].used || prg == nullptr) {
        return -1;
    }
    GlRetainedBuffer& b = gl_ret_buffers[buffer];
    if (b.uploaded) {
        return -1; /* a buffer is built in one go, before its first draw */
    }
    {
        /* the twin program is built here, not at the first draw: a shader that
         * fails to compile refuses the build and the list stays on the
         * interpreter instead of vanishing */
        GlRetainedProgram* rp = gl_ret_program_get(prg);
        if (rp == nullptr || rp->program == 0) {
            return -1;
        }
    }
    const size_t nverts = num_tris * 3;
    if (first_vertex + nverts > b.vertex_count || stride != prg->num_floats || stride < 4 ||
        stride + 4 > GL_RET_MAX_STREAM_FLOATS) {
        return -1;
    }
    int id = -1;
    for (size_t i = 0; i < gl_ret_descs.size(); i++) {
        if (!gl_ret_descs[i].used) { id = (int)i; break; }
    }
    if (id < 0) {
        gl_ret_descs.emplace_back();
        id = (int)gl_ret_descs.size() - 1;
    }
    GlRetainedDesc& d = gl_ret_descs[id];
    d = GlRetainedDesc();
    d.used = true;
    d.buffer = buffer;
    d.byte_offset = b.staging.size() * sizeof(float);
    d.stride_floats = (uint32_t)(stride + 4);
    d.first = first_vertex;
    d.count = (uint32_t)nverts;
    d.prg = prg;
    memcpy(d.input_kind, input_kind, sizeof(d.input_kind));
    d.shade_slot = (int8_t)shade_slot;
    b.staging.reserve(b.staging.size() + nverts * d.stride_floats);
    for (size_t v = 0; v < nverts; v++) {
        const float* src = buf_vbo + v * stride;
        int slot = (int)model_pos[v * 4 + 3];
        if (slot < 0) slot = 0; else if (slot > GL_RET_MAX_SLOTS - 1) slot = GL_RET_MAX_SLOTS - 1;
        b.staging.push_back(model_pos[v * 4 + 0]);
        b.staging.push_back(model_pos[v * 4 + 1]);
        b.staging.push_back(model_pos[v * 4 + 2]);
        b.staging.push_back((float)slot);
        if (model_nrm != nullptr) {
            b.staging.push_back(model_nrm[v * 3 + 0]);
            b.staging.push_back(model_nrm[v * 3 + 1]);
            b.staging.push_back(model_nrm[v * 3 + 2]);
        } else {
            b.staging.push_back(0.0f); b.staging.push_back(0.0f); b.staging.push_back(0.0f);
        }
        b.staging.push_back(0.0f);
        b.staging.insert(b.staging.end(), src + 4, src + stride);
    }
    return id;
}

static void gfx_opengl_retained_matrices(int first_slot, int count, const float* mats) {
    if (mats == nullptr) {
        return;
    }
    if (first_slot < 1) { mats += (1 - first_slot) * 16; count -= (1 - first_slot); first_slot = 1; }
    if (first_slot + count > GL_RET_MAX_SLOTS) count = GL_RET_MAX_SLOTS - first_slot;
    for (int i = 0; i < count; i++) {
        memcpy(gl_ret_mats[first_slot + i], mats + i * 16, 16 * sizeof(float));
    }
    gl_ret_mats_gen++;
}

static void gl_ret_draw(int desc, const uint16_t* idx, uint32_t nidx, const float mvp[16], const float fixup[4],
                        const float fogp[4], const float fogsel[4], const float fog_rgb[3], uint32_t cull_mode,
                        const float* lights, const float* prim_env, const float* decal) {
    if (desc < 0 || (size_t)desc >= gl_ret_descs.size() || !gl_ret_descs[desc].used) {
        return;
    }
    GlRetainedDesc& d = gl_ret_descs[desc];
    if (gl_current_prg != d.prg) {
        return; /* the caller loads the shader first (roc_apply_state) */
    }
    GlRetainedBuffer& b = gl_ret_buffers[d.buffer];
    GlRetainedProgram* rp = gl_ret_program_get(d.prg);
    if (rp == nullptr || rp->program == 0) {
        return;
    }
    if (gl_ret_cull_mode < 0) {
        const char* e = getenv("GE_ROCGL_CULL");
        gl_ret_cull_mode = e ? atoi(e) : 2; /* front = COUNTER-clockwise: with CW every
                                             * room wall was culled */
        const char* v = getenv("GE_ROCGL_VAO");
        gl_ret_vaos = (glGenVertexArrays != NULL && glBindVertexArray != NULL) && !(v && v[0] == '0');
    }
    if (!b.uploaded) {
        glGenBuffers(1, &b.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
        glBufferData(GL_ARRAY_BUFFER, b.staging.size() * sizeof(float), b.staging.data(), GL_STATIC_DRAW);
        std::vector<float>().swap(b.staging);
        b.uploaded = true;
        gl_ret_active = true; /* the array buffer binding moved */
    }
    if (gl_ret_ebo == 0) {
        glGenBuffers(1, &gl_ret_ebo);
    }
    /* program + attribute source */
    if (gl_ret_bound != rp) {
        if (!gl_ret_vaos) {
            if (gl_ret_bound != nullptr) {
                for (int i = 0; i < gl_ret_bound->num_attribs; i++) {
                    if (gl_ret_bound->attrib_locations[i] >= 0) {
                        glDisableVertexAttribArray(gl_ret_bound->attrib_locations[i]);
                    }
                }
            } else if (gl_current_prg != nullptr) {
                for (int i = 0; i < gl_current_prg->num_attribs; i++) {
                    if (gl_current_prg->attrib_locations[i] >= 0) {
                        glDisableVertexAttribArray(gl_current_prg->attrib_locations[i]);
                    }
                }
            }
        }
        glUseProgram(rp->program);
        gl_ret_bound = rp;
    }
    gl_ret_active = true;
    if (gl_ret_vaos) {
        if (d.vao == 0) {
            glGenVertexArrays(1, &d.vao);
            glBindVertexArray(d.vao);
            glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
            for (int i = 0; i < rp->num_attribs; i++) {
                if (rp->attrib_locations[i] >= 0) {
                    glEnableVertexAttribArray(rp->attrib_locations[i]);
                    glVertexAttribPointer(rp->attrib_locations[i], rp->attrib_sizes[i], GL_FLOAT, GL_FALSE,
                                          d.stride_floats * sizeof(float),
                                          (void*)(d.byte_offset + rp->attrib_offsets[i] * sizeof(float)));
                }
            }
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_ret_ebo); /* VAO state: the sorter's index list */
        } else {
            glBindVertexArray(d.vao);
        }
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
        for (int i = 0; i < rp->num_attribs; i++) {
            if (rp->attrib_locations[i] >= 0) {
                glEnableVertexAttribArray(rp->attrib_locations[i]);
                glVertexAttribPointer(rp->attrib_locations[i], rp->attrib_sizes[i], GL_FLOAT, GL_FALSE,
                                      d.stride_floats * sizeof(float),
                                      (void*)(d.byte_offset + rp->attrib_offsets[i] * sizeof(float)));
            }
        }
    }
    /* the frame's constants: the draw's matrix always (slot 0), the rest on change */
    memcpy(gl_ret_mats[0], mvp, 16 * sizeof(float));
    if (rp->u_mats1 >= 0) {
        glUniform4fv(rp->u_mats, 4, &gl_ret_mats[0][0]);
        if (!rp->c_valid || rp->c_mats_gen != gl_ret_mats_gen) {
            glUniform4fv(rp->u_mats1, 4 * (GL_RET_MAX_SLOTS - 1), &gl_ret_mats[1][0]);
            rp->c_mats_gen = gl_ret_mats_gen;
        }
    } else {
        glUniform4fv(rp->u_mats, 4 * GL_RET_MAX_SLOTS, &gl_ret_mats[0][0]);
    }
    const bool cv = rp->c_valid;
    if (!cv || memcmp(rp->c_fixup, fixup, sizeof(rp->c_fixup)) != 0) {
        memcpy(rp->c_fixup, fixup, sizeof(rp->c_fixup));
        glUniform4fv(rp->u_fixup, 1, fixup);
    }
    if (!cv || memcmp(rp->c_fogp, fogp, sizeof(rp->c_fogp)) != 0) {
        memcpy(rp->c_fogp, fogp, sizeof(rp->c_fogp));
        glUniform4fv(rp->u_fogp, 1, fogp);
    }
    if (!cv || memcmp(rp->c_fogsel, fogsel, sizeof(rp->c_fogsel)) != 0) {
        memcpy(rp->c_fogsel, fogsel, sizeof(rp->c_fogsel));
        glUniform4fv(rp->u_fogsel, 1, fogsel);
    }
    {
        const float rgb[4] = { fog_rgb[0], fog_rgb[1], fog_rgb[2], 1.0f };
        if (!cv || memcmp(rp->c_fogrgb, rgb, sizeof(rgb)) != 0) {
            memcpy(rp->c_fogrgb, rgb, sizeof(rgb));
            glUniform4fv(rp->u_fogrgb, 1, rgb);
        }
    }
    {
        static const float no_decal[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        const float* dec = decal ? decal : no_decal;
        if (!cv || memcmp(rp->c_decal, dec, sizeof(rp->c_decal)) != 0) {
            memcpy(rp->c_decal, dec, sizeof(rp->c_decal));
            glUniform4fv(rp->u_decal, 1, dec);
        }
    }
    {
        GLint shade = -1;
        if (lights != nullptr) {
            if (!cv || memcmp(rp->c_lights, lights, sizeof(rp->c_lights)) != 0) {
                memcpy(rp->c_lights, lights, sizeof(rp->c_lights));
                glUniform4fv(rp->u_light, 5, lights);
            }
            shade = d.shade_slot;
        }
        if (!cv || rp->c_shade != shade) {
            rp->c_shade = shade;
            glUniform1i(rp->u_shade, shade);
        }
    }
    {
        GLint kinds[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        if (prim_env != nullptr) {
            for (int j = 0; j < 8; j++) kinds[j] = d.input_kind[j];
            if (!cv || memcmp(rp->c_prim_env, prim_env, sizeof(rp->c_prim_env)) != 0) {
                memcpy(rp->c_prim_env, prim_env, sizeof(rp->c_prim_env));
                glUniform4fv(rp->u_prim, 1, prim_env);
                glUniform4fv(rp->u_env, 1, prim_env + 4);
                glUniform1f(rp->u_lod, prim_env[8]);
            }
        }
        if (rp->u_kind >= 0 && d.prg->num_inputs > 0 && (!cv || memcmp(rp->c_kinds, kinds, sizeof(kinds)) != 0)) {
            memcpy(rp->c_kinds, kinds, sizeof(kinds));
            glUniform1iv(rp->u_kind, d.prg->num_inputs, kinds);
        }
    }
    if (rp->frame_count_location >= 0 && rp->c_frame_count != (GLint)frame_count) {
        rp->c_frame_count = (GLint)frame_count;
        glUniform1i(rp->frame_count_location, frame_count);
    }
    if (rp->noise_scale_location >= 0 && rp->c_noise != current_noise_scale) {
        rp->c_noise = current_noise_scale;
        glUniform1f(rp->noise_scale_location, current_noise_scale);
    }
    for (int t = 0; t < 2; t++) {
        if (rp->three_point_filter_locations[t] >= 0 && rp->c_three[t] != (GLint)current_textures_linear_filter[t]) {
            rp->c_three[t] = (GLint)current_textures_linear_filter[t];
            glUniform1i(rp->three_point_filter_locations[t], current_textures_linear_filter[t]);
        }
    }
    rp->c_valid = true;
    /* Backface cull on the GPU: front = COUNTER-clockwise in GL's y-up window
     * space, verified against a cull-off reference. The clockwise reading of
     * gfx_sp_tri1's cross-product test was wrong. */
    {
        /* Use the gbi's own bits: this port compiles GE's F3DEX v1 gbi
         * (G_CULL_BACK 0x2000), not F3DEX2's 0x400 that the NV2A code
         * hard-wired, which meant nothing was ever culled. */
        const int want = (gl_ret_cull_mode != 0) ? (int)(cull_mode & (uint32_t)G_CULL_BOTH) : 0;
        if (want != gl_ret_cull_state) {
            if (want != 0) {
                glEnable(GL_CULL_FACE);
                glFrontFace(gl_ret_cull_mode == 2 ? GL_CCW : GL_CW);
                if (want == G_CULL_BOTH) glCullFace(GL_FRONT_AND_BACK);
                else if (want & G_CULL_FRONT) glCullFace(GL_FRONT);
                else glCullFace(GL_BACK);
            } else {
                glDisable(GL_CULL_FACE);
            }
            gl_ret_cull_state = want;
        }
    }
    if (idx != nullptr) {
        gl_ret_idx_scratch.resize(nidx);
        for (uint32_t i = 0; i < nidx; i++) {
            const int32_t rel = (int32_t)idx[i] - (int32_t)d.first;
            gl_ret_idx_scratch[i] = (uint16_t)(rel < 0 ? 0 : (rel >= (int32_t)d.count ? d.count - 1 : rel));
        }
        if (!gl_ret_vaos) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_ret_ebo);
        }
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, nidx * sizeof(uint16_t), gl_ret_idx_scratch.data(), GL_STREAM_DRAW);
        glDrawElements(GL_TRIANGLES, (GLsizei)nidx, GL_UNSIGNED_SHORT, (void*)0);
        gl_ret_dbg_tris += nidx / 3u;
    } else {
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)d.count);
        gl_ret_dbg_tris += d.count / 3u;
    }
    gl_ret_dbg_draws++;
}

static void gfx_opengl_retained_draw(int desc, const float mvp[16], const float fixup[4], const float fogp[4],
                                     const float fogsel[4], const float fog_rgb[3], uint32_t cull_mode,
                                     const float* lights, const float* prim_env, const float* decal) {
    gl_ret_draw(desc, nullptr, 0, mvp, fixup, fogp, fogsel, fog_rgb, cull_mode, lights, prim_env, decal);
}

static void gfx_opengl_retained_draw_indexed(int desc, const uint16_t* idx, uint32_t n, const float mvp[16],
                                             const float fixup[4], const float fogp[4], const float fogsel[4],
                                             const float fog_rgb[3], uint32_t cull_mode, const float* lights,
                                             const float* prim_env, const float* decal) {
    if (idx == nullptr || n < 3) {
        return;
    }
    gl_ret_draw(desc, idx, n - (n % 3u), mvp, fixup, fogp, fogsel, fog_rgb, cull_mode, lights, prim_env, decal);
}

/* GE_GFX_STATS-style readout for the port's own stats line (gfx_pc.cpp) */
extern "C" void gfx_opengl_retained_stats(unsigned* draws, unsigned* tris, unsigned* buffers, unsigned* descs) {
    unsigned nb = 0, nd = 0;
    for (const GlRetainedBuffer& b : gl_ret_buffers) nb += b.used ? 1u : 0u;
    for (const GlRetainedDesc& d : gl_ret_descs) nd += d.used ? 1u : 0u;
    if (draws) *draws = gl_ret_dbg_draws;
    if (tris) *tris = gl_ret_dbg_tris;
    if (buffers) *buffers = nb;
    if (descs) *descs = nd;
    gl_ret_dbg_draws = gl_ret_dbg_tris = 0;
}

static struct ShaderProgram* gl_current_prg_set(struct ShaderProgram* prg) {
    gl_current_prg = prg;
    return prg;
}

static void gl_ret_clear_programs(void) {
    gl_ret_restore();
    for (auto& pair : gl_ret_programs) {
        if (pair.second.program != 0) {
            glDeleteProgram(pair.second.program);
        }
    }
    gl_ret_programs.clear();
    gl_current_prg = nullptr;
    /* the descriptors point at programs that are gone: the room cache clears
     * with the texture cache (gfx_texture_cache_clear -> portRoomCacheClear),
     * which is what empties the shader pool's users; drop ours too */
    for (size_t i = 0; i < gl_ret_buffers.size(); i++) {
        if (gl_ret_buffers[i].used) {
            gfx_opengl_retained_destroy((int)i);
        }
    }
    gl_ret_descs.clear();
}


/* ==== GE N64 AA: a Video-Interface-style resolve ===========================
 *
 * The N64 anti-aliased in two places: the RDP blended silhouette pixels by
 * their subpixel COVERAGE (our MSAA stands in for that), then the VI
 * post-filtered the frame on scan-out, blending each partially covered pixel
 * with its 3x3 neighbourhood by the coverage it lacked and resampling the
 * 320x240 image softly to the TV. This approximates that second half on the
 * multisampled level framebuffer:
 *   pass 1 (src size): per pixel, the mean of the samples (the RDP blend) and
 *          a coverage = the size of the largest cluster of equal samples /
 *          the sample count (MSAA shades a triangle once per pixel, so
 *          samples differ only where two triangles meet - exactly an edge);
 *   pass 2 (src size): VI blend = mix(pixel, 3x3 mean, 1 - coverage);
 *   present: a LINEAR blit to the destination (the VI's soft resample).
 * Alpha-cutout edges (fences, grates) stay hard, as they did on the console.
 * The level framebuffer must be a multisample TEXTURE for pass 1 to read
 * samples: gfx_opengl_update_framebuffer_parameters allocates one while
 * gfx_n64_aa is set (needs ARB_texture_multisample; without it the plain
 * resolve runs and a warning names it). */
extern "C" uint32_t gfx_n64_aa; /* gfx_pc.cpp, set by video.c */
extern "C" uint32_t gfx_line_mode; /* gfx_pc.cpp: the Line Mode cheat is on this frame */
extern "C" uint32_t gfx_composite; /* video.c: [display] composite */
static GLuint vi_prog[4] = { 0, 0, 0, 0 };
static GLint vi_u_src[4] = { -1, -1, -1, -1 }, vi_u_samples = -1, vi_u_size = -1, vi_u_strength = -1, vi_u_gain = -1;
static GLint vi_u_csize = -1, vi_u_cscale = -1, vi_u_cfreq = -1, vi_u_cframe = -1;   /* the composite pass */
static GLuint vi_tex[3] = { 0, 0, 0 }, vi_fbo[3] = { 0, 0, 0 };
static uint32_t vi_w = 0, vi_h = 0;
static GLuint vi_vbo = 0, vi_vao = 0;
static bool vi_failed = false;

static GLuint vi_compile(const char* vs, const char* fs) {
    GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
    GLint ok;
    glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
    glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(v, 1024, NULL, log); sysLogPrintf(LOG_ERROR, "N64 AA vertex shader: %s", log); return 0; }
    glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
    glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(f, 1024, NULL, log); sysLogPrintf(LOG_ERROR, "N64 AA fragment shader: %s\n%s", log, fs); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    if (gl_glsl_version < 130) { glBindAttribLocation(p, 0, "aPos"); }
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDetachShader(p, v); glDetachShader(p, f); glDeleteShader(v); glDeleteShader(f);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, 1024, NULL, log); sysLogPrintf(LOG_ERROR, "N64 AA link: %s", log); glDeleteProgram(p); return 0; }
    return p;
}

static bool vi_init(void) {
    if (vi_prog[0] != 0) return true;
    if (vi_failed) return false;
    if (glTexImage2DMultisample == NULL || glGenVertexArrays == NULL) {
        sysLogPrintf(LOG_WARNING, "N64 AA: multisample textures / VAOs unavailable on this GL - plain MSAA resolve instead");
        vi_failed = true;
        return false;
    }
    char hdr[256];
    snprintf(hdr, sizeof(hdr), "#version %s\n%s#extension GL_ARB_texture_multisample : enable\n",
             gl_glsl_version_str, gl_es ? "precision mediump float;\n" : "");
    std::string vs = std::string(hdr) +
        "in vec2 aPos;\n"
        "void main() { gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    std::string fs1 = std::string(hdr) +
        "uniform sampler2DMS uSrc;\n"
        "uniform int uSamples;\n"
        "out vec4 outColor;\n"
        "void main() {\n"
        "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
        "    vec3 c0 = texelFetch(uSrc, p, 0).rgb;\n"
        "    vec3 sum = c0;\n"
        "    int same = 1;\n"
        "    for (int i = 1; i < uSamples; i++) {\n"
        "        vec3 c = texelFetch(uSrc, p, i).rgb;\n"
        "        sum += c;\n"
        "        if (all(lessThan(abs(c - c0), vec3(1.5 / 255.0)))) same++;\n"
        "    }\n"
        "    float cov = float(max(same, uSamples - same)) / float(uSamples);\n"
        "    outColor = vec4(sum / float(uSamples), cov);\n"
        "}\n";
    std::string fs2 = std::string(hdr) +
        "uniform sampler2D uTex;\n"
        "uniform ivec2 uSize;\n"
        "uniform float uStrength;\n"
        "out vec4 outColor;\n"
        "void main() {\n"
        "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
        "    vec4 c = texelFetch(uTex, p, 0);\n"
        "    vec3 acc = vec3(0.0);\n"
        "    for (int dy = -1; dy <= 1; dy++) {\n"
        "        for (int dx = -1; dx <= 1; dx++) {\n"
        "            ivec2 q = clamp(p + ivec2(dx, dy), ivec2(0), uSize - ivec2(1));\n"
        "            acc += texelFetch(uTex, q, 0).rgb;\n"
        "        }\n"
        "    }\n"
        "    float w = clamp((1.0 - c.a) * uStrength, 0.0, 1.0);\n"
        "    outColor = vec4(mix(c.rgb, acc / 9.0, w), 1.0);\n"
        "}\n";
    /* LINE MODE (the cheat): replaces pass 2 with Rare's G_RM_VISCVG blend,
     * white times the pixel's coverage, so covered pixels are white and edges
     * dark. Pass 1's coverage (0.5..1) is stretched back to 0..1, scaled by
     * GE_LINEMODE_GAIN. */
    std::string fs3 = std::string(hdr) +
        "uniform sampler2D uTex;\n"
        "uniform float uGain;\n"
        "out vec4 outColor;\n"
        "void main() {\n"
        "    float cov = texelFetch(uTex, ivec2(gl_FragCoord.xy), 0).a;\n"
        "    float v = clamp(1.0 - (1.0 - cov) * 2.0 * uGain, 0.0, 1.0);\n"
        "    outColor = vec4(v, v, v, 1.0);\n"
        "}\n";
    /* COMPOSITE: encodes the image as an NTSC composite signal at the N64's
 * pixel scale (uScale = N64 pixels per source pixel) and decodes it again.
 * The colour carrier's phase flips every line and frame, as on real NTSC,
 * which gives the shimmering dot crawl. */
    std::string fs4 = std::string(hdr) +
        "uniform sampler2D uTex;\n"
        "uniform vec2 uSizeF;\n"
        "uniform float uScale;\n"
        "uniform float uFreq;\n"
        "uniform float uFrame;\n"
        "out vec4 outColor;\n"
        "const float PI = 3.14159265;\n"
        "vec3 toYiq(vec3 c) { return vec3(dot(c, vec3(0.299, 0.587, 0.114)), dot(c, vec3(0.596, -0.274, -0.322)), dot(c, vec3(0.211, -0.523, 0.312))); }\n"
        "vec3 toRgb(vec3 y) { return vec3(y.x + 0.956 * y.y + 0.621 * y.z, y.x - 0.272 * y.y - 0.647 * y.z, y.x - 1.106 * y.y + 1.703 * y.z); }\n"
        "void main() {\n"
        "    vec2 p = gl_FragCoord.xy;\n"
        "    float line = floor(p.y * uScale);\n"
        "    float x0 = p.x * uScale;\n"
        "    float half_period = 0.5 / uFreq;\n"
        "    float ys = 0.0, yw = 0.0, is = 0.0, iw = 0.0, qs = 0.0, qw = 0.0;\n"
        "    for (int k = -16; k <= 16; k++) {\n"
        "        float dx = float(k) * 0.25;\n"
        "        float xs = x0 + dx;\n"
        "        vec3 yiq = toYiq(texture(uTex, vec2(xs / uScale, p.y) / uSizeF).rgb);\n"
        "        float ph = 2.0 * PI * uFreq * xs + PI * (line + uFrame);\n"
        "        float s = yiq.x + yiq.y * cos(ph) + yiq.z * sin(ph);\n"
        "        float wl = (abs(dx) <= half_period) ? 1.0 : 0.0;\n"
        "        float wi = exp(-dx * dx / 2.0);\n"
        "        float wq = exp(-dx * dx / 4.5);\n"
        "        ys += s * wl; yw += wl;\n"
        "        is += s * cos(ph) * wi; iw += wi;\n"
        "        qs += s * sin(ph) * wq; qw += wq;\n"
        "    }\n"
        /* chroma x 0.95 (2.0 -> 1.9): saturation down 5% */
        "    vec3 o = vec3(ys / yw, 1.9 * is / iw, 1.9 * qs / qw);\n"
        /* x 0.95: brightness down 5% */
        "    outColor = vec4(clamp(toRgb(o) * 0.95, 0.0, 1.0), 1.0);\n"
        "}\n";
    vi_prog[0] = vi_compile(vs.c_str(), fs1.c_str());
    vi_prog[1] = vi_prog[0] ? vi_compile(vs.c_str(), fs2.c_str()) : 0;
    vi_prog[2] = vi_prog[1] ? vi_compile(vs.c_str(), fs3.c_str()) : 0;
    vi_prog[3] = vi_prog[2] ? vi_compile(vs.c_str(), fs4.c_str()) : 0;   /* composite: optional - its failure costs only itself */
    if (vi_prog[0] == 0 || vi_prog[1] == 0 || vi_prog[2] == 0) {
        if (vi_prog[0]) glDeleteProgram(vi_prog[0]);
        if (vi_prog[1]) glDeleteProgram(vi_prog[1]);
        vi_prog[0] = vi_prog[1] = vi_prog[2] = 0;
        vi_failed = true;
        return false;
    }
    vi_u_src[0] = glGetUniformLocation(vi_prog[0], "uSrc");
    vi_u_samples = glGetUniformLocation(vi_prog[0], "uSamples");
    vi_u_src[1] = glGetUniformLocation(vi_prog[1], "uTex");
    vi_u_size = glGetUniformLocation(vi_prog[1], "uSize");
    vi_u_strength = glGetUniformLocation(vi_prog[1], "uStrength");
    vi_u_src[2] = glGetUniformLocation(vi_prog[2], "uTex");
    vi_u_gain = glGetUniformLocation(vi_prog[2], "uGain");
    if (vi_prog[3] != 0) {
        vi_u_src[3] = glGetUniformLocation(vi_prog[3], "uTex");
        vi_u_csize = glGetUniformLocation(vi_prog[3], "uSizeF");
        vi_u_cscale = glGetUniformLocation(vi_prog[3], "uScale");
        vi_u_cfreq = glGetUniformLocation(vi_prog[3], "uFreq");
        vi_u_cframe = glGetUniformLocation(vi_prog[3], "uFrame");
    }
    static const float tri[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
    glGenBuffers(1, &vi_vbo);
    glGenVertexArrays(1, &vi_vao);
    glBindVertexArray(vi_vao);
    glBindBuffer(GL_ARRAY_BUFFER, vi_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
    GLint loc = glGetAttribLocation(vi_prog[0], "aPos");
    if (loc < 0) loc = 0;
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glBindVertexArray((gl_core_profile || gl_es) ? opengl_vao : 0);
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);
    glGenTextures(3, vi_tex);
    glGenFramebuffers(3, vi_fbo);
    sysLogPrintf(LOG_NOTE, "N64 AA: VI resolve ready");
    return true;
}

static void vi_size(uint32_t w, uint32_t h) {
    if (vi_w == w && vi_h == h) return;
    for (int i = 0; i < 3; i++) {
        glBindTexture(GL_TEXTURE_2D, vi_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, vi_fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vi_tex[i], 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    vi_w = w; vi_h = h;
}

static float vi_strength(void) {
    static float s = -1.0f;
    if (s < 0.0f) {
        const char* e = getenv("GE_N64AA_STRENGTH"); /* 1.0 = the VI's full blend by missing coverage */
        s = e ? (float)atof(e) : 1.0f;
        if (s < 0.0f) s = 0.0f;
    }
    return s;
}

static void gfx_opengl_resolve_n64_aa(int fb_dst, int fb_src) {
    if (!gfx_framebuffers_enabled || fb_src <= 0 || fb_src >= (int)framebuffers.size() ||
        fb_dst < 0 || fb_dst >= (int)framebuffers.size()) {
        return;
    }
    Framebuffer& src = framebuffers[fb_src];
    Framebuffer& dst = framebuffers[fb_dst];
    if (src.msaa_level <= 1 || !src.msaa_is_texture || !vi_init()) { /* attached, not merely allocated: the renderbuffer arm keeps the old texture */
        if (src.msaa_level > 1 && (src.width != dst.width || src.height != dst.height)) {
            /* A multisample read framebuffer cannot blit to a rect of a
             * different size (GL_INVALID_OPERATION; the picture went black on
             * GL < 3.2). Resolve at the source size first, then scale, as
             * gfx_pc's plain MSAA branch does. */
            static GLuint fb_fbo = 0, fb_tex = 0;
            static uint32_t fb_w = 0, fb_h = 0;
            if (fb_fbo == 0) {
                glGenFramebuffers(1, &fb_fbo);
                glGenTextures(1, &fb_tex);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, fb_fbo);
            if (fb_w != src.width || fb_h != src.height) {
                GLint had_tex = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &had_tex);
                glBindTexture(GL_TEXTURE_2D, fb_tex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, src.width, src.height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
                glBindTexture(GL_TEXTURE_2D, (GLuint)had_tex);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb_tex, 0);
                fb_w = src.width;
                fb_h = src.height;
            }
            glDisable(GL_SCISSOR_TEST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, src.fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_fbo);
            glBlitFramebuffer(0, 0, (GLint)src.width, (GLint)src.height, 0, 0, (GLint)src.width, (GLint)src.height,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_fbo);
            {
                GLint dx0, dx1;

                gfx_opengl_present_x(fb_dst, (GLint) dst.width, &dx0, &dx1);
                gl_blit_to(dst.fbo, (GLint)src.width, (GLint)src.height, dx0, 0, dx1, (GLint)dst.height, GL_NEAREST);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, current_framebuffer < (int)framebuffers.size() ? framebuffers[current_framebuffer].fbo : 0);
            glEnable(GL_SCISSOR_TEST);
            return;
        }
        gfx_opengl_resolve_msaa_color_buffer(fb_dst, fb_src); /* the plain resolve */
        return;
    }
    gl_ret_restore();
    /* the state the passes disturb */
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    const GLboolean had_depth = glIsEnabled(GL_DEPTH_TEST), had_blend = glIsEnabled(GL_BLEND),
                    had_scissor = glIsEnabled(GL_SCISSOR_TEST), had_cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    vi_size(src.width, src.height);
    glViewport(0, 0, (GLsizei)src.width, (GLsizei)src.height);
    glBindVertexArray(vi_vao);
    glActiveTexture(GL_TEXTURE0);
    /* pass 1: samples -> mean colour + coverage */
    glBindFramebuffer(GL_FRAMEBUFFER, vi_fbo[0]);
    glUseProgram(vi_prog[0]);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, src.clrtex_msaa);
    glUniform1i(vi_u_src[0], 0);
    glUniform1i(vi_u_samples, (GLint)src.msaa_level);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    /* pass 2: the VI's edge blend - or Line Mode's coverage picture */
    glBindFramebuffer(GL_FRAMEBUFFER, vi_fbo[1]);
    glBindTexture(GL_TEXTURE_2D, vi_tex[0]);
    if (gfx_line_mode) {
        static float gain = -1.0f;
        if (gain < 0.0f) {
            const char* e = getenv("GE_LINEMODE_GAIN");
            gain = e ? (float)atof(e) : 1.0f;
            if (gain < 0.0f) gain = 0.0f;
        }
        glUseProgram(vi_prog[2]);
        glUniform1i(vi_u_src[2], 0);
        glUniform1f(vi_u_gain, gain);
    } else {
        glUseProgram(vi_prog[1]);
        glUniform1i(vi_u_src[1], 0);
        glUniform2i(vi_u_size, (GLint)src.width, (GLint)src.height);
        glUniform1f(vi_u_strength, vi_strength());
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);
    /* pass 3 (optional): the NTSC composite signal (fs4 in vi_init) */
    GLuint present_fbo = vi_fbo[1];
    static int nocomp = -1;
    if (nocomp < 0) { const char* e = getenv("GE_NOCOMPOSITE"); nocomp = (e != NULL && e[0] != '0') ? 1 : 0; }
    if (gfx_composite && !nocomp && !gfx_line_mode && vi_prog[3] != 0) {   /* part of Classic Mode (video.c) */
        static float freq = -1.0f;
        static unsigned frame;
        if (freq < 0.0f) {
            const char* e = getenv("GE_COMPOSITE_FREQ");   /* subcarrier cycles per N64 pixel */
            freq = (e != NULL && atof(e) > 0.0) ? (float) atof(e) : 0.5866f;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, vi_fbo[2]);
        glBindTexture(GL_TEXTURE_2D, vi_tex[1]);
        glUseProgram(vi_prog[3]);
        glUniform1i(vi_u_src[3], 0);
        glUniform2f(vi_u_csize, (GLfloat) src.width, (GLfloat) src.height);
        {   /* the console's own pixels: 320x240 in a level, 440x330 in the
             * folder menus, square, with the subcarrier's cycles per pixel
             * following the VI's width (0.5866 is per pixel of a 320-wide line) */
            const GLfloat nh = (SCREEN_HEIGHT > 0) ? (GLfloat) SCREEN_HEIGHT : 240.0f;
            const GLfloat nw = (SCREEN_WIDTH > 0) ? (GLfloat) SCREEN_WIDTH : 320.0f;

            glUniform1f(vi_u_cscale, nh / (GLfloat) src.height);   /* N64 pixels per source pixel */
            glUniform1f(vi_u_cfreq, freq * 320.0f / nw);
        }
        glUniform1f(vi_u_cframe, (GLfloat) (frame++ & 1u));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        present_fbo = vi_fbo[2];
    }
    /* present: the soft resample to the destination */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, present_fbo);
    {
        /* GE_N64AA_SOFT=0 keeps the nearest present (chunky pixels, blended
         * edges); default = the VI's soft resample (bilinear) */
        static int soft = -1;
        GLint dx0, dx1;

        if (soft < 0) { const char* e = getenv("GE_N64AA_SOFT"); soft = (e && e[0] == '0') ? 0 : 1; }
        gfx_opengl_present_x(fb_dst, (GLint) dst.width, &dx0, &dx1);   /* PILLARBOX: the centred rect */
        gl_blit_to(dst.fbo, (GLint)src.width, (GLint)src.height, dx0, 0, dx1, (GLint)dst.height,
                   soft ? GL_LINEAR : GL_NEAREST);
    }
    /* back to the interpreter's world */
    glBindFramebuffer(GL_FRAMEBUFFER, current_framebuffer < (int)framebuffers.size() ? framebuffers[current_framebuffer].fbo : 0);
    glBindVertexArray((gl_core_profile || gl_es) ? opengl_vao : 0);
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);
    glBindTexture(GL_TEXTURE_2D, current_texture_ids[0]);
    glActiveTexture(GL_TEXTURE0 + last_selected_tile);
    if (gl_current_prg != nullptr) {
        glUseProgram(gl_current_prg->opengl_program_id);
        if (!gl_ret_vaos) {
            gfx_opengl_vertex_array_set_attribs(gl_current_prg);
        }
        gfx_opengl_set_uniforms(gl_current_prg);
    }
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    if (had_depth) glEnable(GL_DEPTH_TEST);
    if (had_blend) glEnable(GL_BLEND);
    if (had_scissor) glEnable(GL_SCISSOR_TEST);
    if (had_cull) glEnable(GL_CULL_FACE);
    glDepthMask(current_depth_mask ? GL_TRUE : GL_FALSE);
}

struct GfxRenderingAPI gfx_opengl_api = { 
    gfx_opengl_get_name,
    gfx_opengl_get_max_texture_size,
    gfx_opengl_get_clip_parameters,
    gfx_opengl_unload_shader,
    gfx_opengl_load_shader,
    gfx_opengl_create_and_load_new_shader,
    gfx_opengl_lookup_shader,
    gfx_opengl_shader_get_info,
    gfx_opengl_clear_shaders,
    gfx_opengl_new_texture,
    gfx_opengl_select_texture,
    gfx_opengl_upload_texture,
    gfx_opengl_set_sampler_parameters,
    gfx_opengl_set_depth_mode,
    gfx_opengl_set_depth_range,
    gfx_opengl_set_viewport,
    gfx_opengl_set_scissor,
    gfx_opengl_set_use_alpha,
    gfx_opengl_draw_triangles,
    gfx_opengl_init,
    gfx_opengl_on_resize,
    gfx_opengl_start_frame,
    gfx_opengl_end_frame,
    gfx_opengl_finish_render,
    gfx_opengl_create_framebuffer,
    gfx_opengl_update_framebuffer_parameters,
    gfx_opengl_start_draw_to_framebuffer,
    gfx_opengl_copy_framebuffer,
    gfx_opengl_clear_framebuffer,
    gfx_opengl_resolve_msaa_color_buffer,
    gfx_opengl_get_framebuffer_texture_id,
    gfx_opengl_select_texture_fb,
    gfx_opengl_delete_texture,
    gfx_opengl_set_texture_filter,
    gfx_opengl_get_texture_filter,
    gfx_opengl_retained_create,
    gfx_opengl_retained_destroy,
    gfx_opengl_retained_build,
    gfx_opengl_retained_draw,
    gfx_opengl_retained_matrices,
    gfx_opengl_retained_draw_indexed,
    gfx_opengl_resolve_n64_aa
};
