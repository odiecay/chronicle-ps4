#ifndef KAZAROV_PORT_H
#define KAZAROV_PORT_H

/* Source changes that apply only when the port (which defines PORT) compiles PS2 code. */
#ifdef PORT
#define PORT_INIT(...) = __VA_ARGS__
#define PORT_RETURN(...) return __VA_ARGS__
#define PORT_FIX(...) __VA_ARGS__
#ifdef __cplusplus
[[noreturn]] void Ps2Unimplemented(const char *function, const char *file, int line);
#define PS2_UNIMPLEMENTED() Ps2Unimplemented(__func__, __FILE__, __LINE__)
#endif
#else
#define PORT_INIT(...)
#define PORT_RETURN(...)
#define PORT_FIX(...)
#endif

/* Marks a port definition that replaces the PS2 definition of the same name. */
#define PORT_OVERRIDE

#endif
