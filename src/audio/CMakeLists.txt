# rexaudio - Audio subsystem library
# Audio processing and XMA decoding for Xbox 360 emulation

add_library(rexaudio STATIC
    audio_driver.cpp
    audio_system.cpp
    xma_register_file.cpp
    # NOP backend (always available)
    nop/nop_audio_system.cpp
)
add_library(rex::audio ALIAS rexaudio)

target_include_directories(rexaudio PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(rexaudio
    PUBLIC rexcore
)

# XMA decoding requires the vendored FFmpeg (patched with AV_CODEC_ID_XMAFRAMES).
# SDL audio backend provides the actual audio output via SDL2.
if(TARGET libavcodec AND TARGET libavutil)
    message(STATUS "rexaudio: vendored FFmpeg found - XMA decoding and SDL audio enabled")

    target_sources(rexaudio PRIVATE
        xma_context.cpp
        xma_decoder.cpp
        sdl/sdl_audio_system.cpp
        sdl/sdl_audio_driver.cpp
    )
    target_link_libraries(rexaudio
        PUBLIC SDL2::SDL2
        PRIVATE libavcodec libavutil
    )
    target_compile_definitions(rexaudio PUBLIC REX_HAS_SDL_AUDIO=1 REX_HAS_FFMPEG=1)
else()
    message(STATUS "rexaudio: vendored FFmpeg not found - using stub XMA decoder (NOP audio only)")
    target_sources(rexaudio PRIVATE xma_decoder_stub.cpp)
endif()
