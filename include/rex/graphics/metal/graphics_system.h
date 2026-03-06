#pragma once
/**
 * Metal graphics system.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <memory>

#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>

namespace rex {
namespace graphics {
namespace metal {

class MetalGraphicsSystem : public GraphicsSystem {
 public:
  MetalGraphicsSystem();
  ~MetalGraphicsSystem() override;

  static bool IsAvailable();

  std::string name() const override;

  X_STATUS Setup(runtime::Processor* processor, system::KernelState* kernel_state,
                 ui::WindowedAppContext* app_context, bool with_presentation) override;

 protected:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;
};

}  // namespace metal
}  // namespace graphics
}  // namespace rex
