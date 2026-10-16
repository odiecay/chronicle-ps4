#include "weapon_evolution.hpp"
#include <algorithm>
#include <functional>

namespace evolution {
bool Meets(int current, int required) { return required >= 0 ? current >= required : current <= -required; }
std::vector<int> Destinations(std::span<const Weapon> catalog, int id) {
    const auto source = std::find_if(catalog.begin(), catalog.end(), [id](const auto &w) { return w.id == id; });
    if (source == catalog.end()) return {};
    std::vector<int> result;
    for (const auto &target : catalog) {
        const int p = target.position;
        if (target.owner != source->owner || p < 2 || p > 51) continue;
        const bool linked = source->special_next ? target.id == source->special_next :
            p <= 20 ? (source->low & (std::uint32_t{1} << p)) != 0 :
                      (source->high & (std::uint32_t{1} << (p - 20))) != 0;
        if (linked) result.push_back(target.id);
    }
    return result;
}
std::vector<Node> Tree(std::span<const Weapon> catalog, int root, unsigned limit) {
    std::vector<Node> out;
    std::vector<int> ancestors;
    std::function<void(int, int, int)> visit = [&](int id, int parent, int depth) {
        if (out.size() >= limit) return;
        const bool cycle = std::find(ancestors.begin(), ancestors.end(), id) != ancestors.end();
        out.push_back({id, parent, depth, cycle});
        if (cycle || depth >= 64) return;
        ancestors.push_back(id);
        for (int target : Destinations(catalog, id)) visit(target, id, depth + 1);
        ancestors.pop_back();
    };
    if (std::any_of(catalog.begin(), catalog.end(), [root](const auto &w) { return w.id == root; })) visit(root, 0, 0);
    return out;
}
std::vector<Requirement> Requirements(const Stats &current, const Stats &required) {
    std::vector<Requirement> out;
    for (unsigned i = 0; i < required.size(); ++i) {
        if (required[i] != 0) out.push_back({int(i), current[i], required[i] < 0 ? -required[i] : required[i],
                                           required[i] < 0, Meets(current[i], required[i])});
    }
    return out;
}
} // namespace evolution
