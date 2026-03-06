/**
 * macOS Metal surface implementation.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/ui/surface_macos.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

namespace rex {
namespace ui {

bool MacOSMetalLayerSurface::GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const {
  if (!layer_) {
    width_out = 0;
    height_out = 0;
    return false;
  }
  CGSize sz = layer_.bounds.size;
  width_out  = static_cast<uint32_t>(sz.width);
  height_out = static_cast<uint32_t>(sz.height);
  return true;
}

}  // namespace ui
}  // namespace rex
