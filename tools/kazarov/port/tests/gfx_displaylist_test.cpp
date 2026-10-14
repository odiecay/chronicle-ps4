#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "gfx/displaylist.hpp"
#include "gfx_fixture.hpp"

using namespace dc::test;

namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 48;
const std::array<uint8_t, 4> kClear = {0, 0, 0, 0x80};
const std::vector<uint32_t> kTriangle = {0, 1, 2};

using Rows = std::array<float, 12>;
constexpr Rows kIdentityRows = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

GfxOptions Prog0Options() {
    GfxOptions options;
    options.prog0_meshes = true;
    options.pipeline_cache = std::filesystem::temp_directory_path() / "dc_gfx_test" / "pipeline_cache_prog0.bin";
    return options;
}

void Identity(float (&m)[16]) {
    std::memset(m, 0, sizeof m);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void SetBone(gfx::Prog0Constants &p, uint32_t slot, const Rows &rows) {
    for (uint32_t r = 0; r < 3; r++) {
        for (uint32_t c = 0; c < 4; c++) {
            p.bones[slot][r][c] = rows[r * 4 + c];
        }
    }
}

gfx::Vertex3D Vert(std::array<float, 3> position, std::array<float, 3> normal, std::array<uint8_t, 4> color = {}) {
    return {{position[0], position[1], position[2]},
            {normal[0], normal[1], normal[2]},
            {0.0f, 0.0f},
            {color[0], color[1], color[2], color[3]}};
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

struct Coverage {
    double x = 0.0;
    int    count = 0;
};

Coverage ReadCoverage(GfxFixture &fixture) {
    EXPECT_TRUE(gfx::ReadbackFrame(fixture.pixels, fixture.width, fixture.height));
    Coverage out;
    double   sum = 0.0;
    for (uint32_t y = 0; y < fixture.height; y++) {
        for (uint32_t x = 0; x < fixture.width; x++) {
            if (fixture.Pixel(x, y)[0] > 100) {
                sum += x;
                out.count++;
            }
        }
    }
    if (out.count > 0) {
        out.x = sum / out.count;
    }
    return out;
}

} // namespace

TEST(GfxDisplayList, BonesBlendPerBoneAtHalf) {
    gfx::Prog0Constants before = {};
    gfx::Prog0Constants after = {};
    before.flags = after.flags = gfx::kProg0Skinned;
    for (uint32_t slot = 0; slot < gfx::kProg0Bones; slot++) {
        SetBone(before, slot, kIdentityRows);
        SetBone(after, slot, kIdentityRows);
    }

    const float root = std::sqrt(0.5f);
    const Rows  moved_translation = {1, 0, 0, 10, 0, 1, 0, 0, 0, 0, 1, 0};
    const Rows  rotated = {0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
    const Rows  scaled = {3, 0, 0, 0, 0, 3, 0, 0, 0, 0, 3, 0};
    const Rows  teleported = {1, 0, 0, 500, 0, 1, 0, 0, 0, 0, 1, 0};
    const Rows  still = {1, 0, 0, 7, 0, 1, 0, 0, 0, 0, 1, 0};
    SetBone(after, 0, moved_translation);
    SetBone(after, 1, rotated);
    SetBone(after, 2, scaled);
    SetBone(after, 3, teleported);
    SetBone(before, 4, still);
    SetBone(after, 4, still);

    gfx::Prog0Constants expected = after;
    SetBone(expected, 0, {1, 0, 0, 5, 0, 1, 0, 0, 0, 0, 1, 0});
    SetBone(expected, 1, {root, -root, 0, 0, root, root, 0, 0, 0, 0, 1, 0});
    SetBone(expected, 2, {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0});

    gfx::Prog0Constants out = {};
    gfx::detail::BlendProg0Bones(before, after, 0.5f, 200.0f, out);
    for (uint32_t slot = 0; slot < gfx::kProg0Bones; slot++) {
        for (uint32_t r = 0; r < 3; r++) {
            for (uint32_t c = 0; c < 4; c++) {
                EXPECT_NEAR(out.bones[slot][r][c], expected.bones[slot][r][c], 1e-4f)
                    << "slot " << slot << " row " << r << " column " << c;
            }
        }
    }
    EXPECT_EQ(std::memcmp(out.bones[3], after.bones[3], sizeof out.bones[3]), 0);
    EXPECT_EQ(std::memcmp(out.bones[4], after.bones[4], sizeof out.bones[4]), 0);
    EXPECT_EQ(out.flags, after.flags);
}

TEST(GfxMeshDc2, SkinnedPaletteInterpolatesInDisplayFrame) {
    GfxFixture fixture(kWidth, kHeight, 1.0f, Prog0Options());
    const std::vector<gfx::Vertex3D> vertices =
        Skinned({Vert({-0.2f, -0.2f, 0.5f}, {0, 0, 1}), Vert({0.2f, -0.2f, 0.5f}, {0, 0, 1}),
                 Vert({0.0f, 0.2f, 0.5f}, {0, 0, 1})},
                {1, 0, 0, 0}, {0, 0, 0, 0});
    gfx::MeshConstants constants = {};
    Identity(constants.mvp);
    gfx::MeshTransform transform = gfx::IdentityMeshTransform();
    gfx::Prog0Constants block = {};
    block.flags = gfx::kProg0Unlit | gfx::kProg0Skinned;
    for (uint32_t k = 0; k < 4; k++) {
        block.object_color[k] = k == 3 ? 128.0f : 200.0f;
        block.diffuse[k] = 1.0f;
    }

    auto record = [&](float translation_x, bool cut) {
        gfx::Prog0Constants posed = block;
        SetBone(posed, 0, {1, 0, 0, translation_x, 0, 1, 0, 0, 0, 0, 1, 0});
        gfx::BeginRecording();
        gfx::SetInterpKey(1, false, 200.0f);
        if (cut) {
            gfx::CutInterpolation();
        }
        gfx::Clear(true, kClear.data(), true, 0.0f);
        gfx::DrawMeshImmediateProg0(vertices, kTriangle, constants, posed, {}, {}, &transform);
        return gfx::EndRecording();
    };
    auto canonical = [&](const gfx::DisplayList &list) {
        gfx::RenderOptions options;
        options.canonical = true;
        EXPECT_TRUE(gfx::RenderList(list, 1.0f, options));
        return ReadCoverage(fixture);
    };

    gfx::DisplayListRef list_a = record(-0.25f, false);
    gfx::DisplayListRef list_b = record(0.25f, false);
    ASSERT_TRUE(list_a != nullptr);
    ASSERT_TRUE(list_b != nullptr);
    Coverage at_a = canonical(*list_a);
    Coverage at_b = canonical(*list_b);
    ASSERT_GT(at_a.count, 0);
    ASSERT_GT(at_b.count, 0);
    ASSERT_GT(std::fabs(at_b.x - at_a.x), 4.0);

    gfx::RenderOptions display;
    display.previous = list_a.get();
    display.present = true;
    ASSERT_TRUE(gfx::RenderList(*list_b, 0.5f, display));
    Coverage halfway = ReadCoverage(fixture);
    EXPECT_GT(halfway.count, 0);
    EXPECT_NEAR(halfway.x, (at_a.x + at_b.x) / 2.0, 1.0);

    gfx::DisplayListRef cut_b = record(0.25f, true);
    ASSERT_TRUE(cut_b != nullptr);
    ASSERT_TRUE(gfx::RenderList(*cut_b, 0.5f, display));
    Coverage after_cut = ReadCoverage(fixture);
    EXPECT_NEAR(after_cut.x, at_b.x, 1.0);
}

// A 16:9 window on the 4:3 logical frame leaves a 16 px bar at the right, which the right anchor
// shifts the quad into.
TEST(GfxMeshDc2, AnchoredScissorMovesWithTheDraw) {
    constexpr int                kAnchorWidth = 128;
    constexpr int                kAnchorHeight = 72;
    constexpr uint32_t           kFrameRight = 112;
    const std::array<uint8_t, 4> black = {0, 0, 0, 0xFF};
    const std::array<uint8_t, 4> white = {0xFF, 0xFF, 0xFF, 0xFF};

    GfxFixture fixture(kAnchorWidth, kAnchorHeight, 1.0f);
    auto       draw = [&](bool follow_scissor) {
        fixture.Frame(black, [&] {
            gfx::UiAnchorScope anchor(gfx::UiAnchor::Side(1, -1, follow_scissor));
            gfx::DrawState     state;
            state.scissor = true;
            state.scissor_rect = {560.0f, 100.0f, 40.0f, 100.0f};
            gfx::Draw2D(gfx::Primitive::Quads, Quad(560.0f, 100.0f, 40.0f, 100.0f, white), {}, state);
        });
        int total = 0;
        int in_bar = 0;
        for (uint32_t y = 0; y < fixture.height; y++) {
            for (uint32_t x = 0; x < fixture.width; x++) {
                std::array<uint8_t, 4> p = fixture.Pixel(x, y);
                if (p[0] > 200 && p[1] > 200 && p[2] > 200) {
                    total++;
                    if (x >= kFrameRight) {
                        in_bar++;
                    }
                }
            }
        }
        return std::pair{total, in_bar};
    };

    auto [followed, followed_in_bar] = draw(true);
    EXPECT_GT(followed, 0);
    EXPECT_EQ(followed, followed_in_bar);

    auto [clipped, clipped_in_bar] = draw(false);
    EXPECT_EQ(clipped, 0);
    EXPECT_EQ(clipped_in_bar, 0);
}

namespace {

// A clip-space triangle that covers the whole target, flat red (2.0 is 0xFF through kModulate).
void DrawWindowRed(GfxFixture &fixture) {
    gfx::MeshConstants constants = {};
    Identity(constants.mvp);
    constants.diffuse[0] = 2.0f;
    constants.diffuse[3] = 2.0f;
    const std::vector<gfx::Vertex3D> vertices = {
        {{-1.0f, -1.0f, 0.5f}, {0, 0, 1}, {0, 0}, {}},
        {{3.0f, -1.0f, 0.5f}, {0, 0, 1}, {0, 0}, {}},
        {{-1.0f, 3.0f, 0.5f}, {0, 0, 1}, {0, 0}, {}},
    };
    fixture.Frame({0xFF, 0xFF, 0xFF, 0xFF}, [&] { gfx::DrawMeshImmediate(vertices, kTriangle, constants, {}, {}); });
}

} // namespace

// The bars are cleared to black when the renderer makes the main target, and the frame's clear
// and mesh must leave them that way.
TEST(GfxMeshDc2, LetterboxClipsMeshesAndClearsToTheFrame) {
    constexpr int kLetterboxWidth = 128;
    constexpr int kLetterboxHeight = 72;

    GfxFixture fixture(kLetterboxWidth, kLetterboxHeight, 1.0f);
    gfx::SetFrameLayout(gfx::FrameLayout{gfx::AspectMode::Letterbox, 1.0f});
    DrawWindowRed(fixture);
    EXPECT_TRUE(fixture.PixelNear(4, 36, 0, 0, 0));
    EXPECT_TRUE(fixture.PixelNear(123, 36, 0, 0, 0));
    EXPECT_TRUE(fixture.PixelNear(64, 36, 255, 0, 0));
    gfx::SetFrameLayout(gfx::FrameLayout{gfx::AspectMode::Fill, 1.0f});
}

TEST(GfxMeshDc2, FillKeepsClearsAcrossTheWindow) {
    constexpr int kFillWidth = 128;
    constexpr int kFillHeight = 72;

    GfxFixture fixture(kFillWidth, kFillHeight, 1.0f);
    DrawWindowRed(fixture);
    EXPECT_TRUE(fixture.PixelNear(4, 36, 255, 0, 0));
    EXPECT_TRUE(fixture.PixelNear(123, 36, 255, 0, 0));
    EXPECT_TRUE(fixture.PixelNear(64, 36, 255, 0, 0));
    gfx::SetFrameLayout(gfx::FrameLayout{gfx::AspectMode::Fill, 1.0f});
}
