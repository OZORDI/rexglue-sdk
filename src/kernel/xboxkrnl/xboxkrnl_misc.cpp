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

 // Disable warnings about unused parameters for kernel functions
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <rex/logging.h>
#include <rex/kernel/kernel_state.h>
#include <rex/runtime/guest/function.h>
#include <rex/runtime/guest/types.h>
#include <rex/kernel/xboxkrnl/private.h>
#include <rex/kernel/xthread.h>
#include <rex/kernel/xtypes.h>

namespace rex::kernel::xboxkrnl {
using namespace rex::runtime::guest;

void KeEnableFpuExceptions_entry(dword_t enabled) {
  // TODO(benvanik): can we do anything about exceptions?
}

}  // namespace rex::kernel::xboxkrnl

GUEST_FUNCTION_HOOK(__imp__KeEnableFpuExceptions, rex::kernel::xboxkrnl::KeEnableFpuExceptions_entry)
