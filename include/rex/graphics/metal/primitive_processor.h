/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Metal backend for macOS (ReXGlue runtime)
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <rex/graphics/primitive_processor.h>
#include <rex/graphics/xenos.h>

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

namespace rex::graphics::metal {

class MetalCommandProcessor;

class MetalPrimitiveProcessor : public PrimitiveProcessor {
 public:
  MetalPrimitiveProcessor(const RegisterFile& register_file,
                          memory::Memory& memory,
                          TraceWriter& trace_writer,
                          SharedMemory& shared_memory,
                          MetalCommandProcessor& command_processor);
  ~MetalPrimitiveProcessor() override;

  bool Initialize();
  void Shutdown(bool from_destructor = false);

  void CompletedSubmissionUpdated();
  void BeginSubmission();
  void BeginFrame();

  // Called when memory is invalidated.
  void MemoryInvalidationCallback(uint32_t physical_address_start,
                                  uint32_t length, bool exact_range);

  // Primitive type conversion result.
  struct ConvertedIndices {
    std::vector<uint32_t> indices;
#ifdef __OBJC__
    MTLPrimitiveType metal_primitive_type;
#endif
    uint32_t index_count;
    bool needs_conversion;
  };

  // Convert Xenos primitives to Metal-compatible primitives.
  // Handles triangle fans, quads, rects -> triangle lists.
  ConvertedIndices ConvertPrimitives(
      xenos::PrimitiveType xenos_type,
      const void* index_data, uint32_t index_count,
      xenos::IndexFormat index_format, xenos::Endian endian);

 private:
  MetalCommandProcessor& command_processor_;
};

}  // namespace rex::graphics::metal
