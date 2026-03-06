/**
 * Metal GPU shader wrapper.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/graphics/metal/shader.h>

#include <Metal/Metal.hpp>

namespace rex {
namespace graphics {
namespace metal {

MetalShader::MetalShader(xenos::ShaderType shader_type, uint64_t ucode_data_hash,
                         const uint32_t* ucode_dwords, size_t ucode_dword_count)
    : Shader(shader_type, ucode_data_hash, ucode_dwords, ucode_dword_count) {}

MetalShader::~MetalShader() {
  if (metal_function_) {
    metal_function_->release();
    metal_function_ = nullptr;
  }
}

void MetalShader::SetMetalFunction(MTL::Function* function) {
  if (metal_function_) metal_function_->release();
  metal_function_ = function;
  if (metal_function_) metal_function_->retain();
}

}  // namespace metal
}  // namespace graphics
}  // namespace rex
