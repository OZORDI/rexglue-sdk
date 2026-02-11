#pragma once
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


#include <string>

#include <rex/runtime/export_resolver.h>
#include <rex/kernel/kernel_module.h>
#include <rex/kernel/kernel_state.h>
#include <rex/kernel/xam/ordinals.h>

namespace rex {
namespace kernel {
namespace xam {

bool xeXamIsUIActive();

class XamModule : public KernelModule {
 public:
  XamModule(Runtime* emulator, KernelState* kernel_state);
  virtual ~XamModule();

  static void RegisterExportTable(rex::runtime::ExportResolver* export_resolver);

  struct LoaderData {
    bool launch_data_present = false;
    std::vector<uint8_t> launch_data;
    uint32_t launch_flags = 0;
    std::string launch_path;  // Full path to next xex
  };

  const LoaderData& loader_data() const { return loader_data_; }
  LoaderData& loader_data() { return loader_data_; }

 private:
  LoaderData loader_data_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace rex

