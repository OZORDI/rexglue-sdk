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

#pragma once

#include <rex/kernel/xtypes.h>
#include <rex/kernel/xvideo.h>

namespace rex::runtime {
class ExportResolver;
}

namespace rex::kernel {
class KernelState;
}

namespace rex::kernel::xboxkrnl {

void VdQueryVideoMode(X_VIDEO_MODE* video_mode);

// Register video variable exports (VdGlobalDevice, VdHSIOCalibrationLock, etc.)
// Must be called during kernel initialization before XEX modules are loaded.
void RegisterVideoExports(rex::runtime::ExportResolver* export_resolver,
                          KernelState* kernel_state);

}  // namespace rex::kernel::xboxkrnl
