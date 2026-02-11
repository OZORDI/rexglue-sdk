/**
 * @file        kernel/xboxkrnl/xboxkrnl_strings.cpp
 * @brief       Xbox kernel string function exports
 *
 * @copyright   Copyright 2022 Ben Vanik. All rights reserved. (Xenia Project)
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

 // Disable warnings about unused parameters for kernel functions
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <atomic>
#include <rex/runtime/guest/context.h>
#include <rex/logging.h>

// Minimal stub macro that doesn't need full kernel infrastructure
#define STRING_STUB(name) \
    extern "C" PPC_FUNC(name) { \
        (void)base; \
        static std::atomic<uint32_t> s_counter{0}; \
        uint32_t call_num = ++s_counter; \
        if (call_num <= 3) REXKRNL_DEBUG("{} [#{}] STUB - variadic not supported", #name, call_num); \
        ctx.r3.u64 = 0; \
    }

// DbgPrint - Debug print (outputs format string only, variadic args not supported)
extern "C" PPC_FUNC(__imp__DbgPrint) {
    uint32_t format_addr = ctx.r3.u32;
    const char* format_str = nullptr;
    if (format_addr != 0) {
        format_str = reinterpret_cast<const char*>(base + format_addr);
    }

    if (format_str && format_str[0] != '\0') {
        REXKRNL_INFO("DbgPrint: {}", format_str);
    }

    ctx.r3.u64 = 0;  // Return NTSTATUS success
}

// XamDbgPrint - XAM debug print (outputs format string only, variadic args not supported)
extern "C" PPC_FUNC(__imp__XamDbgPrint) {
    uint32_t format_addr = ctx.r3.u32;
    const char* format_str = nullptr;
    if (format_addr != 0) {
        format_str = reinterpret_cast<const char*>(base + format_addr);
    }

    if (format_str && format_str[0] != '\0') {
        REXKRNL_INFO("XamDbgPrint: {}", format_str);
    }

    ctx.r3.u64 = 0;
}

// sprintf family (variadic)
STRING_STUB(__imp__sprintf)
STRING_STUB(__imp___snprintf)
STRING_STUB(__imp__swprintf)
STRING_STUB(__imp___snwprintf)

// vsprintf family (va_list based)
STRING_STUB(__imp__vsprintf)
STRING_STUB(__imp___vsnprintf)
STRING_STUB(__imp__vswprintf)
STRING_STUB(__imp___vsnwprintf)
STRING_STUB(__imp___vscwprintf)
