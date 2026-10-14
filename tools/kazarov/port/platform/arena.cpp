#include "platform/arena.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "platform/memory.hpp"

namespace {

struct Block {
    const void *owner;
    ArenaMemory memory;
};

std::vector<Block> g_blocks;
// Blocks an owner outgrew stay mapped: retail's memory never went away, and a
// pointer the game kept into an old arena reads zeroes rather than faulting.
std::vector<Block> g_retired;

Block MapBlock(const void *owner, std::size_t bytes, std::size_t lead) {
    return {owner, ArenaMemoryMap(bytes, lead)};
}

bool MapHolds(const std::vector<Block> &blocks, std::uintptr_t address) {
    for (const Block &block : blocks) {
        const auto start = reinterpret_cast<std::uintptr_t>(block.memory.map);
        if (address - start < block.memory.map_size) {
            return true;
        }
    }
    return false;
}

// Every 32-bit truncation the game makes of an arena pointer is widened; one left behind only shows
// when the pointer has bits above 32, so a debug build refuses to run where none would.
void CheckSomeBlockHigh() {
#ifndef NDEBUG
    std::vector<const void *> bases;
    bases.reserve(g_blocks.size());
    for (const Block &block : g_blocks) {
        bases.push_back(block.memory.base);
    }
    ArenaRequireOneAbove4GiB(bases.data(), bases.size());
#endif
}

} // namespace

unsigned char *ArenaBlock(const void *owner, std::size_t bytes, std::size_t lead) {
    for (Block &block : g_blocks) {
        if (block.owner != owner) {
            continue;
        }
        if (block.memory.capacity >= bytes && block.memory.lead >= lead) {
            return block.memory.base;
        }
        ArenaMemoryZero(block.memory);
        g_retired.push_back(block);
        block = MapBlock(owner, bytes, lead);
        CheckSomeBlockHigh();
        return block.memory.base;
    }
    g_blocks.push_back(MapBlock(owner, bytes, lead));
    CheckSomeBlockHigh();
    return g_blocks.back().memory.base;
}

void ArenaClearAll() {
    for (const Block &block : g_blocks) {
        ArenaMemoryZero(block.memory);
    }
}

bool ArenaContains(const void *pointer) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return MapHolds(g_blocks, address) || MapHolds(g_retired, address);
}

void ArenaRequireOneAbove4GiB(const void *const *bases, std::size_t count) {
#ifndef NDEBUG
    for (std::size_t i = 0; i < count; i++) {
        if (IsAbove4GiB(bases[i])) {
            return;
        }
    }
    std::fprintf(stderr, "arena: none lies above 4 GiB, where a truncated pointer would show\n");
    std::abort();
#endif
}

void *PortImagePointer(std::int32_t truncated) {
    static const char anchor = 0;
    if (truncated == 0) {
        return nullptr;
    }
    return reinterpret_cast<void *>(WidenNear(reinterpret_cast<std::uintptr_t>(&anchor), static_cast<std::uint32_t>(truncated)));
}

void ArenaOverflow(const void *arena, int used, int limit) {
    std::fprintf(stderr, "arena %p: allocation overflow, %d of %d quadwords\n", arena, used, limit);
    std::abort();
}

void ArenaOverflow(const char *name, int used, int limit) {
    std::fprintf(stderr, "arena %s: allocation overflow, %d of %d quadwords\n", name, used, limit);
    std::abort();
}
