#pragma once

// OpenOrbis ships libc++ 11. Chronicle is written against a newer standard library, so the pieces of
// C++20/23 it uses are supplied here, force-included into every PS4 translation unit. Each is the
// standard's behaviour for the cases the code uses; this is not a general implementation.

#include <algorithm>
#include <functional>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace std {

#if !defined(__cpp_lib_bit_cast)
template <class To, class From>
constexpr To bit_cast(const From &from) noexcept {
    static_assert(sizeof(To) == sizeof(From), "bit_cast needs equal sizes");
    return __builtin_bit_cast(To, from);
}
#endif

template <class T>
constexpr int bit_width(T value) noexcept {
    using U = make_unsigned_t<T>;
    U v = static_cast<U>(value);
    int width = 0;
    while (v != 0) {
        v = static_cast<U>(v >> 1);
        ++width;
    }
    return width;
}

template <class T>
constexpr T byteswap(T value) noexcept {
    static_assert(is_integral_v<T>, "byteswap is for integers");
    if constexpr (sizeof(T) == 1) {
        return value;
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>(__builtin_bswap16(static_cast<uint16_t>(value)));
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(__builtin_bswap32(static_cast<uint32_t>(value)));
    } else {
        return static_cast<T>(__builtin_bswap64(static_cast<uint64_t>(value)));
    }
}

template <class T, class... Args>
constexpr T *construct_at(T *location, Args &&...args) {
    return ::new (static_cast<void *>(location)) T(std::forward<Args>(args)...);
}

// The few std::ranges algorithms the port calls, with the projection argument they use.
namespace ranges {

namespace detail {
struct Identity {
    template <class T>
    constexpr T &&operator()(T &&value) const noexcept {
        return std::forward<T>(value);
    }
};

template <class Proj, class T>
constexpr decltype(auto) Project(Proj &proj, T &&value) {
    if constexpr (is_member_pointer_v<remove_reference_t<Proj>>) {
        return std::forward<T>(value).*proj;
    } else {
        return proj(std::forward<T>(value));
    }
}
} // namespace detail

template <class Range, class Value, class Proj = detail::Identity>
constexpr auto find(Range &&range, const Value &value, Proj proj = {}) {
    auto it = std::begin(range);
    auto end = std::end(range);
    for (; it != end; ++it) {
        if (detail::Project(proj, *it) == value) {
            break;
        }
    }
    return it;
}

template <class Range, class Pred, class Proj = detail::Identity>
constexpr auto find_if(Range &&range, Pred pred, Proj proj = {}) {
    auto it = std::begin(range);
    auto end = std::end(range);
    for (; it != end; ++it) {
        if (pred(detail::Project(proj, *it))) {
            break;
        }
    }
    return it;
}

template <class Range, class Pred, class Proj = detail::Identity>
constexpr bool any_of(Range &&range, Pred pred, Proj proj = {}) {
    return find_if(range, pred, proj) != std::end(range);
}

template <class Range, class Out, class Fn>
constexpr Out transform(Range &&range, Out out, Fn fn) {
    return std::transform(std::begin(range), std::end(range), out, fn);
}

// sort(range, comp = less, proj = identity): compares the projections.
template <class Range, class Comp = std::less<>, class Proj = detail::Identity>
constexpr void sort(Range &&range, Comp comp = {}, Proj proj = {}) {
    std::sort(std::begin(range), std::end(range),
              [&](const auto &a, const auto &b) { return comp(detail::Project(proj, a), detail::Project(proj, b)); });
}

template <class Range, class Value>
constexpr bool contains(Range &&range, const Value &value) {
    return find(range, value) != std::end(range);
}

template <class Range, class Value>
constexpr void replace(Range &&range, const Value &from, const Value &to) {
    std::replace(std::begin(range), std::end(range), from, to);
}

// The last element satisfying pred, as a (possibly empty) pair of iterators the way
// std::ranges::find_last_if returns a subrange: .empty() and .front().
template <class It>
struct Subrange {
    It first;
    It last;
    bool  empty() const { return first == last; }
    auto &front() const { return *first; }
};

template <class Range, class Pred>
constexpr auto find_last_if(Range &&range, Pred pred) {
    auto begin = std::begin(range);
    auto end = std::end(range);
    auto found = end;
    for (auto it = begin; it != end; ++it) {
        if (pred(*it)) {
            found = it;
        }
    }
    return Subrange<decltype(found)>{found, end};
}

} // namespace ranges

} // namespace std

#include <cstdlib>
#include <charconv>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <system_error>

// from_chars: libc++ 11 has the integer overloads and deletes the floating-point ones.
template <class T>
inline std::from_chars_result FromChars(const char *first, const char *last, T &value) {
    if constexpr (std::is_floating_point_v<T>) {
        char        buffer[64];
        std::size_t length = static_cast<std::size_t>(last - first);
        if (length == 0 || length >= sizeof(buffer)) {
            return {first, std::errc::invalid_argument};
        }
        std::memcpy(buffer, first, length);
        buffer[length] = 0;
        char *end = nullptr;
        double parsed = std::strtod(buffer, &end);
        if (end == buffer) {
            return {first, std::errc::invalid_argument};
        }
        value = static_cast<T>(parsed);
        return {first + (end - buffer), std::errc()};
    } else {
        return std::from_chars(first, last, value);
    }
}

// file_time_type::clock::to_sys (C++20) for libc++ 11: its file clock counts from the system epoch.
template <class Duration>
inline std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
FileTimeToSys(const std::chrono::time_point<std::filesystem::_FilesystemClock, Duration> &time) {
    return std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>(
        std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()));
}
