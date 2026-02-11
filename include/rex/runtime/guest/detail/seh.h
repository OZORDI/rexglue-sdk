/**
 * @file        runtime/guest/detail/seh.h
 * @brief       Platform-specific SEH implementation details
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <atomic>
#include <cstdint>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <setjmp.h>
#endif

// Forward declaration
namespace rex::runtime::guest {
class SehException;
}

namespace rex::runtime::guest::detail {

inline std::atomic<bool> g_seh_initialized{false};

#ifdef _WIN32

//-----------------------------------------------------------------------------
// Windows: Native __try/__except with RaiseException for rethrow
//-----------------------------------------------------------------------------

/// Thread-local storage for exception info (captured by filter, used by rethrow)
struct SehThreadState {
    DWORD code = 0;
    ULONG_PTR info[2] = {0, 0};  // For access violation: [0]=read/write, [1]=address
};

inline thread_local SehThreadState g_seh_state;

/// SEH filter function - captures exception info and determines whether to handle
inline int seh_filter(unsigned int code, EXCEPTION_POINTERS* ep) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
            g_seh_state.code = code;
            if (ep && ep->ExceptionRecord) {
                g_seh_state.info[0] = ep->ExceptionRecord->ExceptionInformation[0];
                g_seh_state.info[1] = ep->ExceptionRecord->ExceptionInformation[1];
            }
            return EXCEPTION_EXECUTE_HANDLER;
        default:
            return EXCEPTION_CONTINUE_SEARCH;
    }
}

/// Re-raise the captured exception
[[noreturn]] inline void seh_rethrow() {
    DWORD code = g_seh_state.code;
    ULONG_PTR info[2] = {g_seh_state.info[0], g_seh_state.info[1]};

    // For access violations, include the extra info (read/write flag and address)
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) {
        RaiseException(code, EXCEPTION_NONCONTINUABLE, 2, info);
    } else {
        RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }
    // RaiseException doesn't return, but compiler may not know that
#ifdef __clang__
    __builtin_unreachable();
#else
    __assume(false);
#endif
}

#else // !_WIN32

//-----------------------------------------------------------------------------
// Linux: libunwind-based Exception Translation
//-----------------------------------------------------------------------------

/// Thread-local flag indicating we're in SEH-protected code
inline thread_local bool g_seh_active = false;

/// Signal handler for SIGSEGV/SIGBUS
void signal_handler(int sig, siginfo_t* info, void* ucontext);

#endif // _WIN32

}  // namespace rex::runtime::guest::detail
