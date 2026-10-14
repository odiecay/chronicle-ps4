#include <cstdio>
#include <cstdlib>

#include "kazarov/port.h"

[[noreturn]] void Ps2Unimplemented(const char *function, const char *file, int line) {
    std::fprintf(stderr, "%s:%d: %s: PlayStation 2 code is not implemented on PC\n", file, line, function);
    std::abort();
}
