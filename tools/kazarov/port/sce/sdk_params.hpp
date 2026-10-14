#pragma once

#include <cstddef>
#include <tuple>

// The games' transcribed libmc.h and libpad.h disagree on some parameter types,
// and neither header can change: a different prototype changes MWCC's code for
// every caller. Each definition takes its parameter types from the declaration
// in the game it is built for, so one source builds against both.
template <class Fn> struct SdkParams;

template <class R, class... A> struct SdkParams<R(A...)> {
    template <std::size_t I> using At = std::tuple_element_t<I, std::tuple<A...>>;
};
