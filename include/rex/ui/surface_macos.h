#pragma once
/**
 * macOS Metal surface (CAMetalLayer).
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/ui/surface.h>

// Forward-declare CAMetalLayer without pulling in Objective-C headers
#ifdef __OBJC__
@class CAMetalLayer;
#else
typedef struct objc_object CAMetalLayer;
#endif

namespace rex {
namespace ui {

class MacOSMetalLayerSurface final : public Surface {
 public:
  explicit MacOSMetalLayerSurface(CAMetalLayer* layer) : layer_(layer) {}

  TypeIndex GetType() const override { return kTypeIndex_MacOSMetalLayer; }

  CAMetalLayer* layer() const { return layer_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override;

 private:
  CAMetalLayer* layer_ = nullptr;
};

}  // namespace ui
}  // namespace rex
