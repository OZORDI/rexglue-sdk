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

#include <rex/audio/audio_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/kernel/kernel_state.h>
#include <rex/runtime/guest/function.h>
#include <rex/runtime/guest/types.h>
#include <rex/kernel/xboxkrnl/private.h>
#include <rex/kernel/xtypes.h>

namespace rex::kernel::xboxkrnl {
using namespace rex::runtime::guest;

dword_result_t XAudioGetSpeakerConfig_entry(lpdword_t config_ptr) {
  *config_ptr = 0x00010001;
  return X_ERROR_SUCCESS;
}

dword_result_t XAudioGetVoiceCategoryVolumeChangeMask_entry(
    lpunknown_t driver_ptr, lpdword_t out_ptr) {
  assert_true((driver_ptr.guest_address() & 0xFFFF0000) == 0x41550000);

  rex::thread::MaybeYield();

  // Checking these bits to see if any voice volume changed.
  // I think.
  *out_ptr = 0;
  return X_ERROR_SUCCESS;
}

dword_result_t XAudioGetVoiceCategoryVolume_entry(dword_t unk,
                                                  lpfloat_t out_ptr) {
  // Expects a floating point single. Volume %?
  *out_ptr = 1.0f;

  return X_ERROR_SUCCESS;
}

dword_result_t XAudioEnableDucker_entry(dword_t unk) { return X_ERROR_SUCCESS; }

dword_result_t XAudioRegisterRenderDriverClient_entry(lpdword_t callback_ptr,
                                                      lpdword_t driver_ptr) {
  uint32_t callback = callback_ptr[0];
  uint32_t callback_arg = callback_ptr[1];

  auto audio_system = kernel_state()->emulator()->audio_system();

  size_t index;
  auto result = audio_system->RegisterClient(callback, callback_arg, &index);
  if (XFAILED(result)) {
    return result;
  }

  assert_true(!(index & ~0x0000FFFF));
  *driver_ptr = 0x41550000 | (static_cast<uint32_t>(index) & 0x0000FFFF);
  return X_ERROR_SUCCESS;
}

dword_result_t XAudioUnregisterRenderDriverClient_entry(
    lpunknown_t driver_ptr) {
  assert_true((driver_ptr.guest_address() & 0xFFFF0000) == 0x41550000);

  auto audio_system = kernel_state()->emulator()->audio_system();
  audio_system->UnregisterClient(driver_ptr.guest_address() & 0x0000FFFF);
  return X_ERROR_SUCCESS;
}

dword_result_t XAudioSubmitRenderDriverFrame_entry(lpunknown_t driver_ptr,
                                                   lpunknown_t samples_ptr) {
  assert_true((driver_ptr.guest_address() & 0xFFFF0000) == 0x41550000);

  auto audio_system = kernel_state()->emulator()->audio_system();
  audio_system->SubmitFrame(driver_ptr.guest_address() & 0x0000FFFF,
                            samples_ptr);

  return X_ERROR_SUCCESS;
}

}  // namespace rex::kernel::xboxkrnl

GUEST_FUNCTION_HOOK(__imp__XAudioGetSpeakerConfig, rex::kernel::xboxkrnl::XAudioGetSpeakerConfig_entry)
GUEST_FUNCTION_HOOK(__imp__XAudioGetVoiceCategoryVolumeChangeMask, rex::kernel::xboxkrnl::XAudioGetVoiceCategoryVolumeChangeMask_entry)
GUEST_FUNCTION_HOOK(__imp__XAudioGetVoiceCategoryVolume, rex::kernel::xboxkrnl::XAudioGetVoiceCategoryVolume_entry)
GUEST_FUNCTION_HOOK(__imp__XAudioEnableDucker, rex::kernel::xboxkrnl::XAudioEnableDucker_entry)
GUEST_FUNCTION_HOOK(__imp__XAudioRegisterRenderDriverClient, rex::kernel::xboxkrnl::XAudioRegisterRenderDriverClient_entry)
GUEST_FUNCTION_HOOK(__imp__XAudioUnregisterRenderDriverClient, rex::kernel::xboxkrnl::XAudioUnregisterRenderDriverClient_entry)
GUEST_FUNCTION_HOOK(__imp__XAudioSubmitRenderDriverFrame, rex::kernel::xboxkrnl::XAudioSubmitRenderDriverFrame_entry)
