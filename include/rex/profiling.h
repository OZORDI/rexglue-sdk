/**
 * @file        debug/profiling.h
 * @brief       Profiling infrastructure stubs
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 *
 * @remarks     Stubs for Xenia profiler system
 */

#pragma once

// CPU profiling stubs
#define SCOPE_profile_cpu_f(name)
#define SCOPE_profile_cpu_i(name, detail)

// GPU profiling stubs
#define SCOPE_profile_gpu_f(name)
#define SCOPE_profile_gpu_i(name, detail)

// Thread profiling stubs
#define PROFILE_THREAD_ENTER(name)
#define PROFILE_THREAD_EXIT()

// Counter profiling stubs
#define COUNT_profile_set(name, value)
#define COUNT_profile_add(name, value)

namespace rex::debug {

// Stub Profiler class for compatibility
class Profiler {
 public:
    static void OnThreadEnter(const char* name = nullptr) { (void)name; }
    static void OnThreadExit() {}
    static void ThreadEnter(const char* name = nullptr) { (void)name; }
    static void ThreadExit() {}
    static void Flip() {}
    static void Flush() {}
    static void Shutdown() {}
    static bool is_enabled() { return false; }
};

}  // namespace rex::debug
