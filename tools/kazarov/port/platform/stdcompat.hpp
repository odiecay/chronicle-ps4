#pragma once

// Two standard-library calls that the PS4 build's libc++ 11 lacks. On the PS4 ps4/compat/ps4_cxx_compat.hpp
// (force-included) defines FromChars and FileTimeToSys; elsewhere they are the standard's.

#ifndef __PS4__

#include <charconv>
#include <chrono>
#include <filesystem>

template <class T>
inline std::from_chars_result FromChars(const char *first, const char *last, T &value) {
    return std::from_chars(first, last, value);
}

template <class Time>
inline auto FileTimeToSys(const Time &time) {
    return Time::clock::to_sys(time);
}

#endif
