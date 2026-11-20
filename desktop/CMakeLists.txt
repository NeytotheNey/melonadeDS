# melonadeDS for PC: an SDL2 frontend around the same core and OpenGL/Compute renderers as the
# Android app, and the app's own native code (MelonInstance and the rest of melonade-shared,
# app/src/main/cpp/SharedNative.cmake). Built with MSYS2 MinGW-w64 (see desktop/README.md):
#
#   cmake -G Ninja -S desktop -B build-desktop -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-desktop

cmake_minimum_required(VERSION 3.16)

project(melonade-desktop LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

set(CORE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../melonDS-android-lib")
set(GL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gl")

# The core includes MELONDS_GL_HEADER for its GL declarations: the GLES 3.2 headers with the
# functions loaded at run time (gl/pc_gl.h). Set before the core's subdirectory so it gets them too
include_directories("${GL_DIR}" "${GL_DIR}/khronos")
add_compile_definitions(MELONDS_GL_HEADER=<pc_gl.h>)

# what app/CMakeLists.txt sets for the core, plus the PC-only bits
set(BUILD_QT_SDL OFF CACHE BOOL "" FORCE)
set(ENABLE_GDBSTUB OFF CACHE BOOL "" FORCE)
set(ENABLE_OGLRENDERER ON CACHE BOOL "" FORCE)
# LTO through MinGW's ld is slow and gains little for a test build
set(ENABLE_LTO_RELEASE OFF CACHE BOOL "" FORCE)
add_subdirectory("${CORE_LIB}" "${CMAKE_BINARY_DIR}/melonDS-android-lib")

# the core's net code (MelonInstance's Net), with the app's enet, as app/CMakeLists.txt adds them
set(APP_CPP "${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp")
add_subdirectory("${APP_CPP}/enet" "${CMAKE_BINARY_DIR}/enet")
target_include_directories(enet INTERFACE "${APP_CPP}/enet/include")
list(APPEND CMAKE_MODULE_PATH "${CORE_LIB}/cmake")
add_subdirectory("${CORE_LIB}/src/net" "${CMAKE_BINARY_DIR}/net-utils")

# the app's shared native code: its <GLES3/gl3.h> is pc_gl.h here (gl/shim)
include("${APP_CPP}/SharedNative.cmake")
target_include_directories(melonade-shared PRIVATE "${GL_DIR}/shim")

find_package(SDL2 REQUIRED)

add_executable(melonade-desktop
    src/main.cpp
    src/DesktopEventMessenger.cpp
    src/DesktopEventMessenger.h
    src/DesktopLog.cpp
    src/DesktopPlatform.cpp
    src/EmulatorThread.cpp
    src/EmulatorThread.h
    src/PlatformBase.cpp
    src/Frontend.h
    src/Presenter.cpp
    src/Presenter.h
    src/RunOptions.h
    gl/pc_gl_loader.cpp
    gl/pc_gl.h)

target_include_directories(melonade-desktop PRIVATE "${CORE_LIB}/src" "${GL_DIR}/shim")
# Windows 10 APIs (SetProcessInformation for power throttling)
target_compile_definitions(melonade-desktop PRIVATE _WIN32_WINNT=0x0A00)
target_link_libraries(melonade-desktop PRIVATE melonade-shared core SDL2::SDL2main SDL2::SDL2 winmm)

if (MINGW)
    # a console stays open for the log; static so the exe only needs SDL2.dll next to it
    target_link_options(melonade-desktop PRIVATE -static-libgcc -static-libstdc++ -mconsole)
    # SDL2.dll next to the exe so it runs outside the MSYS2 shell
    add_custom_command(TARGET melonade-desktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:melonade-desktop>)
endif()
