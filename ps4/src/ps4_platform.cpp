// PS4 platform layer. The Piglet setup, the memory budgeting and the pad handling are adapted from
// alechurri's Ship of Harkinian PS4 port (MIT, see ps4/LICENSE-ps4-port), where they were worked out
// on real consoles; the notes in comments about what the hardware does come from that work.

#ifdef __PS4__

#include "ps4_platform.hpp"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <type_traits>

#include <orbis/Pad.h>
// Pigletv2VSH.h defines __PIGLET__ and then includes EGL: that order gives OrbisPglWindow.
#include <orbis/Pigletv2VSH.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

extern "C" int  Ps4Heap_GetKind(void) __attribute__((weak));
extern "C" void Ps4Heap_GetStats(size_t *arenaSize, size_t *inUse, size_t *peak) __attribute__((weak));

namespace ps4 {

void Log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    fputc('\n', stdout);
    fflush(stdout);
}

// ---- Modules --------------------------------------------------------------------------------------

// Retail firmware ships Piglet without its runtime shader compiler. Like other ES homebrew we load a
// Piglet and shader compiler pair from this directory (the same pair the other PS4 ports use).
static constexpr const char *kUserModuleDir = "/data/self/system/common/lib";
// A package may carry the pair itself, which makes it self-contained.
static constexpr const char *kBundledModuleDir = "/app0/assets/misc";

static int32_t sPigletModule = -1;
static int32_t sShaderCompilerModule = -1;

static int32_t LoadModule(const std::string &path) {
    int32_t start_result = 0;
    Log("[PS4] loading module \"%s\"", path.c_str());
    int32_t handle = static_cast<int32_t>(sceKernelLoadStartModule(path.c_str(), 0, nullptr, 0, nullptr, &start_result));
    if (handle < 0) {
        Log("[PS4] sceKernelLoadStartModule(\"%s\") failed: 0x%08X", path.c_str(), static_cast<uint32_t>(handle));
    } else {
        Log("[PS4] loaded module \"%s\" (handle %d, module_start %d)", path.c_str(), handle, start_result);
    }
    return handle;
}

static std::string SystemModuleDir() {
    const char *word = sceKernelGetFsSandboxRandomWord();
    return std::string("/") + (word != nullptr ? word : "system") + "/common/lib";
}

static bool LoadGraphicsModules() {
    if (sPigletModule >= 0) {
        return true;
    }
    const std::string system_dir = SystemModuleDir();
    static const char *const kSystemModules[] = {
        "libSceSysCore", "libSceMbus", "libSceIpmi", "libSceSystemService", "libSceUserService", "libSceAudioOut",
        "libScePad",
    };
    for (const char *name : kSystemModules) {
        LoadModule(system_dir + "/" + name + ".sprx");
    }

    // Both modules have to come from the same place: they are a matched pair.
    const char *module_dir = kUserModuleDir;
    if (access((std::string(kBundledModuleDir) + "/libScePigletv2VSH.sprx").c_str(), F_OK) == 0) {
        sPigletModule = LoadModule(std::string(kBundledModuleDir) + "/libScePigletv2VSH.sprx");
        if (sPigletModule >= 0) {
            module_dir = kBundledModuleDir;
        }
    }
    if (sPigletModule < 0) {
        sPigletModule = LoadModule(std::string(kUserModuleDir) + "/libScePigletv2VSH.sprx");
    }
    if (sPigletModule < 0) {
        Log("[PS4] no Piglet in %s, falling back to the system one (shader compilation will most likely not work)",
            kUserModuleDir);
        sPigletModule = LoadModule(system_dir + "/libScePigletv2VSH.sprx");
    }
    if (sPigletModule < 0) {
        Log("[PS4] could not load libScePigletv2VSH.sprx");
        return false;
    }
    sShaderCompilerModule = LoadModule(std::string(module_dir) + "/libSceShaccVSH.sprx");
    if (sShaderCompilerModule < 0) {
        sShaderCompilerModule = LoadModule(system_dir + "/libSceShaccVSH.sprx");
    }
    if (sShaderCompilerModule < 0) {
        Log("[PS4] could not load libSceShaccVSH.sprx (the GLSL compiler). Copy libScePigletv2VSH.sprx and "
            "libSceShaccVSH.sprx to %s/",
            kUserModuleDir);
    }
    return true;
}

// ---- Memory log -----------------------------------------------------------------------------------

static size_t LargestFreeDirectBlock() {
    typedef int32_t (*AvailableDirectFn)(off_t, off_t, size_t, off_t *, size_t *);
    off_t  physical_address = 0;
    size_t direct_size = 0;
    ((AvailableDirectFn)(void *)sceKernelAvailableDirectMemorySize)(0, (off_t)sceKernelGetDirectMemorySize(), 0,
                                                                    &physical_address, &direct_size);
    return direct_size;
}

void LogMemoryStats(const char *when) {
    // The prototypes in the OpenOrbis header declare out parameters by value, hence the casts.
    typedef int32_t (*AvailableFlexibleFn)(size_t *);
    size_t flexible = 0;
    ((AvailableFlexibleFn)(void *)sceKernelAvailableFlexibleMemorySize)(&flexible);
    size_t arena = 0;
    size_t in_use = 0;
    size_t peak = 0;
    int    kind = 0;
    if (Ps4Heap_GetStats != nullptr && Ps4Heap_GetKind != nullptr) {
        Ps4Heap_GetStats(&arena, &in_use, &peak);
        kind = Ps4Heap_GetKind();
    }
    Log("[PS4] memory %s: heap %zu MiB used / %zu MiB peak of %zu MiB (%s), free direct block %zu MiB, free flexible "
        "%zu MiB",
        when, in_use >> 20, peak >> 20, arena >> 20,
        kind == 1 ? "system flexible" : kind == 2 ? "direct" : "no arena", LargestFreeDirectBlock() >> 20,
        flexible >> 20);
}

// ---- EGL / Piglet ---------------------------------------------------------------------------------

static EGLDisplay sDisplay = EGL_NO_DISPLAY;
static EGLSurface sSurface = EGL_NO_SURFACE;
static EGLContext sContext = EGL_NO_CONTEXT;

struct PigletSizes {
    uint64_t systemShared;
    uint64_t videoShared;
    uint64_t flexible; // 0: do not let Piglet use flexible memory at all
    uint32_t drawCommandBuffer;
    uint32_t lcueResourceBuffer;
    uint64_t neededDirect; // only tried when this much direct memory is free in one block
};

// Tried in this order until eglGetDisplay() succeeds. The application has 768 MiB of direct memory and
// about 255 MiB of flexible memory; Piglet takes its "shared" memory from the former and, when allowed
// to, textures and render targets from the latter. The first entries need the game's heap outside of
// direct memory (ps4_heap.c); the last are the split verified on a console with the heap inside it.
static const PigletSizes kPigletCandidates[] = {
    {128ull << 20, 512ull << 20, 236ull << 20, 4u << 20, 4u << 20, 700ull << 20},
    {128ull << 20, 512ull << 20, 208ull << 20, 4u << 20, 4u << 20, 700ull << 20},
    {128ull << 20, 512ull << 20, 208ull << 20, 1u << 20, 1u << 20, 700ull << 20},
    {64ull << 20, 128ull << 20, 208ull << 20, 1u << 20, 1u << 20, 0},
    {64ull << 20, 128ull << 20, 176ull << 20, 1u << 20, 1u << 20, 0},
    {64ull << 20, 128ull << 20, 64ull << 20, 1u << 20, 1u << 20, 0},
};

static bool ConfigurePiglet(int width, int height, const PigletSizes &sizes) {
    OrbisPglConfig config;
    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.flags = ORBIS_PGL_FLAGS_USE_COMPOSITE_EXT | 0x60;
    if (sizes.flexible != 0) {
        config.flags |= ORBIS_PGL_FLAGS_USE_FLEXIBLE_MEMORY;
    }
    config.processOrder = 1;
    config.systemSharedMemorySize = sizes.systemShared;
    config.videoSharedMemorySize = sizes.videoShared;
    config.maxMappedFlexibleMemory = sizes.flexible;
    config.drawCommandBufferSize = sizes.drawCommandBuffer;
    config.lcueResourceBufferSize = sizes.lcueResourceBuffer;
    config.dbgPosCmd_0x40 = width;
    config.dbgPosCmd_0x44 = height;
    config.dbgPosCmd_0x48 = 0;
    config.dbgPosCmd_0x4C = 0;
    config.unk_0x5C = 2;
    Log("[PS4] scePigletSetConfigurationVSH: video %llu MiB, system %llu MiB, flexible %llu MiB, command buffer %u MiB",
        static_cast<unsigned long long>(sizes.videoShared >> 20), static_cast<unsigned long long>(sizes.systemShared >> 20),
        static_cast<unsigned long long>(sizes.flexible >> 20), sizes.drawCommandBuffer >> 20);
    if (!scePigletSetConfigurationVSH(&config)) {
        Log("[PS4] scePigletSetConfigurationVSH rejected that configuration");
        return false;
    }
    return true;
}

static bool ChooseConfig(EGLConfig *config) {
    // Preferred: 24 bit depth and stencil. The last entry is what the known-good homebrew uses. The
    // window surface's own depth is never drawn to (the renderer works in its own targets), so any works.
    static const EGLint kDepthStencil[][2] = {{24, 8}, {24, 0}, {16, 0}, {0, 0}};
    for (const auto &ds : kDepthStencil) {
        const EGLint attribs[] = {
            EGL_RED_SIZE,        8,
            EGL_GREEN_SIZE,      8,
            EGL_BLUE_SIZE,       8,
            EGL_ALPHA_SIZE,      8,
            EGL_DEPTH_SIZE,      ds[0],
            EGL_STENCIL_SIZE,    ds[1],
            EGL_SAMPLE_BUFFERS,  0,
            EGL_SAMPLES,         0,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
            EGL_NONE,
        };
        EGLint count = 0;
        if (eglChooseConfig(sDisplay, attribs, config, 1, &count) && count >= 1) {
            Log("[PS4] EGL config: requested depth %d / stencil %d", ds[0], ds[1]);
            return true;
        }
        Log("[PS4] eglChooseConfig(depth %d, stencil %d) failed: 0x%04X", ds[0], ds[1], static_cast<unsigned>(eglGetError()));
    }
    return false;
}

bool InitGraphics(int width, int height) {
    if (sContext != EGL_NO_CONTEXT) {
        return true;
    }
    Log("[PS4] InitGraphics: loading modules");
    if (!LoadGraphicsModules()) {
        return false;
    }
    LogMemoryStats("before Piglet");
    for (const PigletSizes &sizes : kPigletCandidates) {
        if (LargestFreeDirectBlock() < sizes.neededDirect) {
            continue;
        }
        if (!ConfigurePiglet(width, height, sizes)) {
            continue;
        }
        Log("[PS4] eglGetDisplay");
        sDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (sDisplay != EGL_NO_DISPLAY) {
            break;
        }
        Log("[PS4] eglGetDisplay failed with that configuration");
        LogMemoryStats("after the failed attempt");
    }
    if (sDisplay == EGL_NO_DISPLAY) {
        Log("[PS4] eglGetDisplay failed with every Piglet configuration");
        return false;
    }
    EGLint major = 0;
    EGLint minor = 0;
    Log("[PS4] eglInitialize");
    if (!eglInitialize(sDisplay, &major, &minor)) {
        Log("[PS4] eglInitialize failed: 0x%04X", static_cast<unsigned>(eglGetError()));
        return false;
    }
    Log("[PS4] EGL %d.%d", major, minor);
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        Log("[PS4] eglBindAPI failed: 0x%04X", static_cast<unsigned>(eglGetError()));
        return false;
    }
    // Swap interval 1 made eglSwapBuffers block forever on a console, and the game paces its own frames
    // by its clock: Piglet stays at interval 0, set once here like other Piglet homebrew does.
    if (!eglSwapInterval(sDisplay, 0)) {
        Log("[PS4] eglSwapInterval failed: 0x%04X", static_cast<unsigned>(eglGetError()));
    }
    EGLConfig config = nullptr;
    if (!ChooseConfig(&config)) {
        Log("[PS4] no usable EGL configuration");
        return false;
    }
    OrbisPglWindow window;
    memset(&window, 0, sizeof(window));
    window.uID = 0;
    window.uWidth = static_cast<uint32_t>(width);
    window.uHeight = static_cast<uint32_t>(height);
    const EGLint window_attribs[] = {EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE};
    Log("[PS4] eglCreateWindowSurface");
    sSurface = eglCreateWindowSurface(sDisplay, config, &window, window_attribs);
    if (sSurface == EGL_NO_SURFACE) {
        Log("[PS4] eglCreateWindowSurface failed: 0x%04X", static_cast<unsigned>(eglGetError()));
        return false;
    }
    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    Log("[PS4] eglCreateContext");
    sContext = eglCreateContext(sDisplay, config, EGL_NO_CONTEXT, context_attribs);
    if (sContext == EGL_NO_CONTEXT) {
        Log("[PS4] eglCreateContext failed: 0x%04X", static_cast<unsigned>(eglGetError()));
        return false;
    }
    Log("[PS4] eglMakeCurrent");
    if (!eglMakeCurrent(sDisplay, sSurface, sSurface, sContext)) {
        Log("[PS4] eglMakeCurrent failed: 0x%04X", static_cast<unsigned>(eglGetError()));
        return false;
    }
    Log("[PS4] ES 2 context ready (%dx%d)", width, height);
    return true;
}

void *GetProc(const char *name) { return reinterpret_cast<void *>(eglGetProcAddress(name)); }

void SwapBuffers() {
    if (sDisplay != EGL_NO_DISPLAY && sSurface != EGL_NO_SURFACE) {
        eglSwapBuffers(sDisplay, sSurface);
    }
}

bool MakeCurrent(bool on) {
    if (sDisplay == EGL_NO_DISPLAY) {
        return false;
    }
    const EGLBoolean ok = on ? eglMakeCurrent(sDisplay, sSurface, sSurface, sContext)
                             : eglMakeCurrent(sDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (!ok) {
        Log("[PS4] eglMakeCurrent(%s) failed: 0x%04X", on ? "bind" : "release", static_cast<unsigned>(eglGetError()));
    }
    return ok == EGL_TRUE;
}

void ShutdownGraphics() {
    if (sDisplay == EGL_NO_DISPLAY) {
        return;
    }
    eglMakeCurrent(sDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (sSurface != EGL_NO_SURFACE) {
        eglDestroySurface(sDisplay, sSurface);
        sSurface = EGL_NO_SURFACE;
    }
    if (sContext != EGL_NO_CONTEXT) {
        eglDestroyContext(sDisplay, sContext);
        sContext = EGL_NO_CONTEXT;
    }
    eglTerminate(sDisplay);
    sDisplay = EGL_NO_DISPLAY;
}

// The system keeps showing the boot splash until the application says it is alive.
void HideSplashScreen() { sceSystemServiceHideSplashScreen(); }

// ---- User and pad ---------------------------------------------------------------------------------

static bool    sUserServiceReady = false;
static int32_t sUserId = -1;

int32_t GetInitialUserId() {
    if (!sUserServiceReady) {
        // 700 is SCE_KERNEL_PRIO_FIFO_DEFAULT. Fails harmlessly if the service is already up.
        struct {
            int32_t priority;
        } params = {700};
        sceUserServiceInitialize(&params);
        if (sceUserServiceGetInitialUser(&sUserId) < 0) {
            Log("[PS4] sceUserServiceGetInitialUser failed");
            sUserId = -1;
        }
        sUserServiceReady = true;
    }
    return sUserId;
}

static int32_t sPadHandle = -1;
static uint64_t sPadNextTry = 0;

// The pad functions, all taken from one loaded copy of libScePad: calling scePadInit on the copy the
// executable imports and the privilege or open calls on another copy reports "not initialized" (0x80920005).
struct PadApi {
    int32_t (*init)(void) = nullptr;
    int32_t (*open)(int32_t, int32_t, int32_t, void *) = nullptr;
    int32_t (*get_handle)(int32_t, int32_t, int32_t) = nullptr;
    int32_t (*read_state)(int32_t, OrbisPadData *) = nullptr;
    int32_t (*set_vibration)(int32_t, const OrbisPadVibeParam *) = nullptr;
    int32_t (*set_light_bar)(int32_t, OrbisPadColor *) = nullptr;
    int32_t (*set_privilege)(int32_t) = nullptr;
};
static PadApi sPad;
static bool   sPadLoaded = false;

static void LoadPadApi() {
    if (sPadLoaded) {
        return;
    }
    sPadLoaded = true;
    const std::string path = SystemModuleDir() + "/libScePad.sprx";
    const int32_t     module = sceKernelLoadStartModule(path.c_str(), 0, nullptr, 0, nullptr, nullptr);
    Log("[PS4] libScePad module handle %d", module);
    auto find = [&](const char *name, auto &out, auto fallback) {
        void *address = nullptr;
        if (module >= 0 && sceKernelDlsym(module, name, &address) == 0 && address != nullptr) {
            out = reinterpret_cast<std::remove_reference_t<decltype(out)>>(address);
        } else {
            out = reinterpret_cast<std::remove_reference_t<decltype(out)>>(fallback);
            Log("[PS4] %s: using the imported copy", name);
        }
    };
    find("scePadInit", sPad.init, &scePadInit);
    find("scePadOpen", sPad.open, &scePadOpen);
    find("scePadGetHandle", sPad.get_handle, &scePadGetHandle);
    find("scePadReadState", sPad.read_state, &scePadReadState);
    find("scePadSetVibration", sPad.set_vibration, &scePadSetVibration);
    find("scePadSetLightBar", sPad.set_light_bar, &scePadSetLightBar);
    void *privilege = nullptr;
    if (module >= 0 && sceKernelDlsym(module, "scePadSetProcessPrivilege", &privilege) == 0) {
        sPad.set_privilege = reinterpret_cast<int32_t (*)(int32_t)>(privilege);
    }
    Log("[PS4] scePadInit -> 0x%08X", static_cast<uint32_t>(sPad.init()));
    if (sPad.set_privilege != nullptr) {
        Log("[PS4] scePadSetProcessPrivilege -> 0x%08X", static_cast<uint32_t>(sPad.set_privilege(1)));
    }
}

static void OpenPad() {
    LoadPadApi();
    // Which user the controller belongs to is not obvious from here: try the initial user, the foreground
    // user, the logged-in users and the system user in turn.
    int32_t candidates[8];
    int     count = 0;
    candidates[count++] = GetInitialUserId();
    int32_t foreground = -1;
    if (sceUserServiceGetForegroundUser(&foreground) >= 0 && foreground != candidates[0]) {
        candidates[count++] = foreground;
    }
    OrbisUserServiceLoginUserIdList logins;
    memset(&logins, 0, sizeof(logins));
    if (sceUserServiceGetLoginUserIdList(&logins) >= 0) {
        for (int i = 0; i < 4 && count < 7; i++) {
            if (logins.userId[i] > 0 && logins.userId[i] != candidates[0] && logins.userId[i] != foreground) {
                candidates[count++] = logins.userId[i];
            }
        }
    }
    candidates[count++] = 0xFF;
    for (int i = 0; i < count; i++) {
        if (candidates[i] < 0) {
            continue;
        }
        int32_t handle = sPad.open(candidates[i], ORBIS_PAD_PORT_TYPE_STANDARD, 0, nullptr);
        if (handle < 0) {
            const int32_t open_error = handle;
            // Already opened by somebody else in this process.
            handle = sPad.get_handle(candidates[i], ORBIS_PAD_PORT_TYPE_STANDARD, 0);
            Log("[PS4] pad for user %d: scePadOpen 0x%08X, scePadGetHandle 0x%08X", candidates[i],
                static_cast<uint32_t>(open_error), static_cast<uint32_t>(handle));
        } else {
            Log("[PS4] pad for user %d: open (handle %d)", candidates[i], handle);
        }
        if (handle >= 0) {
            sPadHandle = handle;
            return;
        }
    }
}

bool ReadPad(PadState &state) {
    if (sPadHandle < 0) {
        // The controller may appear after the game starts: ask again every few seconds.
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        const uint64_t seconds = static_cast<uint64_t>(now.tv_sec);
        if (seconds >= sPadNextTry) {
            sPadNextTry = seconds + 5;
            OpenPad();
        }
        if (sPadHandle < 0) {
            return false;
        }
    }
    OrbisPadData data;
    memset(&data, 0, sizeof(data));
    static bool logged = false;
    const int32_t result = sPad.read_state(sPadHandle, &data);
    if (!logged) {
        logged = true;
        Log("[PS4] first scePadReadState -> 0x%08X, connected %d, buttons 0x%08X", static_cast<uint32_t>(result),
            data.connected, data.buttons);
    }
    if (result < 0 || !data.connected) {
        state = PadState{};
        return false;
    }
    state.connected = true;
    state.buttons = data.buttons;
    state.left_x = data.leftStick.x;
    state.left_y = data.leftStick.y;
    state.right_x = data.rightStick.x;
    state.right_y = data.rightStick.y;
    state.l2 = data.analogButtons.l2;
    state.r2 = data.analogButtons.r2;
    for (int i = 0; i < 2; i++) {
        state.touch_down[i] = data.touch.fingers > i;
        state.touch_x[i] = data.touch.touch[i].x;
        state.touch_y[i] = data.touch.touch[i].y;
    }
    state.accel[0] = data.acell.x;
    state.accel[1] = data.acell.y;
    state.accel[2] = data.acell.z;
    state.gyro[0] = data.vel.x;
    state.gyro[1] = data.vel.y;
    state.gyro[2] = data.vel.z;
    return true;
}

void SetPadVibration(uint8_t large, uint8_t small) {
    if (sPadHandle < 0) {
        return;
    }
    OrbisPadVibeParam vibration;
    vibration.lgMotor = large;
    vibration.smMotor = small;
    sPad.set_vibration(sPadHandle, &vibration);
}

void SetPadLight(uint8_t r, uint8_t g, uint8_t b) {
    if (sPadHandle < 0) {
        return;
    }
    OrbisPadColor color;
    color.r = r;
    color.g = g;
    color.b = b;
    color.a = 255;
    sPad.set_light_bar(sPadHandle, &color);
}

} // namespace ps4

#endif // __PS4__
