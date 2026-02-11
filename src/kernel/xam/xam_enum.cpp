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
#include <rex/kernel/kernel_state.h>
#include <rex/runtime/guest/function.h>
#include <rex/runtime/guest/types.h>
#include <rex/kernel/xam/module.h>
#include <rex/kernel/xam/private.h>
#include <rex/kernel/xenumerator.h>
#include <rex/kernel/xtypes.h>

#if REX_PLATFORM_WIN32
#include <rex/platform.h>
#endif

#include <fmt/format.h>

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::runtime::guest;

// https://github.com/LestaD/SourceEngine2007/blob/master/se2007/engine/xboxsystem.cpp#L518
uint32_t xeXamEnumerate(uint32_t handle, uint32_t flags, lpvoid_t buffer_ptr,
                        uint32_t buffer_size, uint32_t* items_returned,
                        uint32_t overlapped_ptr) {
  assert_true(flags == 0);

  auto e = kernel_state()->object_table()->LookupObject<XEnumerator>(handle);
  if (!e) {
    return X_ERROR_INVALID_HANDLE;
  }

  auto run = [e, buffer_ptr](uint32_t& extended_error,
                             uint32_t& length) -> X_RESULT {
    X_RESULT result;
    uint32_t item_count = 0;
    if (!buffer_ptr) {
      result = X_ERROR_INVALID_PARAMETER;
    } else {
      result = e->WriteItems(buffer_ptr.guest_address(),
                             buffer_ptr.as<uint8_t*>(), &item_count);
    }
    extended_error = X_HRESULT_FROM_WIN32(result);
    length = item_count;
    return result;
  };

  if (items_returned) {
    assert_true(!overlapped_ptr);
    uint32_t extended_error;
    uint32_t item_count;
    X_RESULT result = run(extended_error, item_count);
    *items_returned = result == X_ERROR_SUCCESS ? item_count : 0;
    return result;
  } else if (overlapped_ptr) {
    assert_true(!items_returned);
    kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
    return X_ERROR_IO_PENDING;
  } else {
    assert_always();
    return X_ERROR_INVALID_PARAMETER;
  }
}

dword_result_t XamEnumerate_entry(dword_t handle, dword_t flags,
                                  lpvoid_t buffer, dword_t buffer_length,
                                  lpdword_t items_returned,
                                  pointer_t<XAM_OVERLAPPED> overlapped) {
  uint32_t dummy;
  auto result = xeXamEnumerate(handle, flags, buffer, buffer_length,
                               !overlapped ? &dummy : nullptr, overlapped);
  if (!overlapped && items_returned) {
    *items_returned = dummy;
  }
  return result;
}

dword_result_t XamCreateEnumeratorHandle_entry(unknown_t unk1, unknown_t unk2,
                                               unknown_t unk3, unknown_t unk4,
                                               unknown_t unk5, unknown_t unk6,
                                               unknown_t unk7, unknown_t unk8) {
  return X_ERROR_INVALID_PARAMETER;
}

dword_result_t XamGetPrivateEnumStructureFromHandle_entry(
    dword_t handle, lpdword_t out_object_ptr) {
  auto e = kernel_state()->object_table()->LookupObject<XEnumerator>(handle);
  if (!e) {
    return X_STATUS_INVALID_HANDLE;
  }

  // Caller takes the reference.
  // It's released in ObDereferenceObject.
  e->RetainHandle();

  if (out_object_ptr.guest_address()) {
    *out_object_ptr = e->guest_object();
  }

  return X_STATUS_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace rex

GUEST_FUNCTION_HOOK(__imp__XamEnumerate, rex::kernel::xam::XamEnumerate_entry)
GUEST_FUNCTION_HOOK(__imp__XamCreateEnumeratorHandle, rex::kernel::xam::XamCreateEnumeratorHandle_entry)
GUEST_FUNCTION_HOOK(__imp__XamGetPrivateEnumStructureFromHandle, rex::kernel::xam::XamGetPrivateEnumStructureFromHandle_entry)
