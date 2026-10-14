#include <algorithm>
#include <cstring>
#include <string>

#include "context.hpp"

namespace gfx::detail {

namespace {

// GLSL ES 1.00 ports of gfx/shaders/*.{vert,frag}. ES 1.00 has no integer bit operations, no
// texelFetch, no textureGather, no gl_ClipDistance and no dual-source blending, so:
//  - the push constant flags are separate float uniforms,
//  - palette and filtered lookups sample single texels at their centres,
//  - clip planes become a varying the fragment shader discards on,
//  - the blend factor comes from the fragment alpha (see MapBlend).

const char *const kPrecision = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
)";

const char *const kVertex2D = R"(
attribute vec3 a_position;
attribute vec2 a_uv;
attribute vec4 a_color;
attribute float a_fog;

uniform vec4 u_xform;
uniform vec4 u_uv_xform;

varying vec4 v_color;
varying vec2 v_uv;
varying float v_fog;
varying vec2 v_clip;

void main() {
    // Depth is the game's 0 far .. 1 near; GL clip z maps [-1, 1] onto the same [0, 1] window range.
    gl_Position = vec4(a_position.xy * u_xform.xy + u_xform.zw, a_position.z * 2.0 - 1.0, 1.0);
    v_color = a_color;
    v_uv = a_uv * u_uv_xform.xy + u_uv_xform.zw;
    v_fog = a_fog;
    v_clip = vec2(1.0);
}
)";

const char *const kVertexMesh = R"(
attribute vec3 a_position;
attribute vec3 a_normal;
attribute vec2 a_uv;
attribute vec4 a_color;

uniform vec4 u_xform;
uniform vec4 u_uv_xform;

uniform mat4 u_mvp;
uniform mat3 u_normal_matrix;
uniform vec4 u_light_direction[4];
uniform vec4 u_light_color[4];
uniform vec4 u_ambient;
uniform vec4 u_diffuse;
uniform vec4 u_ambient_material;
uniform vec4 u_fog_params;
uniform vec4 u_clip_plane[2];
uniform vec4 u_world_uv[2];
uniform float u_flag_lit;
uniform float u_flag_vertex_color;
uniform float u_flag_fog;
uniform float u_flag_shadow;
uniform float u_flag_clip0;
uniform float u_flag_clip1;
uniform float u_flag_screen_uv;
uniform float u_flag_world_uv;
uniform float u_light_count;

varying vec4 v_color;
varying vec2 v_uv;
varying float v_fog;
varying vec2 v_clip;

const float kModulate = 255.0 / 128.0;

void main() {
    vec4 position = vec4(a_position, 1.0);
    vec4 clip = u_mvp * position;
    // The GS rasterises vertices at 1/16-pixel precision; quantise in logical-pixel units so cells
    // that share an edge through different transforms land on one location.
    vec2 step = u_xform.xy / 16.0;
    vec2 ndc = clip.xy / clip.w;
    ndc = floor(ndc / step + 0.5) * step;
    clip.xy = ndc * clip.w;
    // Vulkan clip space has z in [0, w]; GL's is [-w, w].
    gl_Position = vec4(clip.xy, clip.z * 2.0 - clip.w, clip.w);

    v_clip = vec2(u_flag_clip0 > 0.5 ? dot(u_clip_plane[0], position) : 1.0,
                  u_flag_clip1 > 0.5 ? dot(u_clip_plane[1], position) : 1.0);

    vec3 color = u_diffuse.rgb;
    float alpha = u_diffuse.a;
    if (u_flag_shadow < 0.5) {
        if (u_flag_lit > 0.5) {
            vec3 normal = normalize(u_normal_matrix * a_normal);
            // Unrolled: the PS4's shader compiler rejects uniform arrays indexed by a loop counter.
            vec3 light = vec3(0.0);
            if (u_light_count > 0.5) {
                light += max(dot(normal, -u_light_direction[0].xyz), 0.0) * u_light_color[0].rgb;
            }
            if (u_light_count > 1.5) {
                light += max(dot(normal, -u_light_direction[1].xyz), 0.0) * u_light_color[1].rgb;
            }
            if (u_light_count > 2.5) {
                light += max(dot(normal, -u_light_direction[2].xyz), 0.0) * u_light_color[2].rgb;
            }
            if (u_light_count > 3.5) {
                light += max(dot(normal, -u_light_direction[3].xyz), 0.0) * u_light_color[3].rgb;
            }
            color = u_ambient.rgb * u_ambient_material.rgb + light * u_diffuse.rgb;
        }
        if (u_flag_vertex_color > 0.5) {
            color *= a_color.rgb * kModulate;
            alpha *= a_color.a * kModulate;
        }
    }

    v_color = clamp(vec4(color, alpha) / kModulate, 0.0, 1.0);
    if (u_flag_world_uv > 0.5) {
        v_uv = vec2(dot(u_world_uv[0], position), dot(u_world_uv[1], position));
    } else if (u_flag_screen_uv > 0.5) {
        v_uv = clip.xy / clip.w * 0.5 + 0.5 + a_uv;
    } else {
        v_uv = a_uv;
    }
    v_fog = u_flag_fog > 0.5 ? clamp(u_fog_params.x + u_fog_params.y / clip.w, u_fog_params.z, u_fog_params.w) / 255.0
                             : 1.0;
}
)";

const char *const kFragment = R"(
uniform sampler2D u_tex;
uniform sampler2D u_pal;
uniform vec2 u_tex_size;
uniform float u_fog;
uniform float u_texa;
uniform float u_aem;
uniform float u_opaque_target;
uniform float u_transform;
uniform float u_factor_fix;
uniform float u_linear;
uniform float u_alpha_ref;
uniform float u_alpha_func;
uniform float u_fix;
uniform float u_ta0;
uniform vec3 u_fog_color;

varying vec4 v_color;
varying vec2 v_uv;
varying float v_fog;
varying vec2 v_clip;

const float kModulate = 255.0 / 128.0;

vec2 Footprint() {
    return fract(v_uv * u_tex_size - 0.5);
}

// The shared corner of the 2x2 footprint; the four texel centres lie half a texel off it.
vec2 FootprintCorner() {
    return (floor(v_uv * u_tex_size - 0.5) + 1.0) / u_tex_size;
}

vec4 PaletteEntry(float index) {
    return texture2D(u_pal, vec2((index * 255.0 + 0.5) / 256.0, 0.5));
}

#if TEXTURE_MODE == 2
// The GS looks the CLUT up before filtering, so a filtered palette texture blends four entries.
vec4 SamplePalette() {
    if (u_linear < 0.5) {
        return PaletteEntry(texture2D(u_tex, v_uv).r);
    }
    vec2 corner = FootprintCorner();
    vec2 texel = 0.5 / u_tex_size;
    vec4 t00 = PaletteEntry(texture2D(u_tex, corner + vec2(-texel.x, -texel.y)).r);
    vec4 t10 = PaletteEntry(texture2D(u_tex, corner + vec2(texel.x, -texel.y)).r);
    vec4 t01 = PaletteEntry(texture2D(u_tex, corner + vec2(-texel.x, texel.y)).r);
    vec4 t11 = PaletteEntry(texture2D(u_tex, corner + vec2(texel.x, texel.y)).r);
    vec2 f = Footprint();
    return mix(mix(t00, t10, f.x), mix(t01, t11, f.x), f.y);
}
#endif

#if TEXTURE_MODE == 1
float TexelOpaque(vec2 at) {
    vec3 c = texture2D(u_tex, at).rgb;
    return c.r + c.g + c.b > 0.0 ? 1.0 : 0.0;
}

// TEXA expands alpha per texel before filtering too: with AEM a filtered edge between black and
// coloured texels fades out instead of taking TA0 throughout.
vec4 SampleRgba() {
    vec4 t = texture2D(u_tex, v_uv);
    if (u_texa > 0.5) {
        if (u_aem > 0.5 && u_linear > 0.5) {
            vec2 corner = FootprintCorner();
            vec2 texel = 0.5 / u_tex_size;
            float a00 = TexelOpaque(corner + vec2(-texel.x, -texel.y));
            float a10 = TexelOpaque(corner + vec2(texel.x, -texel.y));
            float a01 = TexelOpaque(corner + vec2(-texel.x, texel.y));
            float a11 = TexelOpaque(corner + vec2(texel.x, texel.y));
            vec2 f = Footprint();
            t.a = mix(mix(a00, a10, f.x), mix(a01, a11, f.x), f.y) * u_ta0;
        } else {
            t.a = (u_aem > 0.5 && t.r + t.g + t.b == 0.0) ? 0.0 : u_ta0;
        }
    }
    return t;
}
#endif

#ifdef ALPHA_TEST
bool AlphaPasses(float alpha) {
    float value = floor(alpha * 128.0 + 0.5);
    float ref = u_alpha_ref;
    float f = u_alpha_func;
    if (f < 0.5) return false;
    if (f < 1.5) return true;
    if (f < 2.5) return value < ref;
    if (f < 3.5) return value <= ref;
    if (f < 4.5) return value == ref;
    if (f < 5.5) return value >= ref;
    if (f < 6.5) return value > ref;
    return value != ref;
}
#endif

void main() {
    if (v_clip.x < 0.0 || v_clip.y < 0.0) {
        discard;
    }
    // Alpha is carried in GS units over 128 (1.0 = 0x80) so the alpha test sees values above 0x80
    // before the attachment saturates them.
#if TEXTURE_MODE == 0
    vec4 color = vec4(v_color.rgb, v_color.a * kModulate);
#elif TEXTURE_MODE == 1
    vec4 t = SampleRgba();
    vec4 color = vec4(t.rgb * v_color.rgb * kModulate, t.a * v_color.a * kModulate);
#else
    vec4 t = SamplePalette();
    vec4 color = vec4(t.rgb * v_color.rgb * kModulate, t.a * v_color.a * kModulate);
#endif

    if (u_fog > 0.5) {
        color.rgb = mix(u_fog_color, color.rgb, v_fog);
    }

#ifdef ALPHA_TEST
    if (!AlphaPasses(color.a)) {
        discard;
    }
#endif

    float factor = u_factor_fix > 0.5 ? u_fix : color.a;
    vec3 rgb = u_transform > 1.5 ? vec3(factor) : (u_transform > 0.5 ? color.rgb * (1.0 + factor) : color.rgb);
    gl_FragColor = vec4(rgb, color.a);
}
)";

const char *const kBlitVertex = R"(
attribute vec2 a_position;
uniform vec4 u_rect; // x0, y0, x1, y1 in NDC
uniform vec4 u_uv;   // u0, v0, u1, v1
varying vec2 v_uv;
void main() {
    vec2 t = a_position * 0.5 + 0.5;
    gl_Position = vec4(mix(u_rect.xy, u_rect.zw, t), 0.0, 1.0);
    v_uv = mix(u_uv.xy, u_uv.zw, t);
}
)";

const char *const kBlitFragment = R"(
uniform sampler2D u_tex;
varying vec2 v_uv;
void main() {
    gl_FragColor = texture2D(u_tex, v_uv);
}
)";

GLuint CompileShader(GLenum type, const std::string &source, const char *name) {
    GLuint      shader = gl.CreateShader(type);
    const char *text = source.c_str();
    gl.ShaderSource(shader, 1, &text, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        gl.GetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        Fatal("shader %s failed to compile:\n%s", name, log);
    }
    return shader;
}

GLuint LinkProgram(const std::string &vertex, const std::string &fragment, const char *name,
                   std::initializer_list<const char *> attributes) {
    GLuint v = CompileShader(GL_VERTEX_SHADER, vertex, name);
    GLuint f = CompileShader(GL_FRAGMENT_SHADER, fragment, name);
    GLuint program = gl.CreateProgram();
    gl.AttachShader(program, v);
    gl.AttachShader(program, f);
    GLuint index = 0;
    for (const char *attribute : attributes) {
        gl.BindAttribLocation(program, index++, attribute);
    }
    gl.LinkProgram(program);
    GLint ok = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        gl.GetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        Fatal("program %s failed to link:\n%s", name, log);
    }
    gl.DeleteShader(v);
    gl.DeleteShader(f);
    return program;
}

// ---- Blend equations: Solve() is gfx/pipelines.cpp's, with GL's factors. ----------------------
//
// Vulkan reads the GS factor C from a second fragment output (SRC1_ALPHA). ES 2 has none, so a
// factor from source alpha reads the fragment's own alpha (equal wherever alpha is at most 1.0,
// which is every draw that does not rely on the GS over-0x80 range), and FIX uses the constant blend
// colour, set per draw.

enum Input : uint8_t { kInputSource = 0, kInputDest = 1, kInputZero = 2 };

constexpr BlendEquation kPassThrough = {GL_ONE, GL_ZERO, GL_FUNC_ADD, false};

BlendEquation Solve(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t &transform) {
    bool dest = c == 1;
    bool fix = c == 2;
    // The factor: destination alpha, the constant alpha for FIX, else the source (fragment) alpha.
    GLenum f = dest ? GL_DST_ALPHA : fix ? GL_CONSTANT_ALPHA : GL_SRC_ALPHA;
    GLenum f1 = dest ? GL_ONE_MINUS_DST_ALPHA : fix ? GL_ONE_MINUS_CONSTANT_ALPHA : GL_ONE_MINUS_SRC_ALPHA;
    bool   constant = fix;

    constexpr GLenum kOne = GL_ONE;
    constexpr GLenum kNone = GL_ZERO;
    constexpr GLenum kAdd = GL_FUNC_ADD;
    constexpr GLenum kSub = GL_FUNC_SUBTRACT;
    constexpr GLenum kRev = GL_FUNC_REVERSE_SUBTRACT;

    transform = kSourceColor;
    auto make = [&](GLenum src, GLenum dst, GLenum op) {
        bool uses_constant = constant && (src == f || src == f1 || dst == f || dst == f1);
        return BlendEquation{src, dst, op, uses_constant};
    };

    if (a == b) {
        switch (d) {
            case kInputSource:
                return kPassThrough;
            case kInputDest:
                return make(kNone, kOne, kAdd);
            default:
                return make(kNone, kNone, kAdd);
        }
    }
    if (a == kInputSource && b == kInputDest) {
        switch (d) {
            case kInputDest:
                return make(f, f1, kAdd);
            case kInputZero:
                return make(f, f, kSub);
            default:
                // Cs * (1 + C) - Cd * C: the shader scales the source.
                transform = dest ? kSourceColor : kSourceTimesOnePlusC;
                return make(kOne, f, kSub);
        }
    }
    if (a == kInputDest && b == kInputSource) {
        switch (d) {
            case kInputSource:
                return make(f1, f, kAdd);
            case kInputZero:
                return make(f, f, kRev);
            default:
                return make(f, kOne, kRev);
        }
    }
    if (a == kInputSource) { // B is zero
        switch (d) {
            case kInputDest:
                return make(f, kOne, kAdd);
            case kInputZero:
                return make(f, kNone, kAdd);
            default:
                transform = dest ? kSourceColor : kSourceTimesOnePlusC;
                return make(kOne, kNone, kAdd);
        }
    }
    if (a == kInputDest) { // B is zero
        switch (d) {
            case kInputSource:
                return make(kOne, f, kAdd);
            case kInputZero:
                return make(kNone, f, kAdd);
            default:
                // Cd * (1 + C): the shader writes C as the source colour and the blend adds C * Cd.
                if (dest) {
                    return make(kNone, kOne, kAdd);
                }
                transform = kSourceFactor;
                return BlendEquation{GL_DST_COLOR, kOne, kAdd, false};
        }
    }
    if (b == kInputSource) { // A is zero
        switch (d) {
            case kInputDest:
                return make(f, kOne, kRev);
            case kInputSource:
                return make(f1, kNone, kAdd);
            default:
                return make(kNone, kNone, kAdd);
        }
    }
    switch (d) { // A is zero, B is the destination
        case kInputSource:
            return make(kOne, f, kSub);
        case kInputDest:
            return make(kNone, f1, kAdd);
        default:
            return make(kNone, kNone, kAdd);
    }
}

bool SameEquation(const BlendEquation &x, const BlendEquation &y) {
    return x.src == y.src && x.dst == y.dst && x.op == y.op && x.constant_alpha == y.constant_alpha;
}

uint32_t ProgramKey(PipelineFamily family, TextureMode mode, bool alpha_test) {
    return (static_cast<uint32_t>(family) * kTextureModeCount + static_cast<uint32_t>(mode)) * 2 + (alpha_test ? 1 : 0);
}

void Locate(Program &p, bool mesh) {
    auto at = [&](const char *name) { return gl.GetUniformLocation(p.id, name); };
    p.xform = at("u_xform");
    p.uv_xform = at("u_uv_xform");
    p.tex = at("u_tex");
    p.pal = at("u_pal");
    p.tex_size = at("u_tex_size");
    p.fog = at("u_fog");
    p.texa = at("u_texa");
    p.aem = at("u_aem");
    p.opaque_target = at("u_opaque_target");
    p.transform = at("u_transform");
    p.factor_fix = at("u_factor_fix");
    p.linear = at("u_linear");
    p.alpha_ref = at("u_alpha_ref");
    p.alpha_func = at("u_alpha_func");
    p.fix = at("u_fix");
    p.ta0 = at("u_ta0");
    p.fog_color = at("u_fog_color");
    if (!mesh) {
        return;
    }
    p.mvp = at("u_mvp");
    p.normal_matrix = at("u_normal_matrix");
    p.light_direction = at("u_light_direction[0]");
    p.light_color = at("u_light_color[0]");
    p.ambient = at("u_ambient");
    p.diffuse = at("u_diffuse");
    p.ambient_material = at("u_ambient_material");
    p.fog_params = at("u_fog_params");
    p.clip_plane = at("u_clip_plane[0]");
    p.world_uv = at("u_world_uv[0]");
    p.flag_lit = at("u_flag_lit");
    p.flag_vertex_color = at("u_flag_vertex_color");
    p.flag_fog = at("u_flag_fog");
    p.flag_shadow = at("u_flag_shadow");
    p.flag_clip0 = at("u_flag_clip0");
    p.flag_clip1 = at("u_flag_clip1");
    p.flag_screen_uv = at("u_flag_screen_uv");
    p.flag_world_uv = at("u_flag_world_uv");
    p.light_count = at("u_light_count");
}

} // namespace

void BuildBlendTable() {
    g.equations.clear();
    g.equations.push_back(kPassThrough);
    for (uint8_t a = 0; a < 3; a++) {
        for (uint8_t b = 0; b < 3; b++) {
            for (uint8_t c = 0; c < 3; c++) {
                for (uint8_t d = 0; d < 3; d++) {
                    uint8_t       transform = kSourceColor;
                    BlendEquation equation = Solve(a, b, c, d, transform);
                    auto          it = std::find_if(g.equations.begin(), g.equations.end(),
                                                    [&](const BlendEquation &e) { return SameEquation(e, equation); });
                    uint16_t      slot;
                    if (it == g.equations.end()) {
                        g.equations.push_back(equation);
                        slot = static_cast<uint16_t>(g.equations.size() - 1);
                    } else {
                        slot = static_cast<uint16_t>(it - g.equations.begin());
                    }
                    g.blend_map[a * 27 + b * 9 + c * 3 + d] = BlendMapping{slot, transform, c == 2};
                }
            }
        }
    }
}

BlendMapping MapBlend(const GsBlend &blend, bool enable) {
    if (!enable) {
        return BlendMapping{0, kSourceColor, false};
    }
    // Field value 3 is reserved for A, B and D (taken as zero) and for C (taken as FIX).
    uint32_t a = std::min<uint32_t>(blend.a, 2);
    uint32_t b = std::min<uint32_t>(blend.b, 2);
    uint32_t c = std::min<uint32_t>(blend.c, 2);
    uint32_t d = std::min<uint32_t>(blend.d, 2);
    return g.blend_map[a * 27 + b * 9 + c * 3 + d];
}

const Program *GetProgram(PipelineFamily family, TextureMode mode, bool alpha_test) {
    // Lines run the same program as 2D triangles.
    if (family == kFamily2DLines) {
        family = kFamily2DTriangles;
    }
    uint32_t key = ProgramKey(family, mode, alpha_test);
    auto     it = g.programs.find(key);
    if (it != g.programs.end()) {
        return &it->second;
    }
    bool        mesh = family == kFamilyMesh;
    std::string defines = "#define TEXTURE_MODE " + std::to_string(static_cast<int>(mode)) + "\n";
    if (alpha_test) {
        defines += "#define ALPHA_TEST\n";
    }
    // Vertex shaders keep ES's default highp: kPrecision's fragment fallback would lower it.
    std::string vertex = mesh ? kVertexMesh : kVertex2D;
    std::string fragment = defines + kPrecision + kFragment;
    Program     program;
    program.id = mesh ? LinkProgram(vertex, fragment, "mesh", {"a_position", "a_normal", "a_uv", "a_color"})
                      : LinkProgram(vertex, fragment, "draw2d", {"a_position", "a_uv", "a_color", "a_fog"});
    Locate(program, mesh);
    // Samplers default to unit 0: the palette lives on unit 1.
    gl.UseProgram(program.id);
    g.bound.program = program.id;
    gl.Uniform1i(program.tex, 0);
    gl.Uniform1i(program.pal, 1);
    return &g.programs.emplace(key, program).first->second;
}

bool InitBlit() {
    g.blit_program = LinkProgram(kBlitVertex, std::string(kPrecision) + kBlitFragment, "blit", {"a_position"});
    g.blit_rect = gl.GetUniformLocation(g.blit_program, "u_rect");
    g.blit_uv = gl.GetUniformLocation(g.blit_program, "u_uv");
    g.blit_tex = gl.GetUniformLocation(g.blit_program, "u_tex");
    return true;
}

void ShutdownPrograms() {
    for (auto &[key, program] : g.programs) {
        gl.DeleteProgram(program.id);
    }
    g.programs.clear();
    if (g.blit_program != 0) {
        gl.DeleteProgram(g.blit_program);
        g.blit_program = 0;
    }
}

} // namespace gfx::detail
