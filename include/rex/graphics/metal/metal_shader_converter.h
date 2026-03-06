#pragma once
/**
 * Metal Shader Converter wrapper — DXIL → Metal IR (metallib).
 *
 * Wraps Apple's Metal IR Converter (libmetalirconverter.dylib).
 * Implements the full Xbox 360 root signature matching Xenia Mac's
 * metal_shader_converter.cc (xenia-canary-mac-rebase).
 *
 * Critical correctness requirements (from RENDERING_PIPELINE_ANALYSIS.md):
 *   - IRCompatibilityFlagForceTextureArray  MUST be set — Xenos shaders
 *     expect texture2d_array; MSC 3.0+ defaults to non-array without it.
 *   - IRCompilerIgnoreRootSignature(true)   MUST be set — DXBC translator
 *     embeds a root signature that conflicts with the Xbox 360 one.
 *   - Full Xbox 360 root signature: SRV/UAV/CBV/Sampler ranges matching
 *     DxbcShaderTranslator's SM 5.1 output.
 *
 * @adapted 2026 — from xenia-canary-mac-rebase, ReXGlue namespace
 */

#include <cstdint>
#include <string>
#include <vector>

#include <rex/graphics/xenos.h>

namespace MTL { class Device; }  // metal-cpp forward decl

namespace rex {
namespace graphics {
namespace metal {

struct MetalShaderReflectionInput {
  std::string name;
  uint32_t attribute_index = 0;
};

struct MetalShaderFunctionConstant {
  std::string name;
  uint32_t type = 0;
};

struct MetalShaderReflectionInfo {
  std::vector<MetalShaderReflectionInput> vertex_inputs;
  std::vector<MetalShaderFunctionConstant> function_constants;
  uint32_t vertex_output_size_in_bytes = 0;
  uint32_t vertex_input_count = 0;
  uint32_t gs_max_input_primitives_per_mesh_threadgroup = 0;
  bool has_hull_info = false;
  bool has_domain_info = false;
  uint32_t hs_max_patches_per_object_threadgroup = 0;
  uint32_t hs_max_object_threads_per_patch = 0;
  uint32_t hs_patch_constants_size = 0;
  uint32_t hs_input_control_point_count = 0;
  uint32_t hs_output_control_point_count = 0;
  uint32_t hs_output_control_point_size = 0;
  uint32_t hs_tessellator_domain = 0;
  uint32_t hs_tessellator_partitioning = 0;
  uint32_t hs_tessellator_output_primitive = 0;
  bool hs_tessellation_type_half = false;
  float hs_max_tessellation_factor = 0.0f;
  uint32_t ds_max_input_prims_per_mesh_threadgroup = 0;
  uint32_t ds_input_control_point_count = 0;
  uint32_t ds_input_control_point_size = 0;
  uint32_t ds_patch_constants_size = 0;
  uint32_t ds_tessellator_domain = 0;
  bool ds_tessellation_type_half = false;
};

struct MetalShaderConversionResult {
  bool success = false;
  std::vector<uint8_t> metallib_data;
  std::string function_name;
  std::string error_message;
  bool has_mesh_stage = false;
  bool has_geometry_stage = false;
};

enum class MetalShaderStage {
  kVertex,
  kFragment,
  kGeometry,
  kCompute,
  kHull,
  kDomain,
};

class MetalShaderConverter {
 public:
  MetalShaderConverter();
  ~MetalShaderConverter();

  // Initialise MSC. Returns false if libmetalirconverter is not available.
  bool Initialize();

  bool IsAvailable() const { return is_available_; }

  // Set minimum GPU family + OS version for shader compilation.
  // Must be called after Initialize() and before the first Convert().
  void SetMinimumTarget(uint32_t gpu_family, uint32_t os,
                        const std::string& version);

  // Probe the MTL::Device GPU family and OS version and call SetMinimumTarget
  // automatically. Keeps all IRGPUFamily* types contained in the .mm file.
  void SetupForDevice(MTL::Device* device);

  // Convert DXIL bytecode to a Metal .metallib blob.
  bool Convert(xenos::ShaderType shader_type,
               const std::vector<uint8_t>& dxil_data,
               MetalShaderConversionResult& result);

 private:
  bool ConvertWithStage(MetalShaderStage stage,
                        const std::vector<uint8_t>& dxil_data,
                        MetalShaderConversionResult& result);

  // Returns an IRRootSignature* cast to void* to avoid polluting headers.
  void* CreateXbox360RootSignature(MetalShaderStage stage,
                                   bool force_all_visibility);
  void DestroyRootSignature(void* root_sig);

  bool is_available_ = false;
  bool has_minimum_target_ = false;
  uint32_t minimum_gpu_family_ = 0;
  uint32_t minimum_os_ = 0;
  std::string minimum_os_version_;

  // Register space used by MSC for function-constant specialisation.
  static constexpr uint32_t kFunctionConstantRegisterSpace = 2147420894u;
};

}  // namespace metal
}  // namespace graphics
}  // namespace rex
