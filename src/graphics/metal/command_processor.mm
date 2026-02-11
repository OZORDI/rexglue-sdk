/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Metal backend for macOS (ReXGlue runtime)
 *              Phase 4: Full IssueDraw + IssueCopy implementation
 */

#import <Metal/Metal.h>

#include <algorithm>
#include <cinttypes>
#include <cstring>

#include <rex/graphics/metal/command_processor.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/profiling.h>
#include <rex/ui/metal/metal_presenter.h>
#include <rex/ui/metal/metal_provider.h>

namespace rex::graphics::metal {

MetalCommandProcessor::MetalCommandProcessor(
    MetalGraphicsSystem* graphics_system,
    kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state),
      metal_graphics_system_(graphics_system) {}

MetalCommandProcessor::~MetalCommandProcessor() = default;

ui::metal::MetalProvider& MetalCommandProcessor::GetMetalProvider() const {
  return *static_cast<ui::metal::MetalProvider*>(
      metal_graphics_system_->provider());
}

bool MetalCommandProcessor::Initialize() {
  if (!CommandProcessor::Initialize()) {
    return false;
  }
  XELOGI("MetalCommandProcessor: Initialized");
  return true;
}

void MetalCommandProcessor::Shutdown() {
  AwaitAllSubmissionsCompletion();

  pipeline_cache_.reset();
  texture_cache_.reset();
  render_target_cache_.reset();
  primitive_processor_.reset();
  shared_memory_.reset();

  for (auto& [hash, shader] : shader_map_) {
    delete shader;
  }
  shader_map_.clear();

  CommandProcessor::Shutdown();
}

void MetalCommandProcessor::ClearCaches() {
  CommandProcessor::ClearCaches();
  cache_clear_requested_ = true;
}

bool MetalCommandProcessor::SetupContext() {
  auto& provider = GetMetalProvider();

  shared_memory_ = std::make_unique<MetalSharedMemory>(
      *this, *memory_, trace_writer_);
  if (!shared_memory_->Initialize()) {
    XELOGE("MetalCommandProcessor: Failed to initialize shared memory");
    return false;
  }

  pipeline_cache_ = std::make_unique<MetalPipelineCache>(*this);
  if (!pipeline_cache_->Initialize()) {
    XELOGE("MetalCommandProcessor: Failed to initialize pipeline cache");
    return false;
  }

  render_target_cache_ = std::make_unique<MetalRenderTargetCache>(
      *this, *register_file_);
  if (!render_target_cache_->Initialize()) {
    XELOGE("MetalCommandProcessor: Failed to initialize render target cache");
    return false;
  }

  texture_cache_ = std::make_unique<MetalTextureCache>(
      *this, *register_file_, *shared_memory_);
  if (!texture_cache_->Initialize()) {
    XELOGE("MetalCommandProcessor: Failed to initialize texture cache");
    return false;
  }

  primitive_processor_ = std::make_unique<MetalPrimitiveProcessor>(
      *register_file_, *memory_, trace_writer_, *shared_memory_, *this);
  if (!primitive_processor_->Initialize()) {
    XELOGE("MetalCommandProcessor: Failed to initialize primitive processor");
    return false;
  }

  std::memset(&system_constants_, 0, sizeof(system_constants_));

  // Allocate the index ring buffer to avoid per-draw Metal buffer allocs.
  {
    MTLResourceOptions idx_opts = provider.has_unified_memory()
        ? (MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined)
        : MTLResourceStorageModeManaged;
    index_ring_buffer_ =
        [provider.device() newBufferWithLength:kIndexRingBufferSize
                                       options:idx_opts];
    if (!index_ring_buffer_) {
      XELOGE("MetalCommandProcessor: Failed to allocate index ring buffer");
      return false;
    }
    index_ring_buffer_.label = @"IndexRingBuffer";
    index_ring_buffer_offset_ = 0;
  }

  XELOGI("MetalCommandProcessor: Context setup complete");
  return true;
}

void MetalCommandProcessor::ShutdownContext() {
  AwaitAllSubmissionsCompletion();

  primitive_processor_.reset();
  texture_cache_.reset();
  pipeline_cache_.reset();
  render_target_cache_.reset();
  shared_memory_.reset();

  current_command_buffer_ = nil;
  last_committed_command_buffer_ = nil;
  current_render_encoder_ = nil;
  current_rt_color_count_ = 0;
  current_rt_depth_base_ = UINT32_MAX;
  std::memset(current_rt_color_bases_, 0, sizeof(current_rt_color_bases_));
  current_rt_width_ = 0;
  current_rt_height_ = 0;
  index_ring_buffer_ = nil;
  index_ring_buffer_offset_ = 0;
  index_ring_fences_.clear();
  for (uint32_t i = 0; i < kCopyStagingCount; ++i) {
    copy_staging_textures_[i] = nil;
    copy_staging_submission_[i] = 0;
  }
  copy_staging_index_ = 0;
  copy_staging_width_ = 0;
  copy_staging_height_ = 0;

  submission_open_ = false;
  submission_current_ = 1;
  submission_completed_.store(0, std::memory_order_relaxed);

  frame_open_ = false;
  frame_current_ = 1;
  frame_completed_ = 0;
}

void MetalCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  CommandProcessor::WriteRegister(index, value);

  if (index >= XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 &&
      index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5) {
    if (texture_cache_ != nullptr) {
      texture_cache_->TextureFetchConstantWritten(
          (index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) / 6);
    }
  }

  // Only mark system constants dirty for registers that
  // UpdateSystemConstants() actually reads.
  switch (index) {
    case XE_GPU_REG_PA_CL_VTE_CNTL:
    case XE_GPU_REG_PA_CL_VPORT_XSCALE:
    case XE_GPU_REG_PA_CL_VPORT_XOFFSET:
    case XE_GPU_REG_PA_CL_VPORT_YSCALE:
    case XE_GPU_REG_PA_CL_VPORT_YOFFSET:
    case XE_GPU_REG_PA_CL_VPORT_ZSCALE:
    case XE_GPU_REG_PA_CL_VPORT_ZOFFSET:
    case XE_GPU_REG_PA_SU_POINT_SIZE:
    case XE_GPU_REG_RB_ALPHA_REF:
    case XE_GPU_REG_RB_SURFACE_INFO:
    case XE_GPU_REG_RB_DEPTH_INFO:
      system_constants_dirty_ = true;
      break;
    default:
      break;
  }
}

Shader* MetalCommandProcessor::LoadShader(xenos::ShaderType shader_type,
                                          uint32_t guest_address,
                                          const uint32_t* host_address,
                                          uint32_t dword_count) {
  uint64_t ucode_data_hash =
      XXH3_64bits(host_address, dword_count * sizeof(uint32_t));

  auto it = shader_map_.find(ucode_data_hash);
  if (it != shader_map_.end()) {
    return it->second;
  }

  auto* shader = new MetalShader(shader_type, ucode_data_hash, host_address,
                                 dword_count);
  shader_map_[ucode_data_hash] = shader;

  // Attempt to translate the shader.
  if (pipeline_cache_) {
    pipeline_cache_->TranslateShader(shader);
  }

  return shader;
}

// ============================================================================
// Render encoder caching — avoid creating a new encoder per draw
// ============================================================================

void MetalCommandProcessor::EndCurrentRenderEncoder() {
  if (current_render_encoder_) {
    [current_render_encoder_ endEncoding];
    current_render_encoder_ = nil;
  }
  current_rt_color_count_ = 0;
  current_rt_depth_base_ = UINT32_MAX;
  std::memset(current_rt_color_bases_, 0, sizeof(current_rt_color_bases_));
  current_rt_width_ = 0;
  current_rt_height_ = 0;
}

bool MetalCommandProcessor::EnsureRenderEncoder(
    const MetalRenderTargetCache::RenderTarget color_targets[4],
    uint32_t color_count,
    const MetalRenderTargetCache::RenderTarget& depth_target,
    bool depth_enabled,
    uint32_t& rt_width_out, uint32_t& rt_height_out) {
  if (!current_command_buffer_) return false;

  // Check if the current encoder can be reused.
  bool rt_changed = false;
  if (!current_render_encoder_) {
    rt_changed = true;
  } else if (color_count != current_rt_color_count_) {
    rt_changed = true;
  } else {
    for (uint32_t i = 0; i < color_count; ++i) {
      if (color_targets[i].edram_base != current_rt_color_bases_[i]) {
        rt_changed = true;
        break;
      }
    }
    uint32_t depth_base =
        (depth_enabled && depth_target.texture)
            ? depth_target.edram_base
            : UINT32_MAX;
    if (depth_base != current_rt_depth_base_) {
      rt_changed = true;
    }
  }

  if (!rt_changed) {
    rt_width_out = current_rt_width_;
    rt_height_out = current_rt_height_;
    return true;
  }

  // End the old encoder before creating a new one.
  EndCurrentRenderEncoder();

  MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
  uint32_t rt_width = 1280;
  uint32_t rt_height = 720;

  for (uint32_t i = 0; i < color_count && i < 4; ++i) {
    if (color_targets[i].texture) {
      rpd.colorAttachments[i].texture = color_targets[i].texture;
      // Use DontCare for textures that haven't been rendered to yet —
      // MTLLoadActionLoad on an uninitialized texture reads undefined
      // contents, which can produce artifacts on Intel Macs.
      bool first_use = render_target_cache_ &&
          !render_target_cache_->HasBeenRendered(
              color_targets[i].edram_base, color_targets[i].pitch_tiles,
              color_targets[i].height_pixels, false,
              MetalRenderTargetCache::ColorFormatToMetal(
                  color_targets[i].color_format),
              color_targets[i].msaa);
      rpd.colorAttachments[i].loadAction =
          first_use ? MTLLoadActionDontCare : MTLLoadActionLoad;
      rpd.colorAttachments[i].storeAction = MTLStoreActionStore;
      rt_width = std::max(
          rt_width, (uint32_t)[color_targets[i].texture width]);
      rt_height = std::max(
          rt_height, (uint32_t)[color_targets[i].texture height]);
    }
  }

  if (depth_enabled && depth_target.texture) {
    bool depth_first_use = render_target_cache_ &&
        !render_target_cache_->HasBeenRendered(
            depth_target.edram_base, depth_target.pitch_tiles,
            depth_target.height_pixels, true,
            MetalRenderTargetCache::DepthFormatToMetal(
                depth_target.depth_format),
            depth_target.msaa);
    MTLLoadAction depth_load =
        depth_first_use ? MTLLoadActionDontCare : MTLLoadActionLoad;
    rpd.depthAttachment.texture = depth_target.texture;
    rpd.depthAttachment.loadAction = depth_load;
    rpd.depthAttachment.storeAction = MTLStoreActionStore;
    rpd.stencilAttachment.texture = depth_target.texture;
    rpd.stencilAttachment.loadAction = depth_load;
    rpd.stencilAttachment.storeAction = MTLStoreActionStore;
  }

  current_render_encoder_ =
      [current_command_buffer_ renderCommandEncoderWithDescriptor:rpd];
  if (!current_render_encoder_) {
    XELOGW("MetalCommandProcessor: Failed to create render encoder");
    return false;
  }
  current_render_encoder_.label = @"XenosDraw";

  // Mark all bound render targets as rendered so future passes use Load.
  if (render_target_cache_) {
    for (uint32_t i = 0; i < color_count && i < 4; ++i) {
      if (color_targets[i].texture) {
        render_target_cache_->MarkRendered(
            color_targets[i].edram_base, color_targets[i].pitch_tiles,
            color_targets[i].height_pixels, false,
            MetalRenderTargetCache::ColorFormatToMetal(
                color_targets[i].color_format),
            color_targets[i].msaa);
      }
    }
    if (depth_enabled && depth_target.texture) {
      render_target_cache_->MarkRendered(
          depth_target.edram_base, depth_target.pitch_tiles,
          depth_target.height_pixels, true,
          MetalRenderTargetCache::DepthFormatToMetal(
              depth_target.depth_format),
          depth_target.msaa);
    }
  }

  // Track current RT state for reuse detection.
  current_rt_color_count_ = color_count;
  for (uint32_t i = 0; i < color_count && i < 4; ++i) {
    current_rt_color_bases_[i] = color_targets[i].edram_base;
  }
  current_rt_depth_base_ =
      (depth_enabled && depth_target.texture)
          ? depth_target.edram_base
          : UINT32_MAX;
  current_rt_width_ = rt_width;
  current_rt_height_ = rt_height;

  rt_width_out = rt_width;
  rt_height_out = rt_height;
  return true;
}

// ============================================================================
// IssueDraw - Full draw call implementation
// ============================================================================

bool MetalCommandProcessor::IssueDraw(xenos::PrimitiveType prim_type,
                                      uint32_t index_count,
                                      IndexBufferInfo* index_buffer_info,
                                      bool major_mode_explicit) {
  SCOPE_profile_cpu_f("gpu");

  @autoreleasepool {

  if (!BeginSubmission(true)) {
    return false;
  }

  // --- 1. Upload dirty shared memory pages ---
  if (shared_memory_) {
    shared_memory_->UploadDirtyPages();
  }

  // --- 2. Update system constants ---
  if (system_constants_dirty_) {
    UpdateSystemConstants();
  }

  // --- 3. Get current render target configuration ---
  MetalRenderTargetCache::RenderTarget color_targets[4];
  MetalRenderTargetCache::RenderTarget depth_target;
  bool depth_enabled = false;
  uint32_t color_count = 0;

  if (render_target_cache_) {
    color_count = render_target_cache_->GetCurrentRenderTargets(
        color_targets, depth_target, depth_enabled);
  }

  // Check if we have at least one render target.
  bool has_rt = false;
  for (uint32_t i = 0; i < color_count; ++i) {
    if (color_targets[i].texture) { has_rt = true; break; }
  }
  if (depth_enabled && depth_target.texture) has_rt = true;

  if (!has_rt) {
    // No valid render targets. This can happen during early boot.
    return true;
  }

  // --- 4. Find active shaders ---
  // Use the base class active shader tracking — these are set by PM4
  // processing when the game binds shaders via SQ_PGM_START_VS/FS.
  MetalShader* vertex_shader =
      static_cast<MetalShader*>(active_vertex_shader());
  MetalShader* pixel_shader =
      static_cast<MetalShader*>(active_pixel_shader());

  // --- 5. Build pipeline state key ---
  MetalPipelineCache::RenderPipelineKey pso_key;
  pso_key.vertex_shader_hash = vertex_shader
      ? vertex_shader->ucode_data_hash() : 0;
  pso_key.pixel_shader_hash = pixel_shader
      ? pixel_shader->ucode_data_hash() : 0;
  pso_key.color_target_count = 0;
  pso_key.sample_count = 1;

  // Set color attachment formats.
  for (uint32_t i = 0; i < color_count && i < 4; ++i) {
    if (color_targets[i].texture) {
      pso_key.color_formats[pso_key.color_target_count] =
          MetalRenderTargetCache::ColorFormatToMetal(
              color_targets[i].color_format);
      // Read blend state from registers.
      auto rb_blendcontrol = register_file_->Get<reg::RB_BLENDCONTROL>(
          XE_GPU_REG_RB_BLENDCONTROL_0 + i);
      auto& bs = pso_key.blend_states[pso_key.color_target_count];
      bs.blend_enable = rb_blendcontrol.color_srcblend !=
                            xenos::BlendFactor::kOne ||
                        rb_blendcontrol.color_destblend !=
                            xenos::BlendFactor::kZero;
      // Map Xenos blend factors to Metal.
      // This is a simplified mapping; full mapping would need all factors.
      bs.src_blend = (uint32_t)MTLBlendFactorOne;
      bs.dst_blend = (uint32_t)MTLBlendFactorZero;
      bs.blend_op = (uint32_t)MTLBlendOperationAdd;
      bs.src_blend_alpha = (uint32_t)MTLBlendFactorOne;
      bs.dst_blend_alpha = (uint32_t)MTLBlendFactorZero;
      bs.blend_op_alpha = (uint32_t)MTLBlendOperationAdd;
      bs.write_mask = 0xF;  // RGBA

      if (bs.blend_enable) {
        bs.src_blend = XenosBlendFactorToMetal(rb_blendcontrol.color_srcblend);
        bs.dst_blend = XenosBlendFactorToMetal(rb_blendcontrol.color_destblend);
        bs.blend_op = XenosBlendOpToMetal(rb_blendcontrol.color_comb_fcn);
        bs.src_blend_alpha =
            XenosBlendFactorToMetal(rb_blendcontrol.alpha_srcblend);
        bs.dst_blend_alpha =
            XenosBlendFactorToMetal(rb_blendcontrol.alpha_destblend);
        bs.blend_op_alpha = XenosBlendOpToMetal(rb_blendcontrol.alpha_comb_fcn);
      }

      pso_key.color_target_count++;
    }
  }

  // Depth format.
  if (depth_enabled && depth_target.texture) {
    pso_key.depth_format = MetalRenderTargetCache::DepthFormatToMetal(
        depth_target.depth_format);
  }

  // If no color targets are bound, this is a depth-only draw (shadow map,
  // Z-prepass, etc.).  Skip inserting a dummy color target — that would create
  // pipeline state mismatches if a real color target is bound later, and
  // Metal allows render passes with only a depth attachment.
  if (pso_key.color_target_count == 0 && !(depth_enabled && depth_target.texture)) {
    // No color targets AND no depth target — nothing to render to.
    return true;
  }

  // --- 6. Get or create pipeline state ---
  id<MTLRenderPipelineState> pso = nil;
  if (pipeline_cache_) {
    pso = pipeline_cache_->GetOrCreateRenderPipelineState(
        pso_key, vertex_shader, pixel_shader);
  }
  if (!pso) {
    XELOGW("MetalCommandProcessor: No pipeline state for draw");
    return true;  // Continue processing commands
  }

  // --- 7. Get or create depth/stencil state ---
  id<MTLDepthStencilState> ds_state = nil;
  if (pipeline_cache_) {
    MetalPipelineCache::DepthStencilKey ds_key;
    auto rb_depthcontrol =
        register_file_->Get<reg::RB_DEPTHCONTROL>();
    ds_key.depth_test_enable = rb_depthcontrol.z_enable;
    ds_key.depth_write_enable = rb_depthcontrol.z_write_enable;
    ds_key.depth_compare_func = (uint32_t)MTLCompareFunctionAlways;
    if (rb_depthcontrol.z_enable) {
      ds_key.depth_compare_func =
          XenosCompareFuncToMetal(rb_depthcontrol.zfunc);
    }
    ds_key.stencil_enable = rb_depthcontrol.stencil_enable;
    if (ds_key.stencil_enable) {
      auto rb_stencilrefmask =
          register_file_->Get<reg::RB_STENCILREFMASK>();
      ds_key.stencil_read_mask = rb_stencilrefmask.stencilmask;
      ds_key.stencil_write_mask = rb_stencilrefmask.stencilwritemask;
      ds_key.stencil_front_compare =
          XenosCompareFuncToMetal(rb_depthcontrol.stencilfunc);
      ds_key.stencil_front_pass =
          XenosStencilOpToMetal(rb_depthcontrol.stencilzpass);
      ds_key.stencil_front_fail =
          XenosStencilOpToMetal(rb_depthcontrol.stencilfail);
      ds_key.stencil_front_depth_fail =
          XenosStencilOpToMetal(rb_depthcontrol.stencilzfail);
      // Back face (use front if backface stencil disabled).
      if (rb_depthcontrol.backface_enable) {
        ds_key.stencil_back_compare =
            XenosCompareFuncToMetal(rb_depthcontrol.stencilfunc_bf);
        ds_key.stencil_back_pass =
            XenosStencilOpToMetal(rb_depthcontrol.stencilzpass_bf);
        ds_key.stencil_back_fail =
            XenosStencilOpToMetal(rb_depthcontrol.stencilfail_bf);
        ds_key.stencil_back_depth_fail =
            XenosStencilOpToMetal(rb_depthcontrol.stencilzfail_bf);
      } else {
        ds_key.stencil_back_compare = ds_key.stencil_front_compare;
        ds_key.stencil_back_pass = ds_key.stencil_front_pass;
        ds_key.stencil_back_fail = ds_key.stencil_front_fail;
        ds_key.stencil_back_depth_fail = ds_key.stencil_front_depth_fail;
      }
    }
    ds_state = pipeline_cache_->GetOrCreateDepthStencilState(ds_key);
  }

  // --- 8. Prepare vertex/index buffers via primitive processor ---
  MetalPrimitiveProcessor::ConvertedIndices converted;
  const void* index_data = nullptr;
  if (index_buffer_info && index_buffer_info->guest_base) {
    // Bounds-check the guest physical address before pointer arithmetic.
    uint32_t index_size = (index_buffer_info->format ==
                               xenos::IndexFormat::kInt32)
                              ? 4
                              : 2;
    uint64_t index_end = uint64_t(index_buffer_info->guest_base) +
                         uint64_t(index_count) * index_size;
    if (index_end > MetalSharedMemory::kBufferSize) {
      XELOGW("MetalCommandProcessor: Index buffer OOB (base=0x{:08X}, "
             "count={}, end=0x{:08X}, limit=0x{:08X})",
             index_buffer_info->guest_base, index_count,
             (uint32_t)index_end, (uint32_t)MetalSharedMemory::kBufferSize);
      return true;
    }
    const uint8_t* shared_mem =
        static_cast<const uint8_t*>(shared_memory_->buffer_contents());
    if (shared_mem) {
      index_data = shared_mem + index_buffer_info->guest_base;
    }
  }

  if (primitive_processor_) {
    xenos::IndexFormat index_format = xenos::IndexFormat::kInt16;
    xenos::Endian endian = xenos::Endian::kNone;
    if (index_buffer_info) {
      index_format = index_buffer_info->format;
      endian = index_buffer_info->endianness;
    }
    converted = primitive_processor_->ConvertPrimitives(
        prim_type, index_data, index_count, index_format, endian);
  }

  // --- 9. Get or reuse the render command encoder ---
  // The encoder is cached across draws when render targets don't change,
  // avoiding expensive per-draw load/store cycles on all attachments.
  uint32_t rt_width = 1280;
  uint32_t rt_height = 720;
  if (!EnsureRenderEncoder(color_targets, color_count, depth_target,
                           depth_enabled, rt_width, rt_height)) {
    return true;
  }
  id<MTLRenderCommandEncoder> encoder = current_render_encoder_;

  // --- 10. Set state and draw ---
  [encoder setRenderPipelineState:pso];

  if (ds_state) {
    [encoder setDepthStencilState:ds_state];
  }

  // Viewport.
  {
    auto pa_cl_vte = register_file_->Get<reg::PA_CL_VTE_CNTL>();
    MTLViewport viewport;
    viewport.originX = 0;
    viewport.originY = 0;
    viewport.width = double(rt_width);
    viewport.height = double(rt_height);
    viewport.znear = 0.0;
    viewport.zfar = 1.0;

    if (pa_cl_vte.vport_x_scale_ena) {
      auto pa_vport_x = register_file_->Get<reg::PA_CL_VPORT_XSCALE>();
      auto pa_vport_xoff = register_file_->Get<reg::PA_CL_VPORT_XOFFSET>();
      auto pa_vport_y = register_file_->Get<reg::PA_CL_VPORT_YSCALE>();
      auto pa_vport_yoff = register_file_->Get<reg::PA_CL_VPORT_YOFFSET>();
      auto pa_vport_z = register_file_->Get<reg::PA_CL_VPORT_ZSCALE>();
      auto pa_vport_zoff = register_file_->Get<reg::PA_CL_VPORT_ZOFFSET>();

      float vp_x = pa_vport_xoff.vport_xoffset - std::abs(pa_vport_x.vport_xscale);
      float vp_y = pa_vport_yoff.vport_yoffset - std::abs(pa_vport_y.vport_yscale);
      float vp_w = std::abs(pa_vport_x.vport_xscale) * 2.0f;
      float vp_h = std::abs(pa_vport_y.vport_yscale) * 2.0f;

      viewport.originX = std::max(0.0, double(vp_x));
      viewport.originY = std::max(0.0, double(vp_y));
      viewport.width = std::max(1.0, double(vp_w));
      viewport.height = std::max(1.0, double(vp_h));
      viewport.znear = double(pa_vport_zoff.vport_zoffset -
                               std::abs(pa_vport_z.vport_zscale));
      viewport.zfar = double(pa_vport_zoff.vport_zoffset +
                              std::abs(pa_vport_z.vport_zscale));
    }

    [encoder setViewport:viewport];
  }

  // Scissor.
  {
    auto pa_sc_window_scissor_tl =
        register_file_->Get<reg::PA_SC_WINDOW_SCISSOR_TL>();
    auto pa_sc_window_scissor_br =
        register_file_->Get<reg::PA_SC_WINDOW_SCISSOR_BR>();

    MTLScissorRect scissor;
    scissor.x = pa_sc_window_scissor_tl.tl_x;
    scissor.y = pa_sc_window_scissor_tl.tl_y;
    uint32_t br_x = pa_sc_window_scissor_br.br_x;
    uint32_t br_y = pa_sc_window_scissor_br.br_y;
    br_x = std::min(br_x, rt_width);
    br_y = std::min(br_y, rt_height);
    scissor.width = (br_x > scissor.x) ? (br_x - scissor.x) : 1;
    scissor.height = (br_y > scissor.y) ? (br_y - scissor.y) : 1;
    [encoder setScissorRect:scissor];
  }

  // Cull mode.
  {
    auto pa_su_sc_mode = register_file_->Get<reg::PA_SU_SC_MODE_CNTL>();
    if (pa_su_sc_mode.cull_front && pa_su_sc_mode.cull_back) {
      // Both culled - skip draw (encoder stays open for next draw).
      return true;
    } else if (pa_su_sc_mode.cull_front) {
      [encoder setCullMode:MTLCullModeFront];
    } else if (pa_su_sc_mode.cull_back) {
      [encoder setCullMode:MTLCullModeBack];
    } else {
      [encoder setCullMode:MTLCullModeNone];
    }
    [encoder setFrontFacingWinding:pa_su_sc_mode.face
                                       ? MTLWindingClockwise
                                       : MTLWindingCounterClockwise];
  }

  // Bind vertex data at buffer index 0.
  // The fallback vertex shader expects FallbackVertexIn from [[buffer(0)]].
  if (shared_memory_ && shared_memory_->buffer()) {
    [encoder setVertexBuffer:shared_memory_->buffer() offset:0 atIndex:0];
  }

  // Upload system constants as vertex buffer 1.
  [encoder setVertexBytes:&system_constants_
                   length:sizeof(system_constants_)
                  atIndex:1];

  // Bind textures (up to 16 texture fetch slots for pixel shader).
  if (texture_cache_) {
    for (uint32_t i = 0; i < 16; ++i) {
      auto* cached = texture_cache_->RequestTexture(i);
      if (cached && cached->texture) {
        [encoder setFragmentTexture:cached->texture atIndex:i];
        if (cached->sampler) {
          [encoder setFragmentSamplerState:cached->sampler atIndex:i];
        }
      }
    }
  }

  // --- 11. Issue the draw call ---
  if (converted.index_count == 0) {
    return true;  // Encoder stays open for next draw.
  }

  if (converted.needs_conversion && !converted.indices.empty()) {
    // Upload converted indices to the ring buffer.
    size_t index_bytes = converted.indices.size() * sizeof(uint32_t);

    // Retire completed fences so their regions become reusable.
    uint64_t completed =
        submission_completed_.load(std::memory_order_relaxed);
    index_ring_fences_.erase(
        std::remove_if(index_ring_fences_.begin(), index_ring_fences_.end(),
                        [completed](const IndexRingFence& f) {
                          return f.submission_id <= completed;
                        }),
        index_ring_fences_.end());

    // Wrap around if we'd overflow the end of the buffer.
    if (index_ring_buffer_offset_ + index_bytes > kIndexRingBufferSize) {
      index_ring_buffer_offset_ = 0;
    }

    // Check that the target region doesn't overlap any in-flight fences.
    // If it does, stall until the GPU finishes those submissions.
    size_t write_end = index_ring_buffer_offset_ + index_bytes;
    for (const auto& fence : index_ring_fences_) {
      size_t fence_end = fence.offset + fence.size;
      bool overlaps = index_ring_buffer_offset_ < fence_end &&
                      write_end > fence.offset;
      if (overlaps) {
        AwaitAllSubmissionsCompletion();
        index_ring_fences_.clear();
        break;
      }
    }

    uint8_t* ring_ptr =
        static_cast<uint8_t*>([index_ring_buffer_ contents]);
    std::memcpy(ring_ptr + index_ring_buffer_offset_,
                converted.indices.data(), index_bytes);

    if (index_ring_buffer_.storageMode == MTLStorageModeManaged) {
      [index_ring_buffer_
          didModifyRange:NSMakeRange(index_ring_buffer_offset_, index_bytes)];
    }

    [encoder drawIndexedPrimitives:converted.metal_primitive_type
                        indexCount:converted.index_count
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:index_ring_buffer_
                 indexBufferOffset:index_ring_buffer_offset_];

    // Record this write region as a fence for the current submission.
    index_ring_fences_.push_back(
        {index_ring_buffer_offset_, index_bytes, submission_current_});

    index_ring_buffer_offset_ += index_bytes;
  } else {
    // No index buffer - auto-indexed draw.
    MTLPrimitiveType metal_prim = MTLPrimitiveTypeTriangle;
    if (converted.index_count > 0) {
      metal_prim = converted.metal_primitive_type;
    }
    [encoder drawPrimitives:metal_prim
                vertexStart:0
                vertexCount:converted.index_count];
  }

  // Encoder stays open for the next draw call — it will be closed by
  // EndCurrentRenderEncoder() when render targets change, or by
  // EndSubmission() / IssueCopy() when the submission ends.

  return true;

  }  // @autoreleasepool
}

// ============================================================================
// IssueCopy - EDRAM resolve/copy to guest memory
// ============================================================================

bool MetalCommandProcessor::IssueCopy() {
  SCOPE_profile_cpu_f("gpu");

  @autoreleasepool {

  // End any open render encoder before switching to blit/compute work.
  EndCurrentRenderEncoder();

  if (!BeginSubmission(true)) {
    return false;
  }

  // Read resolve/copy parameters from registers.
  auto rb_copy_control = register_file_->Get<reg::RB_COPY_CONTROL>();
  auto rb_copy_dest_info = register_file_->Get<reg::RB_COPY_DEST_INFO>();
  auto rb_copy_dest_base = register_file_->values[XE_GPU_REG_RB_COPY_DEST_BASE];
  auto rb_copy_dest_pitch =
      register_file_->Get<reg::RB_COPY_DEST_PITCH>();

  uint32_t dest_address = rb_copy_dest_base;  // Already in bytes
  uint32_t dest_pitch = rb_copy_dest_pitch.copy_dest_pitch;

  // Source is an EDRAM surface.
  uint32_t source_select = rb_copy_control.copy_src_select;
  bool depth_copy = (rb_copy_control.copy_src_select == 4);  // 4 = depth

  // Get the source render target.
  MetalRenderTargetCache::RenderTarget color_targets[4];
  MetalRenderTargetCache::RenderTarget depth_target;
  bool depth_enabled = false;
  uint32_t color_count = 0;

  if (render_target_cache_) {
    color_count = render_target_cache_->GetCurrentRenderTargets(
        color_targets, depth_target, depth_enabled);
  }

  id<MTLTexture> src_texture = nil;
  uint32_t src_width = 0;
  uint32_t src_height = 0;

  if (depth_copy && depth_enabled && depth_target.texture) {
    src_texture = depth_target.texture;
    src_width = depth_target.width_pixels;
    src_height = depth_target.height_pixels;
  } else if (source_select < color_count && color_targets[source_select].texture) {
    src_texture = color_targets[source_select].texture;
    src_width = color_targets[source_select].width_pixels;
    src_height = color_targets[source_select].height_pixels;
  }

  if (!src_texture || src_width == 0 || src_height == 0) {
    XELOGW("MetalCommandProcessor: IssueCopy with no valid source RT");
    return true;
  }

  // Read the render target contents back to the EDRAM buffer.
  // Then from EDRAM to guest shared memory at dest_address.
  //
  // For now, do a simplified GPU readback:
  // 1. Create a temporary readable texture (shared/managed storage)
  // 2. Blit from the private RT texture to the readable texture
  // 3. Copy the readable texture data to shared memory

  auto& provider = GetMetalProvider();
  id<MTLDevice> device = provider.device();

  // Double-buffered staging: recreate both textures if dimensions changed.
  if (copy_staging_width_ != src_width ||
      copy_staging_height_ != src_height) {
    MTLTextureDescriptor* staging_desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                    width:src_width
                                   height:src_height
                                mipmapped:NO];
    staging_desc.usage = MTLTextureUsageShaderRead;
    if (provider.has_unified_memory()) {
      staging_desc.storageMode = MTLStorageModeShared;
    } else {
      staging_desc.storageMode = MTLStorageModeManaged;
    }

    for (uint32_t i = 0; i < kCopyStagingCount; ++i) {
      copy_staging_textures_[i] = [device newTextureWithDescriptor:staging_desc];
      if (!copy_staging_textures_[i]) {
        XELOGE("MetalCommandProcessor: Failed to create staging texture {} for copy", i);
        return true;
      }
      copy_staging_textures_[i].label =
          [NSString stringWithFormat:@"CopyStagingTexture_%u", i];
      copy_staging_submission_[i] = 0;
    }
    copy_staging_width_ = src_width;
    copy_staging_height_ = src_height;
    copy_staging_index_ = 0;
  }

  // Pick the current staging slot for the blit, then alternate.
  uint32_t blit_slot = copy_staging_index_;
  copy_staging_index_ = (copy_staging_index_ + 1) % kCopyStagingCount;
  uint32_t read_slot = copy_staging_index_;  // The *other* slot

  id<MTLTexture> blit_staging = copy_staging_textures_[blit_slot];

  // Blit from RT to the current staging texture (async — no CPU wait).
  id<MTLBlitCommandEncoder> blit =
      [current_command_buffer_ blitCommandEncoder];
  if (blit) {
    MTLSize copy_size = MTLSizeMake(src_width, src_height, 1);
    [blit copyFromTexture:src_texture
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:copy_size
                toTexture:blit_staging
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];

    if (blit_staging.storageMode == MTLStorageModeManaged) {
      [blit synchronizeTexture:blit_staging slice:0 level:0];
    }
    [blit endEncoding];
  }

  // Record which submission this blit belongs to.
  copy_staging_submission_[blit_slot] = submission_current_;

  // Read back from the *other* staging texture (the one blitted in a
  // previous submission). Only wait if that submission hasn't completed yet.
  id<MTLTexture> read_staging = copy_staging_textures_[read_slot];
  if (shared_memory_ && dest_address > 0 && read_staging &&
      copy_staging_submission_[read_slot] > 0) {
    uint64_t completed =
        submission_completed_.load(std::memory_order_relaxed);
    if (completed < copy_staging_submission_[read_slot]) {
      // Must wait for the read slot's blit to finish before reading.
      EndSubmission(false);
      AwaitAllSubmissionsCompletion();
      if (!BeginSubmission(true)) {
        XELOGE("MetalCommandProcessor: Failed to reopen submission after copy wait");
        return true;
      }
    }

    uint8_t* dest_mem =
        static_cast<uint8_t*>(shared_memory_->buffer_contents());
    if (dest_mem && dest_address + src_width * src_height * 4 <=
                        MetalSharedMemory::kBufferSize) {
      uint32_t row_bytes = src_width * 4;
      std::vector<uint8_t> pixels(row_bytes * src_height);

      MTLRegion region = MTLRegionMake2D(0, 0, src_width, src_height);
      [read_staging getBytes:pixels.data()
                 bytesPerRow:row_bytes
                  fromRegion:region
                 mipmapLevel:0];

      std::memcpy(dest_mem + dest_address, pixels.data(),
                  std::min(size_t(row_bytes * src_height),
                           size_t(MetalSharedMemory::kBufferSize - dest_address)));

      if (shared_memory_->buffer() &&
          [shared_memory_->buffer() storageMode] == MTLStorageModeManaged) {
        [shared_memory_->buffer()
            didModifyRange:NSMakeRange(dest_address,
                                       row_bytes * src_height)];
      }
    }
  }

  XELOGGPU("MetalCommandProcessor: IssueCopy(src={}, dest=0x{:08X}, {}x{})",
            source_select, dest_address, src_width, src_height);

  return true;

  }  // @autoreleasepool
}

// ============================================================================
// IssueSwap
// ============================================================================

void MetalCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                      uint32_t frontbuffer_width,
                                      uint32_t frontbuffer_height) {
  SCOPE_profile_cpu_f("gpu");

  // End any open render encoder before swap.
  EndCurrentRenderEncoder();

  ui::Presenter* presenter = graphics_system_->presenter();
  if (!presenter) {
    return;
  }

  if (!BeginSubmission(true)) {
    return;
  }

  // Upload any remaining dirty pages.
  if (shared_memory_) {
    shared_memory_->UploadDirtyPages();
  }

  id<MTLTexture> swap_texture = nil;
  xenos::TextureFormat frontbuffer_format = xenos::TextureFormat::k_8_8_8_8;

  if (texture_cache_) {
    swap_texture = texture_cache_->RequestSwapTexture(frontbuffer_format);
  }

  uint32_t scaled_width = frontbuffer_width;
  uint32_t scaled_height = frontbuffer_height;
  if (scaled_width == 0) scaled_width = 1280;
  if (scaled_height == 0) scaled_height = 720;

  presenter->RefreshGuestOutput(
      scaled_width, scaled_height, 1280, 720,
      [this, swap_texture](
          ui::Presenter::GuestOutputRefreshContext& context) -> bool {
        auto& metal_context =
            static_cast<ui::metal::MetalPresenter::MetalGuestOutputRefreshContext&>(
                context);
        id<MTLTexture> target_texture = metal_context.texture();

        if (!target_texture) return false;

        if (swap_texture && current_command_buffer_) {
          // Encode the blit on the existing command buffer instead of
          // creating a separate one with a full GPU sync
          // (waitUntilCompleted). The blit will execute as part of the
          // current submission, avoiding a pipeline bubble every frame.
          id<MTLBlitCommandEncoder> blit_encoder =
              [current_command_buffer_ blitCommandEncoder];
          if (blit_encoder) {
            MTLSize copy_size = MTLSizeMake(
                std::min(swap_texture.width, target_texture.width),
                std::min(swap_texture.height, target_texture.height), 1);
            [blit_encoder copyFromTexture:swap_texture
                              sourceSlice:0
                              sourceLevel:0
                             sourceOrigin:MTLOriginMake(0, 0, 0)
                               sourceSize:copy_size
                                toTexture:target_texture
                         destinationSlice:0
                         destinationLevel:0
                        destinationOrigin:MTLOriginMake(0, 0, 0)];

            [blit_encoder endEncoding];
          }
        }

        return true;
      });

  EndSubmission(true);
  if (cache_clear_requested_) {
    cache_clear_requested_ = false;
    if (pipeline_cache_) pipeline_cache_->ClearCache();
    if (texture_cache_) texture_cache_->ClearCache();
    if (render_target_cache_) render_target_cache_->ClearCache();
  }

  XELOGGPU("MetalCommandProcessor: IssueSwap(ptr=0x{:08X}, {}x{})",
            frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
}

// ============================================================================
// Xenos state → Metal state conversion helpers
// ============================================================================

uint32_t MetalCommandProcessor::XenosBlendFactorToMetal(
    xenos::BlendFactor factor) {
  switch (factor) {
    case xenos::BlendFactor::kZero:
      return (uint32_t)MTLBlendFactorZero;
    case xenos::BlendFactor::kOne:
      return (uint32_t)MTLBlendFactorOne;
    case xenos::BlendFactor::kSrcColor:
      return (uint32_t)MTLBlendFactorSourceColor;
    case xenos::BlendFactor::kOneMinusSrcColor:
      return (uint32_t)MTLBlendFactorOneMinusSourceColor;
    case xenos::BlendFactor::kSrcAlpha:
      return (uint32_t)MTLBlendFactorSourceAlpha;
    case xenos::BlendFactor::kOneMinusSrcAlpha:
      return (uint32_t)MTLBlendFactorOneMinusSourceAlpha;
    case xenos::BlendFactor::kDstColor:
      return (uint32_t)MTLBlendFactorDestinationColor;
    case xenos::BlendFactor::kOneMinusDstColor:
      return (uint32_t)MTLBlendFactorOneMinusDestinationColor;
    case xenos::BlendFactor::kDstAlpha:
      return (uint32_t)MTLBlendFactorDestinationAlpha;
    case xenos::BlendFactor::kOneMinusDstAlpha:
      return (uint32_t)MTLBlendFactorOneMinusDestinationAlpha;
    case xenos::BlendFactor::kSrcAlphaSaturate:
      return (uint32_t)MTLBlendFactorSourceAlphaSaturated;
    case xenos::BlendFactor::kConstantColor:
      return (uint32_t)MTLBlendFactorBlendColor;
    case xenos::BlendFactor::kOneMinusConstantColor:
      return (uint32_t)MTLBlendFactorOneMinusBlendColor;
    case xenos::BlendFactor::kConstantAlpha:
      return (uint32_t)MTLBlendFactorBlendAlpha;
    case xenos::BlendFactor::kOneMinusConstantAlpha:
      return (uint32_t)MTLBlendFactorOneMinusBlendAlpha;
    default:
      return (uint32_t)MTLBlendFactorOne;
  }
}

uint32_t MetalCommandProcessor::XenosBlendOpToMetal(
    xenos::BlendOp op) {
  switch (op) {
    case xenos::BlendOp::kAdd:
      return (uint32_t)MTLBlendOperationAdd;
    case xenos::BlendOp::kSubtract:
      return (uint32_t)MTLBlendOperationSubtract;
    case xenos::BlendOp::kRevSubtract:
      return (uint32_t)MTLBlendOperationReverseSubtract;
    case xenos::BlendOp::kMin:
      return (uint32_t)MTLBlendOperationMin;
    case xenos::BlendOp::kMax:
      return (uint32_t)MTLBlendOperationMax;
    default:
      return (uint32_t)MTLBlendOperationAdd;
  }
}

uint32_t MetalCommandProcessor::XenosCompareFuncToMetal(
    xenos::CompareFunction func) {
  switch (func) {
    case xenos::CompareFunction::kNever:
      return (uint32_t)MTLCompareFunctionNever;
    case xenos::CompareFunction::kLess:
      return (uint32_t)MTLCompareFunctionLess;
    case xenos::CompareFunction::kEqual:
      return (uint32_t)MTLCompareFunctionEqual;
    case xenos::CompareFunction::kLessEqual:
      return (uint32_t)MTLCompareFunctionLessEqual;
    case xenos::CompareFunction::kGreater:
      return (uint32_t)MTLCompareFunctionGreater;
    case xenos::CompareFunction::kNotEqual:
      return (uint32_t)MTLCompareFunctionNotEqual;
    case xenos::CompareFunction::kGreaterEqual:
      return (uint32_t)MTLCompareFunctionGreaterEqual;
    case xenos::CompareFunction::kAlways:
      return (uint32_t)MTLCompareFunctionAlways;
    default:
      return (uint32_t)MTLCompareFunctionAlways;
  }
}

uint32_t MetalCommandProcessor::XenosStencilOpToMetal(
    xenos::StencilOp op) {
  switch (op) {
    case xenos::StencilOp::kKeep:
      return (uint32_t)MTLStencilOperationKeep;
    case xenos::StencilOp::kZero:
      return (uint32_t)MTLStencilOperationZero;
    case xenos::StencilOp::kReplace:
      return (uint32_t)MTLStencilOperationReplace;
    case xenos::StencilOp::kIncrClamp:
      return (uint32_t)MTLStencilOperationIncrementClamp;
    case xenos::StencilOp::kDecrClamp:
      return (uint32_t)MTLStencilOperationDecrementClamp;
    case xenos::StencilOp::kInvert:
      return (uint32_t)MTLStencilOperationInvert;
    case xenos::StencilOp::kIncrWrap:
      return (uint32_t)MTLStencilOperationIncrementWrap;
    case xenos::StencilOp::kDecrWrap:
      return (uint32_t)MTLStencilOperationDecrementWrap;
    default:
      return (uint32_t)MTLStencilOperationKeep;
  }
}

// ============================================================================
// System constants
// ============================================================================

void MetalCommandProcessor::UpdateSystemConstants() {
  auto pa_cl_vte = register_file_->Get<reg::PA_CL_VTE_CNTL>();

  // NDC scale/offset.
  if (pa_cl_vte.vport_x_scale_ena) {
    auto pa_vport_x = register_file_->Get<reg::PA_CL_VPORT_XSCALE>();
    auto pa_vport_xoff = register_file_->Get<reg::PA_CL_VPORT_XOFFSET>();
    auto pa_vport_y = register_file_->Get<reg::PA_CL_VPORT_YSCALE>();
    auto pa_vport_yoff = register_file_->Get<reg::PA_CL_VPORT_YOFFSET>();
    auto pa_vport_z = register_file_->Get<reg::PA_CL_VPORT_ZSCALE>();
    auto pa_vport_zoff = register_file_->Get<reg::PA_CL_VPORT_ZOFFSET>();

    system_constants_.ndc_scale[0] = pa_vport_x.vport_xscale;
    system_constants_.ndc_scale[1] = pa_vport_y.vport_yscale;
    system_constants_.ndc_scale[2] = pa_vport_z.vport_zscale;
    system_constants_.ndc_offset[0] = pa_vport_xoff.vport_xoffset;
    system_constants_.ndc_offset[1] = pa_vport_yoff.vport_yoffset;
    system_constants_.ndc_offset[2] = pa_vport_zoff.vport_zoffset;
  } else {
    system_constants_.ndc_scale[0] = 1.0f;
    system_constants_.ndc_scale[1] = 1.0f;
    system_constants_.ndc_scale[2] = 1.0f;
    system_constants_.ndc_offset[0] = 0.0f;
    system_constants_.ndc_offset[1] = 0.0f;
    system_constants_.ndc_offset[2] = 0.0f;
  }

  // Point size.
  auto pa_su_point = register_file_->Get<reg::PA_SU_POINT_SIZE>();
  system_constants_.point_size_x =
      float(pa_su_point.width) / 8.0f;  // Fixed point 16.3
  system_constants_.point_size_y =
      float(pa_su_point.height) / 8.0f;

  // Alpha test reference.
  auto rb_alpha_ref = register_file_->values[XE_GPU_REG_RB_ALPHA_REF];
  system_constants_.alpha_test_reference =
      *reinterpret_cast<const float*>(&rb_alpha_ref);

  // EDRAM info.
  auto rb_surface_info = register_file_->Get<reg::RB_SURFACE_INFO>();
  system_constants_.edram_pitch_tiles = rb_surface_info.surface_pitch;

  auto rb_depth_info = register_file_->Get<reg::RB_DEPTH_INFO>();
  system_constants_.edram_depth_base_dwords = rb_depth_info.depth_base;

  system_constants_dirty_ = false;
}

void MetalCommandProcessor::UpdateGammaRampValue(GammaRampType type,
                                                 uint32_t index,
                                                 uint32_t value) {
  // The base CommandProcessor already stores the gamma ramp entries in
  // gamma_ramp_256_entry_table_ and gamma_ramp_pwl_rgb_.
  // We just need to mark the gamma ramp dirty so it can be uploaded to the
  // GPU as a 1D texture/LUT when the presenter applies color correction.
  //
  // For now, the presenter applies the guest output directly without gamma
  // correction — the gamma ramp values are tracked here for when a gamma
  // correction pass is added to the presentation pipeline.
  // The values are already persisted by the base class; no additional
  // Metal-side work is needed until the correction shader is implemented.
}

// ============================================================================
// Other command processor methods
// ============================================================================

void MetalCommandProcessor::InitializeShaderStorage(
    const std::filesystem::path& cache_root, uint32_t title_id,
    bool blocking) {
  CommandProcessor::InitializeShaderStorage(cache_root, title_id, blocking);
  if (pipeline_cache_) {
    pipeline_cache_->InitializeShaderStorage(cache_root, title_id, blocking);
  }
}

void MetalCommandProcessor::TracePlaybackWroteMemory(uint32_t base_ptr,
                                                     uint32_t length) {
  if (shared_memory_) {
    shared_memory_->MemoryInvalidationCallback(base_ptr, length, true);
  }
  if (primitive_processor_) {
    primitive_processor_->MemoryInvalidationCallback(base_ptr, length, true);
  }
}

void MetalCommandProcessor::RestoreEdramSnapshot(const void* snapshot) {
  if (!BeginSubmission(true)) return;
  if (render_target_cache_) {
    render_target_cache_->RestoreEdramSnapshot(snapshot);
  }
}

// ============================================================================
// Submission lifecycle
// ============================================================================

bool MetalCommandProcessor::BeginSubmission(bool is_guest_command) {
  if (submission_open_) return true;

  auto& provider = GetMetalProvider();
  id<MTLCommandQueue> queue = provider.command_queue();
  if (!queue) return false;

  current_command_buffer_ = [queue commandBuffer];
  if (!current_command_buffer_) {
    XELOGE("MetalCommandProcessor: Failed to create command buffer");
    return false;
  }
  current_command_buffer_.label = @"XenosSubmission";

  submission_open_ = true;

  if (shared_memory_) shared_memory_->BeginSubmission();
  if (render_target_cache_) render_target_cache_->BeginSubmission();
  if (texture_cache_) texture_cache_->BeginSubmission();
  if (primitive_processor_) primitive_processor_->BeginSubmission();

  if (!frame_open_) {
    frame_open_ = true;
    if (render_target_cache_) render_target_cache_->BeginFrame();
    if (texture_cache_) texture_cache_->BeginFrame();
    if (primitive_processor_) primitive_processor_->BeginFrame();
  }

  return true;
}

bool MetalCommandProcessor::EndSubmission(bool is_swap) {
  if (!submission_open_) return false;

  // Close any open render encoder before committing the command buffer.
  EndCurrentRenderEncoder();

  if (current_command_buffer_) {
    uint64_t submission_id = submission_current_;
    __block uint64_t captured_id = submission_id;
    [current_command_buffer_ addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
      // Use relaxed atomics — the completion handler runs on an arbitrary
      // Metal driver thread. Relaxed is sufficient because we only need
      // monotonic progress visibility, not ordering with other data.
      uint64_t prev = submission_completed_.load(std::memory_order_relaxed);
      while (prev < captured_id) {
        if (submission_completed_.compare_exchange_weak(
                prev, captured_id, std::memory_order_relaxed)) {
          break;
        }
      }
    }];

    [current_command_buffer_ commit];
    last_committed_command_buffer_ = current_command_buffer_;
    current_command_buffer_ = nil;
  }

  submission_current_++;
  submission_open_ = false;

  if (is_swap && frame_open_) {
    frame_open_ = false;
    frame_current_++;
  }

  // Run cache eviction on swap frames and periodically (every 60
  // submissions) to prevent stale resources from accumulating during
  // long loading sequences that don't trigger any swaps.
  bool should_evict = is_swap || (submission_current_ % 60 == 0);
  if (should_evict) {
    if (shared_memory_) shared_memory_->CompletedSubmissionUpdated();
    if (render_target_cache_) render_target_cache_->CompletedSubmissionUpdated();
    if (texture_cache_) texture_cache_->CompletedSubmissionUpdated();
    if (primitive_processor_) primitive_processor_->CompletedSubmissionUpdated();
  }

  return true;
}

void MetalCommandProcessor::AwaitAllSubmissionsCompletion() {
  if (!submission_open_ &&
      submission_completed_.load(std::memory_order_relaxed) >=
          submission_current_ - 1) {
    return;
  }

  if (submission_open_) {
    EndSubmission(false);
  }

  // Wait on the last committed command buffer directly instead of
  // creating a wasteful dummy "fence" buffer. The completion handler
  // on the last real command buffer will update submission_completed_
  // when the GPU finishes.
  if (last_committed_command_buffer_) {
    [last_committed_command_buffer_ waitUntilCompleted];
    last_committed_command_buffer_ = nil;
  }

  submission_completed_.store(submission_current_ - 1,
                              std::memory_order_relaxed);
}

std::string MetalCommandProcessor::GetWindowTitleText() const {
  auto& provider = GetMetalProvider();
  const auto& caps = provider.caps();

  uint64_t frames = frame_current_ - 1;
  uint64_t submissions = submission_current_ - 1;
  uint64_t completed = submission_completed_.load(std::memory_order_relaxed);
  uint64_t pending =
      (submission_current_ > completed + 1)
          ? (submission_current_ - completed - 1)
          : 0;

  return "Metal | " + caps.device_name +
         " | F" + std::to_string(frames) +
         " S" + std::to_string(submissions) +
         " P" + std::to_string(pending);
}

}  // namespace rex::graphics::metal
