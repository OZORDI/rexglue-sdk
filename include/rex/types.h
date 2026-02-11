/**
 * @file        types.h
 * @brief       Core type definitions and aliases
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <concepts>
#include <compare>

#include <rex/byte_order.h>

namespace rex {

//=============================================================================
// Basic Integer Types
//=============================================================================

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using f32 = float;
using f64 = double;

static_assert(sizeof(f32) == 4, "float must be 4 bytes");
static_assert(sizeof(f64) == 8, "double must be 8 bytes");

//=============================================================================
// Memory Address Types
//=============================================================================

using guest_addr_t = u32;       // Xbox 360 guest address (32-bit)
using host_addr_t = uintptr_t;  // Host native address (64-bit on x64)

//=============================================================================
// Big-Endian Type Aliases (using rex::be<T>)
//=============================================================================

using be_u8  = u8;  // No byte-swapping needed for single bytes
using be_u16 = be<u16>;
using be_u32 = be<u32>;
using be_u64 = be<u64>;

using be_i8  = i8;
using be_i16 = be<i16>;
using be_i32 = be<i32>;
using be_i64 = be<i64>;

using be_f32 = be<f32>;
using be_f64 = be<f64>;

//=============================================================================
// SlimGuestPtr - Type-safe 32-bit guest address wrapper
//=============================================================================
// Lightweight wrapper around a 32-bit guest address.
// Unlike runtime::guest::GuestPointer, this does NOT track the host pointer.
// Use this for compile-time type safety when passing guest addresses.
//
// For runtime memory access, use Memory::TranslateVirtual() to get host ptr.
//=============================================================================

template<typename T = void>
class SlimGuestPtr {
    guest_addr_t addr_;

public:
    constexpr SlimGuestPtr() noexcept : addr_(0) {}
    constexpr explicit SlimGuestPtr(guest_addr_t addr) noexcept : addr_(addr) {}

    // Get raw guest address
    [[nodiscard]] constexpr guest_addr_t raw() const noexcept { return addr_; }

    // Check if null
    [[nodiscard]] constexpr bool is_null() const noexcept { return addr_ == 0; }

    // Boolean conversion
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return addr_ != 0; }

    // Explicit conversion to raw address
    [[nodiscard]] constexpr explicit operator guest_addr_t() const noexcept { return addr_; }

    // Pointer arithmetic (only for non-void types)
    template<typename U = T> requires (!std::is_void_v<U>)
    constexpr SlimGuestPtr operator+(std::ptrdiff_t n) const noexcept {
        return SlimGuestPtr(addr_ + static_cast<guest_addr_t>(n * sizeof(T)));
    }

    template<typename U = T> requires (!std::is_void_v<U>)
    constexpr SlimGuestPtr operator-(std::ptrdiff_t n) const noexcept {
        return SlimGuestPtr(addr_ - static_cast<guest_addr_t>(n * sizeof(T)));
    }

    template<typename U = T> requires (!std::is_void_v<U>)
    constexpr SlimGuestPtr& operator+=(std::ptrdiff_t n) noexcept {
        addr_ += static_cast<guest_addr_t>(n * sizeof(T));
        return *this;
    }

    template<typename U = T> requires (!std::is_void_v<U>)
    constexpr SlimGuestPtr& operator-=(std::ptrdiff_t n) noexcept {
        addr_ -= static_cast<guest_addr_t>(n * sizeof(T));
        return *this;
    }

    // Byte offset (works for all types including void)
    constexpr SlimGuestPtr<void> byte_offset(std::ptrdiff_t bytes) const noexcept {
        return SlimGuestPtr<void>(addr_ + static_cast<guest_addr_t>(bytes));
    }

    // Cast to different pointed-to type
    template<typename U>
    constexpr SlimGuestPtr<U> cast() const noexcept {
        return SlimGuestPtr<U>(addr_);
    }

    // Comparison operators
    constexpr auto operator<=>(const SlimGuestPtr&) const = default;
};

// Deduction guide
template<typename T>
SlimGuestPtr(T*) -> SlimGuestPtr<T>;

}  // namespace rex
