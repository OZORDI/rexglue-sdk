#pragma once
/**
 * Metal graphics provider.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <memory>

#include <rex/ui/graphics_provider.h>

namespace MTL {
class Device;
class CommandQueue;
}  // namespace MTL

namespace rex {
namespace ui {
namespace metal {

class MetalProvider : public GraphicsProvider {
 public:
  static std::unique_ptr<MetalProvider> Create(bool with_presentation);
  static bool IsMetalAPIAvailable();

  ~MetalProvider() override;

  std::unique_ptr<Presenter> CreatePresenter(
      Presenter::HostGpuLossCallback host_gpu_loss_callback =
          Presenter::FatalErrorHostGpuLossCallback) override;
  std::unique_ptr<ImmediateDrawer> CreateImmediateDrawer() override;

  MTL::Device* device() const { return device_; }
  MTL::CommandQueue* command_queue() const { return command_queue_; }

 private:
  MetalProvider() = default;
  bool Initialize(bool with_presentation);

  MTL::Device* device_ = nullptr;
  MTL::CommandQueue* command_queue_ = nullptr;
};

}  // namespace metal
}  // namespace ui
}  // namespace rex
