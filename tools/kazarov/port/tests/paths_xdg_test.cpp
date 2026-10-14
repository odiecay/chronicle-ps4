#include <gtest/gtest.h>
#include <stdlib.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "platform/paths.hpp"
#include "platform_fixture.hpp"

namespace fs = std::filesystem;

namespace {

// A data/ or save/ beside the game's test executable would win over XDG_DATA_HOME, as it should for a
// developer, so these cases cannot run in such a build directory.
class PathsXdg : public testing::Test {
protected:
    void SetUp() override {
        saved_cwd_ = fs::current_path();
        for (const char *name : {"LOCALAPPDATA", "KZTEST_DATA", "KZTEST_SAVE", "XDG_DATA_HOME", "HOME"}) {
            saved_env_.push_back(dc::test::SaveEnv(name));
        }
        PathsReset();
        PathsConfigure({.app_dir = "kazarov-test", .env_data = "KZTEST_DATA", .env_save = "KZTEST_SAVE"});
        fs::path beside = PathsExecutable().parent_path();
        if (fs::exists(beside / "data") || fs::exists(beside / "save")) {
            GTEST_SKIP() << "the build directory holds data/ or save/";
        }
    }

    void TearDown() override {
        std::error_code error;
        fs::current_path(saved_cwd_, error);
        for (const dc::test::EnvSnapshot &snapshot : saved_env_) {
            dc::test::RestoreEnv(snapshot);
        }
        saved_env_.clear();
        PathsReset();
    }

private:
    fs::path                           saved_cwd_;
    std::vector<dc::test::EnvSnapshot> saved_env_;
};

// An empty working directory and nothing in the environment but XDG_DATA_HOME and HOME.
fs::path Isolate(const char *tag) {
    fs::path dir = fs::canonical(fs::temp_directory_path()) / std::format("dc_xdg_{}_{}", tag, dc::test::ProcessId());
    fs::remove_all(dir);
    fs::create_directories(dir / "work");
    fs::current_path(dir / "work");
#ifdef _WIN32
    dc::test::UnsetEnv("LOCALAPPDATA");
#endif
    dc::test::UnsetEnv("KZTEST_DATA");
    dc::test::UnsetEnv("KZTEST_SAVE");
    dc::test::SetEnv("XDG_DATA_HOME", (dir / "xdg"), 1);
    dc::test::SetEnv("HOME", (dir / "home"), 1);
    return dir;
}

void Leave(const fs::path &dir) {
    fs::current_path(fs::temp_directory_path());
    fs::remove_all(dir);
}

} // namespace

TEST_F(PathsXdg, UsedWhenNothingIsLocal) {
    fs::path dir = Isolate("default");
    ASSERT_TRUE(PathsDataRoot() == dir / "xdg/kazarov-test/data");
    ASSERT_TRUE(!fs::exists(dir / "xdg/kazarov-test/data"));
    ASSERT_TRUE(PathsSaveRoot() == dir / "xdg/kazarov-test/save");
    ASSERT_TRUE(fs::is_directory(dir / "xdg/kazarov-test/save"));
    ASSERT_TRUE(!fs::exists(dir / "work/save"));
    Leave(dir);
}

TEST_F(PathsXdg, FallsBackToHome) {
    fs::path dir = Isolate("home");
    dc::test::UnsetEnv("XDG_DATA_HOME");
    ASSERT_TRUE(PathsDataRoot() == dir / "home/.local/share/kazarov-test/data");
    ASSERT_TRUE(PathsSaveRoot() == dir / "home/.local/share/kazarov-test/save");
    ASSERT_TRUE(fs::is_directory(dir / "home/.local/share/kazarov-test/save"));
    Leave(dir);
}

TEST_F(PathsXdg, IgnoresARelativeValue) {
    fs::path dir = Isolate("relative");
    dc::test::SetEnv("XDG_DATA_HOME", "relative", 1);
    ASSERT_TRUE(PathsDataRoot() == dir / "home/.local/share/kazarov-test/data");
    ASSERT_TRUE(!fs::exists(dir / "work/relative"));
    Leave(dir);
}

TEST_F(PathsXdg, LocalDataKeepsItsSaveBesideIt) {
    fs::path dir = Isolate("local");
    fs::create_directories(dir / "work/data");
    ASSERT_TRUE(PathsDataRoot() == dir / "work/data");
    ASSERT_TRUE(PathsSaveRoot() == dir / "work/save");
    ASSERT_TRUE(fs::is_directory(dir / "work/save"));
    ASSERT_TRUE(!fs::exists(dir / "xdg"));
    Leave(dir);
}

TEST_F(PathsXdg, LocalSaveAloneWins) {
    fs::path dir = Isolate("local_save");
    fs::create_directories(dir / "work/save");
    ASSERT_TRUE(PathsDataRoot() == dir / "xdg/kazarov-test/data");
    ASSERT_TRUE(PathsSaveRoot() == dir / "work/save");
    ASSERT_TRUE(!fs::exists(dir / "xdg/kazarov-test/save"));
    Leave(dir);
}

TEST_F(PathsXdg, EnvironmentAndFlagsComeFirst) {
    fs::path dir = Isolate("override");
    dc::test::SetEnv("KZTEST_DATA", (dir / "env_data"), 1);
    dc::test::SetEnv("KZTEST_SAVE", (dir / "env_save"), 1);
    ASSERT_TRUE(PathsDataRoot() == dir / "env_data");
    ASSERT_TRUE(PathsSaveRoot() == dir / "env_save");
    ASSERT_TRUE(!fs::exists(dir / "xdg"));
    Leave(dir);
}

#ifdef _WIN32
TEST_F(PathsXdg, LocalAppDataComesBeforeXdg) {
    fs::path dir = Isolate("localappdata");
    dc::test::SetEnv("LOCALAPPDATA", dir / "appdata", 1);
    ASSERT_TRUE(PathsDataRoot() == dir / "appdata/kazarov-test/data");
    ASSERT_TRUE(PathsSaveRoot() == dir / "appdata/kazarov-test/save");
    ASSERT_TRUE(fs::is_directory(dir / "appdata/kazarov-test/save"));
    ASSERT_TRUE(!fs::exists(dir / "xdg"));
    Leave(dir);
}
#endif

#ifdef _WIN32
TEST_F(PathsXdg, LocalAppDataPreservesUnicode) {
    fs::path dir = Isolate("unicode_appdata");
    fs::path base = dir / L"missing-\u6d4b\u8bd5";
    ASSERT_EQ(dc::test::SetEnv("LOCALAPPDATA", base, 1), 0);
    ASSERT_EQ(PathsDataRoot(), base / "kazarov-test/data");
    ASSERT_EQ(PathsSaveRoot(), base / "kazarov-test/save");
    ASSERT_TRUE(fs::is_directory(base / "kazarov-test/save"));
    Leave(dir);
}

TEST_F(PathsXdg, OverridesPreserveUnicode) {
    fs::path dir = Isolate("unicode_overrides");
    fs::path base = dir / L"\u6d4b\u8bd5";
    ASSERT_EQ(dc::test::SetEnv("KZTEST_DATA", base / "data", 1), 0);
    ASSERT_EQ(dc::test::SetEnv("KZTEST_SAVE", base / "save", 1), 0);
    ASSERT_EQ(PathsDataRoot(), base / "data");
    ASSERT_EQ(PathsSaveRoot(), base / "save");
    ASSERT_TRUE(fs::is_directory(base / "save"));
    Leave(dir);
}

TEST_F(PathsXdg, XdgPreservesUnicode) {
    fs::path dir = Isolate("unicode_xdg");
    fs::path base = dir / L"\u6d4b\u8bd5";
    ASSERT_EQ(dc::test::SetEnv("XDG_DATA_HOME", base, 1), 0);
    ASSERT_EQ(PathsDataRoot(), base / "kazarov-test/data");
    ASSERT_EQ(PathsSaveRoot(), base / "kazarov-test/save");
    Leave(dir);
}

TEST_F(PathsXdg, HomePreservesUnicode) {
    fs::path dir = Isolate("unicode_home");
    fs::path base = dir / L"\u6d4b\u8bd5";
    dc::test::UnsetEnv("XDG_DATA_HOME");
    ASSERT_EQ(dc::test::SetEnv("HOME", base, 1), 0);
    ASSERT_EQ(PathsDataRoot(), base / ".local/share/kazarov-test/data");
    ASSERT_EQ(PathsSaveRoot(), base / ".local/share/kazarov-test/save");
    Leave(dir);
}

TEST_F(PathsXdg, FlagsPreserveUtf8) {
    fs::path    dir = Isolate("unicode_flags");
    fs::path    base = dir / L"\u6d4b\u8bd5";
    std::string data = PathsDisplay(base / "data");
    std::string save = "--save=" + PathsDisplay(base / "save");
    const char *argv[] = {"game", "--data", data.c_str(), save.c_str(), "--offscreen", nullptr};
    ASSERT_EQ(PathsConsumeArgs(5, argv), 2);
    ASSERT_EQ(std::string_view(argv[1]), "--offscreen");
    ASSERT_EQ(PathsDataRoot(), base / "data");
    ASSERT_EQ(PathsSaveRoot(), base / "save");
    Leave(dir);
}
#endif
