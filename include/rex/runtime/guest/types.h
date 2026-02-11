/**
 * @file        runtime/guest/types.h
 * @brief       Guest pointer types, address translation, and PPC register definitions
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 *
 * @remarks     GuestPointer types derived from Xenia (https://xenia.jp)
 *              Register semantics based on XenonRecomp/UnleashedRecomp
 */

#pragma once

#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

#include <simde/x86/avx.h>
#include <simde/x86/sse.h>
#include <simde/x86/sse4.1.h>

#include <rex/byte_order.h>
#include <rex/runtime/guest/detail/fpscr.h>

namespace rex::runtime::guest {

//=============================================================================
// Type Traits
//=============================================================================

// Big-Endian Type Detection
template<typename T>
struct is_be_type : std::false_type {};

template<typename T>
struct is_be_type<rex::be<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_be_type_v = is_be_type<T>::value;

// Thread-local base pointer for guest address computation
// Set by HostToGuestFunction wrapper before calling _entry functions
extern thread_local uint8_t* g_memory_base;

//=============================================================================
// TypedValue - Wrapper for scalar types with .value() method
//=============================================================================

template<typename T>
class TypedValue {
    T val_;

public:
    TypedValue() : val_{} {}
    TypedValue(T v) : val_(v) {}

    T value() const { return val_; }

    operator T() const { return val_; }

    bool operator==(const TypedValue& other) const { return val_ == other.val_; }
    bool operator!=(const TypedValue& other) const { return val_ != other.val_; }
    bool operator<(const TypedValue& other) const { return val_ < other.val_; }
    bool operator>(const TypedValue& other) const { return val_ > other.val_; }
    bool operator<=(const TypedValue& other) const { return val_ <= other.val_; }
    bool operator>=(const TypedValue& other) const { return val_ >= other.val_; }

    template<typename U>
      requires std::convertible_to<U, T>
    bool operator==(U other) const { return val_ == static_cast<T>(other); }
    template<typename U>
      requires std::convertible_to<U, T>
    bool operator!=(U other) const { return val_ != static_cast<T>(other); }
    template<typename U>
      requires std::convertible_to<U, T>
    bool operator<(U other) const { return val_ < static_cast<T>(other); }
    template<typename U>
      requires std::convertible_to<U, T>
    bool operator>(U other) const { return val_ > static_cast<T>(other); }
    template<typename U>
      requires std::convertible_to<U, T>
    bool operator<=(U other) const { return val_ <= static_cast<T>(other); }
    template<typename U>
      requires std::convertible_to<U, T>
    bool operator>=(U other) const { return val_ >= static_cast<T>(other); }

    template<typename U>
      requires std::convertible_to<U, T>
    T operator+(U other) const { return val_ + static_cast<T>(other); }
    template<typename U>
      requires std::convertible_to<U, T>
    T operator-(U other) const { return val_ - static_cast<T>(other); }
    template<typename U>
      requires std::convertible_to<U, T>
    T operator*(U other) const { return val_ * static_cast<T>(other); }
    template<typename U>
      requires std::convertible_to<U, T>
    T operator/(U other) const { return val_ / static_cast<T>(other); }
    template<typename U>
      requires std::convertible_to<U, T> && std::integral<T>
    T operator%(U other) const { return val_ % static_cast<T>(other); }
    template<typename U>
      requires std::convertible_to<U, T> && std::integral<T>
    T operator&(U other) const { return val_ & static_cast<T>(other); }
    template<typename U>
      requires std::convertible_to<U, T> && std::integral<T>
    T operator|(U other) const { return val_ | static_cast<T>(other); }
    template<typename U>
      requires std::convertible_to<U, T> && std::integral<T>
    T operator^(U other) const { return val_ ^ static_cast<T>(other); }
    T operator<<(int shift) const { return val_ << shift; }
    T operator>>(int shift) const { return val_ >> shift; }
    T operator~() const { return ~val_; }

    explicit operator bool() const { return val_ != 0; }
};

//=============================================================================
// Value Type Aliases
//=============================================================================

using int_t = TypedValue<int32_t>;
using word_t = TypedValue<uint16_t>;
using dword_t = TypedValue<uint32_t>;
using qword_t = TypedValue<uint64_t>;
using float_t = TypedValue<float>;
using double_t = TypedValue<double>;
using function_t = TypedValue<uint32_t>;
using unknown_t = TypedValue<uint32_t>;

//=============================================================================
// GuestPointer - Wraps host pointer with guest address tracking
//=============================================================================

template<typename T>
class GuestPointer {
    T* host_ptr_;
    uint32_t guest_addr_;

public:
    GuestPointer() : host_ptr_(nullptr), guest_addr_(0) {}
    GuestPointer(T* host_ptr, uint32_t guest_addr) : host_ptr_(host_ptr), guest_addr_(guest_addr) {}
    GuestPointer(std::nullptr_t) : host_ptr_(nullptr), guest_addr_(0) {}

    static GuestPointer from_host(T* host_ptr) {
        return GuestPointer(host_ptr, 0);
    }

    uint32_t guest_address() const { return guest_addr_; }
    T* host_address() const { return host_ptr_; }

    operator T*() const { return host_ptr_; }

    T* operator->() const { return host_ptr_; }
    T& operator*() const { return *host_ptr_; }

    auto value() const {
        if constexpr (is_be_type_v<T>) {
            return static_cast<typename T::value_type>(*host_ptr_);
        } else {
            return *host_ptr_;
        }
    }

    explicit operator bool() const { return host_ptr_ != nullptr; }
    operator uint32_t() const { return guest_address(); }

    GuestPointer operator+(std::ptrdiff_t offset) const {
        return GuestPointer(host_ptr_ + offset, guest_addr_ + static_cast<uint32_t>(offset * sizeof(T)));
    }
    GuestPointer operator-(std::ptrdiff_t offset) const {
        return GuestPointer(host_ptr_ - offset, guest_addr_ - static_cast<uint32_t>(offset * sizeof(T)));
    }

    template<typename U>
    U as() const { return reinterpret_cast<U>(host_ptr_); }

    template<typename U>
    rex::be<U>* as_array() const { return reinterpret_cast<rex::be<U>*>(host_ptr_); }

    void Zero() const {
        if (host_ptr_) {
            std::memset(host_ptr_, 0, sizeof(T));
        }
    }

    void Zero(size_t size) const {
        if (host_ptr_) {
            std::memset(host_ptr_, 0, size);
        }
    }
};

//=============================================================================
// GuestPointer<void> Specialization
//=============================================================================

template<>
class GuestPointer<void> {
    void* host_ptr_;
    uint32_t guest_addr_;

public:
    GuestPointer() : host_ptr_(nullptr), guest_addr_(0) {}
    GuestPointer(void* host_ptr, uint32_t guest_addr) : host_ptr_(host_ptr), guest_addr_(guest_addr) {}
    GuestPointer(std::nullptr_t) : host_ptr_(nullptr), guest_addr_(0) {}

    static GuestPointer from_host(void* host_ptr) {
        return GuestPointer(host_ptr, 0);
    }

    uint32_t guest_address() const { return guest_addr_; }
    uint32_t value() const { return guest_addr_; }
    void* host_address() const { return host_ptr_; }

    operator void*() const { return host_ptr_; }
    operator uint8_t*() const { return static_cast<uint8_t*>(host_ptr_); }
    explicit operator bool() const { return host_ptr_ != nullptr; }
    operator uint32_t() const { return guest_addr_; }

    template<std::integral IntType>
    GuestPointer operator+(IntType offset) const {
        return GuestPointer(
            static_cast<uint8_t*>(host_ptr_) + static_cast<std::ptrdiff_t>(offset),
            guest_addr_ + static_cast<uint32_t>(offset));
    }
    template<std::integral IntType>
    GuestPointer operator-(IntType offset) const {
        return GuestPointer(
            static_cast<uint8_t*>(host_ptr_) - static_cast<std::ptrdiff_t>(offset),
            guest_addr_ - static_cast<uint32_t>(offset));
    }

    template<typename U>
    U as() const { return reinterpret_cast<U>(host_ptr_); }

    template<typename U>
    rex::be<U>* as_array() const { return reinterpret_cast<rex::be<U>*>(host_ptr_); }

    void Zero(size_t size) const {
        if (host_ptr_) {
            std::memset(host_ptr_, 0, size);
        }
    }
};

//=============================================================================
// GuestPointer<char> Specialization (strings)
//=============================================================================

template<>
class GuestPointer<char> {
    char* host_ptr_;
    uint32_t guest_addr_;

public:
    GuestPointer() : host_ptr_(nullptr), guest_addr_(0) {}
    GuestPointer(char* host_ptr, uint32_t guest_addr) : host_ptr_(host_ptr), guest_addr_(guest_addr) {}
    GuestPointer(std::nullptr_t) : host_ptr_(nullptr), guest_addr_(0) {}

    uint32_t guest_address() const { return guest_addr_; }
    char* host_address() const { return host_ptr_; }

    operator char*() const { return host_ptr_; }
    explicit operator bool() const { return host_ptr_ != nullptr; }

    std::string_view value() const {
        return host_ptr_ ? std::string_view(host_ptr_) : std::string_view();
    }

    char* operator->() const { return host_ptr_; }
    char& operator*() const { return *host_ptr_; }

    GuestPointer operator+(std::ptrdiff_t offset) const {
        return GuestPointer(host_ptr_ + offset, guest_addr_ + static_cast<uint32_t>(offset));
    }
    GuestPointer operator-(std::ptrdiff_t offset) const {
        return GuestPointer(host_ptr_ - offset, guest_addr_ - static_cast<uint32_t>(offset));
    }

    char& operator[](size_t idx) const { return host_ptr_[idx]; }

    template<typename U>
    U as() const { return reinterpret_cast<U>(host_ptr_); }
};

//=============================================================================
// GuestPointer<char16_t> Specialization (wide strings)
//=============================================================================

template<>
class GuestPointer<char16_t> {
    char16_t* host_ptr_;
    uint32_t guest_addr_;

public:
    GuestPointer() : host_ptr_(nullptr), guest_addr_(0) {}
    GuestPointer(char16_t* host_ptr, uint32_t guest_addr) : host_ptr_(host_ptr), guest_addr_(guest_addr) {}
    GuestPointer(std::nullptr_t) : host_ptr_(nullptr), guest_addr_(0) {}

    uint32_t guest_address() const { return guest_addr_; }
    char16_t* host_address() const { return host_ptr_; }

    operator char16_t*() const { return host_ptr_; }
    explicit operator bool() const { return host_ptr_ != nullptr; }

    std::u16string_view value() const {
        return host_ptr_ ? std::u16string_view(host_ptr_) : std::u16string_view();
    }

    char16_t* operator->() const { return host_ptr_; }
    char16_t& operator*() const { return *host_ptr_; }

    GuestPointer operator+(std::ptrdiff_t offset) const {
        return GuestPointer(host_ptr_ + offset, guest_addr_ + static_cast<uint32_t>(offset * sizeof(char16_t)));
    }
    GuestPointer operator-(std::ptrdiff_t offset) const {
        return GuestPointer(host_ptr_ - offset, guest_addr_ - static_cast<uint32_t>(offset * sizeof(char16_t)));
    }

    char16_t& operator[](size_t idx) const { return host_ptr_[idx]; }

    template<typename U>
    U as() const { return reinterpret_cast<U>(host_ptr_); }
};

//=============================================================================
// Pointer Type Aliases
//=============================================================================

using lpvoid_t = GuestPointer<void>;
using lpword_t = GuestPointer<rex::be<uint16_t>>;
using lpdword_t = GuestPointer<rex::be<uint32_t>>;
using lpqword_t = GuestPointer<rex::be<uint64_t>>;
using lpfloat_t = GuestPointer<rex::be<float>>;
using lpdouble_t = GuestPointer<rex::be<double>>;
using lpstring_t = GuestPointer<char>;
using lpu16string_t = GuestPointer<char16_t>;
using lpunknown_t = GuestPointer<void>;

template<typename T>
using pointer_t = GuestPointer<T>;

//=============================================================================
// Result Types
//=============================================================================

using int_result_t = int32_t;
using dword_result_t = uint32_t;
using pointer_result_t = uint32_t;
using X_HRESULT_result_t = int32_t;

//=============================================================================
// General Purpose Register
//=============================================================================

union Register {
    int8_t s8;
    uint8_t u8;
    int16_t s16;
    uint16_t u16;
    int32_t s32;
    uint32_t u32;
    int64_t s64;
    uint64_t u64;
    float f32;
    double f64;
};

//=============================================================================
// Fixed-Point Exception Register (XER)
//=============================================================================

struct XERRegister {
    uint8_t so;  // Summary Overflow
    uint8_t ov;  // Overflow
    uint8_t ca;  // Carry
};

//=============================================================================
// Condition Register (CR) Field
//=============================================================================

struct CRRegister {
    uint8_t lt;  // Less Than
    uint8_t gt;  // Greater Than
    uint8_t eq;  // Equal
    union {
        uint8_t so;  // Summary Overflow (for integer compares)
        uint8_t un;  // Unordered (for FP compares - NaN involved)
    };

    // Pack CR field into 4-bit value for serialization
    inline uint32_t raw() const noexcept {
        return (lt << 3) | (gt << 2) | (eq << 1) | so;
    }

    // Set CR field from 4-bit packed value
    inline void set_raw(uint32_t value) noexcept {
        lt = (value >> 3) & 1;
        gt = (value >> 2) & 1;
        eq = (value >> 1) & 1;
        so = value & 1;
    }

    template<typename T>
    inline void compare(T left, T right, const XERRegister& xer) noexcept {
        lt = left < right;
        gt = left > right;
        eq = left == right;
        so = xer.so;
    }

    inline void compare(double left, double right) noexcept {
        un = __builtin_isnan(left) || __builtin_isnan(right);
        lt = !un && (left < right);
        gt = !un && (left > right);
        eq = !un && (left == right);
    }

    inline void setFromMask(simde__m128 mask, int imm) noexcept {
        int m = simde_mm_movemask_ps(mask);
        lt = m == imm;  // all equal
        gt = 0;
        eq = m == 0;    // none equal
        so = 0;
    }

    inline void setFromMask(simde__m128i mask, int imm) noexcept {
        int m = simde_mm_movemask_epi8(mask);
        lt = m == imm;  // all equal
        gt = 0;
        eq = m == 0;    // none equal
        so = 0;
    }
};

//=============================================================================
// Vector Register (128-bit)
//=============================================================================

union alignas(0x10) VRegister {
    int8_t s8[16];
    uint8_t u8[16];
    int16_t s16[8];
    uint16_t u16[8];
    int32_t s32[4];
    uint32_t u32[4];
    int64_t s64[2];
    uint64_t u64[2];
    float f32[4];
    double f64[2];
};

//=============================================================================
// Floating-Point Status and Control Register (FPSCR)
//=============================================================================

// Rounding mode constants
constexpr uint32_t kRoundNearest    = 0x00;
constexpr uint32_t kRoundTowardZero = 0x01;
constexpr uint32_t kRoundUp         = 0x02;
constexpr uint32_t kRoundDown       = 0x03;
constexpr uint32_t kRoundMask       = 0x03;

struct FPSCRRegister {
    uint32_t csr;

    static constexpr size_t HostToGuest[] = {
        kRoundNearest, kRoundDown, kRoundUp, kRoundTowardZero
    };

    using Platform = detail::FPSCRPlatform;
    static constexpr size_t RoundShift = Platform::RoundShift;
    static constexpr size_t RoundMaskVal = Platform::RoundMaskVal;
    static constexpr size_t FlushMask = Platform::FlushMask;

    inline uint32_t getcsr() noexcept {
        return Platform::getcsr();
    }

    inline void setcsr(uint32_t csr) noexcept {
        Platform::setcsr(csr);
    }

    inline uint32_t loadFromHost() noexcept {
        csr = getcsr();
        return HostToGuest[(csr & RoundMaskVal) >> RoundShift];
    }

    inline void storeFromGuest(uint32_t value) noexcept {
        csr &= ~RoundMaskVal;
        csr |= Platform::GuestToHost[value & kRoundMask];
        setcsr(csr);
    }

    inline void enableFlushModeUnconditional() noexcept {
        csr |= FlushMask;
        setcsr(csr);
    }

    inline void disableFlushModeUnconditional() noexcept {
        csr &= ~FlushMask;
        setcsr(csr);
    }

    inline void enableFlushMode() noexcept {
        if ((csr & FlushMask) != FlushMask) [[unlikely]] {
            csr |= FlushMask;
            setcsr(csr);
        }
    }

    inline void disableFlushMode() noexcept {
        if ((csr & FlushMask) != 0) [[unlikely]] {
            csr &= ~FlushMask;
            setcsr(csr);
        }
    }

    // Initialize MXCSR/FPCR with all FP exceptions masked
    inline void InitHost() noexcept {
        csr = getcsr();
        Platform::InitHostExceptions(csr);
        setcsr(csr);
    }
};

}  // namespace rex::runtime::guest

//=============================================================================
// Global Type Aliases
//=============================================================================
using PPCRegister = rex::runtime::guest::Register;
using PPCXERRegister = rex::runtime::guest::XERRegister;
using PPCCRRegister = rex::runtime::guest::CRRegister;
using PPCVRegister = rex::runtime::guest::VRegister;
using PPCFPSCRRegister = rex::runtime::guest::FPSCRRegister;

// Rounding mode macros
#define PPC_ROUND_NEAREST    rex::runtime::guest::kRoundNearest
#define PPC_ROUND_TOWARD_ZERO rex::runtime::guest::kRoundTowardZero
#define PPC_ROUND_UP         rex::runtime::guest::kRoundUp
#define PPC_ROUND_DOWN       rex::runtime::guest::kRoundDown
#define PPC_ROUND_MASK       rex::runtime::guest::kRoundMask

//=============================================================================
// fmt formatter for TypedValue
//=============================================================================

#include <fmt/format.h>

template<typename T>
struct fmt::formatter<rex::runtime::guest::TypedValue<T>> : fmt::formatter<T> {
    template<typename FormatContext>
    auto format(const rex::runtime::guest::TypedValue<T>& v, FormatContext& ctx) const {
        return fmt::formatter<T>::format(v.value(), ctx);
    }
};
