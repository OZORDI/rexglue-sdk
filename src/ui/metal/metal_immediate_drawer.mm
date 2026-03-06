/**
 * Metal immediate-mode UI drawer.
 * Uses runtime-compiled inline MSL shaders (no pre-built metallib needed).
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/ui/metal/metal_immediate_drawer.h>

#import <simd/simd.h>

#include <rex/logging.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/metal/metal_presenter.h>
#include <rex/ui/metal/metal_provider.h>

#import <Metal/Metal.h>
// Include metal-cpp for type completeness (no IMPL macros here, only in metal_provider.mm)
#include <Metal/Metal.hpp>

namespace rex {
namespace ui {
namespace metal {

// ── MetalImmediateTexture dtor ────────────────────────────────────────────────

MetalImmediateDrawer::MetalImmediateTexture::~MetalImmediateTexture() {
  if (texture) { texture->release(); texture = nullptr; }
  if (sampler) { sampler->release(); sampler = nullptr; }
}

// ── MetalImmediateDrawer ──────────────────────────────────────────────────────

MetalImmediateDrawer::MetalImmediateDrawer(MetalProvider* provider)
    : provider_(provider), device_(provider->device()) {}

MetalImmediateDrawer::~MetalImmediateDrawer() {
  if (white_texture_)   { white_texture_->release(); white_texture_ = nullptr; }
  if (default_sampler_) { default_sampler_->release(); default_sampler_ = nullptr; }
  if (pipeline_textured_) { pipeline_textured_->release(); pipeline_textured_ = nullptr; }
}

static const char* kImmediate_MSL = R"(
#include <metal_stdlib>
using namespace metal;

struct ImmVertex {
  float2 pos   [[attribute(0)]];
  float2 uv    [[attribute(1)]];
  uchar4 color [[attribute(2)]];
};

struct ImmVOut {
  float4 pos [[position]];
  float2 uv;
  float4 color;
};

struct PushConstants {
  float2 inv_size;
};

vertex ImmVOut imm_vs(ImmVertex in [[stage_in]],
                      constant PushConstants& pc [[buffer(0)]]) {
  ImmVOut o;
  float2 ndc = in.pos * pc.inv_size * float2(2.0, -2.0) + float2(-1.0, 1.0);
  o.pos   = float4(ndc, 0.0, 1.0);
  o.uv    = in.uv;
  o.color = float4(in.color) / 255.0;
  return o;
}

fragment float4 imm_fs(ImmVOut in [[stage_in]],
                       texture2d<float> tex [[texture(0)]],
                       sampler s [[sampler(0)]]) {
  return in.color * tex.sample(s, in.uv);
}
)";

bool MetalImmediateDrawer::Initialize() {
  id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;

  // White texture
  MTLTextureDescriptor* td = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                   width:1 height:1 mipmapped:NO];
  td.usage = MTLTextureUsageShaderRead;
  td.storageMode = MTLStorageModeShared;
  id<MTLTexture> white = [dev newTextureWithDescriptor:td];
  if (!white) { REXLOG_ERROR("MetalImmediateDrawer: failed to create white texture"); return false; }
  uint32_t px = 0xFFFFFFFF;
  [white replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:&px bytesPerRow:4];
  white_texture_ = (__bridge_retained MTL::Texture*)white;

  // Sampler
  MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
  sd.minFilter = MTLSamplerMinMagFilterLinear;
  sd.magFilter = MTLSamplerMinMagFilterLinear;
  sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
  sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
  id<MTLSamplerState> samp = [dev newSamplerStateWithDescriptor:sd];
  [sd release];
  default_sampler_ = (__bridge_retained MTL::SamplerState*)samp;

  // Compile inline shaders
  NSError* err = nil;
  NSString* src = [NSString stringWithUTF8String:kImmediate_MSL];
  id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:&err];
  if (!lib) {
    REXLOG_ERROR("MetalImmediateDrawer: shader compile error: {}",
           err ? [[err localizedDescription] UTF8String] : "unknown");
    return false;
  }

  id<MTLFunction> vs = [lib newFunctionWithName:@"imm_vs"];
  id<MTLFunction> fs = [lib newFunctionWithName:@"imm_fs"];
  [lib release];

  // Vertex descriptor
  MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
  vd.attributes[0].format = MTLVertexFormatFloat2;
  vd.attributes[0].offset = offsetof(ImmediateVertex, x);
  vd.attributes[0].bufferIndex = 1;
  vd.attributes[1].format = MTLVertexFormatFloat2;
  vd.attributes[1].offset = offsetof(ImmediateVertex, u);
  vd.attributes[1].bufferIndex = 1;
  vd.attributes[2].format = MTLVertexFormatUChar4;
  vd.attributes[2].offset = offsetof(ImmediateVertex, color);
  vd.attributes[2].bufferIndex = 1;
  vd.layouts[1].stride = sizeof(ImmediateVertex);
  vd.layouts[1].stepFunction = MTLVertexStepFunctionPerVertex;

  MTLRenderPipelineDescriptor* rpd = [[MTLRenderPipelineDescriptor alloc] init];
  rpd.vertexFunction   = vs;
  rpd.fragmentFunction = fs;
  rpd.vertexDescriptor = vd;
  auto* ca = rpd.colorAttachments[0];
  ca.pixelFormat = MTLPixelFormatBGRA8Unorm;
  ca.blendingEnabled = YES;
  ca.rgbBlendOperation   = MTLBlendOperationAdd;
  ca.alphaBlendOperation = MTLBlendOperationAdd;
  ca.sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
  ca.sourceAlphaBlendFactor      = MTLBlendFactorSourceAlpha;
  ca.destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
  ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

  err = nil;
  id<MTLRenderPipelineState> ps = [dev newRenderPipelineStateWithDescriptor:rpd error:&err];
  [rpd release]; [vd release]; [vs release]; [fs release];

  if (!ps) {
    REXLOG_ERROR("MetalImmediateDrawer: pipeline creation failed: {}",
           err ? [[err localizedDescription] UTF8String] : "unknown");
    return false;
  }
  pipeline_textured_ = (__bridge_retained MTL::RenderPipelineState*)ps;

  REXLOG_INFO("MetalImmediateDrawer: initialized");
  return true;
}

std::unique_ptr<ImmediateTexture> MetalImmediateDrawer::CreateTexture(
    uint32_t width, uint32_t height, ImmediateTextureFilter filter,
    bool repeat, const uint8_t* data) {
  id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;

  MTLTextureDescriptor* td = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                   width:width height:height mipmapped:NO];
  td.usage = MTLTextureUsageShaderRead;
  td.storageMode = MTLStorageModeShared;
  id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
  if (!tex) return nullptr;
  [tex replaceRegion:MTLRegionMake2D(0,0,width,height) mipmapLevel:0
           withBytes:data bytesPerRow:width*4];

  MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
  sd.minFilter = (filter == ImmediateTextureFilter::kLinear)
                   ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
  sd.magFilter = sd.minFilter;
  sd.sAddressMode = repeat ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
  sd.tAddressMode = sd.sAddressMode;
  id<MTLSamplerState> samp = [dev newSamplerStateWithDescriptor:sd];
  [sd release];

  auto out = std::make_unique<MetalImmediateTexture>(width, height);
  out->texture = (__bridge_retained MTL::Texture*)tex;
  out->sampler = (__bridge_retained MTL::SamplerState*)samp;
  return out;
}

void MetalImmediateDrawer::Begin(UIDrawContext& ctx, float cw, float ch) {
  ImmediateDrawer::Begin(ctx, cw, ch);
  auto& mctx = static_cast<MetalUIDrawContext&>(ctx);
  current_command_buffer_ =
      (__bridge MTL::CommandBuffer*)mctx.command_buffer();
  current_render_encoder_ =
      (__bridge MTL::RenderCommandEncoder*)mctx.render_encoder();
}

void MetalImmediateDrawer::BeginDrawBatch(const ImmediateDrawBatch& batch) {
  if (!current_render_encoder_) return;
  batch_open_ = true;

  id<MTLRenderCommandEncoder> enc =
      (__bridge id<MTLRenderCommandEncoder>)current_render_encoder_;
  id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;

  // Upload vertices
  size_t vb_size = batch.vertex_count * sizeof(ImmediateVertex);
  id<MTLBuffer> vb = [dev newBufferWithBytes:batch.vertices
                                      length:vb_size
                                     options:MTLResourceStorageModeShared];
  [enc setVertexBuffer:vb offset:0 atIndex:1];
  [vb release];

  // Upload push constant (inv_size)
  struct { float inv_w, inv_h; } pc{ 1.0f / coordinate_space_width(), 1.0f / coordinate_space_height() };
  [enc setVertexBytes:&pc length:sizeof(pc) atIndex:0];

  // Upload index buffer if indexed
  if (batch.indices) {
    size_t ib_size = batch.index_count * sizeof(uint16_t);
    id<MTLBuffer> ib = [dev newBufferWithBytes:batch.indices
                                        length:ib_size
                                       options:MTLResourceStorageModeShared];
    current_index_buffer_ = (void*)CFBridgingRetain(ib);
  }
}

void MetalImmediateDrawer::Draw(const ImmediateDraw& draw) {
  if (!current_render_encoder_ || !batch_open_) return;

  id<MTLRenderCommandEncoder> enc =
      (__bridge id<MTLRenderCommandEncoder>)current_render_encoder_;
  id<MTLRenderPipelineState> pipe =
      (__bridge id<MTLRenderPipelineState>)pipeline_textured_;
  [enc setRenderPipelineState:pipe];

  // Scissor
  if (draw.scissor) {
    NSUInteger sx = (NSUInteger)std::max(0.0f, draw.scissor_left);
    NSUInteger sy = (NSUInteger)std::max(0.0f, draw.scissor_top);
    NSUInteger sw = (NSUInteger)std::max(0.0f, draw.scissor_right - draw.scissor_left);
    NSUInteger sh = (NSUInteger)std::max(0.0f, draw.scissor_bottom - draw.scissor_top);
    MTLScissorRect sr{ sx, sy, sw, sh };
    [enc setScissorRect:sr];
  }

  // Texture
  MTL::Texture* raw_tex = nullptr;
  MTL::SamplerState* raw_samp = nullptr;
  if (draw.texture) {
    auto* mt = static_cast<MetalImmediateTexture*>(draw.texture);
    raw_tex = mt->texture;
    raw_samp = mt->sampler;
  }
  if (!raw_tex) raw_tex = white_texture_;
  if (!raw_samp) raw_samp = default_sampler_;

  [enc setFragmentTexture:(__bridge id<MTLTexture>)raw_tex atIndex:0];
  [enc setFragmentSamplerState:(__bridge id<MTLSamplerState>)raw_samp atIndex:0];

  if (current_index_buffer_) {
    id<MTLBuffer> ib = (__bridge id<MTLBuffer>)current_index_buffer_;
    [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                    indexCount:draw.count
                     indexType:MTLIndexTypeUInt16
                   indexBuffer:ib
             indexBufferOffset:draw.index_offset * sizeof(uint16_t)];
  } else {
    [enc drawPrimitives:MTLPrimitiveTypeTriangle
            vertexStart:draw.base_vertex
            vertexCount:draw.count];
  }
}

void MetalImmediateDrawer::EndDrawBatch() {
  if (current_index_buffer_) {
    CFRelease(current_index_buffer_);
    current_index_buffer_ = nullptr;
  }
  batch_open_ = false;
}

void MetalImmediateDrawer::End() {
  ImmediateDrawer::End();
  current_command_buffer_  = nullptr;
  current_render_encoder_  = nullptr;
}

}  // namespace metal
}  // namespace ui
}  // namespace rex
