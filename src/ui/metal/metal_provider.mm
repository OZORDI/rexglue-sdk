/**
 * Metal graphics provider.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/ui/metal/metal_provider.h>

#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <rex/logging.h>
#include <rex/ui/metal/metal_immediate_drawer.h>
#include <rex/ui/metal/metal_presenter.h>

namespace rex {
namespace ui {
namespace metal {

bool MetalProvider::IsMetalAPIAvailable() {
  MTL::Device* device = MTL::CreateSystemDefaultDevice();
  bool available = (device != nullptr);
  if (device) {
    device->release();
  }
  return available;
}

std::unique_ptr<MetalProvider> MetalProvider::Create(bool with_presentation) {
  auto provider = std::unique_ptr<MetalProvider>(new MetalProvider());
  if (!provider->Initialize(with_presentation)) {
    REXLOG_ERROR("MetalProvider: Failed to initialize Metal Graphics Subsystem");
    return nullptr;
  }
  return provider;
}

MetalProvider::~MetalProvider() {
  if (command_queue_) {
    command_queue_->release();
    command_queue_ = nullptr;
  }
  if (device_) {
    device_->release();
    device_ = nullptr;
  }
}

bool MetalProvider::Initialize(bool with_presentation) {
  device_ = MTL::CreateSystemDefaultDevice();
  if (!device_) {
    REXLOG_ERROR("MetalProvider: Failed to create Metal device");
    return false;
  }
  REXLOG_INFO("MetalProvider: Device: {}", device_->name()->utf8String());

  command_queue_ = device_->newCommandQueue();
  if (!command_queue_) {
    REXLOG_ERROR("MetalProvider: Failed to create command queue");
    return false;
  }

#if !defined(NDEBUG)
  setenv("METAL_DEVICE_WRAPPER_TYPE", "1", 0);
  setenv("METAL_DEBUG_ERROR_MODE", "assert", 0);
  REXLOG_INFO("MetalProvider: Metal validation layer enabled (Debug build)");
#endif

  return true;
}

std::unique_ptr<Presenter> MetalProvider::CreatePresenter(
    Presenter::HostGpuLossCallback host_gpu_loss_callback) {
  auto presenter = std::make_unique<MetalPresenter>(this, host_gpu_loss_callback);
  if (!presenter->Initialize()) {
    REXLOG_ERROR("MetalProvider: Presenter failed to initialize");
    return nullptr;
  }
  return std::move(presenter);
}

std::unique_ptr<ImmediateDrawer> MetalProvider::CreateImmediateDrawer() {
  auto drawer = std::make_unique<MetalImmediateDrawer>(this);
  if (!drawer->Initialize()) {
    REXLOG_ERROR("MetalProvider: ImmediateDrawer failed to initialize");
    return nullptr;
  }
  return std::move(drawer);
}

}  // namespace metal
}  // namespace ui
}  // namespace rex
