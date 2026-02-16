/**
 * @file        runtime/guest/exceptions.h
 * @brief       Structured Exception Handling (SEH) support for recompiled code
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 *
 * @remarks     Provides cross-platform SEH support for recompiled PPC code.
 *              On Windows, uses native __try/__except (works with MSVC and Clang-cl).
 *              On Linux, uses signal handlers for SIGSEGV/SIGBUS.
 *
 * @usage       1. Call rex::runtime::guest::initialize() at startup
 *              2. Generated code wraps SEH scopes with try/catch blocks
 *              3. Hardware exceptions (null deref, access violation) throw SehException
 *              4. Catch blocks call finally handlers and rethrow
 */

#pragma once

#include <cstdint>
#include <exception>

#include <rex/runtime/guest/detail/seh.h>

namespace rex::runtime::guest {

//=============================================================================
// SEH Exception Type
//=============================================================================

/**
 * @brief Exception thrown when hardware exception occurs in SEH-protected code
 *
 * This exception is thrown by the SEH translator when a hardware exception
 * (e.g., access violation, null pointer dereference) occurs. The recompiled
 * code catches this exception, runs finally handlers, and rethrows.
 */
class SehException : public std::exception {
public:
    /// Exception codes matching Windows EXCEPTION_* values
    enum Code : uint32_t {
        ACCESS_VIOLATION = 0xC0000005,
        IN_PAGE_ERROR = 0xC0000006,
        ILLEGAL_INSTRUCTION = 0xC000001D,
        STACK_OVERFLOW = 0xC00000FD,
        FLOAT_DIVIDE_BY_ZERO = 0xC000008E,
        INTEGER_DIVIDE_BY_ZERO = 0xC0000094,
        UNKNOWN = 0xFFFFFFFF
    };

    explicit SehException(Code code, uintptr_t address = 0)
        : code_(code), address_(address) {}

    const char* what() const noexcept override {
        switch (code_) {
            case ACCESS_VIOLATION: return "SEH: Access Violation";
            case IN_PAGE_ERROR: return "SEH: In-Page Error";
            case ILLEGAL_INSTRUCTION: return "SEH: Illegal Instruction";
            case STACK_OVERFLOW: return "SEH: Stack Overflow";
            case FLOAT_DIVIDE_BY_ZERO: return "SEH: Float Divide by Zero";
            case INTEGER_DIVIDE_BY_ZERO: return "SEH: Integer Divide by Zero";
            default: return "SEH: Unknown Exception";
        }
    }

    Code code() const noexcept { return code_; }
    uintptr_t address() const noexcept { return address_; }

private:
    Code code_;
    uintptr_t address_;
};

//=============================================================================
// Signal Handler Definition (Linux only - needs SehException above)
//=============================================================================

#ifndef _WIN32
namespace detail {

inline void signal_handler(int sig, siginfo_t* info, void* ucontext) {
    // Only handle if we're in SEH-protected code
    if (!g_seh_active) {
        // Not in SEH region - restore default handler and re-raise
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }

    // Get fault address
    uintptr_t address = info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;

    // NULL function pointer call: PPC_CALL_INDIRECT_FUNC looked up a
    // missing entry in the function table (returned NULL) and called it.
    // Recover by setting PC = LR so execution returns to the caller,
    // just like ExceptionHandler's NULL-PC recovery.
    if (address == 0 && sig == SIGSEGV && ucontext) {
#if defined(__aarch64__) || defined(__arm64__)
        auto* uc = reinterpret_cast<ucontext_t*>(ucontext);
        // x30 = LR on ARM64
        uint64_t lr = uc->uc_mcontext->__ss.__lr;
        uc->uc_mcontext->__ss.__pc = lr;
        return;  // resume at LR
#elif defined(__x86_64__)
        auto* uc = reinterpret_cast<ucontext_t*>(ucontext);
#if defined(__APPLE__)
        uint64_t rsp = uc->uc_mcontext->__ss.__rsp;
        uint64_t ret_addr = *reinterpret_cast<uint64_t*>(rsp);
        uc->uc_mcontext->__ss.__rip = ret_addr;
        uc->uc_mcontext->__ss.__rsp = rsp + 8;
#else
        uint64_t rsp = uc->uc_mcontext.gregs[REG_RSP];
        uint64_t ret_addr = *reinterpret_cast<uint64_t*>(rsp);
        uc->uc_mcontext.gregs[REG_RIP] = ret_addr;
        uc->uc_mcontext.gregs[REG_RSP] = rsp + 8;
#endif
        return;  // resume at return address
#endif
    }

    // Determine exception code based on signal
    SehException::Code code;
    switch (sig) {
        case SIGSEGV:
            code = SehException::ACCESS_VIOLATION;
            break;
        case SIGBUS:
            code = SehException::IN_PAGE_ERROR;
            break;
        case SIGFPE:
            code = SehException::FLOAT_DIVIDE_BY_ZERO;
            break;
        case SIGILL:
            code = SehException::ILLEGAL_INSTRUCTION;
            break;
        default:
            code = SehException::UNKNOWN;
            break;
    }

    // Use libunwind to throw from signal context
    // This works because libunwind can unwind through signal frames
    throw SehException(code, address);
}

}  // namespace detail
#endif // !_WIN32

//=============================================================================
// Initialization
//=============================================================================

#ifdef _WIN32

/**
 * @brief Initialize SEH support for the current thread
 */
inline void initialize_thread() {
    // No per-thread init needed for native SEH with RaiseException
}

/**
 * @brief Initialize SEH support globally
 */
inline void initialize() {
    detail::g_seh_initialized.store(true);
}

#else // !_WIN32

/**
 * @brief Initialize SEH support for the current thread
 *
 * On Linux, sets g_seh_active to false (will be set true by SEH_TRY).
 */
inline void initialize_thread() {
    detail::g_seh_active = false;
}

/**
 * @brief Initialize SEH support globally
 *
 * On Linux, installs signal handlers for SIGSEGV, SIGBUS, SIGFPE, SIGILL.
 * Uses SA_SIGINFO for extended signal information.
 */
inline void initialize() {
    if (detail::g_seh_initialized.exchange(true)) {
        return; // Already initialized
    }

    struct sigaction sa;
    sa.sa_sigaction = detail::signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER; // SA_NODEFER allows re-entry for nested exceptions

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
}

#endif // !_WIN32

//=============================================================================
// SEH Guard Scope and Macros
//=============================================================================

#ifdef _WIN32

/**
 * @brief RAII guard for SEH-protected regions (Windows native SEH version)
 * Note: This is a no-op on Windows since we use __try/__except directly
 */
class SehGuard {
public:
    SehGuard() = default;
    ~SehGuard() = default;
    SehGuard(const SehGuard&) = delete;
    SehGuard& operator=(const SehGuard&) = delete;
};

#define SEH_TRY \
    __try {

#define SEH_CATCH \
    } __except(::rex::runtime::guest::detail::seh_filter(GetExceptionCode(), GetExceptionInformation())) {

#define SEH_CATCH_ALL \
    } __except(::rex::runtime::guest::detail::seh_filter(GetExceptionCode(), GetExceptionInformation())) {

#define SEH_RETHROW \
    ::rex::runtime::guest::detail::seh_rethrow()

#define SEH_END }

#else // !_WIN32

/**
 * @brief RAII guard for SEH-protected regions (Linux version)
 */
class SehGuard {
public:
    SehGuard() {
        was_active_ = detail::g_seh_active;
        detail::g_seh_active = true;
    }

    ~SehGuard() {
        detail::g_seh_active = was_active_;
    }

    // Non-copyable
    SehGuard(const SehGuard&) = delete;
    SehGuard& operator=(const SehGuard&) = delete;

private:
    bool was_active_ = false;
};

#define SEH_TRY \
    { ::rex::runtime::guest::SehGuard __seh_guard; \
    try

#define SEH_CATCH \
    catch (const ::rex::runtime::guest::SehException&)

#define SEH_CATCH_ALL \
    catch (...)

#define SEH_RETHROW throw

#define SEH_END }

#endif // _WIN32

} // namespace rex::runtime::guest
