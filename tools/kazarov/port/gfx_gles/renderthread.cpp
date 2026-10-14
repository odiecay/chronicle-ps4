#include <thread>
#ifdef __PS4__
#include <pthread.h>
#endif

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>

#include "context.hpp"

// The render thread of the ES 2 backend. The game thread records a tick's display list while this thread
// replays the previous tick's, so a tick costs the longer of the two instead of their sum.
//
// The GL context belongs to one thread at a time. The game thread starts out with it; submitting a job hands
// it to the render thread, which gives it back when it has nothing left to do. Any call that is not just
// recording (creating a texture, an immediate frame, a read-back) first drains the thread and takes the
// context back: ApiBarrier.

namespace gfx::detail {

bool g_replaying_game = false;
bool g_replaying_render = false;
bool g_threaded = false;
bool              g_canonical_submitted = false;

namespace {

std::thread::id g_render_id;
bool            g_render_id_set = false;

std::mutex                        g_mutex;
std::condition_variable           g_work;
std::condition_variable           g_idle;
std::deque<std::function<void()>> g_queue;
int                               g_outstanding = 0; // queued plus running
bool                              g_stop = false;
bool                              g_game_owns = true;
bool                              g_render_owns = false; // touched by the render thread only
#ifdef __PS4__
pthread_t g_thread;
#else
std::thread g_thread;
#endif
bool                              g_started = false;
bool                              g_probe_done = false;
bool                              g_probe_ok = false;

void *Main(void *) {
    g_render_id = std::this_thread::get_id();
    g_render_id_set = true;
    {
        // Can this thread make the context current at all? Say so before the game depends on it.
        const bool ok = HostMakeCurrent(true);
        if (ok) {
            HostMakeCurrent(false);
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        g_probe_ok = ok;
        g_probe_done = true;
        g_idle.notify_all();
        if (!ok) {
            return nullptr;
        }
    }
    std::unique_lock<std::mutex> lock(g_mutex);
    for (;;) {
        g_work.wait(lock, [] { return g_stop || !g_queue.empty(); });
        if (g_queue.empty()) {
            if (g_stop) {
                break;
            }
            continue;
        }
        std::function<void()> job = std::move(g_queue.front());
        g_queue.pop_front();
        lock.unlock();
        if (!g_render_owns) {
            HostMakeCurrent(true);
            g_render_owns = true;
        }
        job();
        lock.lock();
        const bool last = g_outstanding == 1 && g_queue.empty();
        lock.unlock();
        if (last) {
            // Nothing is waiting: the game thread may need the context before the next job.
            HostMakeCurrent(false);
            g_render_owns = false;
        }
        lock.lock();
        g_outstanding--;
        g_idle.notify_all();
    }
    lock.unlock();
    if (g_render_owns) {
        HostMakeCurrent(false);
        g_render_owns = false;
    }
    return nullptr;
}

void JoinThread() {
#ifdef __PS4__
    pthread_join(g_thread, nullptr);
#else
    g_thread.join();
#endif
}

} // namespace

bool OnRenderThread() { return g_render_id_set && std::this_thread::get_id() == g_render_id; }

bool RenderThreadStart() {
    if (g_started || !HostRenderThreadWanted()) {
        return false;
    }
    // The game thread lets go of the context so the render thread can try it.
    HostMakeCurrent(false);
    g_game_owns = false;
    g_stop = false;
    g_probe_done = false;
#ifdef __PS4__
    // The console's default thread stack is small.
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 8u << 20);
    const int created = pthread_create(&g_thread, &attributes, Main, nullptr);
    pthread_attr_destroy(&attributes);
#else
    int created = 0;
    try {
        g_thread = std::thread([] { Main(nullptr); });
    } catch (...) {
        created = 1;
    }
#endif
    if (created != 0) {
        HostMakeCurrent(true);
        g_game_owns = true;
        std::fprintf(stderr, "gfx: render thread: could not start it (%d); rendering on the game thread\n", created);
        return false;
    }
    g_started = true;
    {
        std::unique_lock<std::mutex> lock(g_mutex);
        g_idle.wait(lock, [] { return g_probe_done; });
    }
    if (!g_probe_ok) {
        JoinThread();
        g_started = false;
        HostMakeCurrent(true);
        g_game_owns = true;
        std::fprintf(stderr, "gfx: render thread: the context cannot be made current there; rendering on the game thread\n");
        return false;
    }
    // The probe let go of the context again: the game thread starts as its owner.
    HostMakeCurrent(true);
    g_game_owns = true;
    g_threaded = true;
    std::fprintf(stderr, "gfx: rendering on its own thread\n");
    return true;
}

void RenderThreadStop() {
    if (!g_started) {
        return;
    }
    ApiBarrier();
    g_threaded = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_stop = true;
        g_work.notify_all();
    }
    JoinThread();
    g_started = false;
    if (!g_game_owns) {
        HostMakeCurrent(true);
        g_game_owns = true;
    }
}

void RenderSubmit(std::function<void()> job) {
    std::unique_lock<std::mutex> lock(g_mutex);
    // One tick ahead at most: wait here while the previous tick's jobs are still running.
    g_idle.wait(lock, [] { return g_outstanding < 2; });
    if (g_game_owns) {
        lock.unlock();
        HostMakeCurrent(false);
        lock.lock();
        g_game_owns = false;
    }
    g_queue.push_back(std::move(job));
    g_outstanding++;
    g_work.notify_one();
}

void ApiBarrier() {
    if (!g_threaded || OnRenderThread()) {
        return;
    }
    std::unique_lock<std::mutex> lock(g_mutex);
    g_idle.wait(lock, [] { return g_outstanding == 0; });
    if (!g_game_owns) {
        lock.unlock();
        HostMakeCurrent(true);
        lock.lock();
        g_game_owns = true;
    }
}

} // namespace gfx::detail
