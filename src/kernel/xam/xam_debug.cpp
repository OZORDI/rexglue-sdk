/**
 * @file        kernel/xam/xam_debug.cpp
 * @brief       XAM debug export implementations
 *
 * @copyright   Copyright 2022 Ben Vanik. All rights reserved. (Xenia Project)
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/logging.h>
#include <rex/kernel/kernel_state.h>
#include <rex/runtime/guest/function.h>
#include <rex/runtime/guest/types.h>

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::runtime::guest;

// OutputDebugStringA - ANSI debug string output
void OutputDebugStringA_entry(lpstring_t string) {
    if (string) {
        REXKRNL_INFO("OutputDebugStringA: {}", string.value());
    }
}

// OutputDebugStringW - Unicode debug string output
void OutputDebugStringW_entry(lpu16string_t string) {
    if (string) {
        // Convert char16_t to UTF-8 for logging (simple ASCII fallback)
        std::u16string_view sv = string.value();
        std::string utf8;
        utf8.reserve(sv.size());
        for (char16_t c : sv) {
            if (c < 0x80) {
                utf8.push_back(static_cast<char>(c));
            } else {
                utf8.push_back('?');  // Non-ASCII fallback
            }
        }
        REXKRNL_INFO("OutputDebugStringW: {}", utf8);
    }
}

// RtlOutputDebugString - Same as OutputDebugStringA
void RtlOutputDebugString_entry(lpstring_t string) {
    if (string) {
        REXKRNL_INFO("RtlOutputDebugString: {}", string.value());
    }
}

// RtlDebugTrace - Debug trace output
void RtlDebugTrace_entry(lpstring_t string) {
    if (string) {
        REXKRNL_INFO("RtlDebugTrace: {}", string.value());
    }
}

}  // namespace xam
}  // namespace kernel
}  // namespace rex

// Hook registrations
GUEST_FUNCTION_HOOK(__imp__OutputDebugStringA, rex::kernel::xam::OutputDebugStringA_entry)
GUEST_FUNCTION_HOOK(__imp__OutputDebugStringW, rex::kernel::xam::OutputDebugStringW_entry)
GUEST_FUNCTION_HOOK(__imp__RtlOutputDebugString, rex::kernel::xam::RtlOutputDebugString_entry)
GUEST_FUNCTION_HOOK(__imp__RtlDebugTrace, rex::kernel::xam::RtlDebugTrace_entry)
