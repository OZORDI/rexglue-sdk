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
#include <rex/runtime/export_resolver.h>
#include <rex/kernel/kernel_module.h>
#include <rex/kernel/kernel_state.h>
#include <rex/kernel/xboxkrnl/ordinals.h>

// All of the exported functions:
#include <rex/kernel/xboxkrnl/rtl.h>

namespace rex::kernel::xboxkrnl {

class XboxkrnlModule : public KernelModule {
 public:
  static constexpr size_t kExLoadedImageNameSize = 255 + 1;

  XboxkrnlModule(Runtime* emulator, KernelState* kernel_state);
  virtual ~XboxkrnlModule();

  static void RegisterExportTable(rex::runtime::ExportResolver* export_resolver);

  bool SendPIXCommand(const char* cmd);

  void set_pix_function(uint32_t addr) { pix_function_ = addr; }

 protected:
  uint32_t pix_function_ = 0;

 private:
  std::unique_ptr<rex::thread::HighResolutionTimer> timestamp_timer_;
};

}  // namespace rex::kernel::xboxkrnl
