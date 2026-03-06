/**
 * Polyfill for std::move_only_function (C++23) on platforms where it's
 * not yet available (notably Apple libc++ as of Xcode 16/clang 17).
 * Falls back to std::function - slightly less efficient but fully compatible.
 */
#pragma once
#include <functional>

#if !defined(__cpp_lib_move_only_function)
namespace std {
template <typename Sig>
using move_only_function = std::function<Sig>;
}  // namespace std
#endif
