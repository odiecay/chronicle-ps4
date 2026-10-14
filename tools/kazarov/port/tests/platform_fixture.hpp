#pragma once

#include <gtest/gtest.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace dc::test {

inline int ProcessId() {
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}

inline int SetEnv(const char *name, const char *value, int overwrite) {
#ifdef _WIN32
    return !overwrite && std::getenv(name) ? 0 : _putenv_s(name, value);
#else
    return setenv(name, value, overwrite);
#endif
}

inline int SetEnv(const char *name, const std::filesystem::path &value, int overwrite) {
#ifdef _WIN32
    const std::wstring wide_name(name, name + std::strlen(name));
    return !overwrite && ::_wgetenv(wide_name.c_str()) ? 0 : ::_wputenv_s(wide_name.c_str(), value.c_str());
#else
    return SetEnv(name, value.string().c_str(), overwrite);
#endif
}

inline int UnsetEnv(const char *name) {
#ifdef _WIN32
    return _putenv_s(name, "");
#else
    return unsetenv(name);
#endif
}

// Wide on Windows: the narrow environment cannot carry a non-ASCII value back unchanged.
struct EnvSnapshot {
    std::string name;
#ifdef _WIN32
    std::optional<std::wstring> value;
#else
    std::optional<std::string> value;
#endif
};

inline EnvSnapshot SaveEnv(const char *name) {
    EnvSnapshot snapshot{name, {}};
#ifdef _WIN32
    const std::wstring wide_name(name, name + std::strlen(name));
    if (const wchar_t *value = ::_wgetenv(wide_name.c_str())) {
        snapshot.value = value;
    }
#else
    if (const char *value = std::getenv(name)) {
        snapshot.value = value;
    }
#endif
    return snapshot;
}

inline void RestoreEnv(const EnvSnapshot &snapshot) {
#ifdef _WIN32
    const std::wstring wide_name(snapshot.name.begin(), snapshot.name.end());
    ::_wputenv_s(wide_name.c_str(), snapshot.value ? snapshot.value->c_str() : L"");
#else
    if (snapshot.value) {
        setenv(snapshot.name.c_str(), snapshot.value->c_str(), 1);
    } else {
        unsetenv(snapshot.name.c_str());
    }
#endif
}

#ifndef _WIN32
template <class F>
int ChildStatus(F body) {
    std::fflush(stdout);
    pid_t child = fork();
    if (child == 0) {
        body();
        std::_Exit(0);
    }
    int status = 0;
    if (child < 0 || waitpid(child, &status, 0) != child) {
        ADD_FAILURE() << "cannot run child process";
        return -1;
    }
    return status;
}
#endif

} // namespace dc::test

#ifdef _WIN32
#define KZ_ASSERT_EXIT(body, code) ASSERT_EXIT({ body(); std::_Exit(0); }, ::testing::ExitedWithCode(code), "")
// UCRT abort exits with status 3; a guard-page write reports an NT access violation.
#define KZ_ASSERT_ABORT(body) KZ_ASSERT_EXIT(body, 3)
#define KZ_ASSERT_FAULT(body) KZ_ASSERT_EXIT(body, static_cast<int>(0xC0000005))
#else
#define KZ_ASSERT_EXIT(body, code)                                       \
    do {                                                                 \
        int status = ::dc::test::ChildStatus(body);                      \
        ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == (code)); \
    } while (false)
#define KZ_ASSERT_ABORT(body)                                            \
    do {                                                                 \
        int status = ::dc::test::ChildStatus(body);                      \
        ASSERT_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT); \
    } while (false)
#define KZ_ASSERT_FAULT(body)                                                                            \
    do {                                                                                                 \
        int status = ::dc::test::ChildStatus(body);                                                      \
        ASSERT_TRUE(WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS)); \
    } while (false)
#endif
