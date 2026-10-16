#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace evolution {
// The first two entries are attack/magic, then the five elements and ten
// monster affinities, in retail's CompareBuildUpModelData2 order.
using Stats = std::array<int, 17>;
struct Weapon {
    int id = 0;
    int owner = 0;
    int position = 0;
    std::uint32_t low = 0, high = 0;
    Stats requirements{};
    int special_next = 0;
    int required_fish = 0;
};
struct Node {
    int id, parent, depth;
    bool cycle = false;
};
struct Requirement {
    int stat, current, limit;
    bool maximum, met;
};
bool Meets(int current, int signed_requirement);
std::vector<int> Destinations(std::span<const Weapon> catalog, int id);
// Repeated nodes on different branches remain visible; an ancestor cycle stops
// that branch. The limit also bounds deliberately malformed/modded graphs.
std::vector<Node> Tree(std::span<const Weapon> catalog, int root, unsigned limit = 256);
std::vector<Requirement> Requirements(const Stats &current, const Stats &required);
} // namespace evolution
