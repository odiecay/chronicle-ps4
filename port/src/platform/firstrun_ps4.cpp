// platform/firstrun.hpp on the PS4: the first-start setup screen.
//
// The package carries no game data. When /data/darkcloud/data holds no file, the game draws a setup
// screen instead of starting: it says where to put the person's own Dark Cloud disc image (an .iso in
// /data/darkcloud/ over FTP, or on a USB drive), waits for it, then extracts the game data from it with
// the same dcdata code the PC port uses, showing progress. Pad only: X confirms, O quits, up and
// down choose between several images.

#ifdef __PS4__

#include "platform/firstrun.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <orbis/Pad.h>

#include "../../../ps4/src/ps4_platform.hpp"
#include "audio/pcm.hpp"
#include "gfx/gfx.hpp"
#include "platform/audio.hpp"
#include "icon/chronicle.hpp"
#include "platform/overlay.hpp"
#include "platform/paths.hpp"
#include "platform/window.hpp"
#include "screen.hpp"

// main_ps4.cpp: back to the home screen.
extern "C" void DarkCloudPs4Quit();

namespace fs = std::filesystem;

namespace {

using Color = std::array<uint8_t, 4>;

constexpr Color kBackground = {0x14, 0x1a, 0x2a, 0x80};
constexpr Color kText = {0xe8, 0xe8, 0xe8, 0x80};
constexpr Color kDim = {0x90, 0x98, 0xa8, 0x80};
constexpr Color kAccent = {0xe8, 0xb0, 0x4a, 0x80};
constexpr Color kWarn = {0xe8, 0x60, 0x50, 0x80};
constexpr Color kGood = {0x70, 0xd0, 0x80, 0x80};
constexpr Color kTrack = {0x2c, 0x38, 0x56, 0x80};
constexpr Color kPanel = {0x22, 0x2c, 0x46, 0x80};
constexpr Color kSkyTop = {0x12, 0x0e, 0x28, 0x80};
constexpr Color kSkyBottom = {0x34, 0x26, 0x5c, 0x80};

constexpr int                       kIdleMilliseconds = 50;
constexpr std::chrono::milliseconds kIdle{kIdleMilliseconds};
// What the extracted data takes (1.72 GB) with room for the partial copy's metadata.
constexpr std::uint64_t kNeededBytes = 1'900'000'000;
// Images smaller than this are not the disc (DATA.DAT alone is 1.7 GB).
constexpr std::uint64_t kMinImageBytes = 1'000'000'000;
constexpr const char   *kDiscFolder = "/data/darkcloud";

constexpr float kAdvance = kOverlayAdvance;

float TextWidth(std::string_view text, float scale) {
    return text.empty() ? 0.0f : (static_cast<float>(text.size()) * kAdvance - 1.0f) * scale;
}

// Keeps the end of a path, which names the file, when the whole does not fit.
std::string Fit(std::string_view text, float scale, float width) {
    std::size_t room = static_cast<std::size_t>((width / scale + 1.0f) / kAdvance);
    if (text.size() <= room) {
        return std::string(text);
    }
    return room <= 3 ? std::string() : "..." + std::string(text.substr(text.size() - (room - 3)));
}

std::string Gigabytes(std::uint64_t bytes) {
    return std::format("{:.2f}", static_cast<double>(bytes) / 1e9);
}

std::string MegaBytes(std::uint64_t bytes) {
    return std::format("{:.1f}", static_cast<double>(bytes) / 1e6);
}

// Everything a screen draws, in the game's logical 640x480, as one batch of untextured quads.
// ---- The backdrop -------------------------------------------------------------------------------
//
// A night sky with pixel clouds drifting across it, rain, and now and then a flash of lightning. All of it
// is a function of the time, so there is nothing to keep between frames. It is drawn over the whole width
// of the screen, not just the 640x480 the text is laid out in.

using Clock = std::chrono::steady_clock;
const Clock::time_point sStart = Clock::now();

float Seconds() {
    return std::chrono::duration<float>(Clock::now() - sStart).count();
}

std::uint32_t Hash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// A repeatable number from 0 to 1 for each x.
float Unit(std::uint32_t x) {
    return static_cast<float>(Hash(x) & 0xFFFF) / 65535.0f;
}

struct Quad4 {
    gfx::Vertex2D v[4];
};

gfx::Vertex2D Vtx(float x, float y, const Color &c, std::uint8_t alpha) {
    return gfx::Vertex2D{x, y, 0.0f, 0.0f, 0.0f, {c[0], c[1], c[2], alpha}};
}

void PushRect(std::vector<gfx::Vertex2D> &out, float x, float y, float w, float h, const Color &c, std::uint8_t alpha) {
    out.push_back(Vtx(x, y, c, alpha));
    out.push_back(Vtx(x + w, y, c, alpha));
    out.push_back(Vtx(x + w, y + h, c, alpha));
    out.push_back(Vtx(x, y + h, c, alpha));
}

// A thin slanted stroke from (x, y) to (x + dx, y + dy), `width` wide.
void PushStroke(std::vector<gfx::Vertex2D> &out, float x, float y, float dx, float dy, float width, const Color &c,
                std::uint8_t alpha) {
    out.push_back(Vtx(x - width * 0.5f, y, c, alpha));
    out.push_back(Vtx(x + width * 0.5f, y, c, alpha));
    out.push_back(Vtx(x + dx + width * 0.5f, y + dy, c, alpha));
    out.push_back(Vtx(x + dx - width * 0.5f, y + dy, c, alpha));
}

// Rows of a cloud, each as (first block, blocks wide).
struct CloudRow {
    int first;
    int blocks;
};
constexpr CloudRow kCloudA[] = {{2, 3}, {1, 5}, {0, 7}, {1, 5}};
constexpr CloudRow kCloudB[] = {{1, 2}, {0, 4}, {0, 6}, {1, 3}};
constexpr CloudRow kCloudC[] = {{3, 2}, {1, 6}, {0, 8}, {0, 7}, {2, 4}};

void PushCloud(std::vector<gfx::Vertex2D> &out, float x, float y, float block, int shape, const Color &c,
               std::uint8_t alpha) {
    const CloudRow *rows = shape == 0 ? kCloudA : shape == 1 ? kCloudB : kCloudC;
    const int       count = shape == 0 ? 4 : shape == 1 ? 4 : 5;
    for (int row = 0; row < count; row++) {
        PushRect(out, x + static_cast<float>(rows[row].first) * block, y + static_cast<float>(row) * block,
                 static_cast<float>(rows[row].blocks) * block, block, c, alpha);
    }
}

// How much of the lightning's flash is showing, 0 to 1, and whether the bolt itself is.
struct Strike {
    float         flash = 0.0f;
    bool          bolt = false;
    std::uint32_t cycle = 0;
};

Strike StrikeAt(float t) {
    constexpr float kCycle = 11.0f;
    Strike          strike;
    strike.cycle = static_cast<std::uint32_t>(t / kCycle);
    const float local = t - static_cast<float>(strike.cycle) * kCycle;
    if (Unit(strike.cycle * 7 + 1) > 0.8f) {
        return strike; // no storm this time round
    }
    const float dt = local - (3.0f + Unit(strike.cycle * 7 + 2) * 6.0f);
    if (dt < 0.0f || dt > 0.55f) {
        return strike;
    }
    const float fade = 1.0f - dt / 0.55f;
    strike.flash = fade * fade * 0.45f;
    if (dt > 0.2f && dt < 0.34f) {
        strike.flash = std::max(strike.flash, 0.3f * (1.0f - (dt - 0.2f) / 0.14f));
    }
    strike.bolt = dt < 0.14f || (dt > 0.22f && dt < 0.29f);
    return strike;
}

// A cloud as the game draws it: where it is now, and how far it has drifted across.
struct Cloud {
    float         x = 0.0f;
    float         y = 0.0f;
    float         block = 0.0f;
    int           shape = 0;
    std::uint8_t  alpha = 0;
    std::uint32_t seed = 0;
    float         width = 0.0f;
    float         bottom = 0.0f;
    bool          rains = false;
};

// Draws one rectangle, cut to the part of it inside [left, right], so nothing is ever drawn off screen.
void PushRectClipped(std::vector<gfx::Vertex2D> &out, float x, float y, float w, float h, float left, float right,
                     const Color &c, std::uint8_t alpha) {
    const float x0 = std::max(x, left);
    const float x1 = std::min(x + w, right);
    if (x1 > x0 && alpha > 0) {
        PushRect(out, x0, y, x1 - x0, h, c, alpha);
    }
}

// Draws the whole backdrop; `left` and `right` are the logical x of the screen's edges.
void DrawBackdrop(float left, float right) {
    const float t = Seconds();
    const float span = right - left;
    const gfx::Vertex2D sky[4] = {
        Vtx(left, 0.0f, kSkyTop, 0x80),         Vtx(right, 0.0f, kSkyTop, 0x80),
        Vtx(right, kScreenHeight, kSkyBottom, 0x80), Vtx(left, kScreenHeight, kSkyBottom, 0x80),
    };
    gfx::Draw2D(gfx::Primitive::Quads, std::span<const gfx::Vertex2D>(sky), {}, gfx::DrawState{});

    gfx::DrawState blended;
    blended.blend = true;

    // A bank of clouds across the top of the sky, a far layer behind a nearer, bigger one. Each drifts
    // slowly across and wraps round past the edges; one fades in and out at the edges, so it never
    // shows a cut-off or a smear there.
    std::vector<Cloud> clouds;
    for (int layer = 0; layer < 2; layer++) {
        const float        block = layer == 0 ? 16.0f : 24.0f;
        const float        speed = layer == 0 ? 4.0f : 9.0f;
        const std::uint8_t alpha = layer == 0 ? 0x20 : 0x30;
        const int          count = layer == 0 ? 9 : 8;
        const float        wrap = span + 520.0f;
        for (int i = 0; i < count; i++) {
            Cloud       cloud;
            cloud.seed = static_cast<std::uint32_t>(layer * 50 + i);
            cloud.block = block;
            cloud.shape = static_cast<int>(Unit(cloud.seed * 5 + 3) * 2.99f);
            const int   rows = cloud.shape == 2 ? 5 : 4;
            const int   widest = cloud.shape == 0 ? 7 : cloud.shape == 1 ? 6 : 8;
            cloud.width = static_cast<float>(widest) * block;
            // Spread evenly along the sky, with a little jitter, so they overlap into one bank.
            const float slot = (static_cast<float>(i) + Unit(cloud.seed * 5 + 1) * 0.6f) / static_cast<float>(count);
            cloud.x = left - 260.0f + std::fmod(slot * wrap + t * speed, wrap);
            cloud.y = (layer == 0 ? -4.0f : 6.0f) + Unit(cloud.seed * 5 + 2) * (layer == 0 ? 30.0f : 36.0f);
            cloud.bottom = cloud.y + static_cast<float>(rows) * block - block * 0.4f;
            // Fade towards both edges of the screen.
            const float centre = cloud.x + cloud.width * 0.5f;
            const float edge = std::clamp(std::min(centre - left, right - centre) / 140.0f, 0.0f, 1.0f);
            cloud.alpha = static_cast<std::uint8_t>(static_cast<float>(alpha) * edge);
            cloud.rains = layer == 1 || (i % 2 == 0);
            clouds.push_back(cloud);
        }
    }
    std::vector<gfx::Vertex2D> shapes;
    for (const Cloud &cloud : clouds) {
        const Color     tint = cloud.block < 20.0f ? Color{0x7a, 0x6c, 0xb4, 0x80} : Color{0xa4, 0x98, 0xd6, 0x80};
        const CloudRow *rows = cloud.shape == 0 ? kCloudA : cloud.shape == 1 ? kCloudB : kCloudC;
        const int       count = cloud.shape == 2 ? 5 : 4;
        for (int row = 0; row < count; row++) {
            PushRectClipped(shapes, cloud.x + static_cast<float>(rows[row].first) * cloud.block,
                            cloud.y + static_cast<float>(row) * cloud.block,
                            static_cast<float>(rows[row].blocks) * cloud.block, cloud.block, left, right, tint,
                            cloud.alpha);
        }
    }
    gfx::Draw2D(gfx::Primitive::Quads, shapes, {}, blended);

    // Rain falls out of the clouds: each drop starts at the underside of one, drifts back with the wind,
    // appears as it leaves the cloud and is gone by the bottom of the screen.
    std::vector<gfx::Vertex2D> rain;
    const Color                drop = {0xa6, 0xb8, 0xf0, 0x80};
    for (const Cloud &cloud : clouds) {
        if (!cloud.rains || cloud.alpha == 0) {
            continue;
        }
        for (std::uint32_t j = 0; j < 14; j++) {
            const std::uint32_t key = cloud.seed * 101 + j * 7;
            const float         fall = 260.0f + Unit(key + 1) * 170.0f;
            const float         length = 9.0f + Unit(key + 2) * 7.0f;
            const float         travel = kScreenHeight - cloud.bottom + 30.0f;
            const float         along = std::fmod(Unit(key + 3) * travel + t * fall, travel);
            const float         x = cloud.x + (0.12f + 0.76f * Unit(key + 4)) * cloud.width - along * 0.22f;
            const float         y = cloud.bottom + along;
            if (x < left || x > right) {
                continue;
            }
            const float appear = std::clamp(along / 24.0f, 0.0f, 1.0f);
            const float edge = static_cast<float>(cloud.alpha) / 48.0f;
            PushStroke(rain, x, y, -length * 0.22f, length, 1.2f, drop,
                       static_cast<std::uint8_t>(0x34 * appear * std::min(1.0f, edge)));
        }
    }
    gfx::Draw2D(gfx::Primitive::Quads, rain, {}, blended);

    // Lightning: a flash that fades, and a jagged bolt, out of the cloud bank, for a few frames of it.
    const Strike strike = StrikeAt(t);
    if (strike.flash > 0.0f) {
        std::vector<gfx::Vertex2D> flash;
        PushRect(flash, left, 0.0f, span, kScreenHeight, Color{0xd8, 0xdc, 0xff, 0x80},
                 static_cast<std::uint8_t>(strike.flash * 128.0f));
        gfx::Draw2D(gfx::Primitive::Quads, flash, {}, blended);
    }
    if (strike.bolt) {
        std::vector<gfx::Vertex2D> glow;
        std::vector<gfx::Vertex2D> core;
        float                      x = left + span * (0.2f + 0.6f * Unit(strike.cycle * 7 + 3));
        float                      y = 60.0f;
        for (std::uint32_t segment = 0; segment < 9; segment++) {
            const float dx = (Unit(strike.cycle * 13 + segment) - 0.5f) * 44.0f;
            const float dy = 28.0f + Unit(strike.cycle * 17 + segment) * 14.0f;
            PushStroke(glow, x, y, dx, dy, 12.0f, Color{0xb8, 0xc0, 0xff, 0x80}, 0x20);
            PushStroke(core, x, y, dx, dy, 3.0f, Color{0xff, 0xff, 0xf0, 0x80}, 0x80);
            if (segment == 4) {
                PushStroke(core, x + dx, y + dy, 26.0f, 40.0f, 2.0f, Color{0xff, 0xff, 0xf0, 0x80}, 0x70);
            }
            x += dx;
            y += dy;
        }
        gfx::Draw2D(gfx::Primitive::Quads, glow, {}, blended);
        gfx::Draw2D(gfx::Primitive::Quads, core, {}, blended);
    }
}

// ---- Music while setup runs ---------------------------------------------------------------------
//
// Two 8-bit remixes of the game's music, "Norune Village" and "The Daily Life" by FlameCheese, who lets
// them be used in any project with credit. Which plays first changes from one start to the next; when one
// ends the other follows, and so on. They are stored as 48 kHz Ogg Vorbis in the package (/app0/music).

struct Music {
    std::shared_ptr<audio::PcmTrack> track[2];
    std::atomic<bool>                ready[2] = {false, false};
    std::atomic<float>               target{0.0f}; // the volume it is fading towards
    int                              order[2] = {0, 1};
    int                              playing = 0; // an index into order
    std::int64_t                     position = 0;
    float                            level = 0.0f;
    std::thread                      loader;
} sMusic;

constexpr const char *kMusicFiles[2] = {"/app0/music/norune-village.ogg", "/app0/music/the-daily-life.ogg"};
constexpr float       kMusicVolume = 0.55f;

void LoadMusicTrack(int index) {
    std::string error;
    std::shared_ptr<audio::PcmTrack> loaded = audio::LoadPcmTrack(kMusicFiles[index], error);
    if (!loaded) {
        std::fprintf(stderr, "[setup] music %s: %s\n", kMusicFiles[index], error.c_str());
        return;
    }
    sMusic.track[index] = std::move(loaded);
    sMusic.ready[index] = true;
}

// Called from the audio thread for 48 kHz stereo.
void RenderMusic(void *, float *out, int frames) {
    for (int frame = 0; frame < frames; frame++) {
        sMusic.level += (sMusic.target.load() - sMusic.level) * 0.00004f;
        float left = 0.0f;
        float right = 0.0f;
        int   index = sMusic.order[sMusic.playing];
        if (sMusic.ready[index].load()) {
            const audio::PcmTrack &track = *sMusic.track[index];
            if (sMusic.position >= track.Frames()) {
                // This one has ended: on to the other, if it has been decoded, else start this one again.
                const int next = 1 - sMusic.playing;
                if (sMusic.ready[sMusic.order[next]].load()) {
                    sMusic.playing = next;
                }
                sMusic.position = 0;
            } else {
                const std::int16_t *at = &track.samples[static_cast<std::size_t>(sMusic.position) * 2];
                left = static_cast<float>(at[0]) * (1.0f / 32768.0f) * sMusic.level;
                right = static_cast<float>(at[1]) * (1.0f / 32768.0f) * sMusic.level;
                sMusic.position++;
            }
        }
        out[frame * 2] = left;
        out[frame * 2 + 1] = right;
    }
}

void StartMusic() {
    const std::uint32_t seed = static_cast<std::uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFF);
    const int first = static_cast<int>((seed ^ (seed >> 7) ^ (seed >> 13)) & 1U);
    sMusic.order[0] = first;
    sMusic.order[1] = 1 - first;
    LoadMusicTrack(first);
    sMusic.loader = std::thread([second = 1 - first] { LoadMusicTrack(second); });
    sMusic.target = kMusicVolume;
    if (!AudioOutputStart(48000, RenderMusic, nullptr)) {
        std::fprintf(stderr, "[setup] no audio output for the music\n");
    }
}

// Fades out and hands the audio output back, so the game can open it for itself.
void StopMusic() {
    sMusic.target = 0.0f;
    usleep(900 * 1000);
    AudioOutputStop();
    if (sMusic.loader.joinable()) {
        sMusic.loader.join();
    }
    sMusic.track[0].reset();
    sMusic.track[1].reset();
}

// The Chronicle logo (the two moons), made once from the window icon's pixels.
gfx::TextureHandle sLogo = gfx::kNullTexture;

void MakeLogo() {
    gfx::TextureDesc desc;
    desc.width = kIconSize;
    desc.height = kIconSize;
    sLogo = gfx::CreateTexture(desc);
    if (sLogo != gfx::kNullTexture) {
        gfx::UpdateTextureNow(sLogo, 0, 0, 0, kIconSize, kIconSize, kIconRgba);
    }
}

struct Canvas {
    std::vector<gfx::Vertex2D> quads;

    void Rect(float x, float y, float w, float h, const Color &color) {
        auto vertex = [&](float vx, float vy) {
            return gfx::Vertex2D{vx, vy, 0.0f, 0.0f, 0.0f, {color[0], color[1], color[2], color[3]}};
        };
        quads.push_back(vertex(x, y));
        quads.push_back(vertex(x + w, y));
        quads.push_back(vertex(x + w, y + h));
        quads.push_back(vertex(x, y + h));
    }

    void Text(float x, float y, float scale, std::string_view text, const Color &color) {
        for (char c : text) {
            if (const std::uint8_t *rows = OverlayGlyph(c)) {
                for (int row = 0; row < kOverlayGlyphHeight; row++) {
                    for (int column = 0; column < kOverlayGlyphWidth; column++) {
                        if (rows[row] & (0x10 >> column)) {
                            Rect(x + static_cast<float>(column) * scale, y + static_cast<float>(row) * scale, scale,
                                 scale, color);
                        }
                    }
                }
            }
            x += kAdvance * scale;
        }
    }

    void Centered(float y, float scale, std::string_view text, const Color &color) {
        Text((kScreenWidth - TextWidth(text, scale)) * 0.5f, y, scale, text, color);
    }

    void Present() const {
        if (!gfx::BeginFrame()) {
            return;
        }
        gfx::Clear(true, kBackground.data(), true, 0.0f);
        // The screen's own edges in the logical space: wider than 0 to 640 when the picture fills a 16:9 TV.
        const gfx::LogicalMapping mapping = gfx::GetLogicalMapping(gfx::kMainTarget);
        float                     left = 0.0f;
        float                     right = kScreenWidth;
        if (mapping.scale_x > 0.0f) {
            left = std::min(0.0f, -mapping.offset_x / mapping.scale_x);
            right = std::max(static_cast<float>(kScreenWidth),
                             (static_cast<float>(mapping.pixel_width) - mapping.offset_x) / mapping.scale_x);
        }
        DrawBackdrop(left, right);
        if (!quads.empty()) {
            gfx::Draw2D(gfx::Primitive::Quads, quads, {}, gfx::DrawState{});
        }
        if (sLogo != gfx::kNullTexture) {
            constexpr float kEdge = static_cast<float>(kIconSize);
            constexpr float kX = 12.0f, kY = 6.0f, kSize = 68.0f;
            const uint8_t   white[4] = {0x80, 0x80, 0x80, 0x80};
            auto            vertex = [&](float x, float y, float u, float v) {
                return gfx::Vertex2D{x, y, 0.0f, u, v, {white[0], white[1], white[2], white[3]}};
            };
            const gfx::Vertex2D logo[4] = {vertex(kX, kY, 0, 0), vertex(kX + kSize, kY, kEdge, 0),
                                           vertex(kX + kSize, kY + kSize, kEdge, kEdge), vertex(kX, kY + kSize, 0, kEdge)};
            gfx::TextureBinding binding;
            binding.texture = sLogo;
            gfx::DrawState blended;
            blended.blend = true;
            gfx::Draw2D(gfx::Primitive::Quads, logo, binding, blended);
        }
        gfx::EndFrame();
    }
};

void Header(Canvas &canvas, const char *subtitle) {
    canvas.Centered(24.0f, 3.0f, "CHRONICLE PS4 (DARK CLOUD)", kText);
    canvas.Centered(60.0f, 2.0f, subtitle, kAccent);
}

void Footer(Canvas &canvas, std::string_view text) {
    canvas.Rect(40.0f, 428.0f, 560.0f, 2.0f, kTrack);
    canvas.Centered(442.0f, 2.0f, text, kDim);
}

// ---- Pad ----------------------------------------------------------------------------------------

struct Buttons {
    bool confirm = false;
    bool back = false;
    bool up = false;
    bool down = false;
};

Buttons PollPad() {
    static std::uint32_t before = 0;
    ps4::PadState        state;
    std::uint32_t        now = 0;
    if (ps4::ReadPad(state)) {
        now = state.buttons;
    }
    std::uint32_t pressed = now & ~before;
    before = now;
    Buttons out;
    out.confirm = (pressed & ORBIS_PAD_BUTTON_CROSS) != 0;
    out.back = (pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0;
    out.up = (pressed & ORBIS_PAD_BUTTON_UP) != 0;
    out.down = (pressed & ORBIS_PAD_BUTTON_DOWN) != 0;
    return out;
}

// ---- Finding the disc image -----------------------------------------------------------------------

// ---- USB drives, through the console's own FTP server -----------------------------------------------
//
// The game runs in a sandbox that cannot see USB drives (its /mnt/usb0 does not exist), but GoldHEN's FTP
// server on this console can. So the game asks it, over the loopback address, to list the drives and to
// hand over the disc image, which is copied into the game's own folder and unpacked from there.

struct BsdSockaddrIn { // the FreeBSD layout the kernel expects, whatever the libc's struct looks like
    std::uint8_t  length = 16;
    std::uint8_t  family = 2; // AF_INET
    std::uint16_t port = 0;   // network byte order
    std::uint32_t address = 0;
    char          zero[8] = {};
};

int ConnectLoopback(int port, int timeout_seconds) {
    const int fd = ::socket(2, 1, 0); // AF_INET, SOCK_STREAM
    if (fd < 0) {
        return -1;
    }
    BsdSockaddrIn address;
    address.port = static_cast<std::uint16_t>(((port & 0xFF) << 8) | ((port >> 8) & 0xFF));
    address.address = 0x0100007FU; // 127.0.0.1
    timeval limit{};
    limit.tv_sec = timeout_seconds;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

class LocalFtp {
public:
    struct Entry {
        std::string   name;
        std::uint64_t size = 0;
        bool          directory = false;
    };

    LocalFtp() = default;
    LocalFtp(const LocalFtp &) = delete;
    LocalFtp &operator=(const LocalFtp &) = delete;
    ~LocalFtp() { Close(); }

    bool Open() {
        control_ = ConnectLoopback(2121, 5);
        if (control_ < 0) {
            error_ = std::format("cannot reach 127.0.0.1:2121 (errno {})", errno);
            return false;
        }
        if (ReadReply() != 220) {
            return Fail("no greeting from the FTP server");
        }
        int code = Command("USER anonymous");
        if (code == 331) {
            code = Command("PASS guest@console");
        }
        if (code != 230) {
            return Fail("the FTP server did not accept the login");
        }
        Command("TYPE I");
        return true;
    }

    // MLSD of a folder on the console, as seen from outside the game's sandbox. Empty when it is not there.
    std::vector<Entry> List(const std::string &directory) {
        std::vector<Entry> entries;
        const int          data = OpenPassive();
        if (data < 0) {
            return entries;
        }
        const int code = Command("MLSD " + directory);
        if (code != 150 && code != 125) {
            ::close(data);
            return entries;
        }
        std::string text;
        char        buffer[4096];
        ssize_t     got;
        while ((got = ::recv(data, buffer, sizeof(buffer), 0)) > 0) {
            text.append(buffer, static_cast<std::size_t>(got));
        }
        ::close(data);
        ReadReply();
        std::size_t start = 0;
        while (start < text.size()) {
            std::size_t end = text.find('\n', start);
            end = end == std::string::npos ? text.size() : end;
            std::string line = text.substr(start, end - start);
            start = end + 1;
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const std::size_t space = line.find(' ');
            if (space == std::string::npos) {
                continue;
            }
            const std::string facts = ";" + line.substr(0, space);
            Entry             entry;
            entry.name = line.substr(space + 1);
            const std::size_t type = facts.find(";type=");
            const std::size_t size = facts.find(";size=");
            const std::string kind = type == std::string::npos ? "" : facts.substr(type + 6, facts.find(';', type + 1) - type - 6);
            if (kind == "cdir" || kind == "pdir" || entry.name == "." || entry.name == "..") {
                continue;
            }
            entry.directory = kind == "dir";
            if (size != std::string::npos) {
                entry.size = std::strtoull(facts.c_str() + size + 6, nullptr, 10);
            }
            entries.push_back(entry);
        }
        return entries;
    }

    // Copies a file from the console's file system into `local`. `progress` is told how many bytes are in and
    // returns false to stop; stopped is then set.
    bool Fetch(const std::string &remote, const fs::path &local, std::uint64_t total,
               const std::function<bool(std::uint64_t)> &progress, bool &stopped) {
        stopped = false;
        const int data = OpenPassive();
        if (data < 0) {
            return Fail("could not open a data connection");
        }
        const int code = Command("RETR " + remote);
        if (code != 150 && code != 125) {
            ::close(data);
            return Fail("the FTP server would not send " + remote);
        }
        const int out = ::open(local.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (out < 0) {
            ::close(data);
            return Fail("cannot write " + local.string());
        }
        std::vector<char> buffer(1U << 20);
        std::uint64_t     done = 0;
        bool              finished = true;
        for (;;) {
            const ssize_t got = ::recv(data, buffer.data(), buffer.size(), 0);
            if (got <= 0) {
                break;
            }
            std::size_t written = 0;
            while (written < static_cast<std::size_t>(got)) {
                const ssize_t part = ::write(out, buffer.data() + written, static_cast<std::size_t>(got) - written);
                if (part <= 0) {
                    finished = false;
                    break;
                }
                written += static_cast<std::size_t>(part);
            }
            if (!finished) {
                break;
            }
            done += static_cast<std::uint64_t>(got);
            if (!progress(done)) {
                stopped = true;
                finished = false;
                break;
            }
        }
        ::close(out);
        ::close(data);
        if (stopped) {
            Close(); // the server stops sending when its connection goes
            return false;
        }
        ReadReply();
        if (!finished || done != total) {
            return Fail(std::format("got {} of {} bytes", done, total));
        }
        return true;
    }

    const std::string &Error() const { return error_; }

private:
    void Close() {
        if (control_ >= 0) {
            ::close(control_);
            control_ = -1;
        }
    }

    bool Fail(const std::string &why) {
        error_ = why;
        return false;
    }

    int ReadReply() {
        for (;;) {
            std::size_t start = 0;
            for (;;) {
                const std::size_t end = pending_.find('\n', start);
                if (end == std::string::npos) {
                    break;
                }
                std::string line = pending_.substr(start, end - start);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.size() >= 4 && std::isdigit(static_cast<unsigned char>(line[0])) &&
                    std::isdigit(static_cast<unsigned char>(line[1])) && std::isdigit(static_cast<unsigned char>(line[2])) &&
                    line[3] == ' ') {
                    reply_ = line;
                    pending_.erase(0, end + 1);
                    return std::atoi(line.c_str());
                }
                start = end + 1;
            }
            char          buffer[512];
            const ssize_t got = control_ < 0 ? -1 : ::recv(control_, buffer, sizeof(buffer), 0);
            if (got <= 0) {
                return -1;
            }
            pending_.append(buffer, static_cast<std::size_t>(got));
        }
    }

    int Command(const std::string &line) {
        if (control_ < 0) {
            return -1;
        }
        const std::string text = line + "\r\n";
        std::size_t       sent = 0;
        while (sent < text.size()) {
            const ssize_t part = ::send(control_, text.data() + sent, text.size() - sent, 0);
            if (part <= 0) {
                return -1;
            }
            sent += static_cast<std::size_t>(part);
        }
        return ReadReply();
    }

    // PASV, then a connection to the port it names.
    int OpenPassive() {
        if (Command("PASV") != 227) {
            return -1;
        }
        const std::size_t open = reply_.find('(');
        if (open == std::string::npos) {
            return -1;
        }
        int numbers[6] = {};
        if (std::sscanf(reply_.c_str() + open, "(%d,%d,%d,%d,%d,%d)", &numbers[0], &numbers[1], &numbers[2], &numbers[3],
                        &numbers[4], &numbers[5]) != 6) {
            return -1;
        }
        return ConnectLoopback(numbers[4] * 256 + numbers[5], 20);
    }

    int         control_ = -1;
    std::string reply_;
    std::string pending_;
    std::string error_;
};

struct Image {
    fs::path      path;
    std::uint64_t size = 0;
    bool          settled = false; // the size has not changed since the last look: the copy is done
    bool          grew = false;    // the size changed since the last look: a copy is in progress
    bool          remote = false;  // on a USB drive, which only the console's FTP server can reach
};

// How many large compressed files the last look found: people often keep the disc as a .zip.
int sCompressed = 0;
// Whether the console's FTP server could not be reached on the last look: USB drives need it.
bool sFtpDown = false;

bool IsArchiveName(const fs::path &path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".zip" || extension == ".7z" || extension == ".rar";
}

bool IsImageName(const fs::path &path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".iso" || extension == ".bin" || extension == ".img";
}

// Looks in `folder` and, when `depth` allows, the folders in it, so the image can sit anywhere near the top of a drive.
void ScanFolder(const fs::path &folder, std::vector<Image> &images, int depth = 0) {
    std::error_code error;
    for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        std::error_code entry_error;
        if (it->is_directory(entry_error)) {
            const std::string name = it->path().filename().string();
            // The console and Windows keep their own folders on a drive; nothing of ours is in them.
            if (depth > 0 && !name.starts_with('.') && !name.starts_with('$') && name != "System Volume Information" &&
                name != "PS4" && name != "PS3") {
                ScanFolder(it->path(), images, depth - 1);
            }
            continue;
        }
        if (it->is_regular_file(entry_error) && IsArchiveName(it->path())) {
            std::uintmax_t packed = it->file_size(entry_error);
            sCompressed += (!entry_error && packed >= 300'000'000) ? 1 : 0;
            continue;
        }
        if (!it->is_regular_file(entry_error) || !IsImageName(it->path())) {
            continue;
        }
        std::uintmax_t size = it->file_size(entry_error);
        if (!entry_error && size >= kMinImageBytes) {
            images.push_back({it->path(), size, false, false, false});
        }
    }
}

// A folder called darkcloud, on the console or on any USB drive, is where the image is meant to be: when it
// holds one, nothing else is looked at (a drive can hold a whole game library). Without such a folder the
// drives are searched, their root and the folders two levels down.
// Writes to the boot log what each place a drive can appear holds, whenever that changes.
void LogDrives() {
    static std::string before;
    std::string        now;
    for (const char *dir : {"/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3", "/mnt/usb4", "/mnt/usb5", "/mnt/usb6",
                            "/mnt/usb7", "/mnt/ext0", "/mnt/ext1"}) {
        std::error_code error;
        int             entries = 0;
        for (fs::directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) {
            entries++;
        }
        now += error ? std::format("{}: cannot read ({}); ", dir, error.message())
                     : std::format("{}: {} entries; ", dir, entries);
    }
    if (now != before) {
        before = now;
        std::fprintf(stderr, "[setup] drives: %s\n", now.c_str());
    }
}

void ScanRemote(LocalFtp &ftp, const std::string &directory, std::vector<Image> &images, int depth) {
    for (const LocalFtp::Entry &entry : ftp.List(directory)) {
        const std::string full = directory + "/" + entry.name;
        if (entry.directory) {
            if (depth > 0 && !entry.name.starts_with('.') && !entry.name.starts_with('$') &&
                entry.name != "System Volume Information" && entry.name != "PS4" && entry.name != "PS3") {
                ScanRemote(ftp, full, images, depth - 1);
            }
            continue;
        }
        if (IsArchiveName(full) && entry.size >= 300'000'000) {
            sCompressed++;
        } else if (IsImageName(full) && entry.size >= kMinImageBytes) {
            images.push_back({full, entry.size, false, false, true});
        }
    }
}

std::vector<Image> FindImages(std::map<std::string, std::pair<std::uint64_t, int>> &seen) {
    LogDrives();
    sCompressed = 0;
    std::vector<Image> images;
    ScanFolder(kDiscFolder, images);
    for (int unit = 0; unit < 8; unit++) {
        ScanFolder(fs::path(std::format("/mnt/usb{}", unit)) / "darkcloud", images, 1);
    }
    if (images.empty()) {
        for (int unit = 0; unit < 8; unit++) {
            ScanFolder(std::format("/mnt/usb{}", unit), images, 2);
        }
    }
    // Nothing the game can see: look on the USB drives through the console's FTP server.
    if (images.empty()) {
        static std::string said;
        LocalFtp           ftp;
        sFtpDown = !ftp.Open();
        if (!sFtpDown) {
            for (int unit = 0; unit < 8; unit++) {
                ScanRemote(ftp, std::format("/mnt/usb{}/darkcloud", unit), images, 1);
            }
            if (images.empty()) {
                for (int unit = 0; unit < 8; unit++) {
                    ScanRemote(ftp, std::format("/mnt/usb{}", unit), images, 2);
                }
            }
            const std::string now = std::format("reached, {} disc image(s) on USB", images.size());
            if (now != said) {
                said = now;
                std::fprintf(stderr, "[setup] ftp: %s\n", now.c_str());
            }
        } else if (ftp.Error() != said) {
            said = ftp.Error();
            std::fprintf(stderr, "[setup] ftp: %s\n", said.c_str());
        }
    }
    for (Image &image : images) {
        std::string key = image.path.string();
        auto        found = seen.find(key);
        const bool  same = found != seen.end() && found->second.first == image.size;
        const int   looks = same ? found->second.second + 1 : 0;
        image.settled = looks >= 1;
        image.grew = found != seen.end() && !same;
        seen[key] = {image.size, looks};
    }
    return images;
}

std::uint64_t FreeBytes(fs::path where) {
    // The data directory may not exist yet: ask about the nearest folder that does.
    std::error_code error;
    while (!where.empty() && !fs::exists(where, error)) {
        where = where.parent_path();
    }
    struct statvfs info;
    if (where.empty() || statvfs(where.c_str(), &info) != 0) {
        return UINT64_MAX; // unknown: do not block on it
    }
    return static_cast<std::uint64_t>(info.f_bavail) * static_cast<std::uint64_t>(info.f_frsize);
}

// ---- Screens ------------------------------------------------------------------------------------

// Centred lines of text, one under the other.
void Lines(Canvas &canvas, float y, float step, float scale, std::initializer_list<const char *> lines,
           const Color &color) {
    for (const char *line : lines) {
        canvas.Centered(y, scale, line, color);
        y += step;
    }
}

void DrawWaiting(const std::vector<Image> &images, std::uint64_t free_bytes) {
    Canvas canvas;
    Header(canvas, "WELCOME");
    Lines(canvas, 104.0f, 26.0f, 2.0f,
          {"BEFORE YOU CAN PLAY, THE GAME NEEDS", "YOUR OWN DARK CLOUD DISC IMAGE", "(AN .ISO FILE)."}, kText);
    canvas.Centered(206.0f, 2.0f, "PLUG IN A USB DRIVE WITH THE ISO ON IT.", kAccent);
    canvas.Centered(246.0f, 1.0f, "PUT IT IN A FOLDER CALLED darkcloud. THE DRIVE MUST BE EXFAT OR FAT32.", kDim);
    canvas.Centered(262.0f, 1.0f, "NO USB DRIVE? COPY THE ISO TO /data/darkcloud/ WITH FTP (PORT 2121, BINARY MODE).", kDim);
    if (!images.empty() && images.front().grew) {
        canvas.Centered(318.0f, 2.0f, std::format("COPYING... {} MB", MegaBytes(images.front().size)), kAccent);
    } else if (!images.empty()) {
        canvas.Centered(318.0f, 2.0f, "FOUND A DISC IMAGE, CHECKING IT...", kAccent);
    } else if (sCompressed > 0) {
        canvas.Centered(306.0f, 2.0f, "FOUND A COMPRESSED FILE (.ZIP, .7Z, .RAR).", kWarn);
        canvas.Centered(330.0f, 2.0f, "UNPACK THE .ISO FROM IT FIRST.", kWarn);
    } else {
        canvas.Centered(318.0f, 2.0f, "LOOKING FOR YOUR DISC IMAGE...", kDim);
        if (sFtpDown) {
            canvas.Centered(352.0f, 1.0f, "A USB DRIVE NEEDS GOLDHEN'S FTP SERVER ON: SETTINGS > GOLDHEN > SERVER SETTINGS.", kWarn);
        }
    }
    if (free_bytes < kNeededBytes) {
        canvas.Centered(360.0f, 2.0f, std::format("ONLY {} GB FREE. SETUP NEEDS ABOUT 2 GB.", Gigabytes(free_bytes)), kWarn);
    }
    Footer(canvas, "X  CHECK AGAIN          O  QUIT");
    canvas.Present();
}

// What the disc is, by the fingerprint of its index: the release Chronicle knows it as, or a note that it does not.
struct Release {
    std::string name;
    bool        known = false;
    bool        valid = true; // opens as a Dark Cloud disc: has DATA.DAT and DATA.HD2
};

Release IdentifyDisc(const fs::path &path) {
    try {
        dcdata::Archive             archive = dcdata::OpenArchive(path);
        std::vector<unsigned char>  index = dcdata::ReadExtent(archive.hd2);
        if (const dcdata::Release *release = dcdata::IdentifyRelease(index)) {
            return {std::string(release->name), true};
        }
        return {"UNKNOWN RELEASE", false};
    } catch (const std::exception &) {
        return {"NOT A DARK CLOUD DISC", false, false};
    }
}

void DrawCopying(std::uint64_t done, std::uint64_t total) {
    constexpr float kBarX = 60.0f;
    constexpr float kBarY = 104.0f;
    constexpr float kBarW = 520.0f;
    constexpr float kBarH = 26.0f;
    Canvas          canvas;
    Header(canvas, "STEP 1 OF 2: COPYING FROM THE USB DRIVE");
    const float fraction = total == 0 ? 0.0f : static_cast<float>(static_cast<double>(done) / static_cast<double>(total));
    canvas.Rect(kBarX - 4.0f, kBarY - 4.0f, kBarW + 8.0f, kBarH + 8.0f, kTrack);
    canvas.Rect(kBarX, kBarY, kBarW * std::clamp(fraction, 0.0f, 1.0f), kBarH, kAccent);
    canvas.Centered(146.0f, 2.0f,
                    std::format("{}%    {} / {} MB", static_cast<int>(fraction * 100.0f), MegaBytes(done), MegaBytes(total)),
                    kText);
    canvas.Rect(40.0f, 196.0f, 560.0f, 150.0f, kPanel);
    canvas.Centered(210.0f, 2.0f, "WHY THIS STEP?", kAccent);
    Lines(canvas, 248.0f, 26.0f, 2.0f,
          {"THE GAME CAN'T READ A USB DRIVE ITSELF, SO", "THE DISC IMAGE IS COPIED ACROSS FIRST AND", "DELETED AGAIN ONCE THE GAME IS UNPACKED."},
          kText);
    Footer(canvas, "PLEASE LEAVE THE PS4 ON.        O  STOP");
    canvas.Present();
}

void DrawFound(const std::vector<Image> &images, std::size_t chosen, std::uint64_t free_bytes, std::uint64_t needed,
               const std::string &problem, const Release &release, int seconds_left) {
    Canvas canvas;
    Header(canvas, "READY TO SET UP");
    canvas.Centered(100.0f, 2.0f, images.size() > 1 ? "MORE THAN ONE DISC IMAGE FOUND!" : "DISC IMAGE FOUND!", kText);
    float y = 134.0f;
    for (std::size_t i = 0; i < images.size() && i < 3; i++) {
        if (i == chosen) {
            canvas.Rect(40.0f, y - 6.0f, 560.0f, 32.0f, kPanel);
        }
        canvas.Text(52.0f, y, 2.0f, Fit(images[i].path.string(), 2.0f, 420.0f), i == chosen ? kText : kDim);
        std::string size = std::format("{} GB", Gigabytes(images[i].size));
        canvas.Text(588.0f - TextWidth(size, 2.0f), y, 2.0f, size, kDim);
        y += 34.0f;
    }
    y += 18.0f;
    if (images[chosen].remote) {
        // A USB drive: say what is about to happen, and wait for X.
        const std::string room = std::format("IT NEEDS ABOUT {} GB FREE AND A FEW MINUTES.", Gigabytes(needed));
        Lines(canvas, y, 24.0f, 2.0f,
              {"THE GAME CAN'T READ A USB DRIVE ITSELF, SO", "SETUP COPIES THE DISC IMAGE ACROSS FIRST, THEN",
               "UNPACKS THE GAME FROM IT AND DELETES THE COPY.", room.c_str()},
              kText);
    } else {
        Lines(canvas, y, 24.0f, 2.0f,
              {"SETUP COPIES THE GAME'S FILES FROM YOUR", "DISC IMAGE ONTO THIS PS4. IT TAKES A", "FEW MINUTES."}, kText);
    }
    y += 24.0f * 4.0f + 14.0f;
    if (!release.valid) {
        canvas.Centered(y, 2.0f, "THIS DOES NOT LOOK LIKE A DARK CLOUD DISC", kWarn);
        canvas.Centered(y + 24.0f, 2.0f, "IMAGE. IT MAY BE DAMAGED (FTP MUST BE SET TO", kWarn);
        canvas.Centered(y + 48.0f, 2.0f, "BINARY MODE) OR BE A DIFFERENT GAME.", kWarn);
    }
    if (free_bytes < needed) {
        canvas.Centered(y + (release.valid ? 4.0f : 76.0f), 2.0f,
                        std::format("ONLY {} GB FREE. NEEDS ABOUT {} GB.", Gigabytes(free_bytes), Gigabytes(needed)), kWarn);
    } else if (!problem.empty()) {
        canvas.Centered(y + (release.valid ? 4.0f : 76.0f), 2.0f, Fit(problem, 2.0f, 600.0f), kWarn);
    } else if (seconds_left >= 0) {
        canvas.Centered(y + (release.valid ? 4.0f : 76.0f), 2.0f, "STARTING IN A MOMENT...", kAccent);
    }
    Footer(canvas, images.size() > 1 ? "X  START     UP/DOWN  CHOOSE     O  QUIT" : "X  START NOW          O  QUIT");
    canvas.Present();
}

// Each card is three lines, none longer than 44 characters. Things people who have played the game may not know:
// how it was made, how it sold, how fast it has been beaten.
struct Fact {
    const char *lines[3];
};
constexpr Fact kFacts[] = {
    {{"DARK CLOUD HAS SOLD OVER A MILLION COPIES", "WORLDWIDE. ESTIMATES PUT JAPAN AT ONLY", "ABOUT 80,000 OF THEM."}},
    {{"THE ANY% SPEEDRUN RECORD IS ROUGHLY 14", "MINUTES, THANKS TO A GAME-SKIP GLITCH.", "GLITCHLESS RUNS TAKE ABOUT EIGHT HOURS."}},
    {{"LEVEL-5 IS NAMED AFTER THE TOP MARK ON A", "JAPANESE SCHOOL REPORT CARD. DARK CLOUD", "WAS ITS FIRST GAME."}},
    {{"SONY'S CONDITION FOR PS2 ACCESS WAS A NEW", "COMPANY. LEVEL-5 WAS FOUNDED IN OCTOBER", "1998, AND DARK CLOUD BEGAN THAT MONTH."}},
    {{"SONY SHOWED DARK CLOUD AT THE PS2'S 1999", "ANNOUNCEMENT: A MAGIC CARPET FLYING THROUGH", "A VALLEY. IT NEVER MADE THE FINAL GAME."}},
    {{"LEVEL-5 CALLS THE OVERSEAS VERSION A KIND", "OF 'DARK CLOUD 1.5': IT GOT A SYSTEM", "BUILT FOR THE SEQUEL."}},
    {{"THE OVERSEAS RELEASE ADDED A SWORD GROWTH", "CHART, NEW WEAPONS AND MONSTERS, AND THE", "DEMON SHAFT DUNGEON. JAPAN HAD NONE."}},
    {{"AT LEVEL-5'S START THE TEAM WAS SO SMALL", "ITS FOUNDER HAD TROUBLE SAYING IF THE", "JOB WAS PRESIDENT, DESIGNER OR WRITER."}},
    {{"AN EARLY PLAYABLE BUILD WAS SHOWN AT TOKYO", "GAME SHOW 1999 AND A NEAR-FINAL ONE AT", "TOKYO GAME SHOW 2000."}},
    {{"DARK CLOUD WAS MEANT FOR THE PS2'S LAUNCH.", "JAPAN GOT IT ON 14 DECEMBER 2000, NORTH", "AMERICA ON 29 MAY 2001, EUROPE IN SEPTEMBER."}},
    {{"A DARK CLOUD 3 WAS ANNOUNCED AT E3 2004.", "IN 2017 LEVEL-5'S CEO SAID SONY HOLDS THE", "RIGHTS TOO, SO LEVEL-5 CAN'T ACT ALONE."}},
    {{"TOMOHITO NISHIURA, LEVEL-5'S IN-HOUSE", "COMPOSER, SCORED DARK CLOUD AND ITS", "SEQUEL."}},
};

void DrawProgress(bool second_of_two, const dcdata::Progress &progress) {
    constexpr float kBarX = 60.0f;
    constexpr float kBarY = 104.0f;
    constexpr float kBarW = 520.0f;
    constexpr float kBarH = 26.0f;
    Canvas          canvas;
    Header(canvas, second_of_two ? "STEP 2 OF 2: UNPACKING THE GAME" : "UNPACKING THE GAME");
    float done = progress.total_bytes == 0
                     ? 0.0f
                     : static_cast<float>(static_cast<double>(progress.bytes) / static_cast<double>(progress.total_bytes));
    canvas.Rect(kBarX - 4.0f, kBarY - 4.0f, kBarW + 8.0f, kBarH + 8.0f, kTrack);
    canvas.Rect(kBarX, kBarY, kBarW * std::clamp(done, 0.0f, 1.0f), kBarH, kAccent);
    canvas.Centered(146.0f, 2.0f,
                    std::format("{}%    {} / {} FILES", static_cast<int>(done * 100.0f), progress.files,
                                progress.total_files),
                    kText);
    canvas.Rect(40.0f, 196.0f, 560.0f, 150.0f, kPanel);
    canvas.Centered(210.0f, 2.0f, "DARK CLOUD TRIVIA", kAccent);
    // A new card every nine seconds.
    const std::size_t card = static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                          std::chrono::steady_clock::now().time_since_epoch())
                                                          .count() /
                                                      9) %
                             std::size(kFacts);
    Lines(canvas, 248.0f, 26.0f, 2.0f, {kFacts[card].lines[0], kFacts[card].lines[1], kFacts[card].lines[2]}, kText);
    Footer(canvas, "PLEASE LEAVE THE PS4 ON.        O  STOP");
    canvas.Present();
}

void DrawDone() {
    Canvas canvas;
    Header(canvas, "ALL DONE");
    canvas.Centered(130.0f, 3.0f, "YOUR GAME IS READY", kGood);
    canvas.Centered(216.0f, 2.0f, "PRESS X TO START PLAYING", kText);
    Footer(canvas, "X  START THE GAME");
    canvas.Present();
}

// Breaks text on spaces so no line is wider than `columns` characters.
std::vector<std::string> Wrap(const std::string &text, std::size_t columns) {
    std::vector<std::string> lines;
    std::string              line;
    std::size_t              at = 0;
    while (at < text.size()) {
        std::size_t end = text.find(' ', at);
        end = end == std::string::npos ? text.size() : end;
        std::string word = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.size() + 1 + word.size() > columns) {
            lines.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

void DrawFailed(const std::string &message) {
    Canvas canvas;
    Header(canvas, "SETUP DID NOT FINISH");
    float y = 130.0f;
    for (const std::string &line : Wrap(message, 90)) {
        canvas.Text(40.0f, y, 1.0f, line, kText);
        y += 14.0f;
        if (y > 380.0f) {
            break;
        }
    }
    canvas.Centered(390.0f, 2.0f, "THE BOOT LOG IS IN /data/darkcloud/ps4_boot.log", kDim);
    Footer(canvas, "O  QUIT");
    canvas.Present();
}

fs::path Partial(const fs::path &root) {
    fs::path clean = root.has_filename() ? root : root.parent_path();
    fs::path partial = clean;
    partial += ".partial";
    return partial;
}

[[noreturn]] void Leave() {
    DarkCloudPs4Quit();
    for (;;) {
        sleep(1);
    }
}

// Waits on the failure screen for O.
[[noreturn]] void Fail(const std::string &message) {
    std::fprintf(stderr, "[setup] %s\n", message.c_str());
    for (;;) {
        if (PollPad().back) {
            Leave();
        }
        DrawFailed(message);
        usleep(kIdleMilliseconds * 1000);
    }
}

// ---- Data from an earlier setup ------------------------------------------------------------------
//
// An earlier build of setup left out the files the extractor derives from what is on the disc (every dungeon's
// script and the other languages' title screens): the console's text formatting dropped the zero in "d01" and
// wrote letters as numbers. Those files can all be rebuilt from the data already here, so a start that finds the
// data without a version note from a setup that got it right finishes the job first and then starts the game.

constexpr const char *kSetupMarker = ".setup-version";
constexpr int         kSetupVersion = 2;

bool SetupCurrent(const fs::path &root) {
    std::FILE *file = std::fopen((root / kSetupMarker).c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    int version = 0;
    const bool read = std::fscanf(file, "%d", &version) == 1;
    std::fclose(file);
    return read && version >= kSetupVersion;
}

void MarkSetup(const fs::path &root) {
    if (std::FILE *file = std::fopen((root / kSetupMarker).c_str(), "wb")) {
        std::fprintf(file, "%d\n", kSetupVersion);
        std::fclose(file);
    }
}

bool RepairData(const fs::path &root, std::string &error) {
    try {
        const fs::path index = root / "data.hd2";
        if (!fs::is_regular_file(index)) {
            return true; // not made by setup: nothing to rebuild it from
        }
        std::vector<unsigned char>  hd2 = dcdata::ReadExtent(dcdata::Extent{index, 0, fs::file_size(index)});
        std::vector<dcdata::Record> records = dcdata::ParseIndex(hd2);
        dcdata::Normalize(root, records);
        // The title screens the old setup wrote under the wrong names (the letters as numbers).
        for (const char *wrong : {"101", "102", "103", "105", "115"}) {
            std::error_code ignored;
            fs::remove(root / "titledat" / (std::string("title_") + wrong + ".pak"), ignored);
        }
        MarkSetup(root);
        return true;
    } catch (const std::exception &failure) {
        error = failure.what();
        return false;
    }
}

void DrawUpdating() {
    Canvas canvas;
    Header(canvas, "UPDATING YOUR GAME FILES");
    Lines(canvas, 120.0f, 26.0f, 2.0f,
          {"A FEW FILES FROM AN EARLIER SETUP WERE", "MISSING. THEY ARE BEING REBUILT FROM THE", "FILES ALREADY ON THIS PS4."}, kText);
    canvas.Centered(240.0f, 2.0f, "THE GAME STARTS WHEN THIS IS DONE.", kAccent);
    Footer(canvas, "PLEASE LEAVE THE PS4 ON.");
    canvas.Present();
}

// The setup flow, with the renderer already up. Returns once the data is extracted, for the game to
// start; ends the process when the person quits or setup fails.
void RunSetup(const fs::path &root) {
    std::map<std::string, std::pair<std::uint64_t, int>> seen;
    std::map<std::string, Release>                       releases;
    // An image on a USB drive cannot be opened until it has been copied: it is taken to be what it is called.
    auto identify = [](const Image &image) {
        return image.remote ? Release{"", false, true} : IdentifyDisc(image.path);
    };
    const std::uint64_t                  free_bytes = FreeBytes(root);
    std::vector<Image>                   images;
    std::size_t                          chosen = 0;
    std::string                          problem;
    auto                                 last_scan = std::chrono::steady_clock::time_point{};
    auto                                 found_since = std::chrono::steady_clock::time_point{};
    const auto                           started = std::chrono::steady_clock::now();
    std::fprintf(stderr, "[setup] no game data in %s; waiting for a disc image\n", PathsDisplay(root).c_str());
    {
        std::error_code ignored;
        fs::remove(root.parent_path() / "setup-copy.part", ignored);
    }

    for (;;) {
        Buttons buttons = PollPad();
        auto    now = std::chrono::steady_clock::now();
        // Looks again every two seconds, and at once when X is pressed on the waiting screen.
        const bool          looking = !images.empty() && std::none_of(images.begin(), images.end(), [](const Image &i) { return i.settled; });
        const auto          interval = looking ? std::chrono::seconds(1) : std::chrono::seconds(2);
        if (now - last_scan >= interval || last_scan == std::chrono::steady_clock::time_point{} || (buttons.confirm && images.empty())) {
            last_scan = now;
            images = FindImages(seen);
            chosen = std::min(chosen, images.empty() ? std::size_t{0} : images.size() - 1);
        }
        if (buttons.back) {
            Leave();
        }
        // An image whose size is still changing is being copied: wait until it settles.
        std::vector<Image> ready;
        for (const Image &image : images) {
            if (image.settled) {
                ready.push_back(image);
            }
        }
        // However quickly the disc turns up, the first screen stays long enough to read.
        if (now - started < std::chrono::seconds(3)) {
            ready.clear();
        }
        if (ready.empty()) {
            DrawWaiting(images, free_bytes);
            usleep(kIdleMilliseconds * 1000);
            continue;
        }
        // Only Dark Cloud discs are offered: anything else that is big and called .iso is left alone. When
        // none is one, the first is shown with what is wrong with it.
        {
            std::vector<Image> real;
            for (const Image &image : ready) {
                const std::string name = image.path.string();
                auto              seen_release = releases.find(name);
                if (seen_release == releases.end()) {
                    seen_release = releases.emplace(name, identify(image)).first;
                }
                if (seen_release->second.valid) {
                    real.push_back(image);
                }
            }
            if (!real.empty()) {
                ready = real;
            }
        }
        chosen = std::min(chosen, ready.size() - 1);
        if (buttons.up && chosen > 0) {
            chosen--;
            problem.clear();
        }
        if (buttons.down && chosen + 1 < ready.size()) {
            chosen++;
            problem.clear();
        }
        const std::string key = ready[chosen].path.string();
        auto              known = releases.find(key);
        if (known == releases.end()) {
            known = releases.emplace(key, identify(ready[chosen])).first;
        }
        // An image on a USB drive is copied to the console first, so it needs room for itself as well.
        const std::uint64_t needed = kNeededBytes + (ready[chosen].remote ? ready[chosen].size : 0);
        // One Dark Cloud disc and room for the data: setup starts by itself after a
        // few seconds, so the person who plugged the drive in has nothing to do. X starts it at once.
        constexpr int kAutoStartSeconds = 5;
        const bool    automatic = ready.size() == 1 && known->second.valid && free_bytes >= needed && problem.empty();
        if (!automatic) {
            found_since = std::chrono::steady_clock::time_point{};
        } else if (found_since == std::chrono::steady_clock::time_point{}) {
            found_since = now;
        }
        int seconds_left = -1;
        if (automatic) {
            seconds_left = std::max(0, kAutoStartSeconds - static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
                                                                                now - found_since).count()));
        }
        if ((buttons.confirm || seconds_left == 0) && free_bytes >= needed && known->second.valid) {
            fs::path       source = ready[chosen].path;
            const bool     two_steps = ready[chosen].remote; // a copy from the drive, then the unpacking
            const fs::path copy_path = root.parent_path() / "setup-copy.part";
            bool           copied = false;
            auto           cleanup = [&] {
                std::error_code ignored;
                if (copied) {
                    fs::remove(copy_path, ignored);
                }
            };
            if (ready[chosen].remote) {
                // First the image comes off the USB drive, through the console's FTP server.
                std::fprintf(stderr, "[setup] copying %s to %s\n", source.c_str(), copy_path.c_str());
                LocalFtp ftp;
                bool     stopped = false;
                bool     ok = ftp.Open();
                auto     drawn = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                if (ok) {
                    copied = true;
                    ok = ftp.Fetch(source.string(), copy_path, ready[chosen].size,
                                   [&](std::uint64_t done) {
                                       const auto at = std::chrono::steady_clock::now();
                                       if (at - drawn >= std::chrono::milliseconds(150)) {
                                           drawn = at;
                                           DrawCopying(done, ready[chosen].size);
                                       }
                                       return !PollPad().back;
                                   },
                                   stopped);
                }
                if (stopped) {
                    cleanup();
                    Leave();
                }
                if (!ok) {
                    std::fprintf(stderr, "[setup] copy failed: %s\n", ftp.Error().c_str());
                    cleanup();
                    problem = "COULD NOT COPY FROM THE USB DRIVE. IS GOLDHEN'S FTP SERVER ON?";
                    continue;
                }
                source = copy_path;
            }
            try {
                dcdata::Archive archive = dcdata::OpenArchive(source);
                (void)archive;
            } catch (const std::exception &error) {
                problem = "NOT A DARK CLOUD DISC IMAGE: " + std::string(error.what());
                std::fprintf(stderr, "[setup] %s: %s\n", source.c_str(), error.what());
                cleanup();
                continue;
            }
            std::fprintf(stderr, "[setup] extracting %s into %s\n", source.c_str(), PathsDisplay(root).c_str());
            FirstRunOutcome outcome = FirstRunExtract(source, root, [&](const dcdata::Progress &progress) {
                DrawProgress(two_steps, progress);
                return !PollPad().back;
            });
            cleanup();
            if (outcome.ok) {
                MarkSetup(root);
                std::fprintf(stderr, "[setup] extracted %s into %s\n", source.c_str(), PathsDisplay(root).c_str());
                // The data is in place, so if the game cannot start from here, starting it again does.
                for (;;) {
                    if (PollPad().confirm) {
                        return;
                    }
                    DrawDone();
                    usleep(kIdleMilliseconds * 1000);
                }
            }
            if (outcome.cancelled) {
                std::fprintf(stderr, "[setup] stopped; %s holds what was written so far\n",
                             PathsDisplay(Partial(root)).c_str());
                Leave();
            }
            Fail("EXTRACTING " + source.string() + " FAILED: " + outcome.error);
        }
        DrawFound(ready, chosen, free_bytes, needed, problem, known->second, seconds_left);
        usleep(kIdleMilliseconds * 1000);
    }
}

} // namespace

bool FirstRunDataMissing(const fs::path &root) {
    std::error_code error;
    if (!fs::is_directory(root, error)) {
        return true;
    }
    auto options = fs::directory_options::follow_directory_symlink | fs::directory_options::skip_permission_denied;
    for (fs::recursive_directory_iterator it(root, options, error), end; !error && it != end; it.increment(error)) {
        std::error_code entry_error;
        if (it->is_regular_file(entry_error)) {
            return false;
        }
    }
    return true;
}

FirstRunOutcome FirstRunExtract(const fs::path &source, const fs::path &root,
                                const std::function<bool(const dcdata::Progress &)> &tick) {
    // A symlinked data directory is filled in place: replacing it would drop the link.
    std::error_code         error;
    bool                    in_place = fs::is_symlink(root.has_filename() ? root : root.parent_path(), error);
    fs::path                target = in_place ? root : Partial(root);
    std::mutex              mutex;
    dcdata::Progress        latest;
    bool                    finished = false;
    std::atomic<bool>       cancel = false;
    FirstRunOutcome         outcome;

    std::thread worker([&] {
        try {
            dcdata::Archive archive = dcdata::OpenArchive(source);
            dcdata::Extract(archive, target, nullptr, [&](const dcdata::Progress &progress) {
                {
                    std::lock_guard lock(mutex);
                    latest = progress;
                }
                return !cancel.load();
            });
            std::vector<unsigned char>          hd2 = dcdata::ReadExtent(archive.hd2);
            std::vector<dcdata::Record>         records = dcdata::ParseIndex(hd2);
            std::vector<const dcdata::Record *> bad = dcdata::Mismatched(records, target);
            if (!bad.empty()) {
                dcdata::Fail("{} files are missing or the wrong size after extraction, {} among them", bad.size(),
                             bad.front()->path);
            }
            if (!in_place) {
                // The data check found root missing or without a file, so anything there is empty directories.
                if (fs::exists(root)) {
                    if (!FirstRunDataMissing(root)) {
                        dcdata::Fail("{} gained files during the extraction; the extracted data is in {}",
                                     PathsDisplay(root), PathsDisplay(target));
                    }
                    fs::remove_all(root);
                }
                fs::rename(target, root);
            }
            outcome.ok = true;
        } catch (const std::exception &failure) {
            outcome.error = failure.what();
        }
        {
            std::lock_guard lock(mutex);
            finished = true;
        }
    });

    for (;;) {
        dcdata::Progress snapshot;
        bool             done;
        {
            std::lock_guard lock(mutex);
            snapshot = latest;
            done = finished;
        }
        if (done) {
            break;
        }
        if (!tick(snapshot)) {
            cancel = true;
        }
        usleep(16 * 1000);
    }
    worker.join();
    outcome.cancelled = !outcome.ok && cancel.load();
    return outcome;
}

void FirstRunSetChooser(FirstRunChooser) {}

namespace {

// The window and renderer for the screens setup draws, and their end.
void BeginScreens() {
    WindowConfig window;
    WindowInit(window);
    gfx::RendererConfig renderer;
    renderer.app_name = "Dark Cloud";
    renderer.logical_width = kScreenWidth;
    renderer.logical_height = kScreenHeight;
    renderer.display_aspect = 4.0f / 3.0f;
    renderer.pipeline_cache = PathsSaveRoot() / "pipeline_cache.bin";
    // Widescreen: the 4:3 text stays in the middle and the backdrop fills the whole TV.
    renderer.layout = {gfx::AspectMode::Fill, 1.0f};
    gfx::RendererInit(WindowHandle(), renderer);
    MakeLogo();
}

void EndScreens() {
    if (sLogo != gfx::kNullTexture) {
        gfx::DestroyTexture(sLogo);
        sLogo = gfx::kNullTexture;
    }
    gfx::RendererShutdown();
    WindowShutdown();
}

} // namespace

void FirstRunIfNoData(bool) {
    const fs::path &root = PathsDataRoot();
    if (!FirstRunDataMissing(root)) {
        if (!SetupCurrent(root)) {
            std::fprintf(stderr, "[setup] the data in %s is from an earlier setup: completing it\n", PathsDisplay(root).c_str());
            BeginScreens();
            std::atomic<bool> finished = false;
            bool              ok = false;
            std::string       error;
            std::thread       worker([&] {
                ok = RepairData(root, error);
                finished = true;
            });
            while (!finished) {
                DrawUpdating();
                usleep(16 * 1000);
            }
            worker.join();
            std::fprintf(stderr, ok ? "[setup] the game files are complete\n" : "[setup] could not complete them: %s\n",
                         error.c_str());
            EndScreens();
        }
        return;
    }
    BeginScreens();
    StartMusic();
    RunSetup(root);
    StopMusic();
    EndScreens();
    std::fprintf(stderr, "[setup] done, starting the game\n");
}

#endif // __PS4__
