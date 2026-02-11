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

#include <rex/kernel/kernel_state.h>
#include <rex/runtime/guest/function.h>
#include <rex/runtime/guest/types.h>
#include <rex/kernel/xam/private.h>
#include <rex/kernel/xtypes.h>

 // Disable warnings about unused parameters for kernel functions
#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::runtime::guest;

dword_result_t XamPartyGetUserList_entry(dword_t player_count,
                                         lpdword_t party_list) {
  // 5345085D wants specifically this code to skip loading party data.
  // This code is not documented in NT_STATUS code list
  return 0x807D0003;
}

dword_result_t XamPartySendGameInvites_entry(dword_t r3, dword_t r4,
                                             dword_t r5) {
  return X_ERROR_FUNCTION_FAILED;
}

dword_result_t XamPartySetCustomData_entry(dword_t r3, dword_t r4, dword_t r5) {
  return X_ERROR_FUNCTION_FAILED;
}

dword_result_t XamPartyGetBandwidth_entry(dword_t r3, dword_t r4) {
  return X_ERROR_FUNCTION_FAILED;
}

}  // namespace xam
}  // namespace kernel
}  // namespace rex

GUEST_FUNCTION_HOOK(__imp__XamPartyGetUserList, rex::kernel::xam::XamPartyGetUserList_entry)
GUEST_FUNCTION_HOOK(__imp__XamPartySendGameInvites, rex::kernel::xam::XamPartySendGameInvites_entry)
GUEST_FUNCTION_HOOK(__imp__XamPartySetCustomData, rex::kernel::xam::XamPartySetCustomData_entry)
GUEST_FUNCTION_HOOK(__imp__XamPartyGetBandwidth, rex::kernel::xam::XamPartyGetBandwidth_entry)
