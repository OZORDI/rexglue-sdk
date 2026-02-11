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
#include <rex/kernel/xtypes.h>

namespace rex::kernel::xboxkrnl {
using namespace rex::runtime::guest;

dword_result_t HidReadKeys_entry(dword_t unk1, unknown_t unk2, unknown_t unk3) {
  /* TODO(gibbed):
   * Games check for the following errors:
   *   0xC000009D - translated to 0x48F  - ERROR_DEVICE_NOT_CONNECTED
   *   0x103      - translated to 0x10D2 - ERROR_EMPTY
   * Other errors appear to be ignored?
   *
   * unk1 is 0
   * unk2 is a pointer to &unk3[2], possibly a 6-byte buffer
   * unk3 is a pointer to a 20-byte buffer
   */
  return 0xC000009D;
}

dword_result_t HidGetCapabilities_entry(dword_t unk1, unknown_t caps_ptr) {
  // HidGetCapabilities - ordinal 0x01EA
  // Returns capabilities for HID device (keyboard/mouse)
  // Not supported in rexglue - return unsuccessful
  return X_STATUS_UNSUCCESSFUL;
}

dword_result_t HidGetLastInputTime_entry(lpdword_t time_ptr) {
  // HidGetLastInputTime - ordinal 0x01F1
  // Returns the last time any HID input was received
  if (time_ptr) {
    *time_ptr = 0;
  }
  return X_STATUS_SUCCESS;
}

dword_result_t HidReadMouseChanges_entry(dword_t unk1, unknown_t unk2) {
  // HidReadMouseChanges - ordinal 0x0273
  // Reads mouse input changes - not supported in rexglue
  return X_STATUS_UNSUCCESSFUL;
}

}  // namespace rex::kernel::xboxkrnl

GUEST_FUNCTION_HOOK(__imp__HidReadKeys, rex::kernel::xboxkrnl::HidReadKeys_entry)
GUEST_FUNCTION_HOOK(__imp__HidGetCapabilities, rex::kernel::xboxkrnl::HidGetCapabilities_entry)
GUEST_FUNCTION_HOOK(__imp__HidGetLastInputTime, rex::kernel::xboxkrnl::HidGetLastInputTime_entry)
GUEST_FUNCTION_HOOK(__imp__HidReadMouseChanges, rex::kernel::xboxkrnl::HidReadMouseChanges_entry)
