/**
 * Metal presenter - presents guest output via CAMetalLayer.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/ui/metal/metal_presenter.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/QuartzCore.h>

#import <Metal/Metal.h>

#include <rex/logging.h>
#include <rex/ui/surface_macos.h>

namespace rex {
namespace ui {
namespace metal {

MetalPresenter::MetalPresenter(MetalProvider* provider,
                               HostGpuLossCallback host_gpu_loss_callback)
    : Presenter(host_gpu_loss_callback), provider_(provider) {
  device_ = provider_->device();
  guest_output_textures_.fill(nil);
}

MetalPresenter::~MetalPresenter() {
  // Release guest output textures
  for (id tex : guest_output_textures_) {
    if (tex) {
      [tex release];
    }
  }
  if (guest_output_pipeline_) {
    [(id)guest_output_pipeline_ release];
    guest_output_pipeline_ = nullptr;
  }
  if (guest_output_sampler_) {
    [(id)guest_output_sampler_ release];
    guest_output_sampler_ = nullptr;
  }
}

bool MetalPresenter::Initialize() {
  if (!InitializeCommonSurfaceIndependent()) {
    return false;
  }
  command_queue_ = (__bridge id<MTLCommandQueue>)provider_->command_queue();
  REXLOG_INFO("MetalPresenter: Initialized");
  return true;
}

Surface::TypeFlags MetalPresenter::GetSupportedSurfaceTypes() const {
  return Surface::kTypeFlag_MacOSMetalLayer;
}

bool MetalPresenter::CaptureGuestOutput(RawImage& image_out) {
  uint32_t mailbox_index;
  GuestOutputProperties props;
  auto lock = ConsumeGuestOutput(mailbox_index, &props, nullptr);
  if (mailbox_index == UINT32_MAX || !props.IsActive()) {
    return false;
  }

  id<MTLTexture> tex = (__bridge id<MTLTexture>)guest_output_textures_[mailbox_index];
  if (!tex) return false;

  uint32_t w = [tex width];
  uint32_t h = [tex height];
  image_out.width = w;
  image_out.height = h;
  image_out.stride = w * 4;
  image_out.data.resize(h * w * 4);

  [tex getBytes:image_out.data.data()
        bytesPerRow:image_out.stride
         fromRegion:MTLRegionMake2D(0, 0, w, h)
        mipmapLevel:0];
  return true;
}

Presenter::SurfacePaintConnectResult
MetalPresenter::ConnectOrReconnectPaintingToSurfaceFromUIThread(
    Surface& new_surface, uint32_t new_surface_width, uint32_t new_surface_height,
    bool was_paintable, bool& is_vsync_implicit_out) {
  if (new_surface.GetType() != Surface::kTypeIndex_MacOSMetalLayer) {
    return SurfacePaintConnectResult::kFailureSurfaceUnusable;
  }

  auto& metal_surface = static_cast<MacOSMetalLayerSurface&>(new_surface);
  metal_layer_ = metal_surface.layer();
  if (!metal_layer_) {
    return SurfacePaintConnectResult::kFailure;
  }

  id<MTLDevice> mtl_device = (__bridge id<MTLDevice>)device_;
  [metal_layer_ setDevice:mtl_device];
  [metal_layer_ setPixelFormat:MTLPixelFormatBGRA8Unorm];
  [metal_layer_ setFramebufferOnly:YES];

  surface_width_in_points_ = new_surface_width;
  surface_height_in_points_ = new_surface_height;

  // CAMetalLayer doesn't impose vsync by default; we do it via presentAfterMinimumDuration.
  is_vsync_implicit_out = false;

  if (!EnsurePresentPipelines()) {
    return SurfacePaintConnectResult::kFailure;
  }

  return SurfacePaintConnectResult::kSuccess;
}

void MetalPresenter::DisconnectPaintingFromSurfaceFromUIThreadImpl() {
  metal_layer_ = nullptr;
}

bool MetalPresenter::RefreshGuestOutputImpl(
    uint32_t mailbox_index, uint32_t frontbuffer_width, uint32_t frontbuffer_height,
    std::function<bool(GuestOutputRefreshContext& context)> refresher,
    bool& is_8bpc_out_ref) {

  if (!EnsureGuestOutputResources(frontbuffer_width, frontbuffer_height)) {
    return false;
  }

  id tex = guest_output_textures_[mailbox_index];
  MetalGuestOutputRefreshContext context(is_8bpc_out_ref, tex);
  return refresher(context);
}

Presenter::PaintResult MetalPresenter::PaintAndPresentImpl(bool execute_ui_drawers) {
  if (!metal_layer_) {
    return PaintResult::kNotPresentedConnectionOutdated;
  }

  @autoreleasepool {
    id<CAMetalDrawable> drawable = [metal_layer_ nextDrawable];
    if (!drawable) {
      return PaintResult::kNotPresented;
    }

    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
    id<MTLCommandBuffer> cmd = [queue commandBuffer];
    if (!cmd) {
      return PaintResult::kNotPresented;
    }

    // Get the latest guest output
    uint32_t mailbox_index;
    GuestOutputProperties props;
    GuestOutputPaintConfig config;
    {
      auto lock = ConsumeGuestOutput(mailbox_index, &props, &config);
      if (mailbox_index == UINT32_MAX || !props.IsActive()) {
        // No guest output - clear to black
        MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = drawable.texture;
        rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
        rpd.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];

        if (execute_ui_drawers) {
          MetalUIDrawContext ui_ctx(*this,
                                   drawable.texture.width,
                                   drawable.texture.height,
                                   (__bridge id)cmd,
                                   (__bridge id)enc);
          ExecuteUIDrawersFromUIThread(ui_ctx);
        }

        [enc endEncoding];
        [cmd presentDrawable:drawable];
        [cmd commit];
        return PaintResult::kPresented;
      }
    }

    id<MTLTexture> guest_tex = (__bridge id<MTLTexture>)guest_output_textures_[mailbox_index];

    // Simple blit from guest output to drawable
    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture = drawable.texture;
    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpd.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];

    if (guest_tex && guest_output_pipeline_) {
      id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)guest_output_pipeline_;
      id<MTLSamplerState> sampler = (__bridge id<MTLSamplerState>)guest_output_sampler_;
      [enc setRenderPipelineState:pipeline];
      [enc setFragmentTexture:guest_tex atIndex:0];
      [enc setFragmentSamplerState:sampler atIndex:0];
      [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    }

    if (execute_ui_drawers) {
      MetalUIDrawContext ui_ctx(*this,
                                drawable.texture.width,
                                drawable.texture.height,
                                (__bridge id)cmd,
                                (__bridge id)enc);
      ExecuteUIDrawersFromUIThread(ui_ctx);
    }

    [enc endEncoding];
    [cmd presentDrawable:drawable];
    [cmd commit];
  }

  return PaintResult::kPresented;
}

bool MetalPresenter::EnsureGuestOutputResources(uint32_t width, uint32_t height) {
  if (guest_output_width_ == width && guest_output_height_ == height &&
      guest_output_textures_[0] != nil) {
    return true;
  }

  id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;

  // Release old textures
  for (id& tex : guest_output_textures_) {
    if (tex) { [tex release]; tex = nil; }
  }

  MTLTextureDescriptor* desc = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                   width:width
                                  height:height
                               mipmapped:NO];
  desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
  desc.storageMode = MTLStorageModeShared;

  for (id& tex : guest_output_textures_) {
    tex = [[dev newTextureWithDescriptor:desc] retain];
    if (!tex) {
      REXLOG_ERROR("MetalPresenter: Failed to create guest output texture");
      return false;
    }
  }

  guest_output_width_ = width;
  guest_output_height_ = height;
  return true;
}

bool MetalPresenter::EnsurePresentPipelines() {
  if (guest_output_pipeline_) return true;

  id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;

  // Simple full-screen blit shaders
  NSString* src = @R"(
#include <metal_stdlib>
using namespace metal;

struct VertOut {
  float4 pos [[position]];
  float2 uv;
};

vertex VertOut vs_blit(uint vid [[vertex_id]]) {
  VertOut out;
  float2 pos = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? -1.0 : 1.0);
  out.pos = float4(pos, 0.0, 1.0);
  out.uv  = float2((vid & 1) ? 1.0 : 0.0, (vid & 2) ? 1.0 : 0.0);
  return out;
}

fragment float4 fs_blit(VertOut in [[stage_in]],
                         texture2d<float> tex [[texture(0)]],
                         sampler s [[sampler(0)]]) {
  return tex.sample(s, in.uv);
}
)";

  NSError* err = nil;
  id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:&err];
  if (!lib) {
    REXLOG_ERROR("MetalPresenter: Failed to compile blit shaders: {}",
           err ? [[err localizedDescription] UTF8String] : "unknown");
    return false;
  }

  id<MTLFunction> vs = [lib newFunctionWithName:@"vs_blit"];
  id<MTLFunction> fs = [lib newFunctionWithName:@"fs_blit"];

  MTLRenderPipelineDescriptor* rpd = [[MTLRenderPipelineDescriptor alloc] init];
  rpd.vertexFunction = vs;
  rpd.fragmentFunction = fs;
  rpd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

  id<MTLRenderPipelineState> pipeline = [dev newRenderPipelineStateWithDescriptor:rpd error:&err];
  [rpd release];
  [vs release];
  [fs release];
  [lib release];

  if (!pipeline) {
    REXLOG_ERROR("MetalPresenter: Failed to create blit pipeline: {}",
           err ? [[err localizedDescription] UTF8String] : "unknown");
    return false;
  }

  guest_output_pipeline_ = (id)pipeline;

  // Create bilinear sampler
  MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
  sd.minFilter = MTLSamplerMinMagFilterLinear;
  sd.magFilter = MTLSamplerMinMagFilterLinear;
  sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
  sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
  id<MTLSamplerState> sampler = [dev newSamplerStateWithDescriptor:sd];
  [sd release];

  guest_output_sampler_ = (id)sampler;
  present_pixel_format_ = MTLPixelFormatBGRA8Unorm;

  return true;
}

}  // namespace metal
}  // namespace ui
}  // namespace rex
