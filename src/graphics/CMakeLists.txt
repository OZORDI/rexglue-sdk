# rexgraphics: GPU emulation core (ported from Xenia)
# Namespace: rex::graphics

# Core GPU types - minimal dependencies, fully ported
set(REXGRAPHICS_CORE_SOURCES
    flags.cpp
    xenos.cpp
    pipeline/texture/info.cpp
    pipeline/texture/info_formats.cpp
    register_file.cpp
    registers.cpp
    pipeline/shader/shader.cpp
    format/ucode.cpp
    sampler_info.cpp
)

# GPU system files - full backend
set(REXGRAPHICS_SYSTEM_SOURCES
    graphics_system.cpp
    command_processor.cpp
    trace_writer.cpp
    pipeline/texture/extent.cpp
    pipeline/texture/util.cpp
    util/draw_extent_estimator.cpp
    util/draw.cpp
    packet_disassembler.cpp
    primitive_processor.cpp
    pipeline/render_target/cache.cpp
    shared_memory.cpp
    pipeline/texture/cache.cpp
    pipeline/texture/conversion.cpp
    pipeline/shader/interpreter.cpp
    pipeline/shader/translator.cpp
    pipeline/shader/translator_disasm.cpp
)

# SPIR-V shader translation
set(REXGRAPHICS_SPIRV_SOURCES
    pipeline/shader/spirv_builder.cpp
    pipeline/shader/spirv.cpp
    pipeline/shader/spirv_translator.cpp
    pipeline/shader/spirv_translator_alu.cpp
    pipeline/shader/spirv_translator_fetch.cpp
    pipeline/shader/spirv_translator_memexport.cpp
    pipeline/shader/spirv_translator_rb.cpp
)

# Vulkan backend
set(REXGRAPHICS_VULKAN_SOURCES
    vulkan/graphics_system.cpp
    vulkan/deferred_command_buffer.cpp
    vulkan/command_processor.cpp
    vulkan/pipeline_cache.cpp
    vulkan/primitive_processor.cpp
    vulkan/render_target_cache.cpp
    vulkan/shader.cpp
    vulkan/shared_memory.cpp
    vulkan/texture_cache.cpp
)

# Metal backend (macOS only)
if(APPLE)
    # DXBC shader translation (needed for DXBC → DXIL → Metal IR pipeline)
    set(REXGRAPHICS_DXBC_THIRDPARTY_SOURCES_MAC
        ${CMAKE_SOURCE_DIR}/thirdparty/dxbc/DXBCChecksum.cpp
    )

    set(REXGRAPHICS_DXBC_SOURCES_MAC
        pipeline/shader/dxbc.cpp
        pipeline/shader/dxbc_translator.cpp
        pipeline/shader/dxbc_translator_alu.cpp
        pipeline/shader/dxbc_translator_fetch.cpp
        pipeline/shader/dxbc_translator_memexport.cpp
        pipeline/shader/dxbc_translator_om.cpp
    )

    # Metal GPU backend
    set(REXGRAPHICS_METAL_SOURCES
        metal/graphics_system.mm
        metal/command_processor.mm
        metal/pipeline_cache.mm
        metal/primitive_processor.mm
        metal/render_target_cache.mm
        metal/shader.mm
        metal/shared_memory.mm
        metal/texture_cache.mm
        metal/dxbc_to_dxil_converter.mm
        metal/metal_shader_converter.mm
        metal/shader_cache.mm
    )
endif()

# D3D12 backend (Windows only)
if(WIN32)
    # DXBC third-party dependency
    set(REXGRAPHICS_DXBC_THIRDPARTY_SOURCES
        ${CMAKE_SOURCE_DIR}/thirdparty/dxbc/DXBCChecksum.cpp
    )

    # DXBC shader translation (required for D3D12)
    set(REXGRAPHICS_DXBC_SOURCES
        pipeline/shader/dxbc.cpp
        pipeline/shader/dxbc_translator.cpp
        pipeline/shader/dxbc_translator_alu.cpp
        pipeline/shader/dxbc_translator_fetch.cpp
        pipeline/shader/dxbc_translator_memexport.cpp
        pipeline/shader/dxbc_translator_om.cpp
    )

    # D3D12 GPU backend
    set(REXGRAPHICS_D3D12_SOURCES
        d3d12/graphics_system.cpp
        d3d12/command_processor.cpp
        d3d12/primitive_processor.cpp
        d3d12/render_target_cache.cpp
        d3d12/shader.cpp
        d3d12/shared_memory.cpp
        d3d12/texture_cache.cpp
        d3d12/deferred_command_list.cpp
        d3d12/pipeline_cache.cpp
    )
endif()

# Combine sources
set(REXGRAPHICS_SOURCES
    ${REXGRAPHICS_CORE_SOURCES}
    ${REXGRAPHICS_SYSTEM_SOURCES}
    ${REXGRAPHICS_SPIRV_SOURCES}
    ${REXGRAPHICS_VULKAN_SOURCES}
)

# Direct Metal GPU backend sources (macOS only, opt-in via root option).
if(APPLE AND REXGLUE_USE_METAL)
    list(APPEND REXGRAPHICS_SOURCES
        ${REXGRAPHICS_DXBC_THIRDPARTY_SOURCES_MAC}
        ${REXGRAPHICS_DXBC_SOURCES_MAC}
        ${REXGRAPHICS_METAL_SOURCES}
    )
endif()

# Add D3D12 sources on Windows
if(WIN32)
    list(APPEND REXGRAPHICS_SOURCES ${REXGRAPHICS_DXBC_THIRDPARTY_SOURCES} ${REXGRAPHICS_DXBC_SOURCES} ${REXGRAPHICS_D3D12_SOURCES})
endif()

add_library(rexgraphics STATIC ${REXGRAPHICS_SOURCES})
add_library(rex::graphics ALIAS rexgraphics)

target_include_directories(rexgraphics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(rexgraphics PRIVATE
    ${CMAKE_SOURCE_DIR}/thirdparty/renderdoc
    ${CMAKE_SOURCE_DIR}  # For thirdparty/DirectXShaderCompiler includes
)

target_link_libraries(rexgraphics PUBLIC
    rexcore
    rexkernel
    rexui
    volk::volk
    glslang::SPIRV
    # Snappy::snappy - TODO(tomc): Enable on other platforms once the RTTI linking issue is resolved
    xxHash::xxhash
    GPUOpen::VulkanMemoryAllocator
)

# Metal dependencies (macOS only)
if(APPLE)
    # Enable Objective-C++ for Metal .mm sources
    set_source_files_properties(
        metal/graphics_system.mm
        metal/command_processor.mm
        metal/shader.mm
        metal/shared_memory.mm
        metal/texture_cache.mm
        metal/primitive_processor.mm
        metal/pipeline_cache.mm
        metal/render_target_cache.mm
        metal/dxbc_to_dxil_converter.mm
        metal/metal_shader_converter.mm
        metal/shader_cache.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
    target_link_libraries(rexgraphics PUBLIC
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
        "-framework Foundation"
    )
    # REX_HAS_METAL gates runtime backend selection in kernel/runtime.cpp.
    if(REXGLUE_USE_METAL)
        target_compile_definitions(rexgraphics PUBLIC REX_HAS_METAL=1)
    endif()
endif()

# D3D12 dependencies (Windows only)
if(WIN32)
    target_link_libraries(rexgraphics PUBLIC
        d3d12
        dxgi
        dxguid
        Snappy::snappy
    )
    target_compile_definitions(rexgraphics PUBLIC REX_HAS_D3D12=1)
endif()

