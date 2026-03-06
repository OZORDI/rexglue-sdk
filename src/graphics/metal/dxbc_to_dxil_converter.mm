/**
 * DXBC-to-DXIL converter using official IDxbcConverter C++ interface.
 * Fixes the ABI mismatch from the old manual-vtable approach.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/graphics/metal/dxbc_to_dxil_converter.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

// Official interface from DirectXShaderCompiler (wmarti fork)
#include <dxc/WinAdapter.h>
#include <dxc/dxcapi.h>
#include <DxbcConverter.h>

#include <rex/logging.h>

namespace rex {
namespace graphics {
namespace metal {

namespace {

constexpr wchar_t kDefaultExtraOptions[] = L"-skip-container-parts";

const CLSID kClsidDxbcConverter = {
    0x4900391e, 0xb752, 0x4edd,
    {0xa8, 0x85, 0x6f, 0xb7, 0x6e, 0x25, 0xad, 0xdb}};

std::wstring WidenAscii(const std::string& s) {
  std::wstring out;
  out.reserve(s.size());
  for (char c : s) out.push_back(static_cast<wchar_t>(c));
  return out;
}

std::string HexHR(HRESULT hr) {
  char buf[11];
  std::snprintf(buf, sizeof(buf), "%08X", static_cast<unsigned>(hr));
  return buf;
}

struct ThreadConverter {
  IDxbcConverter* converter = nullptr;
  ~ThreadConverter() { if (converter) converter->Release(); }
};

}  // namespace

DxbcToDxilConverter::DxbcToDxilConverter() = default;
DxbcToDxilConverter::~DxbcToDxilConverter() = default;

bool DxbcToDxilConverter::Initialize() {
  const char* env_opts = std::getenv("REX_DXBC2DXIL_FLAGS");
  if (env_opts) {
    extra_options_ = WidenAscii(env_opts);
  } else {
    extra_options_ = kDefaultExtraOptions;
  }

  // Verify the library is available
  IDxbcConverter* test = nullptr;
  HRESULT hr = DxcCreateInstance(kClsidDxbcConverter,
                                  __uuidof(IDxbcConverter),
                                  reinterpret_cast<void**>(&test));
  if (hr != S_OK || !test) {
    REXLOG_ERROR("DxbcToDxilConverter: IDxbcConverter not available (hr=0x{})", HexHR(hr));
    is_available_ = false;
    return false;
  }
  test->Release();
  is_available_ = true;
  REXLOG_INFO("DxbcToDxilConverter: initialized (extra_opts={})",
         env_opts ? env_opts : "-skip-container-parts");
  return true;
}

bool DxbcToDxilConverter::Convert(const std::vector<uint8_t>& dxbc_data,
                                   std::vector<uint8_t>& dxil_data_out,
                                   std::string* error_message) {
  if (!is_available_) {
    if (error_message) *error_message = "DxbcToDxilConverter not available";
    return false;
  }

  if (dxbc_data.size() < 4 ||
      dxbc_data[0] != 'D' || dxbc_data[1] != 'X' ||
      dxbc_data[2] != 'B' || dxbc_data[3] != 'C') {
    if (error_message) *error_message = "Invalid DXBC header";
    return false;
  }

  IDxbcConverter* converter = GetThreadConverter(error_message);
  if (!converter) return false;

  void*   dxil_ptr  = nullptr;
  UINT32  dxil_size = 0;
  wchar_t* diag     = nullptr;

  HRESULT hr = converter->Convert(
      dxbc_data.data(),
      static_cast<UINT32>(dxbc_data.size()),
      extra_options_.empty() ? nullptr : extra_options_.c_str(),
      &dxil_ptr, &dxil_size, &diag);

  if (hr != S_OK || !dxil_ptr || dxil_size == 0) {
    if (error_message) {
      if (diag) {
        std::string msg;
        for (const wchar_t* p = diag; *p; ++p) msg += static_cast<char>(*p);
        *error_message = "DXBC→DXIL failed: " + msg;
      } else {
        *error_message = "DXBC→DXIL failed: HRESULT=0x" + HexHR(hr);
      }
    }
    CoTaskMemFree(diag);
    CoTaskMemFree(dxil_ptr);
    return false;
  }

  dxil_data_out.assign(
      reinterpret_cast<const uint8_t*>(dxil_ptr),
      reinterpret_cast<const uint8_t*>(dxil_ptr) + dxil_size);

  CoTaskMemFree(diag);
  CoTaskMemFree(dxil_ptr);

  REXLOG_DEBUG("DxbcToDxilConverter: {} bytes DXBC → {} bytes DXIL",
         dxbc_data.size(), dxil_data_out.size());
  return true;
}

IDxbcConverter* DxbcToDxilConverter::GetThreadConverter(std::string* error_message) {
  static thread_local ThreadConverter tls;
  if (tls.converter) return tls.converter;

  HRESULT hr = DxcCreateInstance(kClsidDxbcConverter,
                                  __uuidof(IDxbcConverter),
                                  reinterpret_cast<void**>(&tls.converter));
  if (hr != S_OK || !tls.converter) {
    if (error_message)
      *error_message = "Failed to create IDxbcConverter: HRESULT=0x" + HexHR(hr);
    return nullptr;
  }
  return tls.converter;
}

bool DxbcToDxilConverter::WriteFile(const std::string& path,
                                     const std::vector<uint8_t>& data) {
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(data.data()), data.size());
  return f.good();
}

}  // namespace metal
}  // namespace graphics
}  // namespace rex
