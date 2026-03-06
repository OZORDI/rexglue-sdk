/**
 * Metal graphics system.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/graphics/metal/graphics_system.h>

#import <Metal/Metal.h>


#include <rex/logging.h>
#include <rex/graphics/metal/command_processor.h>
#include <rex/ui/metal/metal_provider.h>
#include <rex/ui/windowed_app_context.h>

namespace rex {
namespace graphics {
namespace metal {

MetalGraphicsSystem::MetalGraphicsSystem() = default;
MetalGraphicsSystem::~MetalGraphicsSystem() = default;

bool MetalGraphicsSystem::IsAvailable() {
  id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
  if (dev) { [dev release]; return true; }
  return false;
}

std::string MetalGraphicsSystem::name() const { return "Metal"; }

X_STATUS MetalGraphicsSystem::Setup(runtime::Processor* processor,
                                     system::KernelState* kernel_state,
                                     ui::WindowedAppContext* app_context,
                                     bool with_presentation) {
  auto status = GraphicsSystem::Setup(processor, kernel_state, app_context, with_presentation);
  if (status != X_STATUS_SUCCESS) return status;
  REXLOG_INFO("MetalGraphicsSystem: setup complete");
  return X_STATUS_SUCCESS;
}

std::unique_ptr<CommandProcessor> MetalGraphicsSystem::CreateCommandProcessor() {
  return std::make_unique<MetalCommandProcessor>(this, kernel_state());
}

}  // namespace metal
}  // namespace graphics
}  // namespace rex
