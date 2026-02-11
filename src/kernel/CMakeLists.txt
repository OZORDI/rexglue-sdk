# rexkernel: Kernel emulation (ported from Xenia)
# Namespace: rex::kernel

set(REXKERNEL_SOURCES
    runtime.cpp
    xmemory.cpp

    kernel_module.cpp
    kernel_state.cpp
    user_module.cpp
    xenumerator.cpp
    xevent.cpp
    xfile.cpp
    xiocompletion.cpp
    xmodule.cpp
    xmutant.cpp
    xnotifylistener.cpp
    xobject.cpp
    xsemaphore.cpp
    xsocket.cpp
    xsymboliclink.cpp
    xthread.cpp
    xtimer.cpp

    # Utilities
    util/native_list.cpp
    util/object_table.cpp
    util/xdbf_utils.cpp

    # XAM subsystem - provides __imp__Xam* symbols
    xam/xam_module.cpp
    xam/xam_avatar.cpp
    xam/xam_content.cpp
    xam/xam_content_aggregate.cpp
    xam/xam_content_device.cpp
    xam/xam_debug.cpp
    xam/xam_enum.cpp
    xam/xam_info.cpp
    # xam/xam_input.cpp           # Disabled: depends on rexinput
    xam/xam_locale.cpp
    xam/xam_msg.cpp
    xam/xam_net.cpp
    xam/xam_notify.cpp
    xam/xam_nui.cpp
    xam/xam_party.cpp
    xam/xam_task.cpp
    xam/xam_ui.cpp
    xam/xam_user.cpp
    xam/xam_video.cpp
    xam/xam_voice.cpp
    xam/app_manager.cpp
    xam/content_manager.cpp
    xam/user_profile.cpp
    xam/apps/xam_app.cpp
    xam/apps/xgi_app.cpp
    xam/apps/xlivebase_app.cpp
    xam/apps/xmp_app.cpp

    # xboxkrnl exports - provides __imp__ symbols for generated code
    # xboxkrnl/cert_monitor.cpp    # TODO: Translate JIT methods for AOT
    # xboxkrnl/debug_monitor.cpp   # TODO: Translate JIT methods for AOT
     xboxkrnl/xboxkrnl_crypt.cpp
     xboxkrnl/xboxkrnl_debug.cpp
     xboxkrnl/xboxkrnl_error.cpp
     xboxkrnl/xboxkrnl_hal.cpp
     xboxkrnl/xboxkrnl_hid.cpp
     xboxkrnl/xboxkrnl_io.cpp
     xboxkrnl/xboxkrnl_io_info.cpp
     xboxkrnl/xboxkrnl_memory.cpp
     xboxkrnl/xboxkrnl_misc.cpp
     xboxkrnl/xboxkrnl_module.cpp
     xboxkrnl/xboxkrnl_modules.cpp
     xboxkrnl/xboxkrnl_ob.cpp
     xboxkrnl/xboxkrnl_rtl.cpp
     xboxkrnl/xboxkrnl_strings.cpp
     xboxkrnl/xboxkrnl_threading.cpp
    # xboxkrnl/xboxkrnl_usbcam.cpp  # TODO: lol eventually.
    xboxkrnl/xboxkrnl_video.cpp
    xboxkrnl/xboxkrnl_xconfig.cpp
    # xboxkrnl/xboxkrnl_audio.cpp       # Disabled: depends on rexaudio
    # xboxkrnl/xboxkrnl_audio_xma.cpp   # Disabled: depends on rexaudio
)

add_library(rexkernel STATIC ${REXKERNEL_SOURCES})
add_library(rex::kernel ALIAS rexkernel)

target_include_directories(rexkernel PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(rexkernel PRIVATE
    ${CMAKE_SOURCE_DIR}/thirdparty
)

target_link_libraries(rexkernel
    PUBLIC
        rexcore
        rexfilesystem
        rexruntime
        simde
    PRIVATE
        # rexgraphics rexinput rexaudio - disabled for headless codegen build
        aes128
)

# Headless mode: no graphics/input/audio subsystems
target_compile_definitions(rexkernel PUBLIC REX_HEADLESS=1
)

if(WIN32)
    target_link_libraries(rexkernel PRIVATE ws2_32)
endif()

