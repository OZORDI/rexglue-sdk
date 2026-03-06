/**
 * Metal GPU command processor.
 *
 * Shader pipeline:
 *   Xenos ucode -> DXBC  (DxbcShaderTranslator)
 *              -> DXIL  (libdxilconv via DxbcToDxilConverter)
 *              -> metallib  (libmetalirconverter via MetalShaderConverter)
 *              -> MTLFunction
 *
 * Code-review fixes vs original port (2026-03-06):
 *   [CR-1] Use MetalShaderConverter with full Xbox 360 root signature instead
 *          of inline IRCompiler with empty IRRootSignatureDescriptor1{}.
 *   [CR-2] Entry-point names resolved via MSC reflection, not hardcoded
 *          "vs_main"/"ps_main". Probes fallback names if reflection fails.
 *   [CR-3] __bridge + retain() instead of __bridge_retained to avoid the
 *          double-free caused by ARC releasing lib_objc at scope exit.
 *   [CR-4] GPU family + OS version targeting so MSC doesn't emit instructions
 *          unsupported on older Apple Silicon (e.g. M1 = Apple7 family).
 *   [CR-5] Restored DXBC/DXIL debug dump env-vars (REX_DXBC_OUTPUT_DIR /
 *          REX_DXIL_OUTPUT_DIR) — required for diagnosing error 0x9AE4BC00.
 */

#include <rex/graphics/metal/command_processor.h>

#import <Metal/Metal.h>
#include <dispatch/dispatch.h>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include <rex/graphics/metal/graphics_system.h>
#include <rex/graphics/metal/metal_shader_converter.h>
#include <rex/graphics/metal/shader.h>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/string/buffer.h>
#include <rex/ui/metal/metal_provider.h>
#include <rex/ui/presenter.h>

#include <xxhash.h>

#ifndef DISPATCH_DATA_DESTRUCTOR_NONE
#define DISPATCH_DATA_DESTRUCTOR_NONE DISPATCH_DATA_DESTRUCTOR_DEFAULT
#endif

namespace rex {
namespace graphics {
namespace metal {

MetalCommandProcessor::MetalCommandProcessor(
    MetalGraphicsSystem* graphics_system,
    system::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}

MetalCommandProcessor::~MetalCommandProcessor() {
  ShutdownContext();
}

bool MetalCommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) return false;

  auto* provider =
      static_cast<ui::metal::MetalProvider*>(graphics_system_->provider());
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

  REXLOG_INFO("MetalCommandProcessor: Ready on '{}'",
              device_->name()->utf8String());
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
  metal_shader_converter_.reset();
  shader_cache_.clear();
  dxbc_translator_.reset();
  CommandProcessor::ShutdownContext();
}

bool MetalCommandProcessor::InitializeShaderTranslation() {
  // DXBC -> DXIL
  dxbc_to_dxil_converter_.Initialize();
  if (!dxbc_to_dxil_converter_.is_available()) {
    REXLOG_WARN(
        "MetalCommandProcessor: libdxilconv not available — "
        "shader translation disabled");
  }

  // [CR-1] Full MetalShaderConverter with Xbox 360 root signature.
  metal_shader_converter_ = std::make_unique<MetalShaderConverter>();
  if (!metal_shader_converter_->Initialize()) {
    REXLOG_ERROR(
        "MetalCommandProcessor: MetalShaderConverter init failed — "
        "libmetalirconverter.dylib not available");
    return false;
  }

  // [CR-4] Probe GPU family + OS version via MetalShaderConverter helper.
  // All IRGPUFamily* / IROperatingSystem constants are encapsulated in
  // metal_shader_converter.mm to avoid polluting this translation unit.
  metal_shader_converter_->SetupForDevice(device_);

  REXLOG_INFO("MetalCommandProcessor: Shader translation ready");
  return true;
}

void MetalCommandProcessor::ClearCaches() {
  CommandProcessor::ClearCaches();
  shader_cache_.clear();
}

void MetalCommandProcessor::TracePlaybackWroteMemory(uint32_t, uint32_t) {}
void MetalCommandProcessor::RestoreEdramSnapshot(const void*) {}

void MetalCommandProcessor::OnGammaRamp256EntryTableValueWritten() {
  gamma_ramp_table_dirty_ = true;
}
void MetalCommandProcessor::OnGammaRampPWLValueWritten() {
  gamma_ramp_pwl_dirty_ = true;
}

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

  auto shader = std::make_unique<MetalShader>(
      shader_type, hash, host_address, dword_count);
  MetalShader* raw = shader.get();
  shader_cache_.emplace(hash, std::move(shader));

  TranslateShaderToMetal(raw);
  return raw;
}

bool MetalCommandProcessor::TranslateShaderToMetal(MetalShader* shader) {
  // Step 1: Analyse ucode (required before DXBC translation)
  if (!shader->is_ucode_analyzed()) {
    rex::string::StringBuffer buf;
    shader->AnalyzeUcode(buf);
  }

  // Step 2: ucode -> DXBC
  if (!dxbc_translator_) {
    dxbc_translator_ = std::make_unique<DxbcShaderTranslator>(
        rex::ui::GraphicsProvider::GpuVendorID::kApple,
        /*bindless_resources_used=*/false,
        /*edram_rov_used=*/false);
  }

  Shader::Translation* translation = shader->GetOrCreateTranslation(0);
  if (!dxbc_translator_->TranslateAnalyzedShader(*translation)) {
    REXLOG_WARN("MetalCommandProcessor: DXBC translation failed for {:016X}",
                shader->ucode_data_hash());
    return false;
  }

  const auto& dxbc_data = translation->translated_binary();
  if (dxbc_data.empty()) return false;
  shader->set_dxbc_data(dxbc_data);

  // [CR-5] Debug dump of DXBC before conversion (helps diagnose 0x9AE4BC00)
  if (const char* dxbc_dir = std::getenv("REX_DXBC_OUTPUT_DIR")) {
    char path[512];
    snprintf(path, sizeof(path), "%s/shader_%016llx.dxbc",
             dxbc_dir,
             static_cast<unsigned long long>(shader->ucode_data_hash()));
    if (FILE* f = fopen(path, "wb")) {
      fwrite(dxbc_data.data(), 1, dxbc_data.size(), f);
      fclose(f);
      REXLOG_DEBUG("MetalCommandProcessor: dumped DXBC to {}", path);
    }
  }

  // Step 3: DXBC -> DXIL
  if (!dxbc_to_dxil_converter_.is_available()) return false;

  std::vector<uint8_t> dxil_data;
  std::string err;
  if (!dxbc_to_dxil_converter_.Convert(dxbc_data, dxil_data, &err)) {
    REXLOG_WARN("MetalCommandProcessor: DXBC->DXIL failed for {:016X}: {}",
                shader->ucode_data_hash(), err);
    return false;
  }

  // [CR-5] Debug dump of DXIL output
  if (const char* dxil_dir = std::getenv("REX_DXIL_OUTPUT_DIR")) {
    char path[512];
    snprintf(path, sizeof(path), "%s/shader_%016llx.dxil",
             dxil_dir,
             static_cast<unsigned long long>(shader->ucode_data_hash()));
    if (FILE* f = fopen(path, "wb")) {
      fwrite(dxil_data.data(), 1, dxil_data.size(), f);
      fclose(f);
      REXLOG_DEBUG("MetalCommandProcessor: dumped DXIL to {}", path);
    }
  }

  // Step 4: DXIL -> metallib via MetalShaderConverter
  // [CR-1] Uses full Xbox 360 root signature + IRCompatibilityFlagForceTextureArray
  if (!metal_shader_converter_ || !metal_shader_converter_->IsAvailable()) {
    REXLOG_ERROR("MetalCommandProcessor: MetalShaderConverter not available");
    return false;
  }

  MetalShaderConversionResult msc;
  if (!metal_shader_converter_->Convert(shader->type(), dxil_data, msc)) {
    REXLOG_WARN("MetalCommandProcessor: DXIL->Metal failed for {:016X}: {}",
                shader->ucode_data_hash(), msc.error_message);
    return false;
  }

  // Step 5: Create MTLLibrary from metallib bytes
  // [CR-3] __bridge (no ARC transfer) + explicit retain() to avoid double-free.
  //   __bridge_retained would increment the refcount, then ARC would release
  //   lib_objc at scope exit causing a use-after-free.
  id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;
  NSError* ns_err = nil;
  dispatch_data_t mtl_data = dispatch_data_create(
      msc.metallib_data.data(), msc.metallib_data.size(),
      nullptr, DISPATCH_DATA_DESTRUCTOR_NONE);
  id<MTLLibrary> lib_objc = [dev newLibraryWithData:mtl_data error:&ns_err];
  dispatch_release(mtl_data);

  if (!lib_objc) {
    if (ns_err) {
      REXLOG_ERROR("MetalCommandProcessor: newLibraryWithData failed: {}",
                   [[ns_err localizedDescription] UTF8String]);
    }
    return false;
  }

  MTL::Library* lib = (__bridge MTL::Library*)lib_objc;
  lib->retain();  // we now own it; release below after extracting the function

  // Step 6: Look up the entry-point function.
  // [CR-2] Use reflection-derived name, fall back to common MSC output names.
  MTL::Function* fn = lib->newFunction(
      NS::String::string(msc.function_name.c_str(), NS::UTF8StringEncoding));

  if (!fn) {
    REXLOG_WARN(
        "MetalCommandProcessor: function '{}' not in metallib — probing "
        "fallbacks",
        msc.function_name);
    static const char* const kAlt[] = {
        "main0", "main", "vertexMain", "fragmentMain", nullptr};
    for (const char* const* a = kAlt; *a; ++a) {
      fn = lib->newFunction(
          NS::String::string(*a, NS::UTF8StringEncoding));
      if (fn) {
        REXLOG_DEBUG("MetalCommandProcessor: found fn via fallback '{}'", *a);
        break;
      }
    }
  }

  lib->release();

  if (!fn) {
    NS::Array* names = lib->functionNames();
    REXLOG_ERROR(
        "MetalCommandProcessor: no entry point found for {:016X}. "
        "Available functions:",
        shader->ucode_data_hash());
    for (NS::UInteger i = 0; names && i < names->count(); ++i) {
      REXLOG_ERROR("  {}",
                   static_cast<NS::String*>(names->object(i))->utf8String());
    }
    return false;
  }

  shader->SetMetalFunction(fn);
  fn->release();

  REXLOG_DEBUG("MetalCommandProcessor: shader {:016X} compiled (fn={})",
               shader->ucode_data_hash(), msc.function_name);
  return true;
}

bool MetalCommandProcessor::IssueDraw(xenos::PrimitiveType, uint32_t,
                                       IndexBufferInfo*, bool) {
  // TODO: full draw implementation (pipeline state, vertex/index binding)
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
