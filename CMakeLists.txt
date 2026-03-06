# ReXGlue SDK - Xbox 360 Recompilation Toolkit
# Modern C++23 cross-platform SDK for recompiling Xbox 360 executables
cmake_minimum_required(VERSION 3.25)

project(ReXGlue
    VERSION 0.2.3
    LANGUAGES C CXX
    DESCRIPTION "Xbox 360 Recompilation SDK"
)

#==========================================================
# Build Options
#==========================================================
option(REXGLUE_BUILD_TESTS       "Build PPC instruction and unit tests" OFF)
option(REXGLUE_ENABLE_SANITIZERS "Enable UndefinedBehaviorSanitizer" OFF)

# Graphics backend selection
# D3D12: Windows only, ON by default.
# Vulkan: Cross-platform, ON by default on Linux, OFF by default on Windows.
if(WIN32)
    option(REXGLUE_USE_D3D12  "Enable D3D12 graphics backend" ON)
    option(REXGLUE_USE_VULKAN "Enable Vulkan graphics backend" OFF)
    option(REXGLUE_USE_METAL  "Enable Metal graphics backend"  OFF)
elseif(APPLE)
    set(REXGLUE_USE_D3D12  OFF)
    set(REXGLUE_USE_VULKAN OFF)
    option(REXGLUE_USE_METAL "Enable Metal graphics backend" ON)
else()
    set(REXGLUE_USE_D3D12  OFF)
    set(REXGLUE_USE_METAL  OFF)
    option(REXGLUE_USE_VULKAN "Enable Vulkan graphics backend" ON)
endif()

if(NOT REXGLUE_USE_D3D12 AND NOT REXGLUE_USE_VULKAN AND NOT REXGLUE_USE_METAL)
    message(FATAL_ERROR "At least one graphics backend must be enabled (REXGLUE_USE_D3D12, REXGLUE_USE_VULKAN, or REXGLUE_USE_METAL)")
endif()

# Root of the SDK source tree
set(REXGLUE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# SDK install support
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Enforce Clang18+ compiler (Clang or AppleClang)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "ReXGlue requires Clang compiler. Please set CMAKE_CXX_COMPILER to clang++")
endif()

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "18.0")
        message(FATAL_ERROR "ReXGlue requires Clang 18 or newer (found ${CMAKE_CXX_COMPILER_VERSION})")
    endif()
endif()

# Enforce 64-bit architecture (x86_64 or arm64)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ReXGlue requires a 64-bit target architecture")
endif()

# Set C++23 standard (also ObjC++ for .mm files on macOS)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 23)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_DEBUG_POSTFIX d)
set(CMAKE_RELWITHDEBINFO_POSTFIX rd)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Use large code model on Linux for linking with very large recompiled executables (35MB+)
    add_compile_options(-mcmodel=large)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Global compile options
add_compile_options(
    -fno-strict-aliasing
    -ffp-model=strict
    $<$<CONFIG:DEBUG>:-g>
    $<$<CONFIG:DEBUG>:-O0>
    $<$<CONFIG:RELEASE>:-O3>
    $<$<CONFIG:RELEASE>:-DNDEBUG>
    -fno-char8_t
)

# Platform detection
if(WIN32)
    set(REX_PLATFORM "win-amd64")
    add_compile_definitions(REX_PLATFORM_WINDOWS=1)
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(REX_PLATFORM "macos-arm64")
    else()
        set(REX_PLATFORM "macos-x86_64")
    endif()
    add_compile_definitions(REX_PLATFORM_MACOS=1)
    # Use LLVM's own libc++ to match the Homebrew LLVM compiler ABI
    if(CMAKE_CXX_COMPILER MATCHES "/opt/homebrew/opt/llvm")
        set(LLVM_LIBCXX_DIR "/opt/homebrew/opt/llvm/lib/c++")
        add_compile_options(-stdlib=libc++)
        add_link_options(-stdlib=libc++ -L${LLVM_LIBCXX_DIR} -Wl,-rpath,${LLVM_LIBCXX_DIR})
    endif()
elseif(UNIX AND NOT APPLE)
    set(REX_PLATFORM "linux-amd64")
    add_compile_definitions(REX_PLATFORM_LINUX=1)
else()
    message(FATAL_ERROR "Unsupported platform.")
endif()

# Output directories (multi-config appends /<Config>/ automatically)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${REXGLUE_ROOT}/out/${REX_PLATFORM}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${REXGLUE_ROOT}/out/${REX_PLATFORM}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${REXGLUE_ROOT}/out/${REX_PLATFORM}")

# Sanitizer support for debugging memory corruption and undefined behavior
if(REXGLUE_ENABLE_SANITIZERS)
    message(STATUS "Enabling UndefinedBehaviorSanitizer (UBSan only - ASan conflicts with custom memory mapping)")
    # Note: ASan doesn't work well with the custom mmap at 0x100000000, so we only use UBSan
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=undefined)
endif()

if(REXGLUE_BUILD_TESTS)
    list(APPEND CMAKE_MODULE_PATH "${REXGLUE_ROOT}/thirdparty/catch2/extras")
endif()

# Third-party dependencies (vendored sources only)
add_subdirectory(thirdparty)

# Enable warnings after thirdparty
add_compile_options(
    -Wall 
    -Wextra
)

include_directories(${REXGLUE_ROOT}/include)

add_subdirectory(src/core)
add_subdirectory(src/filesystem)
add_subdirectory(src/ui)
add_subdirectory(src/input)
add_subdirectory(src/audio)
add_subdirectory(src/graphics)
add_subdirectory(src/system)
add_subdirectory(src/kernel)
add_subdirectory(src/codegen)    
add_subdirectory(src/rexglue)

# Add tests subdirectory if tests are enabled
if(REXGLUE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print configuration summary
message(STATUS "===========================================")
message(STATUS "ReXGlue Configuration Summary:")
message(STATUS "  Platform: ${REX_PLATFORM}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Configurations: ${CMAKE_CONFIGURATION_TYPES}")
message(STATUS "  Output Dir: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "  C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Graphics: D3D12=${REXGLUE_USE_D3D12} Vulkan=${REXGLUE_USE_VULKAN} Metal=${REXGLUE_USE_METAL}")
message(STATUS "===========================================")

# SDK install/export rules (skipped when consumed as a subdirectory)
if(PROJECT_IS_TOP_LEVEL)
    include(cmake/rexglue_install.cmake)
else()
    # When consumed via add_subdirectory(), provide the same interface as find_package().
    # This sets REXGLUE_SHARE_DIR and makes rexglue_configure_target() available.
    set(REXGLUE_SHARE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/ui" PARENT_SCOPE)
    set(REXGLUE_SHARE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rexglue_helpers.cmake)
endif()
