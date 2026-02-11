#pragma once
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */


#include <rex/runtime/export_resolver.h>
#include <rex/kernel/kernel_state.h>
#include <rex/kernel/xam/ordinals.h>

namespace rex {
namespace kernel {
namespace xam {

bool xeXamIsUIActive();

rex::runtime::Export* RegisterExport_xam(rex::runtime::Export* export_entry);

// Registration functions, one per file.
#define XE_MODULE_EXPORT_GROUP(m, n)                                  \
  void Register##n##Exports(rex::runtime::ExportResolver* export_resolver, \
                            KernelState* kernel_state);
#include "module_export_groups.inc"
#undef XE_MODULE_EXPORT_GROUP

}  // namespace xam
}  // namespace kernel
}  // namespace xe

