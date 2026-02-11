/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#pragma once

#include <memory>
#include <rex/thread.h>
#include <rex/byte_order.h>
#include <rex/kernel/kernel_module.h>
#include <rex/kernel/kernel_state.h>

namespace rex::kernel::xboxkrnl {

struct X_KECERTMONITORDATA {
  rex::be<uint32_t> callback_fn;
};

void KeCertMonitorCallback(::PPCContext* ppc_context,
                           rex::kernel::KernelState* kernel_state);

}  // namespace rex::kernel::xboxkrnl
