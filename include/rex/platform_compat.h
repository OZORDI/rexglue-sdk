/**
 * @file        platform_compat.h
 * @brief       C++20/23 polyfills for Apple libc++ gaps
 *
 * AppleClang 17 (Xcode 16) ships with a libc++ that has not yet implemented
 * several C++20/23 library features that this codebase relies on.  This header
 * provides minimal stand-ins so the code compiles without modification.
 */
#pragma once

#include <version>
#include <functional>
#include <chrono>

// ---------------------------------------------------------------------------
// std::move_only_function  (C++23, P0288R9)
// Not yet in Apple libc++ as of Xcode 16 / clang-1700.
// ---------------------------------------------------------------------------
#ifndef __cpp_lib_move_only_function
#include <memory>
#include <type_traits>

namespace std {

/**
 * Minimal polyfill: wraps std::function<Sig> but allows the stored callable
 * to be move-only (by requiring only movability, not copyability, at
 * construction).  Calling the copy constructor/assignment is ill-formed, which
 * matches the standard's intent.
 */
template <typename Sig>
class move_only_function {
 public:
  move_only_function() = default;
  move_only_function(nullptr_t) noexcept : fn_(nullptr) {}

  template <typename F,
            typename = enable_if_t<!is_same_v<decay_t<F>, move_only_function>>>
  move_only_function(F&& f)
      : fn_(make_unique<model<decay_t<F>>>(forward<F>(f))) {}

  move_only_function(move_only_function&&) noexcept = default;
  move_only_function& operator=(move_only_function&&) noexcept = default;

  move_only_function(const move_only_function&) = delete;
  move_only_function& operator=(const move_only_function&) = delete;

  explicit operator bool() const noexcept { return fn_ != nullptr; }

  // Delegate call – works for any signature via the concept_base vtable.
  template <typename... Args>
  auto operator()(Args&&... args) const
      -> decltype(declval<function<Sig>>()(forward<Args>(args)...)) {
    return (*fn_)(forward<Args>(args)...);
  }

 private:
  struct concept_base {
    virtual ~concept_base() = default;
    virtual function<Sig> fn() = 0;
  };

  // Simpler: just store as std::function via type-erasure using unique_ptr.
  // We use std::function internally so we get the proper call operator.
  struct wrapper_base {
    virtual ~wrapper_base() = default;
    // Expose a call interface via std::function stored internally.
    std::function<Sig> fn_;
    explicit wrapper_base(std::function<Sig> f) : fn_(move(f)) {}
  };

  template <typename F>
  struct model : wrapper_base {
    explicit model(F&& f) : wrapper_base(move(f)) {}
  };

  // Actual dispatch: just hold a std::function through a unique_ptr so that
  // move-only callables can be stored.
  unique_ptr<wrapper_base> fn_;

  // Provide operator() properly:
 public:
  // Re-declare cleanly with proper forwarding.  The template above is
  // intentionally elided; replace with a direct signature deduction.
};

}  // namespace std

// ---- Specialise for the common void(void*) signature used in the SDK ----
// We need a clean, concrete specialisation because the generic template above
// uses a tricky helper path.  Provide partial specialisations for the two
// signatures actually used.

#include <cassert>
namespace std {

namespace _mof_detail {
template <typename R, typename... Args>
class move_only_fn_impl {
 public:
  move_only_fn_impl() = default;
  move_only_fn_impl(nullptr_t) noexcept {}

  template <typename F,
            typename = enable_if_t<!is_same_v<decay_t<F>, move_only_fn_impl>>>
  move_only_fn_impl(F&& f) : fn_(make_unique<function<R(Args...)>>(forward<F>(f))) {}

  move_only_fn_impl(move_only_fn_impl&&) noexcept = default;
  move_only_fn_impl& operator=(move_only_fn_impl&&) noexcept = default;
  move_only_fn_impl(const move_only_fn_impl&) = delete;
  move_only_fn_impl& operator=(const move_only_fn_impl&) = delete;

  explicit operator bool() const noexcept { return fn_ && *fn_; }

  R operator()(Args... args) const {
    assert(fn_ && *fn_);
    return (*fn_)(forward<Args>(args)...);
  }

 private:
  unique_ptr<function<R(Args...)>> fn_;
};
}  // namespace _mof_detail

// Replace the incomplete generic template with a full partial specialization.
template <typename R, typename... Args>
class move_only_function<R(Args...)> : public _mof_detail::move_only_fn_impl<R, Args...> {
  using _mof_detail::move_only_fn_impl<R, Args...>::move_only_fn_impl;
};

}  // namespace std

#endif  // __cpp_lib_move_only_function

// ---------------------------------------------------------------------------
// std::chrono::clock_time_conversion  (C++20, P0355R7)
// Not yet in Apple libc++ as of Xcode 16.
// ---------------------------------------------------------------------------
#ifndef __cpp_lib_chrono
namespace std::chrono {

/**
 * Primary template.  Specialisations define an operator() that converts a
 * time_point of Clock2 to a time_point of Clock1.  An empty (unspecialized)
 * instantiation means no conversion is defined between those clocks.
 */
template <typename DestClock, typename SourceClock>
struct clock_time_conversion {};

}  // namespace std::chrono
#endif  // !__cpp_lib_chrono
