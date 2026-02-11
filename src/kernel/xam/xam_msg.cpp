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
#include <rex/kernel/xam/private.h>
#include <rex/kernel/xboxkrnl/error.h>
#include <rex/kernel/xevent.h>
#include <rex/kernel/xthread.h>
#include <rex/kernel/xtypes.h>
#include <rex/kernel/xio.h>

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::runtime::guest;

dword_result_t XMsgInProcessCall_entry(dword_t app, dword_t message,
                                       dword_t arg1, dword_t arg2) {
  auto result = kernel_state()->app_manager()->DispatchMessageSync(app, message,
                                                                   arg1, arg2);
  if (result == X_ERROR_NOT_FOUND) {
    REXKRNL_ERROR("XMsgInProcessCall: app {:08X} undefined", app);
  }
  return result;
}

dword_result_t XMsgSystemProcessCall_entry(dword_t app, dword_t message,
                                           dword_t buffer,
                                           dword_t buffer_length) {
  auto result = kernel_state()->app_manager()->DispatchMessageAsync(
      app, message, buffer, buffer_length);
  if (result == X_ERROR_NOT_FOUND) {
    REXKRNL_ERROR("XMsgSystemProcessCall: app {:08X} undefined", app);
  }
  return result;
}

struct XMSGSTARTIOREQUEST_UNKNOWNARG {
  be<uint32_t> unk_0;
  be<uint32_t> unk_1;
};

X_HRESULT xeXMsgStartIORequestEx(uint32_t app, uint32_t message,
                                 uint32_t overlapped_ptr, uint32_t buffer_ptr,
                                 uint32_t buffer_length,
                                 XMSGSTARTIOREQUEST_UNKNOWNARG* unknown) {
  auto result = kernel_state()->app_manager()->DispatchMessageAsync(
      app, message, buffer_ptr, buffer_length);
  if (result == X_E_NOTFOUND) {
    REXKRNL_ERROR("XMsgStartIORequestEx: app {:08X} undefined", app);
    result = X_E_INVALIDARG;
    XThread::SetLastError(X_ERROR_NOT_FOUND);
  }
  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    result = X_ERROR_IO_PENDING;
  }
  if (result == X_ERROR_SUCCESS || result == X_ERROR_IO_PENDING) {
    XThread::SetLastError(0);
  }
  return result;
}

dword_result_t XMsgStartIORequestEx_entry(
    dword_t app, dword_t message, pointer_t<XAM_OVERLAPPED> overlapped_ptr,
    dword_t buffer_ptr, dword_t buffer_length,
    pointer_t<XMSGSTARTIOREQUEST_UNKNOWNARG> unknown_ptr) {
  return xeXMsgStartIORequestEx(app, message, overlapped_ptr, buffer_ptr,
                                buffer_length, unknown_ptr);
}

dword_result_t XMsgStartIORequest_entry(
    dword_t app, dword_t message, pointer_t<XAM_OVERLAPPED> overlapped_ptr,
    dword_t buffer_ptr, dword_t buffer_length) {
  return xeXMsgStartIORequestEx(app, message, overlapped_ptr, buffer_ptr,
                                buffer_length, nullptr);
}

dword_result_t XMsgCancelIORequest_entry(
    pointer_t<XAM_OVERLAPPED> overlapped_ptr, dword_t wait) {
  X_HANDLE event_handle = XOverlappedGetEvent(overlapped_ptr);
  if (event_handle && wait) {
    auto ev =
        kernel_state()->object_table()->LookupObject<XEvent>(event_handle);
    if (ev) {
      ev->Wait(0, 0, true, nullptr);
    }
  }

  return 0;
}

dword_result_t XMsgCompleteIORequest_entry(
    pointer_t<XAM_OVERLAPPED> overlapped_ptr, dword_t result,
    dword_t extended_error, dword_t length) {
  kernel_state()->CompleteOverlappedImmediateEx(overlapped_ptr, result,
                                                extended_error, length);
  return X_ERROR_SUCCESS;
}

dword_result_t XamGetOverlappedResult_entry(
    pointer_t<XAM_OVERLAPPED> overlapped_ptr, lpdword_t length_ptr,
    dword_t unknown) {
  uint32_t result;
  if (overlapped_ptr->result != X_ERROR_IO_PENDING) {
    result = overlapped_ptr->result;
  } else if (!overlapped_ptr->event) {
    result = X_ERROR_IO_INCOMPLETE;
  } else {
    auto ev = kernel_state()->object_table()->LookupObject<XEvent>(
        overlapped_ptr->event);
    result = ev->Wait(3, 1, 0, nullptr);
    if (XSUCCEEDED(result)) {
      result = overlapped_ptr->result;
    } else {
      result = xboxkrnl::xeRtlNtStatusToDosError(result);
    }
  }
  if (XSUCCEEDED(result) && length_ptr) {
    *length_ptr = overlapped_ptr->length;
  }
  return result;
}

}  // namespace xam
}  // namespace kernel
}  // namespace rex

GUEST_FUNCTION_HOOK(__imp__XMsgInProcessCall, rex::kernel::xam::XMsgInProcessCall_entry)
GUEST_FUNCTION_HOOK(__imp__XMsgSystemProcessCall, rex::kernel::xam::XMsgSystemProcessCall_entry)
GUEST_FUNCTION_HOOK(__imp__XMsgStartIORequestEx, rex::kernel::xam::XMsgStartIORequestEx_entry)
GUEST_FUNCTION_HOOK(__imp__XMsgStartIORequest, rex::kernel::xam::XMsgStartIORequest_entry)
GUEST_FUNCTION_HOOK(__imp__XMsgCancelIORequest, rex::kernel::xam::XMsgCancelIORequest_entry)
GUEST_FUNCTION_HOOK(__imp__XMsgCompleteIORequest, rex::kernel::xam::XMsgCompleteIORequest_entry)
GUEST_FUNCTION_HOOK(__imp__XamGetOverlappedResult, rex::kernel::xam::XamGetOverlappedResult_entry)
