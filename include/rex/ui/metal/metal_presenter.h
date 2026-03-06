#pragma once
/**
 * Metal presenter (CAMetalLayer-based).
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <array>
#include <atomic>
#include <cstddef>

#include <rex/ui/metal/metal_provider.h>
#include <rex/ui/presenter.h>
#include <rex/ui/surface.h>

#ifdef __OBJC__
@class CAMetalLayer;
#else
typedef struct objc_object CAMetalLayer;
typedef struct objc_object* id;
#endif

namespace MTL {
class Device;
class Texture;
class CommandQueue;
class RenderPipelineState;
class SamplerState;
class Buffer;
class ComputePipelineState;
}  // namespace MTL

namespace rex {
namespace ui {
namespace metal {

class MetalGuestOutputRefreshContext final : public Presenter::GuestOutputRefreshContext {
 public:
  MetalGuestOutputRefreshContext(bool& is_8bpc_out_ref, id resource)
      : Presenter::GuestOutputRefreshContext(is_8bpc_out_ref), resource_(resource) {}
  id resource_uav_capable() const { return resource_; }

 private:
  id resource_;
};

class MetalUIDrawContext final : public UIDrawContext {
 public:
  MetalUIDrawContext(Presenter& presenter, uint32_t render_target_width,
                     uint32_t render_target_height, id command_buffer, id render_encoder)
      : UIDrawContext(presenter, render_target_width, render_target_height),
        command_buffer_(command_buffer),
        render_encoder_(render_encoder) {}
  id command_buffer() const { return command_buffer_; }
  id render_encoder() const { return render_encoder_; }

 private:
  id command_buffer_;
  id render_encoder_;
};

class MetalPresenter : public Presenter {
 public:
  MetalPresenter(MetalProvider* provider, HostGpuLossCallback host_gpu_loss_callback);
  ~MetalPresenter() override;

  bool Initialize();

  Surface::TypeFlags GetSupportedSurfaceTypes() const override;
  bool CaptureGuestOutput(RawImage& image_out) override;

 protected:
  PaintResult PaintAndPresentImpl(bool execute_ui_drawers) override;
  SurfacePaintConnectResult ConnectOrReconnectPaintingToSurfaceFromUIThread(
      Surface& new_surface, uint32_t new_surface_width, uint32_t new_surface_height,
      bool was_paintable, bool& is_vsync_implicit_out) override;
  void DisconnectPaintingFromSurfaceFromUIThreadImpl() override;
  bool RefreshGuestOutputImpl(
      uint32_t mailbox_index, uint32_t frontbuffer_width, uint32_t frontbuffer_height,
      std::function<bool(GuestOutputRefreshContext& context)> refresher,
      bool& is_8bpc_out_ref) override;

 private:
  bool EnsureGuestOutputResources(uint32_t width, uint32_t height);
  bool EnsurePresentPipelines();

  MetalProvider* provider_;
  MTL::Device* device_ = nullptr;

  CAMetalLayer* metal_layer_ = nullptr;
  id command_queue_ = nullptr;

  // Guest output mailbox textures (3 for triple-buffering)
  std::array<id, kGuestOutputMailboxSize> guest_output_textures_{};
  uint32_t guest_output_width_ = 0;
  uint32_t guest_output_height_ = 0;

  // Present pipeline
  id guest_output_pipeline_ = nullptr;  // id<MTLRenderPipelineState>
  id guest_output_sampler_ = nullptr;   // id<MTLSamplerState>
  uint32_t present_pixel_format_ = 0;

  // MetalFX upscaler (optional)
  id metalfx_scaler_ = nullptr;
  id metalfx_output_texture_ = nullptr;
  uint32_t metalfx_input_width_ = 0;
  uint32_t metalfx_input_height_ = 0;
  uint32_t metalfx_output_width_ = 0;
  uint32_t metalfx_output_height_ = 0;

  float surface_scale_ = 1.0f;
  uint32_t surface_width_in_points_ = 0;
  uint32_t surface_height_in_points_ = 0;

  std::atomic<uint32_t> last_guest_output_mailbox_index_{UINT32_MAX};
};

}  // namespace metal
}  // namespace ui
}  // namespace rex
