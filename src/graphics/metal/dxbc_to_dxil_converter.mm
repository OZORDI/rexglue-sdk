/**
 ******************************************************************************
 * ReXGlue : Xbox 360 Static Recompilation Runtime
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.
 * Released under the BSD license - see LICENSE in the root for more details.
 ******************************************************************************
 *
 * DXBC to DXIL converter implementation.
 *
 * Dynamically loads the dxilconv library and uses the IDxbcConverter COM
 * interface to convert DXBC SM5.1 bytecode to DXIL SM6.0 bytecode.
 *
 * The dxilconv library must be installed at one of the searched paths
 * (see kDxilconvSearchPaths). On macOS this is typically a cross-compiled
 * version of Microsoft's dxilconv from the DirectXShaderCompiler project.
 */

#include <rex/graphics/metal/dxbc_to_dxil_converter.h>
#include <rex/logging.h>

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>

namespace rex::graphics::metal {

// ============================================================================
// Minimal COM-like types for IDxbcConverter (avoids dependency on Windows SDK)
// ============================================================================

using HRESULT = int32_t;
using UINT32 = uint32_t;
constexpr HRESULT S_OK = 0;

struct GUID {
  uint32_t Data1;
  uint16_t Data2;
  uint16_t Data3;
  uint8_t Data4[8];
};

// Minimal IUnknown-like vtable for IDxbcConverter.
// The actual dxilconv library exports a C function DxcCreateInstance that
// returns an object with this vtable layout.
struct IDxbcConverterVtbl {
  // IUnknown
  HRESULT (*QueryInterface)(void* self, const GUID* riid, void** ppv);
  uint32_t (*AddRef)(void* self);
  uint32_t (*Release)(void* self);
  // IDxbcConverter::Convert
  HRESULT (*Convert)(void* self, const void* dxbc_data, UINT32 dxbc_size,
                     const wchar_t* extra_options, void** dxil_data_out,
                     UINT32* dxil_size_out, wchar_t** diagnostics_out);
};

struct IDxbcConverterObj {
  IDxbcConverterVtbl* vtbl;
};

// CLSID for IDxbcConverter from DirectXShaderCompiler
static const GUID kClsidDxbcConverter = {
    0x4900391e,
    0xb752,
    0x4edd,
    {0xa8, 0x85, 0x6f, 0xb7, 0x6e, 0x25, 0xad, 0xdb}};

// IID for IDxbcConverter
static const GUID kIidDxbcConverter = {
    0x74e4bdb0,
    0x550d,
    0x4f09,
    {0x89, 0xd9, 0x7a, 0x1a, 0x4a, 0x76, 0x2f, 0x2d}};

// WinAdapter emulated __uuidof tokens used by some macOS dxilconv builds.
// These are compared as opaque REFIID values (pointer identity), not dereferenced
// as GUIDs in that mode.
static constexpr uintptr_t kIidTokenIUnknown = 0x0C52523E;
static constexpr uintptr_t kIidTokenINoMarshal = 0x038D7B3C;

// Default extra options passed to the converter.
static const wchar_t kDefaultExtraOptions[] = L"-skip-container-parts";

// Resolved extra options (may be overridden by REX_DXBC2DXIL_FLAGS env var).
static const wchar_t* resolved_extra_options_ = kDefaultExtraOptions;
static std::wstring custom_extra_options_storage_;

// Search paths for the dxilconv dynamic library.
// Only search for libdxilconv.dylib — the standard libdxcompiler.dylib does
// NOT export CLSID_DxbcConverter / IDxbcConverter, so loading it would always
// fail the DxcCreateInstance test below and produce confusing log output.
static const char* const kDxilconvSearchPaths[] = {
    "libdxilconv.dylib",
    "/usr/local/lib/libdxilconv.dylib",
    "/opt/homebrew/lib/libdxilconv.dylib",
    nullptr,
};

// ============================================================================
// Per-thread converter instance
// ============================================================================

namespace {

struct ThreadConverterState {
  IDxbcConverterObj* converter = nullptr;
  ~ThreadConverterState() {
    if (converter) {
      converter->vtbl->Release(converter);
      converter = nullptr;
    }
  }
};

std::string HResultHex(HRESULT hr) {
  char buf[11];
  std::snprintf(buf, sizeof(buf), "%08X", static_cast<unsigned>(hr));
  return std::string(buf);
}

}  // namespace

// ============================================================================
// DxbcToDxilConverter
// ============================================================================

DxbcToDxilConverter::DxbcToDxilConverter() = default;

DxbcToDxilConverter::~DxbcToDxilConverter() {
  // Intentionally do NOT dlclose the library.
  // Thread-local ThreadConverterState destructors call into the library's
  // vtable (Release). If we dlclose here and a thread exits later, the
  // destructor will jump into unmapped memory → segfault.
  // Leaking the handle is acceptable for a process-lifetime singleton.
  dxilconv_lib_ = nullptr;
}

bool DxbcToDxilConverter::Initialize() {
  // Try each search path until we find the library.
  for (const char* const* path = kDxilconvSearchPaths; *path; ++path) {
    dxilconv_lib_ = dlopen(*path, RTLD_LAZY);
    if (dxilconv_lib_) {
      REXGPU_INFO("DxbcToDxilConverter: Loaded dxilconv from {}", *path);
      break;
    }
  }

  if (!dxilconv_lib_) {
    REXGPU_WARN(
        "DxbcToDxilConverter: libdxilconv.dylib not found. "
        "DXBC→DXIL conversion will not be available. "
        "Install libdxilconv.dylib (from the wmarti/DirectXShaderCompiler "
        "macOS fork) to enable.");
    is_available_ = false;
    return false;
  }

  // Resolve DxcCreateInstance symbol.
  create_instance_fn_ = reinterpret_cast<DxcCreateInstanceFn>(
      dlsym(dxilconv_lib_, "DxcCreateInstance"));
  if (!create_instance_fn_) {
    REXGPU_ERROR(
        "DxbcToDxilConverter: DxcCreateInstance symbol not found in library");
    dlclose(dxilconv_lib_);
    dxilconv_lib_ = nullptr;
    is_available_ = false;
    return false;
  }

  // Test that we can create a converter instance.
  // Different dxilconv macOS builds may expect REFIID either as:
  // 1) A GUID pointer (standard COM style), or
  // 2) A WinAdapter emulated __uuidof token (opaque integer-as-pointer).
  struct IidCandidate {
    const char* name;
    const void* value;
  };
  const IidCandidate iid_candidates[] = {
      {"GUID(IDxbcConverter)", &kIidDxbcConverter},
      {"WinAdapterToken(IUnknown)",
       reinterpret_cast<const void*>(kIidTokenIUnknown)},
      {"WinAdapterToken(INoMarshal)",
       reinterpret_cast<const void*>(kIidTokenINoMarshal)},
  };

  IDxbcConverterObj* test_converter = nullptr;
  HRESULT hr = 0;
  const char* selected_iid_mode = "<none>";
  for (const IidCandidate& iid_candidate : iid_candidates) {
    test_converter = nullptr;
    hr = create_instance_fn_(&kClsidDxbcConverter, iid_candidate.value,
                             reinterpret_cast<void**>(&test_converter));
    if (hr == S_OK && test_converter) {
      converter_iid_ = iid_candidate.value;
      selected_iid_mode = iid_candidate.name;
      break;
    }
  }

  if (!converter_iid_ || !test_converter) {
    REXGPU_ERROR(
        "DxbcToDxilConverter: Failed to create test IDxbcConverter "
        "with any known IID mode (last hr=0x{:08X})",
        static_cast<unsigned>(hr));
    dlclose(dxilconv_lib_);
    dxilconv_lib_ = nullptr;
    is_available_ = false;
    return false;
  }

  // Runtime COM vtable ABI verification.
  // Our manually-defined IDxbcConverterVtbl must match the library's actual
  // vtable layout exactly. Verify by calling QueryInterface with the known
  // IID — if the vtable offset for QueryInterface is wrong, this will crash
  // or return an unexpected result, catching ABI mismatches early.
  void* qi_result = nullptr;
  HRESULT qi_hr = test_converter->vtbl->QueryInterface(
      test_converter, reinterpret_cast<const GUID*>(converter_iid_),
      &qi_result);
  if (qi_hr != S_OK || qi_result == nullptr) {
    REXGPU_ERROR(
        "DxbcToDxilConverter: COM vtable ABI verification failed "
        "(iid_mode={}, QueryInterface returned hr=0x{:08X}, ptr={}). "
        "The IDxbcConverterVtbl layout may not match the library.",
        selected_iid_mode, static_cast<unsigned>(qi_hr), qi_result);
    test_converter->vtbl->Release(test_converter);
    dlclose(dxilconv_lib_);
    dxilconv_lib_ = nullptr;
    is_available_ = false;
    return false;
  }
  // QueryInterface AddRef'd, release the extra reference.
  static_cast<IDxbcConverterObj*>(qi_result)->vtbl->Release(
      static_cast<IDxbcConverterObj*>(qi_result));
  test_converter->vtbl->Release(test_converter);

  REXGPU_INFO("DxbcToDxilConverter: Using IID mode {}", selected_iid_mode);

  // Check for env var override of extra options (matches reference
  // code's XENIA_DXBC2DXIL_FLAGS pattern).
  const char* flags_override = std::getenv("REX_DXBC2DXIL_FLAGS");
  if (flags_override && flags_override[0] != '\0') {
    // Widen ASCII to wchar_t.
    custom_extra_options_storage_.clear();
    for (const char* p = flags_override; *p; ++p) {
      custom_extra_options_storage_ += static_cast<wchar_t>(*p);
    }
    resolved_extra_options_ = custom_extra_options_storage_.c_str();
    REXGPU_INFO("DxbcToDxilConverter: Using custom extra options from "
                "REX_DXBC2DXIL_FLAGS: {}", flags_override);
  }

  is_available_ = true;
  REXGPU_INFO("DxbcToDxilConverter: Initialized successfully");
  return true;
}

void* DxbcToDxilConverter::GetThreadConverter(std::string* error_message) {
  static thread_local ThreadConverterState thread_state;
  if (thread_state.converter) {
    return thread_state.converter;
  }

  if (!create_instance_fn_ || !converter_iid_) {
    if (error_message) {
      *error_message = "DxcCreateInstance not available or IID mode unresolved";
    }
    return nullptr;
  }

  HRESULT hr = create_instance_fn_(
      &kClsidDxbcConverter, converter_iid_,
      reinterpret_cast<void**>(&thread_state.converter));
  if (hr != S_OK || !thread_state.converter) {
    if (error_message) {
      *error_message =
          "Failed to create IDxbcConverter (HRESULT 0x" + HResultHex(hr) + ")";
    }
    return nullptr;
  }
  return thread_state.converter;
}

bool DxbcToDxilConverter::Convert(const std::vector<uint8_t>& dxbc_data,
                                  std::vector<uint8_t>& dxil_data_out,
                                  std::string* error_message) {
  if (!is_available_) {
    if (error_message) {
      *error_message =
          "DxbcToDxilConverter not initialized or dxilconv unavailable";
    }
    return false;
  }

  // Validate DXBC header magic.
  if (dxbc_data.size() < 4 || dxbc_data[0] != 'D' || dxbc_data[1] != 'X' ||
      dxbc_data[2] != 'B' || dxbc_data[3] != 'C') {
    if (error_message) {
      *error_message = "Invalid DXBC data - missing DXBC magic header";
    }
    return false;
  }

  // Debug output directories from environment.
  const char* dxbc_dir = std::getenv("REX_DXBC_OUTPUT_DIR");
  const char* dxil_dir = std::getenv("REX_DXIL_OUTPUT_DIR");

  std::string shader_id;
  if (dxbc_dir || dxil_dir) {
    // Generate a unique shader ID for debug dumps.
    uint64_t hash = 0;
    for (size_t i = 0; i < dxbc_data.size(); ++i) {
      hash = hash * 31 + dxbc_data[i];
    }
    shader_id = std::to_string(hash) + "_" + std::to_string(getpid());
  }

  if (dxbc_dir) {
    std::string debug_path =
        std::string(dxbc_dir) + "/shader_" + shader_id + ".dxbc";
    WriteDebugFile(debug_path, dxbc_data);
  }

  // Get or create per-thread converter.
  auto* converter =
      static_cast<IDxbcConverterObj*>(GetThreadConverter(error_message));
  if (!converter) {
    return false;
  }

  // Perform the conversion.
  void* dxil_ptr = nullptr;
  UINT32 dxil_size = 0;
  wchar_t* diag = nullptr;

  HRESULT hr = converter->vtbl->Convert(
      converter, dxbc_data.data(), static_cast<UINT32>(dxbc_data.size()),
      resolved_extra_options_, &dxil_ptr, &dxil_size, &diag);

  if (hr != S_OK || dxil_ptr == nullptr || dxil_size == 0) {
    if (error_message) {
      if (diag) {
        std::string diag_utf8;
        std::mbstate_t state{};
        const wchar_t* src = diag;
        size_t len = std::wcsrtombs(nullptr, &src, 0, &state);
        if (len != static_cast<size_t>(-1)) {
          diag_utf8.resize(len);
          src = diag;
          state = std::mbstate_t{};
          std::wcsrtombs(diag_utf8.data(), &src, len + 1, &state);
        } else {
          diag_utf8 = "<non-convertible diagnostic>";
        }
        *error_message = "dxbc2dxil failed: " + diag_utf8;
      } else {
        *error_message = "dxbc2dxil failed with HRESULT 0x" + HResultHex(hr);
      }
    }
    // Allocator assumption: The wmarti/DirectXShaderCompiler macOS fork
    // maps CoTaskMemAlloc → malloc and CoTaskMemFree → free.
    // Verified: wmarti fork's WinAdapter.h defines:
    //   #define CoTaskMemAlloc malloc
    //   #define CoTaskMemFree  free
    // If using a different dxilconv build, verify its CoTaskMemFree
    // implementation before changing this.
    free(diag);
    free(dxil_ptr);
    return false;
  }

  dxil_data_out.assign(reinterpret_cast<const uint8_t*>(dxil_ptr),
                       reinterpret_cast<const uint8_t*>(dxil_ptr) + dxil_size);

  // See allocator note above — wmarti fork maps CoTaskMemFree → free.
  free(diag);
  free(dxil_ptr);

  if (dxil_dir) {
    std::string debug_path =
        std::string(dxil_dir) + "/shader_" + shader_id + ".dxil";
    WriteDebugFile(debug_path, dxil_data_out);
  }

  // Validate output.
  if (dxil_data_out.size() < 4) {
    dxil_data_out.clear();
    if (error_message) {
      *error_message = "Output DXIL blob too small";
    }
    return false;
  }

  REXGPU_DEBUG(
      "DxbcToDxilConverter: Converted {} bytes DXBC → {} bytes DXIL",
      dxbc_data.size(), dxil_data_out.size());

  return true;
}

bool DxbcToDxilConverter::WriteDebugFile(const std::string& path,
                                         const std::vector<uint8_t>& data) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  file.write(reinterpret_cast<const char*>(data.data()), data.size());
  return file.good();
}

}  // namespace rex::graphics::metal
