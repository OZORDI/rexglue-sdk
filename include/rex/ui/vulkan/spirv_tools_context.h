#pragma once
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */


#include <cstdint>
#include <string>

#include <spirv-tools/libspirv.h>
#include <rex/platform.h>

#if REX_PLATFORM_LINUX || REX_PLATFORM_MAC
#include <dlfcn.h>
#elif REX_PLATFORM_WIN32
#include <rex/platform/win.h>
#endif

namespace rex {
namespace ui {
namespace vulkan {

class SpirvToolsContext {
 public:
  SpirvToolsContext() {}
  SpirvToolsContext(const SpirvToolsContext& context) = delete;
  SpirvToolsContext& operator=(const SpirvToolsContext& context) = delete;
  ~SpirvToolsContext() { Shutdown(); }
  bool Initialize(unsigned int spirv_version);
  void Shutdown();

  spv_result_t Validate(const uint32_t* words, size_t num_words,
                        std::string* error) const;

 private:
#if REX_PLATFORM_LINUX || REX_PLATFORM_MAC
  void* library_ = nullptr;
#elif REX_PLATFORM_WIN32
  HMODULE library_ = nullptr;
#endif

  template <typename FunctionPointer>
  bool LoadLibraryFunction(FunctionPointer& function, const char* name) {
#if REX_PLATFORM_LINUX || REX_PLATFORM_MAC
    function = reinterpret_cast<FunctionPointer>(dlsym(library_, name));
#elif REX_PLATFORM_WIN32
    function =
        reinterpret_cast<FunctionPointer>(GetProcAddress(library_, name));
#else
#error No SPIRV-Tools LoadLibraryFunction provided for the target platform.
#endif
    return function != nullptr;
  }
  decltype(&spvContextCreate) fn_spvContextCreate_ = nullptr;
  decltype(&spvContextDestroy) fn_spvContextDestroy_ = nullptr;
  decltype(&spvValidateBinary) fn_spvValidateBinary_ = nullptr;
  decltype(&spvDiagnosticDestroy) fn_spvDiagnosticDestroy_ = nullptr;

  spv_context context_ = nullptr;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace rex

