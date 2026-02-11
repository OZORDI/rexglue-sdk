/**
 ******************************************************************************
 * @modified    Tom Clay, 2026 - Metal backend for macOS (ReXGlue runtime)
 */

#import <Metal/Metal.h>

#include <cstring>

#include <rex/graphics/metal/primitive_processor.h>
#include <rex/graphics/metal/command_processor.h>
#include <rex/graphics/xenos.h>
#include <rex/byte_order.h>
#include <rex/logging.h>

namespace rex::graphics::metal {

MetalPrimitiveProcessor::MetalPrimitiveProcessor(
    const RegisterFile& register_file,
    memory::Memory& memory,
    TraceWriter& trace_writer,
    SharedMemory& shared_memory,
    MetalCommandProcessor& command_processor)
    : PrimitiveProcessor(register_file, memory, trace_writer, shared_memory),
      command_processor_(command_processor) {}

MetalPrimitiveProcessor::~MetalPrimitiveProcessor() {
  Shutdown(true);
}

bool MetalPrimitiveProcessor::Initialize() {
  XELOGI("MetalPrimitiveProcessor: Initialized");
  return true;
}

void MetalPrimitiveProcessor::Shutdown(bool from_destructor) {
  // Nothing to release.
}

void MetalPrimitiveProcessor::CompletedSubmissionUpdated() {}

void MetalPrimitiveProcessor::BeginSubmission() {}

void MetalPrimitiveProcessor::BeginFrame() {}

void MetalPrimitiveProcessor::MemoryInvalidationCallback(
    uint32_t physical_address_start, uint32_t length, bool exact_range) {
  // TODO(metal): Invalidate cached converted index buffers in this range.
}

MetalPrimitiveProcessor::ConvertedIndices
MetalPrimitiveProcessor::ConvertPrimitives(
    xenos::PrimitiveType xenos_type,
    const void* index_data, uint32_t index_count,
    xenos::IndexFormat index_format, xenos::Endian endian) {
  ConvertedIndices result;
  result.index_count = 0;
  result.needs_conversion = false;

  // Helper: read an index with endian swap.
  auto read_index = [&](uint32_t i) -> uint32_t {
    if (index_format == xenos::IndexFormat::kInt16) {
      uint16_t idx;
      std::memcpy(&idx, static_cast<const uint8_t*>(index_data) + i * 2, 2);
      if (endian == xenos::Endian::k8in16 ||
          endian == xenos::Endian::k8in32) {
        idx = __builtin_bswap16(idx);
      }
      return uint32_t(idx);
    } else {
      uint32_t idx;
      std::memcpy(&idx, static_cast<const uint8_t*>(index_data) + i * 4, 4);
      if (endian == xenos::Endian::k8in32) {
        idx = __builtin_bswap32(idx);
      } else if (endian == xenos::Endian::k8in16) {
        idx = ((idx & 0xFF00FF00) >> 8) | ((idx & 0x00FF00FF) << 8);
      } else if (endian == xenos::Endian::k16in32) {
        idx = ((idx & 0xFFFF0000) >> 16) | ((idx & 0x0000FFFF) << 16);
      }
      return idx;
    }
  };

  switch (xenos_type) {
    case xenos::PrimitiveType::kTriangleList:
      // Metal supports triangle lists natively.
      result.metal_primitive_type = MTLPrimitiveTypeTriangle;
      result.needs_conversion = false;
      result.index_count = index_count;
      break;

    case xenos::PrimitiveType::kTriangleStrip:
      // Metal supports triangle strips natively.
      result.metal_primitive_type = MTLPrimitiveTypeTriangleStrip;
      result.needs_conversion = false;
      result.index_count = index_count;
      break;

    case xenos::PrimitiveType::kTriangleFan:
      // Metal does NOT support triangle fans.
      // Convert: fan with N vertices → (N-2) triangles.
      if (index_count < 3) {
        result.index_count = 0;
        return result;
      }
      result.needs_conversion = true;
      result.metal_primitive_type = MTLPrimitiveTypeTriangle;
      result.index_count = (index_count - 2) * 3;
      result.indices.reserve(result.index_count);
      if (index_data) {
        uint32_t center = read_index(0);
        for (uint32_t i = 1; i < index_count - 1; ++i) {
          result.indices.push_back(center);
          result.indices.push_back(read_index(i));
          result.indices.push_back(read_index(i + 1));
        }
      } else {
        // Auto-indexed fan.
        for (uint32_t i = 1; i < index_count - 1; ++i) {
          result.indices.push_back(0);
          result.indices.push_back(i);
          result.indices.push_back(i + 1);
        }
      }
      break;

    case xenos::PrimitiveType::kLineList:
      result.metal_primitive_type = MTLPrimitiveTypeLine;
      result.needs_conversion = false;
      result.index_count = index_count;
      break;

    case xenos::PrimitiveType::kLineStrip:
      result.metal_primitive_type = MTLPrimitiveTypeLineStrip;
      result.needs_conversion = false;
      result.index_count = index_count;
      break;

    case xenos::PrimitiveType::kPointList:
      result.metal_primitive_type = MTLPrimitiveTypePoint;
      result.needs_conversion = false;
      result.index_count = index_count;
      break;

    case xenos::PrimitiveType::kQuadList:
      // Metal does NOT support quads.
      // Convert: each quad (4 vertices) → 2 triangles (6 indices).
      if (index_count < 4) {
        result.index_count = 0;
        return result;
      }
      result.needs_conversion = true;
      result.metal_primitive_type = MTLPrimitiveTypeTriangle;
      result.index_count = (index_count / 4) * 6;
      result.indices.reserve(result.index_count);
      for (uint32_t i = 0; i + 3 < index_count; i += 4) {
        uint32_t v0, v1, v2, v3;
        if (index_data) {
          v0 = read_index(i);
          v1 = read_index(i + 1);
          v2 = read_index(i + 2);
          v3 = read_index(i + 3);
        } else {
          v0 = i; v1 = i + 1; v2 = i + 2; v3 = i + 3;
        }
        // Quad: v0-v1-v2-v3 → triangles: v0-v1-v2, v0-v2-v3
        result.indices.push_back(v0);
        result.indices.push_back(v1);
        result.indices.push_back(v2);
        result.indices.push_back(v0);
        result.indices.push_back(v2);
        result.indices.push_back(v3);
      }
      break;

    case xenos::PrimitiveType::kRectangleList:
      // Metal does NOT support rectangle lists.
      // Xenos kRectangleList provides 3 vertices per rect (two corners +
      // a third defining the opposite edge). The fourth vertex must be
      // synthesized as v3 = v1 + v2 - v0. Proper implementation requires
      // a vertex shader expansion pass or geometry shader emulation.
      //
      // TODO(metal): Implement vertex shader–based rect expansion.
      // For now, emit only the single triangle (v0, v1, v2) per rect.
      // This renders half the quad — incorrect but avoids the misleading
      // reversed-winding duplicate that was here before.
      if (index_count < 3) {
        result.index_count = 0;
        return result;
      }
      result.needs_conversion = true;
      result.metal_primitive_type = MTLPrimitiveTypeTriangle;
      result.index_count = (index_count / 3) * 3;
      result.indices.reserve(result.index_count);
      for (uint32_t i = 0; i + 2 < index_count; i += 3) {
        uint32_t v0, v1, v2;
        if (index_data) {
          v0 = read_index(i);
          v1 = read_index(i + 1);
          v2 = read_index(i + 2);
        } else {
          v0 = i; v1 = i + 1; v2 = i + 2;
        }
        // First triangle only — second triangle needs synthesized v3.
        result.indices.push_back(v0);
        result.indices.push_back(v1);
        result.indices.push_back(v2);
      }
      XELOGW("MetalPrimitiveProcessor: kRectangleList rendered as half-quad "
             "(vertex shader rect expansion not yet implemented)");
      break;

    default:
      XELOGW("MetalPrimitiveProcessor: Unsupported primitive type {}",
             uint32_t(xenos_type));
      result.metal_primitive_type = MTLPrimitiveTypeTriangle;
      result.needs_conversion = false;
      result.index_count = index_count;
      break;
  }

  // If we don't need conversion but have index data that requires endian
  // swapping, copy the indices into the output buffer with the swap applied.
  // For native endian (kNone), skip the copy entirely — the caller can use
  // the original index data directly from shared memory.
  if (!result.needs_conversion && index_data && endian != xenos::Endian::kNone) {
    result.indices.reserve(index_count);
    for (uint32_t i = 0; i < index_count; ++i) {
      result.indices.push_back(read_index(i));
    }
    result.needs_conversion = true;  // We have converted indices to use
  }

  return result;
}

}  // namespace rex::graphics::metal
