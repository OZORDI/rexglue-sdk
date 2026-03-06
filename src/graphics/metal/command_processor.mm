/**
 * Metal GPU command processor.
 * Pipeline: Xenos ucode -> DXBC (DxbcShaderTranslator)
 *           -> DXIL (libdxilconv) -> Metal IR (libmetalirconverter) -> MTLLibrary
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/graphics/metal/command_processor.h>

#import <Metal/Metal.h>
#include <vector>


#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#define IR_RUNTIME_METALCPP
#include <metal_irconverter/metal_irconverter.h>
#include <metal_irconverter_runtime/metal_irconverter_runtime.h>

#include <rex/graphics/metal/graphics_system.h>
#include <rex/graphics/metal/shader.h>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/string/buffer.h>
#include <rex/ui/metal/metal_provider.h>
#include <rex/ui/presenter.h>

#include <xxhash.h>

namespace rex {
namespace graphics {
namespace metal {

MetalCommandProcessor::MetalCommandProcessor(MetalGraphicsSystem* graphics_system,
                                              system::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}

MetalCommandProcessor::~MetalCommandProcessor() {
  ShutdownContext();
}

bool MetalCommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) return false;

  auto* provider = static_cast<ui::metal::MetalProvider*>(graphics_system_->provider());
  if (!provider) {
    REXLOG_ERROR("MetalCommandProcessor: No Metal provider on graphics system");
    return false;
  }

  device_        = provider->device();
  command_queue_ = provider->command_queue();

  if (!device_ || !command_queue_) {
    REXLOG_ERROR("MetalCommandProcessor: Invalid Metal device or command queue");
    return false;
  }

  if (!InitializeShaderTranslation()) {
    REXLOG_ERROR("MetalCommandProcessor: Shader translation init failed");
    return false;
  }

  REXLOG_INFO("MetalCommandProcessor: Ready on '{}'", device_->name()->utf8String());
  return true;
}

void MetalCommandProcessor::ShutdownContext() {
  if (current_render_encoder_) {
    current_render_encoder_->endEncoding();
    current_render_encoder_ = nullptr;
  }
  if (current_command_buffer_) {
    current_command_buffer_->commit();
    current_command_buffer_->waitUntilCompleted();
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
  }
  if (ir_compiler_) {
    IRCompilerDestroy(ir_compiler_);
    ir_compiler_ = nullptr;
  }
  shader_cache_.clear();
  dxbc_translator_.reset();
  CommandProcessor::ShutdownContext();
}

bool MetalCommandProcessor::InitializeShaderTranslation() {
  dxbc_to_dxil_converter_.Initialize();
  if (!dxbc_to_dxil_converter_.is_available()) {
    REXLOG_WARN("MetalCommandProcessor: libdxilconv not available – shader translation disabled");
  }

  ir_compiler_ = IRCompilerCreate();
  if (!ir_compiler_) {
    REXLOG_ERROR("MetalCommandProcessor: IRCompilerCreate failed");
    return false;
  }

  // Minimal root signature for Xbox 360 shaders
  IRRootSignatureDescriptor1 rs_desc{};
  IRVersionedRootSignatureDescriptor versioned{};
  versioned.version  = IRRootSignatureVersion_1_1;
  versioned.desc_1_1 = rs_desc;

  IRError* rs_err = nullptr;
  IRRootSignature* rs = IRRootSignatureCreateFromDescriptor(&versioned, &rs_err);
  if (rs) {
    IRCompilerSetGlobalRootSignature(ir_compiler_, rs);
    IRRootSignatureDestroy(rs);
  } else {
    if (rs_err) IRErrorDestroy(rs_err);
    REXLOG_WARN("MetalCommandProcessor: Using default root signature");
  }

  REXLOG_INFO("MetalCommandProcessor: Shader translation ready");
  return true;
}

void MetalCommandProcessor::ClearCaches() {
  CommandProcessor::ClearCaches();
  shader_cache_.clear();
}

void MetalCommandProcessor::TracePlaybackWroteMemory(uint32_t, uint32_t) {}
void MetalCommandProcessor::RestoreEdramSnapshot(const void*) {}
void MetalCommandProcessor::OnGammaRamp256EntryTableValueWritten() { gamma_ramp_table_dirty_ = true; }
void MetalCommandProcessor::OnGammaRampPWLValueWritten()           { gamma_ramp_pwl_dirty_   = true; }

void MetalCommandProcessor::PrepareForWait() {
  EndRenderEncoder();
  if (current_command_buffer_) {
    current_command_buffer_->commit();
    current_command_buffer_->waitUntilCompleted();
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
  }
  CommandProcessor::PrepareForWait();
}

MTL::CommandBuffer* MetalCommandProcessor::EnsureCommandBuffer() {
  if (!current_command_buffer_) BeginCommandBuffer();
  return current_command_buffer_;
}

void MetalCommandProcessor::EndRenderEncoder() {
  if (current_render_encoder_) {
    current_render_encoder_->endEncoding();
    current_render_encoder_ = nullptr;
  }
}

void MetalCommandProcessor::BeginCommandBuffer() {
  current_command_buffer_ = command_queue_->commandBuffer();
  if (current_command_buffer_) current_command_buffer_->retain();
}

void MetalCommandProcessor::EndCommandBuffer() {
  EndRenderEncoder();
  if (current_command_buffer_) {
    current_command_buffer_->commit();
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
  }
}

void MetalCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                       uint32_t frontbuffer_width,
                                       uint32_t frontbuffer_height) {
  EndCommandBuffer();
  saw_swap_ = true;

  if (graphics_system_->is_paused()) return;

  uint32_t w = frontbuffer_width  ? frontbuffer_width  : 1280;
  uint32_t h = frontbuffer_height ? frontbuffer_height : 720;

  auto* presenter = graphics_system_->presenter();
  if (!presenter) return;

  presenter->RefreshGuestOutput(
      w, h, w, h,
      [](ui::Presenter::GuestOutputRefreshContext&) { return true; });
}

Shader* MetalCommandProcessor::LoadShader(xenos::ShaderType shader_type,
                                           uint32_t /*guest_address*/,
                                           const uint32_t* host_address,
                                           uint32_t dword_count) {
  uint64_t hash = XXH3_64bits(host_address, dword_count * sizeof(uint32_t));

  auto it = shader_cache_.find(hash);
  if (it != shader_cache_.end()) return it->second.get();

  auto shader = std::make_unique<MetalShader>(shader_type, hash,
                                               host_address, dword_count);
  MetalShader* raw = shader.get();
  shader_cache_.emplace(hash, std::move(shader));

  // Translate asynchronously (best-effort; draw calls fall back if not ready)
  TranslateShaderToMetal(raw);

  return raw;
}

bool MetalCommandProcessor::TranslateShaderToMetal(MetalShader* shader) {
  // Step 1: Analyse ucode (required before DXBC translation)
  if (!shader->is_ucode_analyzed()) {
    rex::string::StringBuffer disasm_buf; shader->AnalyzeUcode(disasm_buf);
  }

  // Step 2: ucode -> DXBC
  if (!dxbc_translator_) {
    dxbc_translator_ = std::make_unique<DxbcShaderTranslator>(
        rex::ui::GraphicsProvider::GpuVendorID::kApple,
        /* bindless_resources_used */ false,
        /* edram_rov_used */ false);
  }

  Shader::Translation* translation = shader->GetOrCreateTranslation(0);
  if (!dxbc_translator_->TranslateAnalyzedShader(*translation)) {
    REXLOG_WARN("MetalCommandProcessor: DXBC translation failed");
    return false;
  }

  const auto& dxbc_data = translation->translated_binary();
  if (dxbc_data.empty()) return false;
  shader->set_dxbc_data(dxbc_data);

  // Step 3: DXBC -> DXIL
  if (!dxbc_to_dxil_converter_.is_available()) return false;

  std::vector<uint8_t> dxil_data;
  std::string err;
  if (!dxbc_to_dxil_converter_.Convert(dxbc_data, dxil_data, &err)) {
    REXLOG_WARN("MetalCommandProcessor: DXBC->DXIL: {}", err);
    return false;
  }

  // Step 4: DXIL -> Metal IR
  IRObject* dxil_obj = IRObjectCreateFromDXIL(dxil_data.data(), dxil_data.size(),
                                               IRBytecodeOwnershipNone);
  if (!dxil_obj) return false;

  IRError* ir_err = nullptr;
  IRObject* metal_ir = IRCompilerAllocCompileAndLink(ir_compiler_, nullptr, dxil_obj, &ir_err);
  IRObjectDestroy(dxil_obj);
  if (!metal_ir) {
    if (ir_err) { IRErrorDestroy(ir_err); }
    return false;
  }

  // Step 5: Metal IR -> metallib -> MTLFunction
  IRShaderStage stage = (shader->type() == xenos::ShaderType::kVertex)
                            ? IRShaderStageVertex
                            : IRShaderStageFragment;

  IRMetalLibBinary* mlib = IRMetalLibBinaryCreate();
  IRObjectGetMetalLibBinary(metal_ir, stage, mlib);
  IRObjectDestroy(metal_ir);

  size_t mlib_sz = IRMetalLibGetBytecodeSize(mlib);
  if (mlib_sz == 0) { IRMetalLibBinaryDestroy(mlib); return false; }

  // Collect bytecode and create NSData via Obj-C bridge
  std::vector<uint8_t> mtl_bytecode(mlib_sz);
  IRMetalLibGetBytecode(mlib, mtl_bytecode.data());
  IRMetalLibBinaryDestroy(mlib);

  id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;
  NSError* ns_err_objc = nil;
  dispatch_data_t mtl_data = dispatch_data_create(
      mtl_bytecode.data(), mtl_bytecode.size(),
      nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  id<MTLLibrary> lib_objc = [dev newLibraryWithData:mtl_data error:&ns_err_objc];
  dispatch_release(mtl_data);
  if (!lib_objc) {
    if (ns_err_objc) REXLOG_ERROR("MetalCommandProcessor: newLibrary failed: {}",
        [[ns_err_objc localizedDescription] UTF8String]);
    return false;
  }
  MTL::Library* lib = (__bridge_retained MTL::Library*)lib_objc;

  const char* entry = (shader->type() == xenos::ShaderType::kVertex) ? "vs_main" : "ps_main";
  MTL::Function* fn = lib->newFunction(NS::String::string(entry, NS::UTF8StringEncoding));
  lib->release();
  if (!fn) return false;

  shader->SetMetalFunction(fn);
  fn->release();

  REXLOG_DEBUG("MetalCommandProcessor: Shader {:016X} translated", shader->ucode_data_hash());
  return true;
}

bool MetalCommandProcessor::IssueDraw(xenos::PrimitiveType, uint32_t,
                                       IndexBufferInfo*, bool) {
  // TODO: full implementation (pipeline state, vertex/index binding, dispatch)
  return true;
}

bool MetalCommandProcessor::IssueCopy() {
  // TODO: EDRAM resolve / copy-to-texture
  return true;
}

void MetalCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  CommandProcessor::WriteRegister(index, value);
}

}  // namespace metal
}  // namespace graphics
}  // namespace rex
