#pragma once
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */


#include <memory>

#include <renderdoc_app.h>
#include <rex/platform.h>

#if REX_PLATFORM_WIN32
#include <rex/platform/win.h>
#endif

namespace rex {
namespace ui {

class RenderDocAPI {
 public:
  static std::unique_ptr<RenderDocAPI> CreateIfConnected();

  RenderDocAPI(const RenderDocAPI&) = delete;
  RenderDocAPI& operator=(const RenderDocAPI&) = delete;

  ~RenderDocAPI();

  // Always present if this object exists.
  const RENDERDOC_API_1_0_0* api_1_0_0() const { return api_1_0_0_; }

 private:
  explicit RenderDocAPI() = default;

#if REX_PLATFORM_LINUX
  void* library_ = nullptr;
#elif REX_PLATFORM_WIN32
  HMODULE library_ = nullptr;
#endif

  const RENDERDOC_API_1_0_0* api_1_0_0_ = nullptr;
};

}  // namespace ui
}  // namespace xe

