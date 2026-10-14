#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "gfx_fixture.hpp"

using namespace dc::test;

namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 48;
const std::array<uint8_t, 4> kClear = {0, 0, 0, 0x80};
const std::vector<uint32_t> kTriangle = {0, 1, 2};
constexpr std::array<float, 12> kIdentityBone = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

GfxOptions Prog0Options() {
    GfxOptions options;
    options.prog0_meshes = true;
    // Its own cache: the plain fixtures' cache is not rewritten with more pipelines by every case here.
    options.pipeline_cache = std::filesystem::temp_directory_path() / "dc_gfx_test" / "pipeline_cache_prog0.bin";
    return options;
}

void Identity(float (&m)[16]) {
    std::memset(m, 0, sizeof m);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

template <size_t N>
void Set(float (&dst)[N], std::initializer_list<float> values) {
    size_t i = 0;
    for (float v : values) {
        dst[i++] = v;
    }
}

// clip = (w x, w y, 0.5 w, w): the screen triangle keeps its NDC place and depth 0.5 at clip.w = w.
void SetWMvp(float (&m)[16], float w) {
    std::memset(m, 0, sizeof m);
    m[0] = m[5] = m[15] = w;
    m[14] = 0.5f * w;
}

// mgSetFogParam: the values at near_w and far_w, on a 1/w scale.
void SetFog(float (&fog)[4], float near_w, float far_w, float near_value, float far_value) {
    float range = far_value - near_value;
    fog[0] = (far_value + near_value + range * (far_w + near_w) / (far_w - near_w)) / 2.0f;
    fog[1] = near_value;
    fog[2] = far_value;
    fog[3] = -far_w * near_w * range / (far_w - near_w);
}

// A kMeshProg0 draw: the mesh constants and the Vu_prog0 block the game uploads with them.
struct Prog0Draw {
    gfx::MeshConstants  mesh;
    gfx::Prog0Constants prog0;
};

Prog0Draw Prog0(uint32_t flags, uint32_t face = 0) {
    Prog0Draw c{};
    Identity(c.mesh.mvp);
    Identity(c.prog0.model_world);
    Set(c.prog0.diffuse, {1, 1, 1, 1});
    // Offset 255 with near value 255 and scale 0: no fog.
    Set(c.prog0.fog, {255, 255, 0, 0});
    c.prog0.flags = flags;
    c.prog0.face = face;
    c.prog0.specular_power = 3;
    return c;
}

gfx::MeshConstants ChronicleLit() {
    gfx::MeshConstants c{};
    c.flags = gfx::kMeshLit;
    c.light_count = 1;
    Identity(c.mvp);
    Set(c.normal_matrix, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
    Set(c.light_direction[0], {0, 0, -1, 0});
    Set(c.light_color[0], {0.5f, 0.25f, 0, 0});
    Set(c.ambient, {0.25f, 0.25f, 0.25f, 0});
    Set(c.ambient_material, {1, 1, 1, 1});
    Set(c.diffuse, {1, 1, 1, 1});
    return c;
}

gfx::Vertex3D Vert(std::array<float, 3> position, std::array<float, 3> normal, std::array<uint8_t, 4> color = {}) {
    return {{position[0], position[1], position[2]},
            {normal[0], normal[1], normal[2]},
            {0.0f, 0.0f},
            {color[0], color[1], color[2], color[3]}};
}

std::vector<gfx::Vertex3D> ScreenVertices(std::array<float, 3> normal, std::array<uint8_t, 4> color = {}) {
    return {Vert({-1.0f, -1.0f, 0.5f}, normal, color), Vert({3.0f, -1.0f, 0.5f}, normal, color),
            Vert({-1.0f, 3.0f, 0.5f}, normal, color)};
}

// Magnified by UseTinyMvp, so the centre pixel samples model position (0, 0, 0).
std::vector<gfx::Vertex3D> TinyVertices(std::array<float, 3> normal, std::array<uint8_t, 4> color = {}) {
    return {Vert({-0.01f, -0.01f, 0.0f}, normal, color), Vert({0.03f, -0.01f, 0.0f}, normal, color),
            Vert({-0.01f, 0.03f, 0.0f}, normal, color)};
}

std::vector<gfx::Vertex3D> QuadVertices() {
    return {Vert({-1.0f, -1.0f, 0.5f}, {0, 0, 1}), Vert({0.0f, -1.0f, 0.5f}, {0, 0, 1}),
            Vert({0.0f, 1.0f, 0.5f}, {0, 0, 1}), Vert({-1.0f, 1.0f, 0.5f}, {0, 0, 1})};
}

void UseTinyMvp(float (&m)[16]) {
    Identity(m);
    m[0] = 100.0f;
    m[5] = 100.0f;
    m[14] = 0.5f;
}

// Each vertex followed by its skin, as Vu_prog0 reads a kProg0Skinned mesh.
std::vector<gfx::Vertex3D> Skinned(const std::vector<gfx::Vertex3D> &vertices, std::array<float, 4> weight,
                                   std::array<uint8_t, 4> bone) {
    gfx::Prog0Skin skin = {};
    for (size_t i = 0; i < 4; i++) {
        skin.weight[i] = weight[i];
        skin.bone[i] = bone[i];
    }
    gfx::Vertex3D pair = {};
    std::memcpy(&pair, &skin, sizeof pair);
    std::vector<gfx::Vertex3D> out;
    for (const gfx::Vertex3D &vertex : vertices) {
        out.push_back(vertex);
        out.push_back(pair);
    }
    return out;
}

void SetDirectional(gfx::Prog0Constants &p, uint32_t light, std::array<float, 3> direction,
                    std::array<float, 4> color) {
    for (uint32_t k = 0; k < 3; k++) {
        p.light_direction[k][light] = direction[k];
    }
    for (uint32_t k = 0; k < 4; k++) {
        p.light_color[light][k] = color[k];
    }
}

void SetPoint(gfx::Prog0Constants &p, uint32_t k, std::array<float, 4> position_power, std::array<float, 4> color) {
    for (uint32_t j = 0; j < 4; j++) {
        p.point_light[k][j] = position_power[j];
        p.point_color[k][j] = color[j];
    }
}

// Rows 0-2 of the bone's 4x4, row-major.
void SetBone(gfx::Prog0Constants &p, uint32_t slot, const std::array<float, 12> &rows) {
    for (uint32_t r = 0; r < 3; r++) {
        for (uint32_t c = 0; c < 4; c++) {
            p.bones[slot][r][c] = rows[r * 4 + c];
        }
    }
}

Prog0Draw Unlit(uint32_t flags, uint32_t face = 0) {
    Prog0Draw c = Prog0(flags, face);
    Set(c.prog0.object_color, {128, 64, 32, 128});
    Set(c.prog0.diffuse, {0.5f, 0.5f, 0.5f, 1});
    SetDirectional(c.prog0, 0, {0, 0, 1}, {255, 255, 255, 0});
    return c;
}

Prog0Draw FourLights() {
    Prog0Draw c = Prog0(0);
    Set(c.prog0.ambient, {10, 20, 30, 128});
    SetDirectional(c.prog0, 0, {0, 0, 1}, {40, 0, 0, 0});
    SetDirectional(c.prog0, 1, {0, 0.6f, 0.8f}, {0, 50, 0, 0});
    SetDirectional(c.prog0, 2, {0, 0, -1}, {0, 0, 200, 0});
    SetDirectional(c.prog0, 3, {1, 0, 0}, {0, 0, 90, 0});
    return c;
}

gfx::TextureHandle RgbQuadTexture() {
    gfx::TextureHandle texture = gfx::CreateTexture(gfx::TextureDesc{2, 2});
    const uint8_t texels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    EXPECT_TRUE(gfx::UpdateTexture(texture, 0, 0, 0, 2, 2, texels));
    return texture;
}

gfx::TextureBinding NearestBinding(gfx::TextureHandle texture) {
    gfx::TextureBinding binding;
    binding.texture = texture;
    binding.filter = gfx::Filter::Nearest;
    return binding;
}

void Render(GfxFixture &fixture, const std::vector<gfx::Vertex3D> &vertices, const std::vector<uint32_t> &indices,
            const gfx::MeshConstants &constants, const gfx::TextureBinding &texture = {},
            const gfx::DrawState &state = {}) {
    fixture.Frame(kClear, [&] { gfx::DrawMeshImmediate(vertices, indices, constants, texture, state); });
}

void Render(GfxFixture &fixture, const std::vector<gfx::Vertex3D> &vertices, const std::vector<uint32_t> &indices,
            const Prog0Draw &draw, const gfx::TextureBinding &texture = {}, const gfx::DrawState &state = {}) {
    fixture.Frame(kClear, [&] {
        gfx::DrawMeshImmediateProg0(vertices, indices, draw.mesh, draw.prog0, texture, state);
    });
}

bool Centre(const GfxFixture &fixture, int r, int g, int b, int tolerance = 1) {
    return fixture.PixelNear(fixture.width / 2, fixture.height / 2, r, g, b, tolerance);
}

} // namespace

TEST(GfxMeshDc2, MeshConstantsKeepChroniclesSize) {
    EXPECT_EQ(sizeof(gfx::MeshConstants), 400u);
}

TEST(GfxMeshDc2, PipelinesOnlyWhenAsked) {
    uint32_t plain = 0;
    {
        GfxFixture fixture(kWidth, kHeight);
        plain = gfx::PipelineCount();
    }
    ASSERT_EQ(plain % 3, 0u);
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    EXPECT_EQ(gfx::PipelineCount(), plain / 3 * 5);
}

TEST(GfxMeshDc2, PlainMeshesIgnoreTheProg0Pipelines) {
    std::array<uint8_t, 4> plain = {};
    {
        GfxFixture fixture(kWidth, kHeight);
        Render(fixture, ScreenVertices({0, 0, 1}), kTriangle, ChronicleLit());
        plain = fixture.Pixel(fixture.width / 2, fixture.height / 2);
    }
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Render(fixture, ScreenVertices({0, 0, 1}), kTriangle, ChronicleLit());
    EXPECT_EQ(fixture.Pixel(fixture.width / 2, fixture.height / 2), plain);
}

TEST(GfxMeshDc2, PlainDrawWithProg0FlagDrawsPlain) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    gfx::MeshConstants plain = ChronicleLit();
    Render(fixture, ScreenVertices({0, 0, 1}), kTriangle, plain);
    const std::array<uint8_t, 4> expected = fixture.Pixel(fixture.width / 2, fixture.height / 2);
    gfx::MeshConstants flagged = plain;
    flagged.flags |= gfx::kMeshProg0;
    Render(fixture, ScreenVertices({0, 0, 1}), kTriangle, flagged);
    EXPECT_EQ(fixture.Pixel(fixture.width / 2, fixture.height / 2), expected);
}

TEST(GfxMeshDc2, FourDirectionalLights) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Render(fixture, ScreenVertices({0, 0, 1}), kTriangle, FourLights());
    EXPECT_TRUE(Centre(fixture, 50, 60, 30));
}

TEST(GfxMeshDc2, IndexedProg0MeshMatchesImmediate) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    const Prog0Draw c = FourLights();
    const std::vector<gfx::Vertex3D> vertices = ScreenVertices({0, 0, 1});
    Render(fixture, vertices, kTriangle, c);
    const std::array<uint8_t, 4> immediate = fixture.Pixel(fixture.width / 2, fixture.height / 2);

    gfx::MeshHandle mesh = gfx::CreateMesh(vertices, kTriangle);
    ASSERT_NE(mesh, gfx::kNullMesh);
    fixture.Frame(kClear, [&] { gfx::DrawMeshProg0(mesh, 0, 3, c.mesh, c.prog0, {}, {}); });
    EXPECT_EQ(fixture.Pixel(fixture.width / 2, fixture.height / 2), immediate);
    EXPECT_TRUE(Centre(fixture, 50, 60, 30));
    gfx::DestroyMesh(mesh);
}

TEST(GfxMeshDc2, RecordedProg0DrawReplaysThroughRenderList) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    const Prog0Draw c = FourLights();
    const std::vector<gfx::Vertex3D> vertices = ScreenVertices({0, 0, 1});
    Render(fixture, vertices, kTriangle, c);
    const std::array<uint8_t, 4> direct = fixture.Pixel(fixture.width / 2, fixture.height / 2);

    gfx::BeginRecording();
    gfx::Clear(true, kClear.data(), true, 0.0f);
    gfx::DrawMeshImmediateProg0(vertices, kTriangle, c.mesh, c.prog0, {}, {});
    gfx::DisplayListRef list = gfx::EndRecording();
    ASSERT_TRUE(list != nullptr);
    gfx::RenderOptions options;
    options.canonical = true;
    ASSERT_TRUE(gfx::RenderList(*list, 1.0f, options));
    ASSERT_TRUE(gfx::ReadbackFrame(fixture.pixels, fixture.width, fixture.height));
    EXPECT_EQ(fixture.Pixel(fixture.width / 2, fixture.height / 2), direct);
    EXPECT_TRUE(Centre(fixture, 50, 60, 30));
}

TEST(GfxMeshDc2, FourDirectionalLightsSaturate) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Prog0Draw c = Prog0(0);
    Set(c.prog0.ambient, {200, 200, 200, 128});
    SetDirectional(c.prog0, 0, {0, 0, 1}, {100, 100, 100, 0});
    Render(fixture, ScreenVertices({0, 0, 1}), kTriangle, c);
    EXPECT_TRUE(Centre(fixture, 255, 255, 255));
}

TEST(GfxMeshDc2, LightsTurnIntoModelSpace) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Prog0Draw c = Prog0(0);
    Set(c.prog0.model_world, {0, 2, 0, 0, -2, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1});
    SetDirectional(c.prog0, 0, {1, 0, 0}, {100, 0, 0, 0});
    Render(fixture, ScreenVertices({0, -1, 0}), kTriangle, c);
    EXPECT_TRUE(Centre(fixture, 100, 0, 0));
    Render(fixture, ScreenVertices({0, 1, 0}), kTriangle, c);
    EXPECT_TRUE(Centre(fixture, 0, 0, 0));
}

TEST(GfxMeshDc2, Unlit) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Render(fixture, ScreenVertices({0, 0, 1}), kTriangle, Unlit(gfx::kProg0Unlit));
    EXPECT_TRUE(Centre(fixture, 64, 32, 16));
    Render(fixture, ScreenVertices({0, 0, 1}), kTriangle, Unlit(0, gfx::kProg0FaceNoNormal));
    EXPECT_TRUE(Centre(fixture, 64, 32, 16));
}

TEST(GfxMeshDc2, PointLights) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Prog0Draw c = Prog0(gfx::kProg0PointLights);
    UseTinyMvp(c.mesh.mvp);
    Set(c.prog0.ambient, {10, 10, 10, 0});
    SetPoint(c.prog0, 0, {0, 0, 2, 1}, {128, 0, 0, 0});
    SetPoint(c.prog0, 1, {0, 0, 0.5f, 1}, {0, 64, 0, 0});
    SetPoint(c.prog0, 2, {0, 0, -2, 10}, {0, 0, 255, 0});
    // O-4: power enters squared, so a negative power attenuates as its magnitude.
    SetPoint(c.prog0, 3, {0, 0, 2, -1}, {0, 0, 128, 0});
    Render(fixture, TinyVertices({0, 0, 1}), kTriangle, c);
    EXPECT_TRUE(Centre(fixture, 42, 74, 42));
}

TEST(GfxMeshDc2, RealSpecular) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Prog0Draw c = Prog0(gfx::kProg0Real);
    UseTinyMvp(c.mesh.mvp);
    Set(c.prog0.diffuse, {0.25f, 0.25f, 0.25f, 1});
    Set(c.prog0.specular, {1, 1, 1, 1});
    // Five units along a unit direction 0.9 off the normal: the reflection term is 0.9 at the origin.
    Set(c.prog0.eye, {2.1794495f, 0, 4.5f, 1});
    SetDirectional(c.prog0, 0, {0, 0, 1}, {128, 128, 128, 0});
    Render(fixture, TinyVertices({0, 0, 1}), kTriangle, c);
    EXPECT_TRUE(Centre(fixture, 55, 55, 55, 2));
    c.prog0.specular_power = 1;
    Render(fixture, TinyVertices({0, 0, 1}), kTriangle, c);
    EXPECT_TRUE(Centre(fixture, 115, 115, 115, 2));
    c.prog0.specular_power = 3;
    c.prog0.flags = gfx::kProg0Real | gfx::kProg0PointLights;
    Render(fixture, TinyVertices({0, 0, 1}), kTriangle, c);
    EXPECT_TRUE(Centre(fixture, 32, 32, 32, 2));
}

TEST(GfxMeshDc2, VertexColour) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Prog0Draw c = Prog0(gfx::kProg0Unlit, gfx::kProg0FaceColor);
    Set(c.prog0.object_color, {100, 100, 100, 128});
    Render(fixture, ScreenVertices({0, 0, 1}, {64, 64, 64, 128}), kTriangle, c);
    EXPECT_TRUE(Centre(fixture, 50, 50, 50));
    c.prog0.flags = gfx::kProg0Unlit | gfx::kProg0ColorAdd;
    Render(fixture, ScreenVertices({0, 0, 1}, {64, 64, 64, 128}), kTriangle, c);
    EXPECT_TRUE(Centre(fixture, 164, 164, 164));
    // O-10: the ColorAdd alpha is the squared vertex alpha, 128 * 0.5^2 = 32, and that is what blends.
    gfx::DrawState blend;
    blend.blend = true;
    blend.alpha = gfx::GsBlend{0, 2, 0, 2};
    Render(fixture, ScreenVertices({0, 0, 1}, {64, 64, 64, 64}), kTriangle, c, {}, blend);
    EXPECT_TRUE(Centre(fixture, 41, 41, 41, 2));
}

TEST(GfxMeshDc2, RefSphere) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    gfx::TextureHandle texture = RgbQuadTexture();
    ASSERT_NE(texture, gfx::kNullTexture);
    gfx::TextureBinding binding = NearestBinding(texture);

    Prog0Draw c = Prog0(gfx::kProg0Unlit | gfx::kProg0RefSphere);
    Set(c.prog0.object_color, {128, 128, 128, 128});
    Identity(c.prog0.clip_or_view);
    Render(fixture, ScreenVertices({0.5f, -0.5f, 0.70710678f}), kTriangle, c, binding);
    EXPECT_TRUE(Centre(fixture, 0, 255, 0));
    Render(fixture, ScreenVertices({-0.5f, 0.5f, 0.70710678f}), kTriangle, c, binding);
    EXPECT_TRUE(Centre(fixture, 0, 0, 255));
    gfx::DestroyTexture(texture);
}

TEST(GfxMeshDc2, RefSphereIgnoresSkinning) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    gfx::TextureHandle texture = RgbQuadTexture();
    ASSERT_NE(texture, gfx::kNullTexture);
    gfx::TextureBinding binding = NearestBinding(texture);

    Prog0Draw c = Prog0(gfx::kProg0Unlit | gfx::kProg0RefSphere | gfx::kProg0Skinned);
    Set(c.prog0.object_color, {128, 128, 128, 128});
    Identity(c.prog0.clip_or_view);
    SetBone(c.prog0, 0, {0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0});
    // O-9: the env UV is taken from the unblended normal, before the skin is applied.
    Render(fixture, Skinned(ScreenVertices({0.5f, -0.5f, 0.70710678f}), {1, 0, 0, 0}, {0, 0, 0, 0}), kTriangle, c,
           binding);
    EXPECT_TRUE(Centre(fixture, 0, 255, 0));
    gfx::DestroyTexture(texture);
}

TEST(GfxMeshDc2, Skinning) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Prog0Draw c = Prog0(gfx::kProg0Unlit | gfx::kProg0Skinned);
    Set(c.prog0.object_color, {200, 200, 200, 128});
    SetBone(c.prog0, 0, kIdentityBone);
    SetBone(c.prog0, 1, {1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0});
    Render(fixture, Skinned(QuadVertices(), {0.5f, 0.5f, 0, 0}, {0, 1, 0, 0}), {0, 1, 2, 0, 2, 3}, c);
    EXPECT_TRUE(fixture.PixelNear(fixture.width * 5 / 8, fixture.height / 2, 200, 200, 200));
    EXPECT_TRUE(fixture.PixelNear(fixture.width / 8, fixture.height / 2, 0, 0, 0));
}

TEST(GfxMeshDc2, SkinnedNormals) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Prog0Draw c = Prog0(gfx::kProg0Skinned);
    SetBone(c.prog0, 0, kIdentityBone);
    SetDirectional(c.prog0, 0, {0, 0, 1}, {100, 100, 100, 0});
    Render(fixture, Skinned(ScreenVertices({0, 0, 1}), {1, 0, 0, 0}, {0, 0, 0, 0}), kTriangle, c);
    EXPECT_TRUE(Centre(fixture, 100, 100, 100));
}

TEST(GfxMeshDc2, SkinnedFourthInfluenceNormal) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Prog0Draw c = Prog0(gfx::kProg0Skinned);
    SetBone(c.prog0, 3, kIdentityBone);
    const std::vector<gfx::Vertex3D> vertices = Skinned(ScreenVertices({0, 0, 1}), {0, 0, 0, 1}, {0, 0, 0, 3});
    // O-8: influence 3's z term reads column 1, so the retail normal is (0, 1, 0).
    SetDirectional(c.prog0, 0, {0, 1, 0}, {100, 100, 100, 0});
    Render(fixture, vertices, kTriangle, c);
    EXPECT_TRUE(Centre(fixture, 100, 100, 100));
    SetDirectional(c.prog0, 0, {0, 0, 1}, {100, 100, 100, 0});
    Render(fixture, vertices, kTriangle, c);
    EXPECT_TRUE(Centre(fixture, 0, 0, 0));
}

TEST(GfxMeshDc2, ScissorClipsToClipOrView) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Prog0Draw c = Prog0(gfx::kProg0Unlit | gfx::kProg0Scissor);
    Set(c.prog0.object_color, {200, 200, 200, 128});
    Set(c.prog0.clip_or_view, {2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    Render(fixture, ScreenVertices({0, 0, 1}), kTriangle, c);
    EXPECT_TRUE(fixture.PixelNear(fixture.width / 2, fixture.height / 2, 200, 200, 200));
    EXPECT_TRUE(fixture.PixelNear(fixture.width * 7 / 8, fixture.height / 2, 0, 0, 0));
    c.prog0.flags = gfx::kProg0Unlit;
    Render(fixture, ScreenVertices({0, 0, 1}), kTriangle, c);
    EXPECT_TRUE(fixture.PixelNear(fixture.width * 7 / 8, fixture.height / 2, 200, 200, 200));
}

TEST(GfxMeshDc2, Prog0CullModeFollowsFlagAndMirror) {
    EXPECT_EQ(gfx::Prog0CullMode(Prog0(0).prog0), gfx::CullMode::None);
    EXPECT_EQ(gfx::Prog0CullMode(Prog0(gfx::kProg0Cull).prog0), gfx::CullMode::Back);
    Prog0Draw mirrored = Prog0(gfx::kProg0Unlit | gfx::kProg0Cull);
    Set(mirrored.prog0.model_world, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1});
    EXPECT_EQ(gfx::Prog0CullMode(mirrored.prog0), gfx::CullMode::Front);

    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    const std::vector<gfx::Vertex3D> triangle = {Vert({0.0f, 0.0f, 0.5f}, {0, 0, 1}),
                                                 Vert({0.5f, 0.0f, 0.5f}, {0, 0, 1}),
                                                 Vert({0.0f, 0.5f, 0.5f}, {0, 0, 1})};
    Prog0Draw c = Prog0(gfx::kProg0Unlit | gfx::kProg0Cull);
    Set(c.prog0.object_color, {200, 200, 200, 128});
    gfx::DrawState state;
    state.cull = gfx::Prog0CullMode(c.prog0);
    // Pixel (35, 26) is at NDC (0.11, 0.10), inside the triangle.
    Render(fixture, triangle, {0, 1, 2}, c, {}, state);
    EXPECT_TRUE(fixture.PixelNear(35, 26, 0, 0, 0));
    Render(fixture, triangle, {0, 2, 1}, c, {}, state);
    EXPECT_TRUE(fixture.PixelNear(35, 26, 200, 200, 200));

    Prog0Draw draw_mirrored = c;
    Set(draw_mirrored.prog0.model_world, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1});
    gfx::DrawState mirrored_state;
    mirrored_state.cull = gfx::Prog0CullMode(draw_mirrored.prog0);
    Render(fixture, triangle, {0, 1, 2}, draw_mirrored, {}, mirrored_state);
    EXPECT_TRUE(fixture.PixelNear(35, 26, 200, 200, 200));
}

TEST(GfxMeshDc2, Fog) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Prog0Draw c = Prog0(gfx::kProg0Unlit);
    Set(c.prog0.object_color, {255, 255, 255, 128});
    SetFog(c.prog0.fog, 10, 1000, 255, 0);
    gfx::DrawState state;
    state.fog = true;
    auto render_at = [&](float w) {
        SetWMvp(c.mesh.mvp, w);
        Render(fixture, ScreenVertices({0, 0, 1}), kTriangle, c, {}, state);
    };
    render_at(10);
    EXPECT_TRUE(Centre(fixture, 255, 255, 255));
    render_at(1000);
    EXPECT_TRUE(Centre(fixture, 0, 0, 0));
    render_at(100);
    EXPECT_TRUE(Centre(fixture, 23, 23, 23));
}

TEST(GfxMeshDc2, EditorFogSaturatesAtFarValue) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    Prog0Draw c = Prog0(gfx::kProg0Unlit);
    Set(c.prog0.object_color, {255, 255, 255, 128});
    SetFog(c.prog0.fog, 10, 1000, 0, 255);
    SetWMvp(c.mesh.mvp, 100);
    gfx::DrawState state;
    state.fog = true;
    // The shader clamps with min against the near value, then max against the far value, so the
    // result is the far value at every w.
    Render(fixture, ScreenVertices({0, 0, 1}), kTriangle, c, {}, state);
    EXPECT_TRUE(Centre(fixture, 255, 255, 255));
}
