// PS4 process entry point. Adapted from alechurri's Ship of Harkinian PS4 port (MIT, see
// ps4/LICENSE-ps4-port): a boot log, a big-stack game thread and an orderly return to the home screen.

#ifdef __PS4__

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <orbis/SystemService.h>
#include <orbis/libkernel.h>

#include "ps4_platform.hpp"

// port/src/main.cpp's main, renamed by the build (-Dmain=DarkCloudMain on that unit).
int DarkCloudMain(int argc, const char **argv, const char **envp);
extern "C" void Ps4Heap_GetStats(size_t *arenaSize, size_t *inUse, size_t *peak);

namespace {

// Everything the game reads and writes lives here: the extracted game data (made on a PC with
// `dcdata extract` and copied over, see ps4/README.md), saves, config and logs.
constexpr const char *kDataDirectory = "/data/darkcloud";
// The decompiled game code recurses deeper than the default main thread stack is comfortable with.
constexpr size_t kGameThreadStackSize = 16 * 1024 * 1024;
constexpr const char *kBootLog = "/data/darkcloud/ps4_boot.log";
int sBootLogFd = -1;

void BootLogRaw(const char *text) {
    if (sBootLogFd >= 0) {
        write(sBootLogFd, text, strlen(text));
        fsync(sBootLogFd);
    }
}

void InstallBootLog() {
    sBootLogFd = open(kBootLog, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (sBootLogFd >= 0) {
        // The same descriptor for both streams keeps the order of the lines.
        dup2(sBootLogFd, STDOUT_FILENO);
        dup2(sBootLogFd, STDERR_FILENO);
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
}

void *GameThread(void *) {
    BootLogRaw("[PS4] game thread started\n");
    static const char *argv[] = {"darkcloud", nullptr};
    static const char *envp[] = {nullptr};
    int status = DarkCloudMain(1, argv, envp);
    char line[80];
    snprintf(line, sizeof(line), "[PS4] the game returned %d\n", status);
    BootLogRaw(line);
    return nullptr;
}

// A fatal signal in the game ends the process the way the kernel does; this only says which one came first.
void FatalSignal(int signal_number) {
    char line[64];
    snprintf(line, sizeof(line), "[PS4] FATAL: signal %d\n", signal_number);
    BootLogRaw(line);
    signal(signal_number, SIG_DFL);
    raise(signal_number);
}

static const char kDefaultConfig[] =
    "{\n"
    "    \"game\": {\n"
    "        \"tick_rate\": 60.0,\n"
    "        \"debug_mode\": false,\n"
    "        \"qte_always_win\": false,\n"
    "        \"element_quick_select\": false,\n"
    "        \"language\": \"ask\",\n"
    "        \"save_cursor_position\": true,\n"
    "        \"message_speed\": \"normal\",\n"
    "        \"clock\": true,\n"
    "        \"time_speed\": \"normal\",\n"
    "        \"map\": 2,\n"
    "        \"enemy_damage\": true,\n"
    "        \"player_damage\": true,\n"
    "        \"enemy_hp\": true,\n"
    "        \"names\": true\n"
    "    },\n"
    "    \"video\": {\n"
    "        \"text_font\": \"sharp\",\n"
    "        \"text_shadow\": 50,\n"
    "        \"glyph_shadow\": 25,\n"
    "        \"name_shadow\": 50,\n"
    "        \"floor_shadow\": 50,\n"
    "        \"boss_shadow\": 50,\n"
    "        \"present_mode\": \"mailbox\",\n"
    "        \"interpolation\": true,\n"
    "        \"max_fps\": 60.0,\n"
    "        \"width\": 1920,\n"
    "        \"height\": 1080,\n"
    "        \"fullscreen\": true,\n"
    "        \"aspect\": \"auto\",\n"
    "        \"ui_scale\": 1.0,\n"
    "        \"show_fps\": false,\n"
    "        \"fps_detail\": \"all\",\n"
    "        \"detail_distance\": 0.0,\n"
    "        \"shadow_distance\": 0.0,\n"
    "        \"anisotropy\": 0,\n"
    "        \"soft_focus\": false\n"
    "    },\n"
    "    \"audio\": {\n"
    "        \"master_volume\": 1.0,\n"
    "        \"sound\": \"stereo\",\n"
    "        \"soundtrack\": \"ps2\"\n"
    "    },\n"
    "    \"input\": {\n"
    "        \"mouse_sensitivity\": 0.1,\n"
    "        \"stick_sensitivity\": 1.33,\n"
    "        \"stick_invert_x\": false,\n"
    "        \"stick_invert_y\": false,\n"
    "        \"gyro\": \"held\",\n"
    "        \"gyro_sensitivity\": 0.5,\n"
    "        \"gyro_invert_x\": false,\n"
    "        \"gyro_invert_y\": false,\n"
    "        \"mouse_invert_y\": false,\n"
    "        \"mouse_capture\": true,\n"
    "        \"mouse_zoom\": false,\n"
    "        \"touchpad\": true,\n"
    "        \"touchpad_sensitivity\": 1.0,\n"
    "        \"lightbar\": true,\n"
    "        \"rumble_strength\": 1.0,\n"
    "        \"glyphs\": \"new\",\n"
    "        \"glyph_device\": \"auto\",\n"
    "        \"mouse_camera_return\": 0.2,\n"
    "        \"mouse_release\": [],\n"
    "        \"vibration\": true,\n"
    "        \"bindings\": {}\n"
    "    },\n"
    "    \"discord\": {\n"
    "        \"rich_presence\": true\n"
    "    }\n"
    "}\n";

void ReturnToSystem() {
    size_t arena = 0;
    size_t in_use = 0;
    size_t peak = 0;
    Ps4Heap_GetStats(&arena, &in_use, &peak);
    printf("[PS4] heap arena %zu MiB, in use %zu MiB, peak %zu MiB\n", arena >> 20, in_use >> 20, peak >> 20);
    fflush(stdout);
    // The orderly way for an application to quit back to the home screen.
    sceSystemServiceLoadExec("exit", nullptr);
}

} // namespace

extern "C" void DarkCloudPs4Quit() {
    ReturnToSystem();
    _Exit(0);
}

int main(int, char **) {
    if (mkdir(kDataDirectory, 0777) != 0 && errno != EEXIST) {
        printf("[PS4] could not create %s: %s\n", kDataDirectory, strerror(errno));
    }
    if (chdir(kDataDirectory) != 0) {
        printf("[PS4] could not change directory to %s: %s\n", kDataDirectory, strerror(errno));
    }
    InstallBootLog();
    BootLogRaw("[PS4] Chronicle PS4 (Dark Cloud) experimental boot\n");

    // Where paths.cpp looks for the game data and the saves: the console's /data, or a USB drive
    // holding darkcloud/data (1.7 GB, so copying it to a stick is the quick way).
    const char *data_root = "/data/darkcloud/data";
    for (const char *usb : {"/mnt/usb0/darkcloud/data", "/mnt/usb1/darkcloud/data"}) {
        if (access(usb, F_OK) == 0) {
            data_root = usb;
            break;
        }
    }
    printf("[PS4] game data: %s\n", data_root);
    setenv("DC_DATA", data_root, 1);
    setenv("DC_SAVE", "/data/darkcloud/save", 1);
    // The Sony and Take 5 logos are skipped and the game starts at the title. An empty file
    // /data/darkcloud/ps4_show_intro plays them. (A dungeon crash once blamed on skipping them was the overlapping
    // sprintf, fixed since; the skip has been in every test run since.)
    if (access("/data/darkcloud/ps4_show_intro", F_OK) != 0) {
        setenv("DC_SKIP_INTRO", "1", 1);
    }
    mkdir("/data/darkcloud/save", 0777);
    // The first run writes the settings this port is tuned for: soft focus off (with it on, the heavy cinematics and
    // dungeons ran at 12 to 20 ticks a second instead of 60), mailbox presentation, a 60 fps cap, 1080p. A config.json
    // that is already there is left alone, and every setting stays changeable from the game's options menu.
    if (access("/data/darkcloud/save/config.json", F_OK) != 0) {
        if (FILE *file = fopen("/data/darkcloud/save/config.json", "wb")) {
            fwrite(kDefaultConfig, 1, sizeof(kDefaultConfig) - 1, file);
            fclose(file);
            printf("[PS4] wrote the default config.json\n");
        }
    }
    // The retail loading screens hold for about 6.6 s (400 ticks), and the location captions that come up with them
    // run on the same timer. The holds are cut to a few ticks (so the captions flash past); an empty file
    // /data/darkcloud/ps4_retail_loading keeps retail's holds and captions.
    // An empty file /data/darkcloud/ps4_depth16 gives the scene the 16-bit depth buffer the driver otherwise left it
    // with (it has no 24-bit format): only for comparing what shadows look like at 16 bits and at 32.
    if (access("/data/darkcloud/ps4_depth16", F_OK) == 0) {
        setenv("GFX_FORCE_DEPTH16", "1", 1);
    }
    if (access("/data/darkcloud/ps4_retail_loading", F_OK) != 0) {
        setenv("DC_FAST_LOAD", "1", 1);
    }

    for (int number : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) {
        signal(number, FatalSignal);
    }
    atexit(ReturnToSystem);

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, kGameThreadStackSize);
    pthread_t thread;
    const int result = pthread_create(&thread, &attributes, GameThread, nullptr);
    pthread_attr_destroy(&attributes);
    if (result != 0) {
        printf("[PS4] could not create the game thread (%d), running on the main thread\n", result);
        GameThread(nullptr);
    } else {
        pthread_join(thread, nullptr);
    }
    return 0;
}

#endif // __PS4__
