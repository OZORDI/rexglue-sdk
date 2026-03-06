# rexinput - Input system library
# Input system and drivers for Xbox 360 controller emulation

add_library(rexinput STATIC
    input_system.cpp
    nop/nop_input_driver.cpp
)
add_library(rex::input ALIAS rexinput)

target_include_directories(rexinput PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_sources(rexinput PRIVATE sdl/sdl_input_driver.cpp)
target_link_libraries(rexinput PUBLIC rexcore SDL2::SDL2)
