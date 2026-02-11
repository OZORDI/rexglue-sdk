/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Metal backend for macOS (ReXGlue runtime)
 */

#include <rex/graphics/metal/graphics_system.h>
#include <rex/graphics/metal/command_processor.h>
#include <rex/ui/metal/metal_provider.h>
#include <rex/kernel.h>
#include <rex/logging.h>

namespace rex::graphics::metal {

MetalGraphicsSystem::MetalGraphicsSystem() {}

MetalGraphicsSystem::~MetalGraphicsSystem() {}

bool MetalGraphicsSystem::IsAvailable() {
  return ui::metal::MetalProvider::IsAvailable();
}

std::string MetalGraphicsSystem::name() const {
  return "Metal";
}

X_STATUS MetalGraphicsSystem::Setup(runtime::Processor* processor,
                                    kernel::KernelState* kernel_state,
                                    ui::WindowedAppContext* app_context,
                                    bool with_presentation) {
  XELOGI("MetalGraphicsSystem: Setting up Metal graphics system");

  // Create the Metal provider.
  provider_ = ui::metal::MetalProvider::Create(true, with_presentation);
  if (!provider_) {
    XELOGE("MetalGraphicsSystem: Failed to create Metal provider");
    return X_STATUS_UNSUCCESSFUL;
  }

  return GraphicsSystem::Setup(processor, kernel_state, app_context,
                               with_presentation);
}

std::unique_ptr<CommandProcessor>
MetalGraphicsSystem::CreateCommandProcessor() {
  return std::make_unique<MetalCommandProcessor>(this, kernel_state_);
}

}  // namespace rex::graphics::metal
