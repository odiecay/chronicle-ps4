// Adapted from alechurri/Shipwright ps4; see THIRD_PARTY.md.
// PS4: runtime bits the OpenOrbis C library doesn't provide.

#ifdef __PS4__

#include <pthread.h>
#include <stdlib.h>

// libc++abi forwards the registration of thread_local destructors to the C library.
// This is the usual implementation on top of a pthread key: destructors run when the thread
// exits, and from an atexit handler for the thread that calls exit().

typedef void (*Ps4ThreadDtor)(void*);

typedef struct Ps4ThreadDtorEntry {
    Ps4ThreadDtor destructor;
    void* object;
    struct Ps4ThreadDtorEntry* next;
} Ps4ThreadDtorEntry;

static pthread_key_t sDtorKey;
static pthread_once_t sDtorKeyOnce = PTHREAD_ONCE_INIT;

static void Ps4RunThreadDtors(void* list) {
    Ps4ThreadDtorEntry* entry = (Ps4ThreadDtorEntry*)list;
    while (entry != NULL) {
        Ps4ThreadDtorEntry* next = entry->next;
        entry->destructor(entry->object);
        free(entry);
        entry = next;
    }
}

static void Ps4RunExitingThreadDtors(void) {
    // A destructor may register new ones, keep going until the list stays empty.
    for (int i = 0; i < 8; i++) {
        void* list = pthread_getspecific(sDtorKey);
        if (list == NULL) {
            break;
        }
        pthread_setspecific(sDtorKey, NULL);
        Ps4RunThreadDtors(list);
    }
}

static void Ps4CreateDtorKey(void) {
    pthread_key_create(&sDtorKey, Ps4RunThreadDtors);
    atexit(Ps4RunExitingThreadDtors);
}

int __cxa_thread_atexit_impl(Ps4ThreadDtor destructor, void* object, void* dsoHandle) {
    (void)dsoHandle;

    pthread_once(&sDtorKeyOnce, Ps4CreateDtorKey);

    Ps4ThreadDtorEntry* entry = (Ps4ThreadDtorEntry*)malloc(sizeof(Ps4ThreadDtorEntry));
    if (entry == NULL) {
        return -1;
    }
    entry->destructor = destructor;
    entry->object = object;
    entry->next = (Ps4ThreadDtorEntry*)pthread_getspecific(sDtorKey);
    pthread_setspecific(sDtorKey, entry);
    return 0;
}


// ---- sprintf with the destination as its own format -------------------------------------------------
//
// The game builds file names with sprintf(path, path, n): the destination is also the format string
// (ps2/src/menu_dungeon.cpp's dungeon entrance, among others). That is undefined, and musl's vsnprintf
// stores a terminating zero in the destination before it reads the format, which empties it: the dungeon
// entrance asked for the file "" and crashed on the missing result. glibc and the Windows runtime copy
// the format first. This formats into a temporary and copies it over, which is safe either way.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int vsprintf(char *s, const char *format, va_list args) {
    va_list measure;
    va_copy(measure, args);
    const int length = vsnprintf(NULL, 0, format, measure);
    va_end(measure);
    if (length < 0) {
        return length;
    }
    char *temporary = (char *)malloc((size_t)length + 1);
    if (temporary == NULL) {
        return -1;
    }
    vsnprintf(temporary, (size_t)length + 1, format, args);
    memcpy(s, temporary, (size_t)length + 1);
    free(temporary);
    return length;
}

int sprintf(char *s, const char *format, ...) {
    va_list args;
    va_start(args, format);
    const int result = vsprintf(s, format, args);
    va_end(args);
    return result;
}

// ---- Clocks ----------------------------------------------------------------------------------------
//
// OpenOrbis' <time.h> numbers the clocks the Linux way (CLOCK_MONOTONIC is 1) and its clock_gettime hands
// the number straight to the PS4 kernel, which is FreeBSD: there 1 is CLOCK_VIRTUAL, the process's own
// CPU time. Every std::chrono::steady_clock (and so every game timer) then ran on CPU time, which only
// moves while the program computes: a game that sleeps between ticks saw time crawl. The kernel's real
// numbers: 0 realtime, 4 monotonic, 2 process CPU time. This replaces clock_gettime for the whole program.

#include <time.h>

extern int sceKernelClockGettime(int clock_id, struct timespec *tp);

int clock_gettime(clockid_t clock_id, struct timespec *tp) {
    int kernel_id;
    switch (clock_id) {
        case CLOCK_REALTIME:
        case CLOCK_REALTIME_COARSE:
        case CLOCK_REALTIME_ALARM:
            kernel_id = 0;
            break;
        case CLOCK_PROCESS_CPUTIME_ID:
            kernel_id = 2;
            break;
        default:
            // Monotonic, raw, coarse, boot time and the rest: the kernel's monotonic clock.
            kernel_id = 4;
            break;
    }
    return sceKernelClockGettime(kernel_id, tp) < 0 ? -1 : 0;
}

#endif // __PS4__
