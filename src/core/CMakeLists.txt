# rexcore - Core utilities library
# Foundation types, data structures, and threading primitives

# Allow embedding in a parent project via REXGLUE_SDK_ROOT
if(NOT DEFINED REXGLUE_SDK_ROOT)
    set(REXGLUE_SDK_ROOT "${CMAKE_SOURCE_DIR}")
endif()

add_library(rexcore STATIC
    arena.cpp
    bit_map.cpp
    bit_stream.cpp
    byte_stream.cpp
    clock.cpp
    cvar.cpp
    exception_handler.cpp
    filesystem.cpp
    filesystem_wildcard.cpp
    logging.cpp
    memory.cpp
    mutex.cpp
    ring_buffer.cpp
    sha256.cpp
    string.cpp
    string_buffer.cpp
    threading.cpp
    timer_queue.cpp
    utf8.cpp
    vec128.cpp
)
add_library(rex::core ALIAS rexcore)

# Platform-specific sources
if(WIN32)
    target_sources(rexcore PRIVATE
        debugging_win.cpp
        exception_handler_win.cpp
        filesystem_win.cpp
        mapped_memory_win.cpp
        memory_win.cpp
    )
elseif(UNIX)
    target_sources(rexcore PRIVATE
        debugging_posix.cpp
        exception_handler_posix.cpp
        filesystem_posix.cpp
        mapped_memory_posix.cpp
        memory_posix.cpp
    )
endif()

target_include_directories(rexcore PUBLIC
    $<BUILD_INTERFACE:${REXGLUE_SDK_ROOT}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(rexcore PRIVATE
    ${REXGLUE_SDK_ROOT}
)

if(UNIX)
    target_link_libraries(rexcore PRIVATE pthread)
    if(NOT APPLE)
        # librt is needed on Linux for shm_open/shm_unlink but doesn't exist on macOS
        target_link_libraries(rexcore PRIVATE rt)
    endif()
endif()

# Allow parent project to override fmt target (e.g. for vcpkg compatibility)
if(NOT DEFINED REXGLUE_FMT_TARGET)
    set(REXGLUE_FMT_TARGET fmt::fmt)
endif()

target_link_libraries(rexcore
    PUBLIC 
        ${REXGLUE_FMT_TARGET}
        date::date 
        simde 
        absl::flags 
        absl::flags_parse 
        tomlplusplus
        spdlog::spdlog
    PRIVATE 
        disruptorplus
)
