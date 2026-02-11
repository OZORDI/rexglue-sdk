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

#include <rex/kernel/xboxkrnl/cert_monitor.h>

#include <vector>

#include <rex/time/clock.h>
#include <rex/debugging.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/runtime/guest/context.h>
#include <rex/kernel/kernel_state.h>
#include <rex/kernel/user_module.h>
#include <rex/kernel/xboxkrnl/private.h>
#include <rex/kernel/xthread.h>

namespace rex::kernel::xboxkrnl {
using namespace rex::runtime::guest;

void KeCertMonitorCallback(PPCContext* ppc_context,
                           rex::kernel::KernelState* kernel_state) {
  auto id = ppc_context->r[3];
  auto arg = ppc_context->r[4];
  REXKRNL_DEBUG("KeCertMonitorCallback({}, {:08X})", id, arg);
  // TODO: Implement cert monitor callback if needed
}

}  // namespace rex::kernel::xboxkrnl
