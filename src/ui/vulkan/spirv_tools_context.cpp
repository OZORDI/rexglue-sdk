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

#include <rex/ui/vulkan/spirv_tools_context.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include <rex/logging.h>
#include <rex/platform.h>

#if REX_PLATFORM_LINUX || REX_PLATFORM_MAC
#include <dlfcn.h>
#elif REX_PLATFORM_WIN32
#include <rex/platform.h>
#endif

namespace rex {
namespace ui {
namespace vulkan {

bool SpirvToolsContext::Initialize(unsigned int spirv_version) {
  const char* vulkan_sdk_env = std::getenv("VULKAN_SDK");
  if (!vulkan_sdk_env) {
    REXLOG_ERROR("SPIRV-Tools: Failed to get the VULKAN_SDK environment variable");
    Shutdown();
    return false;
  }
  std::filesystem::path vulkan_sdk_path(vulkan_sdk_env);
#if REX_PLATFORM_LINUX || REX_PLATFORM_MAC
#if REX_PLATFORM_MAC
  library_ = dlopen((vulkan_sdk_path / "lib/libSPIRV-Tools-shared.dylib").c_str(),
                    RTLD_NOW | RTLD_LOCAL);
#else
  library_ = dlopen((vulkan_sdk_path / "bin/libSPIRV-Tools-shared.so").c_str(),
                    RTLD_NOW | RTLD_LOCAL);
#endif
  if (!library_) {
    REXLOG_ERROR(
        "SPIRV-Tools: Failed to load SPIRV-Tools-shared library");
    Shutdown();
    return false;
  }
#elif REX_PLATFORM_WIN32
  library_ = LoadLibraryW(
      (vulkan_sdk_path / "Bin/SPIRV-Tools-shared.dll").wstring().c_str());
  if (!library_) {
    REXLOG_ERROR(
        "SPIRV-Tools: Failed to load %VULKAN_SDK%/Bin/SPIRV-Tools-shared.dll");
    Shutdown();
    return false;
  }
#else
#error No SPIRV-Tools library loading provided for the target platform.
#endif
  if (!LoadLibraryFunction(fn_spvContextCreate_, "spvContextCreate") ||
      !LoadLibraryFunction(fn_spvContextDestroy_, "spvContextDestroy") ||
      !LoadLibraryFunction(fn_spvValidateBinary_, "spvValidateBinary") ||
      !LoadLibraryFunction(fn_spvDiagnosticDestroy_, "spvDiagnosticDestroy")) {
    REXLOG_ERROR("SPIRV-Tools: Failed to get library function pointers");
    Shutdown();
    return false;
  }
  spv_target_env target_env;
  if (spirv_version >= 0x10500) {
    target_env = SPV_ENV_VULKAN_1_2;
  } else if (spirv_version >= 0x10400) {
    target_env = SPV_ENV_VULKAN_1_1_SPIRV_1_4;
  } else if (spirv_version >= 0x10300) {
    target_env = SPV_ENV_VULKAN_1_1;
  } else {
    target_env = SPV_ENV_VULKAN_1_0;
  }
  context_ = fn_spvContextCreate_(target_env);
  if (!context_) {
    REXLOG_ERROR("SPIRV-Tools: Failed to create a Vulkan 1.0 context");
    Shutdown();
    return false;
  }
  return true;
}

void SpirvToolsContext::Shutdown() {
  if (context_) {
    fn_spvContextDestroy_(context_);
    context_ = nullptr;
  }
  if (library_) {
#if REX_PLATFORM_LINUX || REX_PLATFORM_MAC
    dlclose(library_);
#elif REX_PLATFORM_WIN32
    FreeLibrary(library_);
#endif
    library_ = nullptr;
  }
}

spv_result_t SpirvToolsContext::Validate(const uint32_t* words,
                                         size_t num_words,
                                         std::string* error) const {
  if (error) {
    error->clear();
  }
  if (!context_) {
    return SPV_UNSUPPORTED;
  }
  spv_diagnostic diagnostic = nullptr;
  spv_result_t result =
      fn_spvValidateBinary_(context_, words, num_words, &diagnostic);
  if (diagnostic) {
    if (error && diagnostic && diagnostic->error) {
      *error = diagnostic->error;
    }
    fn_spvDiagnosticDestroy_(diagnostic);
  }
  return result;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace rex
