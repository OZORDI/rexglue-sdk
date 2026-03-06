#pragma once
/**
 * Metal GPU command processor.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/graphics/command_processor.h>
#include <rex/graphics/metal/dxbc_to_dxil_converter.h>
#include <rex/graphics/metal/metal_shader_converter.h>
#include <rex/graphics/xenos.h>

// metal-cpp forward declarations
namespace MTL {
class Device;
class CommandQueue;
class CommandBuffer;
class RenderCommandEncoder;
class RenderPassDescriptor;
}  // namespace MTL

// IRConverter forward declaration
struct IRCompiler;

namespace rex {
namespace graphics {
class DxbcShaderTranslator;  // forward declaration in correct namespace
namespace metal {

class MetalGraphicsSystem;
class MetalShader;

class MetalCommandProcessor : public CommandProcessor {
 public:
  MetalCommandProcessor(MetalGraphicsSystem* graphics_system,
                        system::KernelState* kernel_state);
  ~MetalCommandProcessor() override;

  void ClearCaches() override;
  void TracePlaybackWroteMemory(uint32_t base_ptr, uint32_t length) override;
  void RestoreEdramSnapshot(const void* snapshot) override;

  MTL::Device* GetMetalDevice() const { return device_; }
  MTL::CommandQueue* GetMetalCommandQueue() const { return command_queue_; }
  MTL::CommandBuffer* EnsureCommandBuffer();
  void EndRenderEncoder();

 protected:
  bool SetupContext() override;
  void ShutdownContext() override;

  void OnGammaRamp256EntryTableValueWritten() override;
  void OnGammaRampPWLValueWritten() override;
  void PrepareForWait() override;

  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

  Shader* LoadShader(xenos::ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address, uint32_t dword_count) override;

  bool IssueDraw(xenos::PrimitiveType prim_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info, bool major_mode_explicit) override;
  bool IssueCopy() override;
  void WriteRegister(uint32_t index, uint32_t value) override;

 private:
  bool InitializeShaderTranslation();
  bool TranslateShaderToMetal(MetalShader* shader);
  void BeginCommandBuffer();
  void EndCommandBuffer();

  MTL::Device* device_ = nullptr;
  MTL::CommandQueue* command_queue_ = nullptr;
  MTL::CommandBuffer* current_command_buffer_ = nullptr;
  MTL::RenderCommandEncoder* current_render_encoder_ = nullptr;

  // Shader cache (owned here, unlike Vulkan which uses pipeline_cache_)
  std::unordered_map<uint64_t, std::unique_ptr<MetalShader>> shader_cache_;

  DxbcToDxilConverter dxbc_to_dxil_converter_;
  std::unique_ptr<MetalShaderConverter> metal_shader_converter_;
  std::unique_ptr<rex::graphics::DxbcShaderTranslator> dxbc_translator_;

  IRCompiler* ir_compiler_ = nullptr;

  bool gamma_ramp_table_dirty_ = true;
  bool gamma_ramp_pwl_dirty_ = true;
  bool saw_swap_ = false;
};

}  // namespace metal
}  // namespace graphics
}  // namespace rex
