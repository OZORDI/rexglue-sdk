/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#pragma once

#include <bit>
#include <cstdint>
#include <type_traits>

#include <rex/assert.h>
#include <rex/platform.h>

// Check for mixed endian
static_assert((std::endian::native == std::endian::big) ||
              (std::endian::native == std::endian::little));

namespace rex {

/// Byte-swap a value of any trivially copyable type (1, 2, 4, or 8 bytes).
/// Uses std::bit_cast for safe type punning and std::byteswap for the swap.
template <class T>
constexpr T byte_swap(T value) noexcept {
  static_assert(sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1,
                "byte_swap(T value): Type T has illegal size");
  if constexpr (sizeof(T) == 1) {
    return value;
  } else {
    // Convert to unsigned integer of same size, byteswap, convert back
    using uint_t = std::conditional_t<sizeof(T) == 8, uint64_t,
                   std::conditional_t<sizeof(T) == 4, uint32_t, uint16_t>>;
    return std::bit_cast<T>(std::byteswap(std::bit_cast<uint_t>(value)));
  }
}

template <typename T, std::endian E>
struct endian_store {
  using value_type = T;  // Type alias for value() in GuestPointer

  endian_store() = default;
  endian_store(const T& src) { set(src); }
  endian_store(const endian_store&) = default;
  endian_store& operator=(const endian_store&) = default;
  operator T() const { return get(); }

  void set(const T& src) {
    if constexpr (std::endian::native == E) {
      value = src;
    } else {
      value = rex::byte_swap(src);
    }
  }
  T get() const {
    if constexpr (std::endian::native == E) {
      return value;
    }
    return rex::byte_swap(value);
  }

  endian_store<T, E>& operator+=(int a) {
    *this = *this + a;
    return *this;
  }
  endian_store<T, E>& operator-=(int a) {
    *this = *this - a;
    return *this;
  }
  endian_store<T, E>& operator++() {
    *this += 1;
    return *this;
  }  // ++a
  endian_store<T, E> operator++(int) {
    *this += 1;
    return (*this - 1);
  }  // a++
  endian_store<T, E>& operator--() {
    *this -= 1;
    return *this;
  }  // --a
  endian_store<T, E> operator--(int) {
    *this -= 1;
    return (*this + 1);
  }  // a--

  T value;
};

template <typename T>
using be = endian_store<T, std::endian::big>;
template <typename T>
using le = endian_store<T, std::endian::little>;

}  // namespace rex

