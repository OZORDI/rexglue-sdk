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
#include <rex/string/util.h>
#include <rex/runtime/processor.h>
#include <rex/kernel/kernel_state.h>
#include <rex/kernel/user_module.h>
#include <rex/runtime/guest/function.h>
#include <rex/runtime/guest/types.h>
#include <rex/kernel/xam/module.h>
#include <rex/kernel/xam/private.h>
#include <rex/kernel/xthread.h>
#include <rex/kernel/xtypes.h>

#if REX_PLATFORM_WIN32
#include <rex/platform.h>
#endif

#include <fmt/format.h>

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::runtime::guest;

struct XTASK_MESSAGE {
  be<uint32_t> unknown_00;
  be<uint32_t> unknown_04;
  be<uint32_t> unknown_08;
  be<uint32_t> callback_arg_ptr;
  be<uint32_t> event_handle;
  be<uint32_t> unknown_14;
  be<uint32_t> task_handle;
};
static_assert_size(XTASK_MESSAGE, 0x1C);

dword_result_t XamTaskSchedule_entry(lpvoid_t callback,
                                     pointer_t<XTASK_MESSAGE> message,
                                     lpdword_t unknown, lpdword_t handle_ptr) {
  // TODO(gibbed): figure out what this is for
  *handle_ptr = 12345;

  uint32_t stack_size = kernel_state()->GetExecutableModule()->stack_size();

  // Stack must be aligned to 16kb pages
  stack_size = std::max((uint32_t)0x4000, ((stack_size + 0xFFF) & 0xFFFFF000));

  auto thread =
      object_ref<XThread>(new XThread(kernel_state(), stack_size, 0, callback,
                                      message.guest_address(), 0, true));

  X_STATUS result = thread->Create();

  if (XFAILED(result)) {
    // Failed!
    REXKRNL_ERROR("XAM task creation failed: {:08X}", result);
    return result;
  }

  REXKRNL_DEBUG("XAM task ({:08X}) scheduled asynchronously",
         callback.guest_address());

  return X_STATUS_SUCCESS;
}

dword_result_t XamTaskShouldExit_entry(dword_t r3) { return 0; }

dword_result_t XamTaskCloseHandle_entry(dword_t handle) {
  REXKRNL_DEBUG("XamTaskCloseHandle({:#x}) - stub", (uint32_t)handle);
  return X_STATUS_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace rex

GUEST_FUNCTION_HOOK(__imp__XamTaskSchedule, rex::kernel::xam::XamTaskSchedule_entry)
GUEST_FUNCTION_HOOK(__imp__XamTaskShouldExit, rex::kernel::xam::XamTaskShouldExit_entry)
GUEST_FUNCTION_HOOK(__imp__XamTaskCloseHandle, rex::kernel::xam::XamTaskCloseHandle_entry)

