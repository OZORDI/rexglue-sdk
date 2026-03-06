/**
 * Metal Shader Converter — DXIL → Metal IR (metallib).
 *
 * Ported from Xenia Mac's metal_shader_converter.cc (xenia-canary-mac-rebase).
 * Key changes vs original:
 *   - REXLOG_* instead of XELOGE/XELOGI/XELOGD
 *   - Namespace rex::graphics::metal instead of xe::gpu::metal
 *
 * Why the Xbox 360 root signature matters:
 *   DxbcShaderTranslator emits SM 5.1 DXBC with specific descriptor-table
 *   layouts (SRVs in spaces 0-3/10, UAVs 0-3, Samplers in space 0, CBVs).
 *   The root signature passed to IRCompilerSetGlobalRootSignature MUST match
 *   that layout exactly, or resource bindings will be wrong at draw time.
 *
 * Why IRCompatibilityFlagForceTextureArray is required:
 *   Xenia's DXBC translator generates code expecting texture2d_array bindings.
 *   MSC 3.0+ defaults to texture2d without this flag, causing all textured
 *   draws to sample from the wrong texture type.
 */

#include <rex/graphics/metal/metal_shader_converter.h>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#define IR_RUNTIME_METALCPP
#include <metal_irconverter/metal_irconverter.h>
#include <metal_irconverter_runtime/metal_irconverter_runtime.h>

#include <rex/logging.h>

namespace rex {
namespace graphics {
namespace metal {

MetalShaderConverter::MetalShaderConverter() = default;
MetalShaderConverter::~MetalShaderConverter() = default;

void MetalShaderConverter::SetMinimumTarget(uint32_t gpu_family, uint32_t os,
                                            const std::string& version) {
  has_minimum_target_  = true;
  minimum_gpu_family_  = gpu_family;
  minimum_os_          = os;
  minimum_os_version_  = version;
}

bool MetalShaderConverter::Initialize() {
  IRCompiler* test = IRCompilerCreate();
  if (!test) {
    REXLOG_ERROR(
        "MetalShaderConverter: IRCompilerCreate failed — "
        "libmetalirconverter.dylib not available");
    is_available_ = false;
    return false;
  }
  IRCompilerDestroy(test);
  is_available_ = true;
  REXLOG_INFO("MetalShaderConverter: initialized");
  return true;
}

// ---------------------------------------------------------------------------
// Xbox 360 root signature
// Matches the Xenos resource layout used by DxbcShaderTranslator (SM 5.1).
//   SRV  spaces 0-3 + space 10  (1025 descriptors each)
//   UAV  spaces 0-3             (1025 descriptors each)
//   Sampler space 0             (257 descriptors)
//   CBV  space 0 = 5 slots (b0-b4), spaces 1-3 = 1 slot
//   CBV  space kFunctionConstantRegisterSpace (1 slot — MSC specialisation)
// ---------------------------------------------------------------------------
void* MetalShaderConverter::CreateXbox360RootSignature(
    MetalShaderStage stage, bool force_all_visibility) {
  IRShaderVisibility vis = IRShaderVisibilityAll;
  if (!force_all_visibility) {
    switch (stage) {
      case MetalShaderStage::kVertex:   vis = IRShaderVisibilityVertex; break;
      case MetalShaderStage::kFragment: vis = IRShaderVisibilityPixel;  break;
      case MetalShaderStage::kHull:     vis = IRShaderVisibilityHull;   break;
      case MetalShaderStage::kDomain:   vis = IRShaderVisibilityDomain; break;
      default:                          vis = IRShaderVisibilityAll;    break;
    }
  }

  IRDescriptorRange1 ranges[20] = {};
  int n = 0;

  // SRVs in spaces 0-3
  for (int s = 0; s < 4; s++, n++) {
    ranges[n].RangeType = IRDescriptorRangeTypeSRV;
    ranges[n].NumDescriptors = 1025;
    ranges[n].BaseShaderRegister = 0;
    ranges[n].RegisterSpace = s;
    ranges[n].Flags = IRDescriptorRangeFlagNone;
    ranges[n].OffsetInDescriptorsFromTableStart = 0;
  }
  // SRV space 10 (hull shader path)
  ranges[n].RangeType = IRDescriptorRangeTypeSRV;
  ranges[n].NumDescriptors = 1025;
  ranges[n].BaseShaderRegister = 0;
  ranges[n].RegisterSpace = 10;
  ranges[n].Flags = IRDescriptorRangeFlagNone;
  ranges[n].OffsetInDescriptorsFromTableStart = 0;
  n++;

  // UAVs in spaces 0-3
  for (int s = 0; s < 4; s++, n++) {
    ranges[n].RangeType = IRDescriptorRangeTypeUAV;
    ranges[n].NumDescriptors = 1025;
    ranges[n].BaseShaderRegister = 0;
    ranges[n].RegisterSpace = s;
    ranges[n].Flags = IRDescriptorRangeFlagNone;
    ranges[n].OffsetInDescriptorsFromTableStart = 0;
  }

  // Sampler space 0
  ranges[n].RangeType = IRDescriptorRangeTypeSampler;
  ranges[n].NumDescriptors = 257;
  ranges[n].BaseShaderRegister = 0;
  ranges[n].RegisterSpace = 0;
  ranges[n].Flags = IRDescriptorRangeFlagNone;
  ranges[n].OffsetInDescriptorsFromTableStart = 0;
  n++;

  // CBVs spaces 0-3 (space 0 = 5 slots, others = 1)
  for (int s = 0; s < 4; s++, n++) {
    ranges[n].RangeType = IRDescriptorRangeTypeCBV;
    ranges[n].NumDescriptors = (s == 0) ? 5 : 1;
    ranges[n].BaseShaderRegister = 0;
    ranges[n].RegisterSpace = s;
    ranges[n].Flags = IRDescriptorRangeFlagNone;
    ranges[n].OffsetInDescriptorsFromTableStart = 0;
  }

  // Function-constant CBV for MSC specialisation
  ranges[n].RangeType = IRDescriptorRangeTypeCBV;
  ranges[n].NumDescriptors = 1;
  ranges[n].BaseShaderRegister = 0;
  ranges[n].RegisterSpace = kFunctionConstantRegisterSpace;
  ranges[n].Flags = IRDescriptorRangeFlagNone;
  ranges[n].OffsetInDescriptorsFromTableStart = 0;
  n++;

  IRRootDescriptorTable1 tables[20] = {};
  IRRootParameter1 params[20] = {};
  for (int i = 0; i < n; i++) {
    tables[i].NumDescriptorRanges = 1;
    tables[i].pDescriptorRanges   = &ranges[i];
    params[i].ParameterType    = IRRootParameterTypeDescriptorTable;
    params[i].DescriptorTable  = tables[i];
    params[i].ShaderVisibility = vis;
  }

  IRRootSignatureDescriptor1 desc = {};
  desc.NumParameters     = n;
  desc.pParameters       = params;
  desc.NumStaticSamplers = 0;
  desc.pStaticSamplers   = nullptr;
  desc.Flags             = IRRootSignatureFlagNone;

  IRVersionedRootSignatureDescriptor versioned = {};
  versioned.version  = IRRootSignatureVersion_1_1;
  versioned.desc_1_1 = desc;

  IRError* error = nullptr;
  IRRootSignature* rs = IRRootSignatureCreateFromDescriptor(&versioned, &error);
  if (error) {
    const char* msg = static_cast<const char*>(IRErrorGetPayload(error));
    REXLOG_ERROR("MetalShaderConverter: root signature creation failed: {}",
                 msg ? msg : "unknown");
    IRErrorDestroy(error);
    return nullptr;
  }
  return rs;
}

void MetalShaderConverter::DestroyRootSignature(void* rs) {
  if (rs) IRRootSignatureDestroy(static_cast<IRRootSignature*>(rs));
}

bool MetalShaderConverter::Convert(xenos::ShaderType shader_type,
                                   const std::vector<uint8_t>& dxil_data,
                                   MetalShaderConversionResult& result) {
  MetalShaderStage stage;
  switch (shader_type) {
    case xenos::ShaderType::kVertex: stage = MetalShaderStage::kVertex;   break;
    case xenos::ShaderType::kPixel:  stage = MetalShaderStage::kFragment; break;
    default:
      result.success = false;
      result.error_message = "Unsupported shader type";
      return false;
  }
  return ConvertWithStage(stage, dxil_data, result);
}

bool MetalShaderConverter::ConvertWithStage(MetalShaderStage stage,
                                             const std::vector<uint8_t>& dxil_data,
                                             MetalShaderConversionResult& result) {
  if (!is_available_) {
    result.success = false;
    result.error_message = "MetalShaderConverter not initialized";
    return false;
  }
  if (dxil_data.empty()) {
    result.success = false;
    result.error_message = "Empty DXIL data";
    return false;
  }

  IRObject* dxil_obj = IRObjectCreateFromDXIL(
      dxil_data.data(), dxil_data.size(), IRBytecodeOwnershipNone);
  if (!dxil_obj) {
    result.success = false;
    result.error_message = "Failed to create DXIL object from bytes";
    return false;
  }

  IRCompiler* compiler = IRCompilerCreate();
  if (!compiler) {
    IRObjectDestroy(dxil_obj);
    result.success = false;
    result.error_message = "Failed to create IRCompiler";
    return false;
  }

  // --- Critical MSC flags ---
  // ForceTextureArray: Xenos shaders expect texture2d_array; without this
  //   MSC 3.0+ generates texture2d bindings and all textured draws break.
  // BoundsCheck: guards out-of-bounds buffer accesses.
  IRCompilerSetCompatibilityFlags(
      compiler,
      static_cast<IRCompatibilityFlags>(IRCompatibilityFlagForceTextureArray |
                                        IRCompatibilityFlagBoundsCheck));

  // Ignore any root signature embedded in the DXIL blob — we supply our own.
  IRCompilerIgnoreRootSignature(compiler, true);

  // Enable function-constant specialisation space.
  IRCompilerSetFunctionConstantResourceSpace(compiler,
                                             kFunctionConstantRegisterSpace);

  // Per-GPU minimum target (prevents generating unsupported instructions).
  if (has_minimum_target_) {
    IRCompilerSetMinimumGPUFamily(
        compiler, static_cast<IRGPUFamily>(minimum_gpu_family_));
    IRCompilerSetMinimumDeploymentTarget(
        compiler, static_cast<IROperatingSystem>(minimum_os_),
        minimum_os_version_.c_str());
  }

  // Set the full Xbox 360 root signature.
  auto* rs = static_cast<IRRootSignature*>(
      CreateXbox360RootSignature(stage, /*force_all_visibility=*/true));
  if (!rs) {
    IRCompilerDestroy(compiler);
    IRObjectDestroy(dxil_obj);
    result.success = false;
    result.error_message = "Failed to create Xbox 360 root signature";
    return false;
  }
  IRCompilerSetGlobalRootSignature(compiler, rs);

  // --- Compile ---
  IRError* error = nullptr;
  IRObject* metal_obj =
      IRCompilerAllocCompileAndLink(compiler, nullptr, dxil_obj, &error);
  if (error) {
    const char* msg = static_cast<const char*>(IRErrorGetPayload(error));
    result.success = false;
    result.error_message =
        std::string("MSC compile failed: ") + (msg ? msg : "unknown");
    REXLOG_ERROR("MetalShaderConverter: {}", result.error_message);
    IRErrorDestroy(error);
    IRRootSignatureDestroy(rs);
    IRCompilerDestroy(compiler);
    IRObjectDestroy(dxil_obj);
    return false;
  }
  if (!metal_obj) {
    result.success = false;
    result.error_message = "IRCompilerAllocCompileAndLink returned null";
    IRRootSignatureDestroy(rs);
    IRCompilerDestroy(compiler);
    IRObjectDestroy(dxil_obj);
    return false;
  }

  // --- Extract metallib bytecode ---
  IRShaderStage ir_stage = IRShaderStageInvalid;
  switch (stage) {
    case MetalShaderStage::kVertex:   ir_stage = IRShaderStageVertex;   break;
    case MetalShaderStage::kFragment: ir_stage = IRShaderStageFragment; break;
    case MetalShaderStage::kCompute:  ir_stage = IRShaderStageCompute;  break;
    case MetalShaderStage::kHull:     ir_stage = IRShaderStageHull;     break;
    case MetalShaderStage::kDomain:   ir_stage = IRShaderStageDomain;   break;
    default:                          ir_stage = IRShaderStageInvalid;  break;
  }

  if (ir_stage != IRShaderStageInvalid) {
    IRMetalLibBinary* mlib = IRMetalLibBinaryCreate();
    if (mlib) {
      if (IRObjectGetMetalLibBinary(metal_obj, ir_stage, mlib)) {
        size_t sz = IRMetalLibGetBytecodeSize(mlib);
        if (sz > 0) {
          result.metallib_data.resize(sz);
          IRMetalLibGetBytecode(mlib, result.metallib_data.data());
        }
      }
      IRMetalLibBinaryDestroy(mlib);
    }
  }

  if (result.metallib_data.empty()) {
    result.success = false;
    result.error_message = "Generated metallib has zero size";
    REXLOG_ERROR("MetalShaderConverter: {} (stage={})",
                 result.error_message, static_cast<int>(stage));
    IRObjectDestroy(metal_obj);
    IRRootSignatureDestroy(rs);
    IRCompilerDestroy(compiler);
    IRObjectDestroy(dxil_obj);
    return false;
  }

  // --- Reflection: resolve the actual entry-point function name ---
  IRShaderReflection* refl = IRShaderReflectionCreate();
  if (refl && ir_stage != IRShaderStageInvalid) {
    if (IRObjectGetReflection(metal_obj, ir_stage, refl)) {
      const char* entry = IRShaderReflectionGetEntryPointFunctionName(refl);
      if (entry && entry[0]) {
        result.function_name = entry;
      }
    }
    IRShaderReflectionDestroy(refl);
  }

  // Fallback names if reflection didn't return one.
  if (result.function_name.empty()) {
    switch (stage) {
      case MetalShaderStage::kVertex:   result.function_name = "vertexMain";   break;
      case MetalShaderStage::kFragment: result.function_name = "fragmentMain"; break;
      case MetalShaderStage::kCompute:  result.function_name = "computeMain";  break;
      default:                          result.function_name = "main";         break;
    }
  }

  REXLOG_DEBUG("MetalShaderConverter: {} B DXIL -> {} B metallib (fn={})",
               dxil_data.size(), result.metallib_data.size(),
               result.function_name);

  IRObjectDestroy(metal_obj);
  IRRootSignatureDestroy(rs);
  IRCompilerDestroy(compiler);
  IRObjectDestroy(dxil_obj);

  result.success = true;
  return true;
}


void MetalShaderConverter::SetupForDevice(MTL::Device* device) {
  if (!device) return;

  // metal-cpp bundled headers only define up to GPUFamilyApple9.
  // Cast numeric literals for anything beyond that to stay forward-compatible.
  uint32_t min_family = static_cast<uint32_t>(IRGPUFamilyMetal3);
  if      (device->supportsFamily(static_cast<MTL::GPUFamily>(1010)))
      min_family = static_cast<uint32_t>(IRGPUFamilyApple9);  // Apple10 -> treat as Apple9
  else if (device->supportsFamily(MTL::GPUFamilyApple9))
      min_family = static_cast<uint32_t>(IRGPUFamilyApple9);
  else if (device->supportsFamily(MTL::GPUFamilyApple8))
      min_family = static_cast<uint32_t>(IRGPUFamilyApple8);
  else if (device->supportsFamily(MTL::GPUFamilyApple7))
      min_family = static_cast<uint32_t>(IRGPUFamilyApple7);
  else if (device->supportsFamily(MTL::GPUFamilyApple6))
      min_family = static_cast<uint32_t>(IRGPUFamilyApple6);

  // Read macOS version via metal-cpp NS::ProcessInfo (no Obj-C needed).
  NS::OperatingSystemVersion v =
      NS::ProcessInfo::processInfo()->operatingSystemVersion();
  char ver[32];
  snprintf(ver, sizeof(ver), "%ld.%ld.%ld",
           (long)v.majorVersion, (long)v.minorVersion, (long)v.patchVersion);

  SetMinimumTarget(min_family, static_cast<uint32_t>(IROperatingSystem_macOS), ver);
  REXLOG_INFO("MetalShaderConverter: SetupForDevice gpu_family={} os={}",
              min_family, ver);
}

}  // namespace metal
}  // namespace graphics
}  // namespace rex
