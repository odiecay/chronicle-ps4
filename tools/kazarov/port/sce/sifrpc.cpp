#include <sifrpc.h>

// There is no IOP: rebooting it, syncing with it and loading its modules
// succeed at once. The RPC and IOP heap calls stay stubs until the audio
// replacement removes their callers.

void sceSifInitRpc(int mode) {}

int sceSifRebootIop(const char *path) {
    return 1;
}

int sceSifSyncIop() {
    return 1;
}

int sceSifLoadModule(const char *path, int arg_len, const char *args) {
    static int next_id;
    return ++next_id;
}

void sceSifExitCmd(void) {}

int sceSifBindRpc(struct sceSifClientData *client, unsigned int number, unsigned int mode) {
    PS2_UNIMPLEMENTED();
}

// Static constructors reach the RPC layer before the game binds its clients;
// retail survives that, so an unbound client reports failure instead of aborting.
int sceSifCheckStatRpc(struct sceSifClientData *client) {
    if (client->server == nullptr) {
        return 0;
    }

    PS2_UNIMPLEMENTED();
}

int sceSifCallRpc(struct sceSifClientData *client, unsigned int number, unsigned int mode,
                  void *send, int send_size, void *receive, int receive_size,
                  void (*end_callback)(void *), void *end_parameter) {
    if (client->server == nullptr) {
        return -1;
    }

    PS2_UNIMPLEMENTED();
}

int sceSifInitIopHeap(void) {
    PS2_UNIMPLEMENTED();
}

void *sceSifAllocIopHeap(unsigned int size) {
    PS2_UNIMPLEMENTED();
}

int sceSifFreeIopHeap(void *address) {
    PS2_UNIMPLEMENTED();
}

void *sceSifAllocSysMemory(int mode, unsigned int size, void *address) {
    PS2_UNIMPLEMENTED();
}

int sceSifFreeSysMemory(void *address) {
    PS2_UNIMPLEMENTED();
}
