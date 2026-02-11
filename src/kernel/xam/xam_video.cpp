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
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/kernel/xtypes.h>

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::runtime::guest;

void XGetVideoMode_entry(pointer_t<X_VIDEO_MODE> video_mode) {
  // TODO(benvanik): actually check to see if these are the same.
  xboxkrnl::VdQueryVideoMode(std::move(video_mode));
}

dword_result_t XGetVideoCapabilities_entry() { return 0; }

}  // namespace xam
}  // namespace kernel
}  // namespace rex

GUEST_FUNCTION_HOOK(__imp__XGetVideoMode, rex::kernel::xam::XGetVideoMode_entry)
GUEST_FUNCTION_HOOK(__imp__XGetVideoCapabilities, rex::kernel::xam::XGetVideoCapabilities_entry)
