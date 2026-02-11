/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/logging.h>
#include <rex/kernel/kernel_state.h>
#include <rex/runtime/guest/function.h>
#include <rex/runtime/guest/types.h>
#include <rex/kernel/xam/private.h>
#include <rex/kernel/xtypes.h>

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::runtime::guest;

dword_result_t XamAvatarInitialize_entry(
    dword_t unk1,              // 1, 4, etc
    dword_t unk2,              // 0 or 1
    dword_t processor_number,  // for thread creation?
    lpdword_t function_ptrs,   // 20b, 5 pointers
    lpunknown_t unk5,          // ptr in data segment
    dword_t unk6               // flags - 0x00300000, 0x30, etc
) {
  // Negative to fail. Game should immediately call XamAvatarShutdown.
  return ~0u;
}

void XamAvatarShutdown_entry() {
  // No-op.
}

}  // namespace xam
}  // namespace kernel
}  // namespace rex

GUEST_FUNCTION_HOOK(__imp__XamAvatarInitialize, rex::kernel::xam::XamAvatarInitialize_entry)
GUEST_FUNCTION_HOOK(__imp__XamAvatarShutdown, rex::kernel::xam::XamAvatarShutdown_entry)
