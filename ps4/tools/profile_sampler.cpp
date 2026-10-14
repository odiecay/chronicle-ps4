// A sampling profiler for Windows builds, for finding where the game's logic spends its time (the console
// cannot be profiled: a signal sent to a thread there kills the process). With GFX_SAMPLE=1 in the
// environment, a thread suspends the thread that started the process about a thousand times a second, notes where
// it was executing, and at exit writes the addresses (relative to the executable's base) to the file named by
// GFX_SAMPLE_OUT. Without the variable it does nothing at all.

#if defined(_WIN32)

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {

struct Sampler {
    HANDLE                 target = nullptr;
    std::thread            worker;
    std::atomic<bool>      running{false};
    std::vector<uint64_t>  samples;
    uintptr_t              base = 0;

    Sampler() {
        if (std::getenv("GFX_SAMPLE") == nullptr) {
            return;
        }
        // The thread that runs the static initialisers is the one that goes on to run main.
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target, 0, FALSE,
                             DUPLICATE_SAME_ACCESS)) {
            return;
        }
        base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        samples.reserve(1 << 20);
        running = true;
        worker = std::thread([this] {
            HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            LARGE_INTEGER due;
            due.QuadPart = -5000; // 0.5 ms
            while (running) {
                if (timer != nullptr) {
                    SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
                    WaitForSingleObject(timer, 10);
                } else {
                    Sleep(1);
                }
                if (SuspendThread(target) == static_cast<DWORD>(-1)) {
                    continue;
                }
                CONTEXT context = {};
                context.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(target, &context)) {
                    samples.push_back(static_cast<uint64_t>(context.Rip));
                }
                ResumeThread(target);
            }
        });
    }

    ~Sampler() {
        if (!running) {
            return;
        }
        running = false;
        worker.join();
        const char *path = std::getenv("GFX_SAMPLE_OUT");
        std::FILE  *out = std::fopen(path != nullptr ? path : "samples.txt", "w");
        if (out != nullptr) {
            std::fprintf(out, "base %llx\n", static_cast<unsigned long long>(base));
            for (uint64_t address : samples) {
                std::fprintf(out, "%llx\n", static_cast<unsigned long long>(address));
            }
            std::fclose(out);
        }
        std::fprintf(stderr, "sampled %zu times\n", samples.size());
    }
} g_sampler;

} // namespace

#endif // _WIN32
