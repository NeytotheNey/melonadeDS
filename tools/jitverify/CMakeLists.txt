# jitverify: a headless per-frame state logger for JIT verification (see README.md). Software
# renderer only, no SDL or GL. Builds for Windows x64 (MSYS2 MinGW, x64 JIT) and Android
# arm64-v8a (NDK, A64 JIT); tools/jitverify/build.sh does both.

cmake_minimum_required(VERSION 3.16)

project(jitverify LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(CORE_LIB "${REPO_ROOT}/melonDS-android-lib")
set(DESKTOP_SRC "${REPO_ROOT}/desktop/src")

# the core as the app builds it, minus OpenGL; the JIT comes from the core's architecture check
set(BUILD_QT_SDL OFF CACHE BOOL "" FORCE)
set(ENABLE_GDBSTUB OFF CACHE BOOL "" FORCE)
set(ENABLE_OGLRENDERER OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO_RELEASE OFF CACHE BOOL "" FORCE)
add_subdirectory("${CORE_LIB}" "${CMAKE_BINARY_DIR}/melonDS-android-lib")

add_executable(jitverify
    main.cpp
    "${DESKTOP_SRC}/Platform.cpp"
    "${DESKTOP_SRC}/PlatformBase.cpp"
    "${DESKTOP_SRC}/Frontend.h"
    "${DESKTOP_SRC}/RunOptions.h")

target_include_directories(jitverify PRIVATE "${CORE_LIB}/src" "${DESKTOP_SRC}")
target_link_libraries(jitverify PRIVATE core)

if (WIN32)
    # SetProcessInformation (power throttling)
    target_compile_definitions(jitverify PRIVATE _WIN32_WINNT=0x0A00)
endif()
if (MINGW)
    target_link_options(jitverify PRIVATE -static)
endif()
if (ANDROID)
    # the STL comes from ANDROID_STL=c++_static (build.sh); dlopen for libandroid's ASharedMemory
    target_link_libraries(jitverify PRIVATE dl)
endif()
