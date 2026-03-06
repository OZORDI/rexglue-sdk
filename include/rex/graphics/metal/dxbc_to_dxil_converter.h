#pragma once

// Forward declare in global scope
struct IDxbcConverter;
/**
 * DXBC-to-DXIL converter using the official IDxbcConverter C++ interface.
 * Uses the wmarti/DirectXShaderCompiler dxbc2dxil-perf branch or any
 * compatible libdxilconv.dylib.
 *
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <string>
#include <vector>

namespace rex {
namespace graphics {
namespace metal {

class DxbcToDxilConverter {
 public:
  DxbcToDxilConverter();
  ~DxbcToDxilConverter();

  // Initialize the converter - loads the IDxbcConverter interface from the
  // linked libdxilconv.dylib. Returns true if available.
  bool Initialize();

  bool is_available() const { return is_available_; }

  // Convert a DXBC blob to DXIL. Returns true and fills dxil_data_out on
  // success. Sets error_message on failure.
  bool Convert(const std::vector<uint8_t>& dxbc_data,
               std::vector<uint8_t>& dxil_data_out,
               std::string* error_message = nullptr);

 private:
  ::IDxbcConverter* GetThreadConverter(std::string* error_message);

  static bool WriteFile(const std::string& path, const std::vector<uint8_t>& data);

  bool is_available_ = false;
  std::wstring extra_options_;
};

}  // namespace metal
}  // namespace graphics
}  // namespace rex
