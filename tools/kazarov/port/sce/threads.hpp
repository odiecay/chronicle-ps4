#pragma once

// The host's calls into the PS2 thread scheduler in eekernel_threads.cpp.

// Sets the main thread's priority, the one the game's main loop runs on. The game's threads are created at its
// priority, so it must be set before the game starts. Default 1.
void KzThreadsSetMainPriority(int priority);
