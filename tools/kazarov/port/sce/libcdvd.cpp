#include <libcdvd.h>

// The game reads a directory, not a drive: initialising it and choosing the media succeed. The
// rest of libcdvd has no caller left, InitCDFile and the ReadBG group being replaced.

int sceCdInit(int mode) {
    return 1;
}

int sceCdMmode(int media) {
    return 1;
}

int sceCdSearchFile(sceCdlFILE *file, const char *name) {
    PS2_UNIMPLEMENTED();
}

int sceCdRead(u_int lsn, u_int sectors, void *buffer, sceCdRMode *mode) {
    PS2_UNIMPLEMENTED();
}

int sceCdSync(int mode) {
    PS2_UNIMPLEMENTED();
}

int sceCdGetError(void) {
    PS2_UNIMPLEMENTED();
}

int sceCdBreak(void) {
    PS2_UNIMPLEMENTED();
}

int sceCdSeek(u_int lsn) {
    PS2_UNIMPLEMENTED();
}

int sceCdDiskReady(int mode) {
    PS2_UNIMPLEMENTED();
}

int sceCdStInit(int sectors, int banks, void *buffer) {
    PS2_UNIMPLEMENTED();
}

int sceCdStStart(unsigned int sector, sceCdRMode *mode) {
    PS2_UNIMPLEMENTED();
}

int sceCdStSeekF(unsigned int sector) {
    PS2_UNIMPLEMENTED();
}

int sceCdStStop(void) {
    PS2_UNIMPLEMENTED();
}

int sceCdStRead(unsigned int sectors, void *buffer, unsigned int mode, unsigned int *error) {
    PS2_UNIMPLEMENTED();
}
