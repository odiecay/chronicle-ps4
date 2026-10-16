#include "mod_loader.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

void Check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
void Write(const fs::path &p, const std::string &text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}
void Pack(const fs::path &root, const std::string &id) {
    fs::create_directories(root / id / "assets" / "textures");
    Write(root / id / "mod.json", "{\"format\":1,\"id\":\"" + id +
          "\",\"name\":\"Test\",\"version\":\"1.0\",\"game\":\"dark-cloud\",\"type\":\"texture-pack\"}");
}

int main(int argc, char **argv) {
    try {
        Check(argc == 2, "pass a fresh test-output directory");
        const fs::path root = argv[1];
        Check(!fs::exists(root), "test output already exists; preserving it");
        fs::create_directories(root);
        Check(LoadModPackages(root).enabled.empty(), "missing order must enable none");
        Pack(root, "first"); Pack(root, "second");
        Check(LoadModPackages(root).enabled.empty(), "discovered mods must not auto-enable");
        Write(root / "load-order.json", R"({"format":1,"enabled":["first","second"]})");
        auto m = LoadModPackages(root);
        Check(m.enabled.size() == 2 && m.enabled[0].id == "second" && m.enabled[1].id == "first",
              "later enabled mods must have higher priority");
        Write(root / "load-order.json", R"({"format":1,"enabled":["first","first"]})");
        Check(LoadModPackages(root).enabled.empty(), "duplicate IDs must reject the entire order");
        Write(root / "load-order.json", R"({"format":1,"enabled":["first","../escape"]})");
        Check(LoadModPackages(root).enabled.empty(), "path traversal must reject the entire order");
        Write(root / "load-order.json", R"({"format":1,"enabled":["first",42]})");
        Check(LoadModPackages(root).enabled.empty(), "wrong ID types must reject the entire order");
        Write(root / "load-order.json", R"({"format":2,"enabled":["first"]})");
        Check(LoadModPackages(root).enabled.empty(), "future loader formats must not silently activate");
        Write(root / "load-order.json", R"({"format":1,"enabled":["first","missing"]})");
        m = LoadModPackages(root);
        Check(m.enabled.size() == 1 && !m.messages.empty(), "missing mod must be reported without disabling valid mods");
        Write(root / "second" / "mod.json", R"({"format":1,"id":"wrong","name":"Bad","version":"1","game":"dark-cloud","type":"texture-pack"})");
        Write(root / "load-order.json", R"({"format":1,"enabled":["second"]})");
        Check(LoadModPackages(root).enabled.empty(), "manifest ID must match its directory");
        Write(root / "second" / "mod.json", R"({"format":1,"id":"second","name":"Bad","version":"1","game":"other-game","type":"texture-pack"})");
        Check(LoadModPackages(root).enabled.empty(), "wrong game must be rejected");
        Write(root / "second" / "mod.json", R"({"format":1,"id":"second","name":"Bad","version":"1","game":"dark-cloud","type":"pcsx2-trainer"})");
        Check(LoadModPackages(root).enabled.empty(), "unsupported mod types must be rejected");
        Write(root / "second" / "mod.json", "{broken");
        Check(LoadModPackages(root).enabled.empty(), "malformed manifest must be handled");
        Write(root / "second" / "mod.json", std::string(262145, ' '));
        Check(LoadModPackages(root).enabled.empty(), "oversized manifests must be handled");
        Check(fs::exists(root / "first" / "mod.json"), "loading must not change installed mods");
        std::cout << "PASS: mod activation, load order, diagnostics, compatibility and path contracts\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
