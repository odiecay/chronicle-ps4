#include <eekernel.h>

#include "threads.hpp"

#include "platform/clock.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <map>
#include <memory>

// PS2 threads are fibers on the main OS thread. The game's threads are cooperative and the window, SDL and
// the Vulkan queue must stay on one thread, so no OS thread is ever created.

#if defined(_WIN32)
// windows.h declares a CreateThread that clashes with the PS2 one in this unit, so the four fiber calls are
// declared here. They are kernel32 exports with one calling convention on x64 and arm64.
extern "C" {
void *ConvertThreadToFiber(void *parameter);
void *CreateFiber(std::size_t stack_size, void (*start)(void *), void *parameter);
void  SwitchToFiber(void *fiber);
void  DeleteFiber(void *fiber);
}
#else
#include <ucontext.h>
#endif

namespace {

constexpr std::size_t kStackSize = 1024 * 1024;
constexpr int         kMainThreadId = 1;

enum class State { Dormant, Ready, Running, Waiting };

void RunEntry();

#if defined(_WIN32)

struct Context {
    void *fiber = nullptr;
};

void FiberStart(void *) {
    RunEntry();
}

void ContextInitMain(Context &c) {
    c.fiber = ConvertThreadToFiber(nullptr);
    if (c.fiber == nullptr) {
        std::fprintf(stderr, "eekernel: ConvertThreadToFiber failed\n");
        std::abort();
    }
}

void ContextCreate(Context &c) {
    c.fiber = CreateFiber(kStackSize, FiberStart, nullptr);
    if (c.fiber == nullptr) {
        std::fprintf(stderr, "eekernel: CreateFiber failed\n");
        std::abort();
    }
}

void ContextSwitch(Context & /*prev*/, Context &next) {
    SwitchToFiber(next.fiber);
}

void ContextDestroy(Context &c) {
    if (c.fiber != nullptr) {
        DeleteFiber(c.fiber);
        c.fiber = nullptr;
    }
}

#else

struct Context {
    ucontext_t uc{};
    void      *stack = nullptr;
};

void ContextInitMain(Context &) {}

void ContextCreate(Context &c) {
    c.stack = std::malloc(kStackSize);
    if (c.stack == nullptr) {
        std::fprintf(stderr, "eekernel: out of memory for a thread stack\n");
        std::abort();
    }
    getcontext(&c.uc);
    c.uc.uc_stack.ss_sp = c.stack;
    c.uc.uc_stack.ss_size = kStackSize;
    c.uc.uc_link = nullptr;
    makecontext(&c.uc, RunEntry, 0);
}

void ContextSwitch(Context &prev, Context &next) {
    swapcontext(&prev.uc, &next.uc);
}

void ContextDestroy(Context &c) {
    std::free(c.stack);
    c.stack = nullptr;
}

#endif

struct Thread {
    int             id = 0;
    int             priority = 0;
    State           state = State::Dormant;
    void (*entry)(void *) = nullptr;
    void           *arg = nullptr;
    int             waiting_on = 0;
    Context         context;
};

struct Sema {
    int                  count = 0;
    int                  max = 0;
    std::deque<Thread *> waiters;
};

struct Scheduler {
    std::map<int, std::unique_ptr<Thread>> threads;
    // Keyed by priority; a lower number outranks a higher one, so begin() is always the next thread to run.
    std::map<int, std::deque<Thread *>> ready;
    std::map<int, Sema>                 semas;
    Thread                             *running = nullptr;
    int                                 next_thread_id = kMainThreadId + 1;
    int                                 next_sema_id = 1;
};

void EnsureMain(Scheduler &s) {
    auto main = std::make_unique<Thread>();
    main->id = kMainThreadId;
    main->priority = 1;
    main->state = State::Running;
    ContextInitMain(main->context);
    s.running = main.get();
    s.threads.emplace(kMainThreadId, std::move(main));
}

// Every entry point goes through here, so the main thread exists before the first call from any side.
Scheduler &Sched() {
    static Scheduler scheduler;
    if (scheduler.running == nullptr) {
        EnsureMain(scheduler);
    }
    return scheduler;
}

Thread *FindThread(int id) {
    auto &threads = Sched().threads;
    auto  it = threads.find(id);
    return it == threads.end() ? nullptr : it->second.get();
}

Sema *FindSema(int id) {
    auto &semas = Sched().semas;
    auto  it = semas.find(id);
    return it == semas.end() ? nullptr : &it->second;
}

void Enqueue(Scheduler &s, Thread *t, bool front) {
    t->state = State::Ready;
    std::deque<Thread *> &queue = s.ready[t->priority];
    if (front) {
        queue.push_front(t);
    } else {
        queue.push_back(t);
    }
}

void Unqueue(Scheduler &s, Thread *t) {
    for (auto it = s.ready.begin(); it != s.ready.end();) {
        std::erase(it->second, t);
        it = it->second.empty() ? s.ready.erase(it) : std::next(it);
    }
}

Thread *PopHighest(Scheduler &s) {
    if (s.ready.empty()) {
        return nullptr;
    }
    auto    it = s.ready.begin();
    Thread *t = it->second.front();
    it->second.pop_front();
    if (it->second.empty()) {
        s.ready.erase(it);
    }
    return t;
}

// Hands the processor to `next`, which the caller has already taken off the ready queues. Returns when the
// caller is switched back to.
void Run(Scheduler &s, Thread *next) {
    Thread *prev = s.running;
    next->state = State::Running;
    s.running = next;
    if (next != prev) {
        // A switch inside ClockPump would leave its guard set on this stack and run host code on another one.
        if (ClockPumping()) {
            std::fprintf(stderr, "eekernel_threads: thread switch inside ClockPump (a tick callback or pump hook called a switching kernel call)\n");
            std::abort();
        }
        ContextSwitch(prev->context, next->context);
    }
}

// The body of every created thread's context. A thread that returns is dormant and never resumed, so its
// context is left suspended here until DeleteThread or a restart frees it.
void RunEntry() {
    Scheduler &s = Sched();
    Thread    *self = s.running;
    self->entry(self->arg);

    self->state = State::Dormant;
    Thread *next = PopHighest(s);
    if (next == nullptr) {
        std::fprintf(stderr, "eekernel: deadlock: thread %d exited with no ready thread\n", self->id);
        std::abort();
    }
    Run(s, next);
}

} // namespace

void KzThreadsSetMainPriority(int priority) {
    Scheduler &s = Sched();
    Thread    *main = FindThread(kMainThreadId);
    const bool queued = main->state == State::Ready;
    if (queued) {
        Unqueue(s, main);
    }
    main->priority = priority;
    if (queued) {
        Enqueue(s, main, false);
    }
}

int CreateThread(struct ThreadParam *param) {
    Scheduler &s = Sched();
    if (param == nullptr || param->entry == nullptr) {
        return -1;
    }
    auto t = std::make_unique<Thread>();
    t->id = s.next_thread_id++;
    t->priority = param->initPriority;
    t->entry = param->entry;
    const int id = t->id;
    s.threads.emplace(id, std::move(t));
    return id;
}

int StartThread(int thread_id, void *arg) {
    Scheduler &s = Sched();
    Thread    *t = FindThread(thread_id);
    if (t == nullptr || t->state != State::Dormant) {
        return -1;
    }
    t->arg = arg;
    // A dormant thread that ran before keeps its suspended context; a restart replaces it.
    ContextDestroy(t->context);
    ContextCreate(t->context);
    Enqueue(s, t, false);

    Thread *cur = s.running;
    if (t->priority < cur->priority) {
        Unqueue(s, t);
        Enqueue(s, cur, true);
        Run(s, t);
    }
    return thread_id;
}

int RotateThreadReadyQueue(int priority) {
    Scheduler &s = Sched();
    Thread    *cur = s.running;
    if (cur->priority == priority) {
        Enqueue(s, cur, false);
        Run(s, PopHighest(s));
        return 0;
    }
    // The running thread outranks this queue, so only its order changes.
    auto it = s.ready.find(priority);
    if (it != s.ready.end()) {
        it->second.push_back(it->second.front());
        it->second.pop_front();
    }
    return 0;
}

int TerminateThread(int thread_id) {
    Scheduler &s = Sched();
    Thread    *t = FindThread(thread_id);
    if (t == nullptr || t == s.running) {
        return -1;
    }
    if (t->state == State::Ready) {
        Unqueue(s, t);
    } else if (t->state == State::Waiting) {
        if (Sema *sema = FindSema(t->waiting_on)) {
            std::erase(sema->waiters, t);
        }
    }
    t->state = State::Dormant;
    return 0;
}

int DeleteThread(int thread_id) {
    Scheduler &s = Sched();
    if (thread_id == kMainThreadId) {
        return -1;
    }
    Thread *t = FindThread(thread_id);
    if (t == nullptr || t->state != State::Dormant) {
        return -1;
    }
    ContextDestroy(t->context);
    s.threads.erase(thread_id);
    return 0;
}

int CreateSema(struct SemaParam *param) {
    Scheduler &s = Sched();
    if (param == nullptr || param->maxCount < 1 || param->initCount < 0 || param->initCount > param->maxCount) {
        return -1;
    }
    Sema sema;
    sema.count = param->initCount;
    sema.max = param->maxCount;
    const int id = s.next_sema_id++;
    s.semas.emplace(id, std::move(sema));
    return id;
}

int DeleteSema(int sema_id) {
    Scheduler &s = Sched();
    auto       it = s.semas.find(sema_id);
    if (it == s.semas.end() || !it->second.waiters.empty()) {
        return -1;
    }
    s.semas.erase(it);
    return 0;
}

int WaitSema(int sema_id) {
    Scheduler &s = Sched();
    Sema      *sema = FindSema(sema_id);
    if (sema == nullptr) {
        return -1;
    }
    if (sema->count > 0) {
        --sema->count;
        return sema_id;
    }

    Thread *cur = s.running;
    cur->state = State::Waiting;
    cur->waiting_on = sema_id;
    sema->waiters.push_back(cur);

    Thread *next = PopHighest(s);
    if (next == nullptr) {
        std::fprintf(stderr, "eekernel: deadlock: thread %d waits on semaphore %d with no ready thread\n",
                     cur->id, sema_id);
        std::abort();
    }
    Run(s, next);
    return sema_id;
}

int SignalSema(int sema_id) {
    Scheduler &s = Sched();
    Sema      *sema = FindSema(sema_id);
    if (sema == nullptr) {
        return -1;
    }
    if (!sema->waiters.empty()) {
        // The count is handed to the waiter, so it never passes through the semaphore.
        Thread *waiter = sema->waiters.front();
        sema->waiters.pop_front();
        waiter->waiting_on = 0;
        Enqueue(s, waiter, false);

        Thread *cur = s.running;
        if (waiter->priority < cur->priority) {
            Unqueue(s, waiter);
            Enqueue(s, cur, true);
            Run(s, waiter);
        }
        return sema_id;
    }
    if (sema->count >= sema->max) {
        return -1;
    }
    ++sema->count;
    return sema_id;
}

namespace {

// There are no asynchronous interrupts on the host: the VSync handler runs synchronously inside ClockPump,
// so the flag only has to report and restore the state the game expects.
bool g_interrupts_enabled = true;

} // namespace

int DIntr(void) {
    const int previous = g_interrupts_enabled ? 1 : 0;
    g_interrupts_enabled = false;
    return previous;
}

int EIntr(...) {
    const int previous = g_interrupts_enabled ? 1 : 0;
    g_interrupts_enabled = true;
    return previous;
}

int AddIntcHandler(int cause, int (*handler)(int), int next) {
    PS2_UNIMPLEMENTED();
}

int RemoveIntcHandler(int cause, int handler) {
    PS2_UNIMPLEMENTED();
}

int EnableIntc(int cause) {
    PS2_UNIMPLEMENTED();
}

int AddDmacHandler(int channel, int (*handler)(int), int next) {
    PS2_UNIMPLEMENTED();
}

int RemoveDmacHandler(int channel, int handler) {
    PS2_UNIMPLEMENTED();
}

int EnableDmac(int channel) {
    PS2_UNIMPLEMENTED();
}

int DisableDmac(int channel) {
    PS2_UNIMPLEMENTED();
}
