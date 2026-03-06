#pragma once
/**
 * Metal immediate mode drawer for UI rendering.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/ui/immediate_drawer.h>

namespace MTL {
class Device;
class Texture;
class RenderPipelineState;
class CommandBuffer;
class RenderCommandEncoder;
class SamplerState;
}  // namespace MTL

namespace rex {
namespace ui {
namespace metal {

class MetalProvider;

class MetalImmediateDrawer : public ImmediateDrawer {
 public:
  explicit MetalImmediateDrawer(MetalProvider* provider);
  ~MetalImmediateDrawer() override;

  bool Initialize();

  std::unique_ptr<ImmediateTexture> CreateTexture(uint32_t width, uint32_t height,
                                                   ImmediateTextureFilter filter, bool repeat,
                                                   const uint8_t* data) override;

  void Begin(UIDrawContext& ui_draw_context, float coordinate_space_width,
             float coordinate_space_height) override;
  void BeginDrawBatch(const ImmediateDrawBatch& batch) override;
  void Draw(const ImmediateDraw& draw) override;
  void EndDrawBatch() override;
  void End() override;

 private:
  class MetalImmediateTexture : public ImmediateTexture {
   public:
    MetalImmediateTexture(uint32_t width, uint32_t height)
        : ImmediateTexture(width, height) {}
    ~MetalImmediateTexture() override;
    MTL::Texture* texture = nullptr;
    MTL::SamplerState* sampler = nullptr;
  };

  MetalProvider* provider_;
  MTL::Device* device_ = nullptr;
  MTL::RenderPipelineState* pipeline_textured_ = nullptr;
  MTL::Texture* white_texture_ = nullptr;
  MTL::SamplerState* default_sampler_ = nullptr;

  MTL::CommandBuffer* current_command_buffer_ = nullptr;
  MTL::RenderCommandEncoder* current_render_encoder_ = nullptr;
  bool batch_open_ = false;
  void* current_index_buffer_ = nullptr;
};

}  // namespace metal
}  // namespace ui
}  // namespace rex
