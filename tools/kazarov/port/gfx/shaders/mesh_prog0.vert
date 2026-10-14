#version 460

// Dark Chronicle's Vu_prog0, for DrawMeshProg0 draws. Spec: docs/vu1/prog0.md and progmain.md in the
// game's port; the O-n notes below are that spec's open items, kept as retail does them.

layout(constant_id = 0) const bool kSkinned = false;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;
layout(location = 4) in vec4 a_skin_weight;
layout(location = 5) in uvec4 a_skin_bone;

layout(push_constant, std430) uniform Push {
    vec4 xform;
    vec4 uv_xform;
    uint texture_slot;
    uint palette_slot;
    uint sampler_slot;
    uint flags;
    uint alpha;
    uint fog_color;
}
pc;

// gfx::MeshConstants, identical to mesh.vert's block.
layout(set = 1, binding = 0, std140) uniform MeshConstants {
    mat4 mvp;
    mat3 normal_matrix;
    vec4 light_direction[4];
    vec4 light_color[4];
    vec4 ambient;
    vec4 diffuse;
    vec4 ambient_material;
    vec4 specular;
    vec4 fog;
    vec4 clip_plane[2];
    vec4 world_uv[2];
    uint flags;
    uint light_count;
}
mc;

// gfx::Prog0Constants in the VU's units (128 is 1.0), the draw's own block at set 2.
layout(set = 2, binding = 0, std140) uniform Prog0Block {
    mat4 model_world;
    vec4 light_direction[3];
    vec4 light_color[4];
    vec4 ambient;
    vec4 object_color;
    vec4 eye;
    mat4 clip_or_view;
    vec4 diffuse;
    vec4 specular;
    vec4 point_light[4];
    vec4 point_color[4];
    vec4 fog;
    uint flags;
    uint face;
    uint specular_power;
    uint pad;
    vec4 bones[96];
}
p0;

const uint kScissor      = 0x002u;
const uint kReal         = 0x008u;
const uint kPointLights  = 0x010u;
const uint kUnlit        = 0x020u;
const uint kBlend        = 0x040u;
const uint kColorAdd     = 0x080u;
const uint kRefSphere    = 0x100u;

const uint kFaceColor    = 0x100u;
const uint kFaceNoNormal = 0x200u;

// A vertex colour byte of 0x80 is the VU's 1.0.
const float kModulate = 255.0 / 128.0;
const float kVuMax    = 3.40282347e38;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out float v_fog;

out float gl_ClipDistance[6];

// VU rsqrt(0) is a finite maximum; anything scaled by it is zero.
float vu_rsqrt(float s) {
    return s > 0.0 ? inversesqrt(s) : 0.0;
}

vec3 vu_unit(vec3 v) {
    return v * vu_rsqrt(dot(v, v));
}

void blend(inout vec3 position, inout vec3 normal) {
    vec3 P = vec3(0.0);
    vec3 N = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        float w = a_skin_weight[i];
        uint b = min(a_skin_bone[i], 31u) * 3u;
        vec4 r0 = p0.bones[b];
        vec4 r1 = p0.bones[b + 1u];
        vec4 r2 = p0.bones[b + 2u];

        vec4 pw = vec4(position * w, w);
        P += vec3(dot(r0, pw), dot(r1, pw), dot(r2, pw));

        vec3 nw = normal * w;
        vec3 c0 = vec3(r0.x, r1.x, r2.x);
        vec3 c1 = vec3(r0.y, r1.y, r2.y);
        vec3 c2 = vec3(r0.z, r1.z, r2.z);
        // O-8: retail reads column 1 for the z term of influence 3 (0x013E).
        N += c0 * nw.x + c1 * nw.y + (i == 3 ? c1 : c2) * nw.z;
    }
    position = P;
    normal = N;
}

void main() {
    uint F = p0.flags;
    uint T = p0.face;

    vec3 position = a_position;
    vec3 normal = a_normal;
    vec2 uv = a_uv;

    if ((F & kRefSphere) != 0u) {
        // O-9: env UV is taken from the unblended normal, before VERTEX_BLEND, as retail.
        vec3 nh = vu_unit(mat3(p0.clip_or_view) * a_normal);
        uv = (nh.xy + 1.0) * 0.5;
    }

    if (kSkinned && (F & kBlend) != 0u) {
        blend(position, normal);
    }

    mat3 W = mat3(p0.model_world);
    vec3 L[4];
    // progmain's MSCAL 0 setup: world directions into model space, each normalised.
    for (int c = 0; c < 4; c++) {
        vec3 lw = vec3(p0.light_direction[0][c], p0.light_direction[1][c],
                       p0.light_direction[2][c]);
        L[c] = vu_unit(vec3(dot(lw, W[0]), dot(lw, W[1]), dot(lw, W[2])));
    }

    vec4 D = p0.diffuse;
    vec4 color;
    if ((F & kUnlit) != 0u || (T & kFaceNoNormal) != 0u) {
        color = min(p0.object_color * D, vec4(255.0));
    } else {
        color = p0.ambient * D;
        float s[4];
        for (int c = 0; c < 4; c++) {
            s[c] = max(0.0, dot(L[c], normal));
            color += p0.light_color[c] * D * s[c];
        }

        if ((F & kPointLights) != 0u) {
            vec3 pw = W * position;
            vec3 nw = W * normal;
            float inv_n = vu_rsqrt(dot(nw, nw));
            for (int k = 0; k < 4; k++) {
                vec3 d = p0.point_light[k].xyz - pw;
                float inv_d = vu_rsqrt(dot(d, d));
                float cosine = max(0.0, dot(d, nw)) * inv_d * inv_n;
                float a = p0.point_light[k].w * inv_d;
                // O-4: power enters squared, so a negative power acts as its magnitude.
                float att = min(1.0, a * a);
                color += p0.point_color[k] * D * (cosine * att);
            }
        } else if ((F & kReal) != 0u) {
            vec3 V = p0.eye.xyz - position;
            float inv_v = vu_rsqrt(dot(V, V));
            float vn = dot(V, normal);
            // Power 0 is the VU's 65536-pass loop; 32 passes reach the same saturated result.
            uint n = p0.specular_power == 0u ? 32u : min(p0.specular_power, 32u);
            for (int c = 0; c < 4; c++) {
                float lit = min(1.0, 1.0e4 * s[c]);
                float r = max(0.0, 2.0 * vn * dot(L[c], normal) - dot(L[c], V)) * inv_v;
                float x = r * r;
                for (uint j = 0u; j < n; j++) {
                    // Saturates at the VU's largest float; keeps lit * x finite.
                    x = min(x * x, kVuMax);
                }
                color += p0.light_color[c] * p0.specular * (lit * x);
            }
        }
        color = min(color, vec4(255.0));
    }

    if ((T & kFaceColor) != 0u) {
        vec4 vc = a_color * kModulate;
        if ((F & kColorAdd) != 0u) {
            // O-10: retail squares vertex alpha and drops the lit alpha.
            color.rgb = min(color.rgb + 128.0 * vc.rgb, vec3(255.0));
            color.a = min(128.0 * vc.a * vc.a, 255.0);
        } else {
            color = min(color * vc, vec4(255.0));
        }
    }

    vec4 clip = mc.mvp * vec4(position, 1.0);
    gl_Position = clip;

    if ((F & kScissor) != 0u) {
        vec4 c = p0.clip_or_view * vec4(position, 1.0);
        gl_ClipDistance[0] = c.w - c.x;
        gl_ClipDistance[1] = c.w + c.x;
        gl_ClipDistance[2] = c.w - c.y;
        gl_ClipDistance[3] = c.w + c.y;
        gl_ClipDistance[4] = c.w - c.z;
        gl_ClipDistance[5] = c.w + c.z;
    } else {
        for (int i = 0; i < 6; i++) {
            gl_ClipDistance[i] = 1.0;
        }
    }

    // ftoi0 truncates; the GS fog field is the integer part of F.
    v_color = floor(clamp(color, 0.0, 255.0)) / 255.0;
    v_uv = uv;
    // The VU clamps against the near value first and the far value last, so a far value above the
    // near value fogs nothing; GLSL clamp leaves that order undefined.
    float f = p0.fog.x + p0.fog.w / clip.w;
    v_fog = floor(max(max(min(f, p0.fog.y), p0.fog.z), 0.0)) / 255.0;
}
