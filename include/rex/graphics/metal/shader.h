#pragma once
/**
 * Metal GPU shader.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <string>
#include <vector>

#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/xenos.h>

namespace MTL {
class Library;
class Function;
}  // namespace MTL

namespace rex {
namespace graphics {
namespace metal {

class MetalShader : public Shader {
 public:
  MetalShader(xenos::ShaderType shader_type, uint64_t ucode_data_hash,
              const uint32_t* ucode_dwords, size_t ucode_dword_count);
  ~MetalShader() override;

  // The Metal function compiled from the translated source.
  MTL::Function* metal_function() const { return metal_function_; }
  bool is_metal_compiled() const { return metal_function_ != nullptr; }

  void SetMetalFunction(MTL::Function* function);

  // DXBC bytecode for this shader (set during translation).
  const std::vector<uint8_t>& dxbc_data() const { return dxbc_data_; }
  void set_dxbc_data(std::vector<uint8_t> data) { dxbc_data_ = std::move(data); }

 private:
  MTL::Function* metal_function_ = nullptr;
  std::vector<uint8_t> dxbc_data_;
};

}  // namespace metal
}  // namespace graphics
}  // namespace rex
