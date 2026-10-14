#include <gtest/gtest.h>
#include <libmc.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "platform/paths.hpp"
#include "sce/sdk_params.hpp"
#include "platform_fixture.hpp"

// Not every game's libmc.h declares these; keep them C-linked like the rest of the family.
extern "C" {
int sceMcEnd(void);
int sceMcSeek(int fd, int offset, int origin);
}

// libmc on a save root in a temporary directory.

namespace fs = std::filesystem;

namespace {

fs::path UseTempSaveRoot() {
    PathsConfigure({.app_dir = "kazarov-test", .env_data = "KZTEST_DATA", .env_save = "KZTEST_SAVE"});
    fs::path root = fs::temp_directory_path() / ("dc_mc_test_" + std::to_string(dc::test::ProcessId()));
    fs::remove_all(root);
    fs::create_directories(root);
    dc::test::SetEnv("KZTEST_SAVE", root, 1);
    return root;
}

std::vector<char> ReadFile(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int Settle(int issued, int function) {
    int cmd = -1;
    int result = 0;
    EXPECT_EQ(issued, 0);
    EXPECT_EQ(sceMcSync(MC_WAIT, &cmd, &result), 1);
    EXPECT_EQ(cmd, function);
    return result;
}

} // namespace

TEST(PlatformMc, SyncReportsCommandCodes) {
    UseTempSaveRoot();
    int cmd = -1;
    int result = -1;
    ASSERT_TRUE(sceMcInit() == sceMcIniSucceed);
    ASSERT_TRUE(sceMcSync(MC_NOWAIT, &cmd, &result) == -1);

    int type = 0, free_size = 0, formatted = 0;
    ASSERT_TRUE(sceMcGetInfo(0, 0, &type, &free_size, &formatted) == 0);
    ASSERT_TRUE(sceMcSync(MC_NOWAIT, &cmd, &result) == 1);
    ASSERT_TRUE(cmd == 1 && result == sceMcResSucceed);
    ASSERT_TRUE(type == sceMcTypePS2 && formatted == 1 && free_size >= 0x190);
    ASSERT_TRUE(sceMcSync(MC_NOWAIT, &cmd, &result) == -1);

    ASSERT_TRUE(sceMcGetInfo(1, 0, &type, &free_size, &formatted) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1);
    ASSERT_TRUE(cmd == 1 && result <= -10 && type == sceMcTypeNoCard);

    ASSERT_TRUE(sceMcChdir(0, 0, (char *) "/nothing", nullptr) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1);
    ASSERT_TRUE(cmd == 0xC && result == sceMcResNoEntry);

    ASSERT_TRUE(sceMcMkdir(0, 0, (char *) "dir") == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 0xB && result == 0);
    ASSERT_TRUE(sceMcMkdir(0, 0, (char *) "dir") == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 0xB && result < 0);

    char current[0x40] = "garbage";
    ASSERT_TRUE(sceMcChdir(0, 0, (char *) "dir/", current) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 0xC && result == 0);
    ASSERT_TRUE(std::strcmp(current, "/") == 0);

    ASSERT_TRUE(sceMcOpen(0, 0, (char *) "missing", 1) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 2 && result == sceMcResNoEntry);
    ASSERT_TRUE(sceMcOpen(0, 0, (char *) "file", 0x203) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 2 && result >= 0);
    int  fd = result;
    char data[5] = "abcd";
    ASSERT_TRUE(sceMcWrite(fd, data, 4) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 6 && result == 4);
    ASSERT_TRUE(sceMcFlush(fd) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 0xA && result == 0);
    ASSERT_TRUE(sceMcClose(fd) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 3 && result == 0);

    ASSERT_TRUE(sceMcOpen(0, 0, (char *) "/dir/file", 1) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && result >= 0);
    fd = result;
    char back[8] = {};
    ASSERT_TRUE(sceMcRead(fd, back, 8) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 5 && result == 4);
    ASSERT_TRUE(std::memcmp(back, "abcd", 4) == 0);
    ASSERT_TRUE(sceMcWrite(fd, data, 4) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 6 && result == sceMcResDeniedPermit);
    ASSERT_TRUE(sceMcClose(fd) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1);

    unsigned char table[4][0x40] = {};
    ASSERT_TRUE(sceMcGetDir(0, 0, (char *) "*", 0, 4, reinterpret_cast<SdkParams<decltype(sceMcGetDir)>::At<5>>(table)) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 0xD && result == 3);
    ASSERT_TRUE(std::strcmp(reinterpret_cast<char *>(table[0] + 0x20), ".") == 0);
    ASSERT_TRUE(std::strcmp(reinterpret_cast<char *>(table[1] + 0x20), "..") == 0);
    ASSERT_TRUE(std::strcmp(reinterpret_cast<char *>(table[2] + 0x20), "file") == 0);
    std::uint32_t size;
    std::memcpy(&size, table[2] + 0x10, 4);
    ASSERT_TRUE(size == 4);
    std::uint16_t attributes;
    std::memcpy(&attributes, table[2] + 0x14, 2);
    ASSERT_TRUE((attributes & 0x8010) == 0x8010);
    std::uint16_t year;
    std::memcpy(&year, table[2] + 0x0E, 2);
    ASSERT_TRUE(year >= 2000);

    ASSERT_TRUE(sceMcDelete(0, 0, (char *) "file") == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 0xF && result == 0);
    ASSERT_TRUE(sceMcDelete(0, 0, (char *) "file") == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 0xF && result == sceMcResNoEntry);

    ASSERT_TRUE(sceMcUnformat(0, 0) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 0x11 && result == 0);
    ASSERT_TRUE(sceMcGetInfo(0, 0, &type, &free_size, &formatted) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && result == sceMcResNoFormat && formatted == 0);
    ASSERT_TRUE(sceMcFormat(0, 0) == 0);
    ASSERT_TRUE(sceMcSync(MC_WAIT, &cmd, &result) == 1 && cmd == 0x10 && result == 0);
}

TEST(PlatformMc, SeekMovesPositionForReadAndWrite) {
    UseTempSaveRoot();
    ASSERT_EQ(sceMcInit(), sceMcIniSucceed);
    int cmd = -1;
    int result = -1;
    ASSERT_EQ(sceMcOpen(0, 0, (char *) "file", 0x203), 0);
    ASSERT_EQ(sceMcSync(MC_WAIT, &cmd, &result), 1);
    ASSERT_GE(result, 0);
    int  fd = result;
    char digits[] = "0123456789";
    ASSERT_EQ(Settle(sceMcWrite(fd, digits, 10), 6), 10);

    ASSERT_EQ(Settle(sceMcSeek(fd, 3, SEEK_SET), 4), 3);
    char back[4] = {};
    ASSERT_EQ(Settle(sceMcRead(fd, back, 2), 5), 2);
    ASSERT_EQ(std::memcmp(back, "34", 2), 0);

    ASSERT_EQ(Settle(sceMcSeek(fd, 1, SEEK_CUR), 4), 6);
    char ab[] = "AB";
    ASSERT_EQ(Settle(sceMcWrite(fd, ab, 2), 6), 2);

    ASSERT_EQ(Settle(sceMcSeek(fd, -3, SEEK_END), 4), 7);
    ASSERT_EQ(Settle(sceMcRead(fd, back, 3), 5), 3);
    ASSERT_EQ(std::memcmp(back, "B89", 3), 0);
    ASSERT_EQ(Settle(sceMcClose(fd), 3), 0);

    ASSERT_EQ(sceMcOpen(0, 0, (char *) "file", 1), 0);
    ASSERT_EQ(sceMcSync(MC_WAIT, &cmd, &result), 1);
    fd = result;
    char whole[16] = {};
    ASSERT_EQ(Settle(sceMcRead(fd, whole, 16), 5), 10);
    ASSERT_EQ(std::memcmp(whole, "012345AB89", 10), 0);
    ASSERT_EQ(Settle(sceMcClose(fd), 3), 0);
}

TEST(PlatformMc, SeekRejectsBadFileAndOrigin) {
    UseTempSaveRoot();
    ASSERT_EQ(sceMcInit(), sceMcIniSucceed);
    int cmd = -1;
    int result = -1;
    ASSERT_EQ(Settle(sceMcSeek(-1, 0, SEEK_SET), 4), sceMcResNoEntry);
    ASSERT_EQ(Settle(sceMcSeek(1000, 0, SEEK_SET), 4), sceMcResNoEntry);

    ASSERT_EQ(sceMcOpen(0, 0, (char *) "file", 0x203), 0);
    ASSERT_EQ(sceMcSync(MC_WAIT, &cmd, &result), 1);
    int fd = result;
    ASSERT_EQ(Settle(sceMcSeek(fd, 0, 3), 4), sceMcResDeniedPermit);
    ASSERT_EQ(Settle(sceMcSeek(fd, -1, SEEK_SET), 4), sceMcResDeniedPermit);
    ASSERT_EQ(Settle(sceMcSeek(fd, 0, SEEK_CUR), 4), 0);
    ASSERT_EQ(Settle(sceMcClose(fd), 3), 0);
    ASSERT_EQ(Settle(sceMcSeek(fd, 0, SEEK_SET), 4), sceMcResNoEntry);
}

TEST(PlatformMc, EndReturnsZeroAndKeepsOpenFiles) {
    UseTempSaveRoot();
    ASSERT_EQ(sceMcInit(), sceMcIniSucceed);
    int cmd = -1;
    int result = -1;
    ASSERT_EQ(sceMcOpen(0, 0, (char *) "file", 0x203), 0);
    ASSERT_EQ(sceMcSync(MC_WAIT, &cmd, &result), 1);
    int fd = result;
    ASSERT_EQ(sceMcEnd(), 0);
    ASSERT_EQ(Settle(sceMcSeek(fd, 0, SEEK_CUR), 4), 0);
    ASSERT_EQ(Settle(sceMcClose(fd), 3), 0);
}

#ifdef _WIN32
TEST(PlatformMc, WindowsPathsCannotEscapeCardRoot) {
    fs::path root = UseTempSaveRoot();
    PathsSetSaveRoot(root / "save");
    ASSERT_EQ(sceMcInit(), sceMcIniSucceed);
    fs::create_directories(root / "outside");
    std::ofstream(root / "sentinel.bin", std::ios::binary) << "sentinel";
    const std::vector<char>  sentinel = ReadFile(root / "sentinel.bin");
    std::vector<std::string> prefixes = {
        "..\\..\\",
        root.string() + "\\",
        "\\" + root.relative_path().string() + "\\",
        root.root_name().string() + root.relative_path().string() + "\\",
        "\\\\?\\" + root.string() + "\\",
    };
    auto rejected = [](int issued) {
        ASSERT_EQ(issued, 0);
        int result = 0;
        ASSERT_EQ(sceMcSync(MC_WAIT, nullptr, &result), 1);
        ASSERT_EQ(result, sceMcResNoEntry);
    };
    for (const std::string &prefix : prefixes) {
        SCOPED_TRACE(prefix);
        std::string file = prefix + "sentinel.bin";
        for (int flags : {1, 2, 0x203}) {
            rejected(sceMcOpen(0, 0, file.data(), flags));
        }
        rejected(sceMcDelete(0, 0, file.data()));
        std::string fresh = prefix + "new.bin";
        rejected(sceMcOpen(0, 0, fresh.data(), 0x203));
        std::string dir = prefix + "outside";
        rejected(sceMcChdir(0, 0, dir.data(), nullptr));
        std::string   listing = dir + "/*";
        unsigned char table[4][0x40] = {};
        rejected(sceMcGetDir(0, 0, listing.data(), 0, 4, reinterpret_cast<SdkParams<decltype(sceMcGetDir)>::At<5>>(table)));
        dir = prefix + "new-directory";
        rejected(sceMcMkdir(0, 0, dir.data()));
        ASSERT_EQ(ReadFile(root / "sentinel.bin"), sentinel);
        ASSERT_FALSE(fs::exists(root / "new.bin"));
        ASSERT_FALSE(fs::exists(root / "new-directory"));
    }
    for (std::string name : {"sentinel.bin.", "sentinel.bin "}) {
        rejected(sceMcOpen(0, 0, name.data(), 0x203));
        rejected(sceMcDelete(0, 0, name.data()));
        rejected(sceMcMkdir(0, 0, name.data()));
    }
    for (std::string name : {"NUL", "nul", "Con", "AUX.txt", "prn.tar.gz", "COM1", "lpt9.bin", "CONIN$", "conout$"}) {
        SCOPED_TRACE(name);
        for (int flags : {1, 2, 0x203}) {
            rejected(sceMcOpen(0, 0, name.data(), flags));
        }
        rejected(sceMcDelete(0, 0, name.data()));
        rejected(sceMcMkdir(0, 0, name.data()));
    }
    ASSERT_TRUE(fs::is_directory(root / "save/mc0"));
    ASSERT_TRUE(fs::is_empty(root / "save/mc0"));
    fs::remove_all(root);
}
#endif
