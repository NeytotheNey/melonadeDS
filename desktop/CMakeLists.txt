# melonadeDS for PC: an SDL2 frontend around the same core and OpenGL/Compute renderers as the
# Android app, and the app's own native code (MelonInstance and the rest of melonade-shared,
# app/src/main/cpp/SharedNative.cmake). Built with MSYS2 MinGW-w64 (see desktop/README.md):
#
#   cmake -G Ninja -S desktop -B build-desktop -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-desktop

cmake_minimum_required(VERSION 3.16)

project(melonade-desktop LANGUAGES C CXX RC)

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
# (C and C++ only: windres runs its preprocessor through the shell, which takes the <> for redirections)
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-DMELONDS_GL_HEADER=<pc_gl.h$<ANGLE-R>>")

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

# Dear ImGui (third_party/imgui, a tagged release) with its SDL2 and OpenGL3 backends, the latter on its GLES 3 path: the context is GLES 3.2,
# whose functions it calls through pc_gl.h (gl/shim/GLES3/gl3.h), the same pointers as the core
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC "${IMGUI_DIR}" PRIVATE "${GL_DIR}/shim")
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS PRIVATE IMGUI_IMPL_OPENGL_ES3)
target_link_libraries(imgui PRIVATE SDL2::SDL2)

# the game library's vendored C libraries (third_party/README.md): SQLite for library.db, miniz for zip files, the LZMA SDK's 7z decoder
# (stb_image, for the downloaded art, is a header that src/ArtImage.cpp builds)
set(THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
add_library(sqlite3 STATIC "${THIRD_PARTY}/sqlite/sqlite3.c")
# no extensions loaded from DLLs; double-quoted strings are identifiers only (SQLite's advice for new code)
target_compile_definitions(sqlite3 PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0 SQLITE_THREADSAFE=1 SQLITE_DEFAULT_MEMSTATUS=0)
add_library(miniz STATIC "${THIRD_PARTY}/miniz/miniz.c")
add_library(lzma7z STATIC
    "${THIRD_PARTY}/lzma/7zAlloc.c" "${THIRD_PARTY}/lzma/7zArcIn.c" "${THIRD_PARTY}/lzma/7zBuf.c" "${THIRD_PARTY}/lzma/7zBuf2.c"
    "${THIRD_PARTY}/lzma/7zCrc.c" "${THIRD_PARTY}/lzma/7zCrcOpt.c" "${THIRD_PARTY}/lzma/7zDec.c" "${THIRD_PARTY}/lzma/7zFile.c"
    "${THIRD_PARTY}/lzma/7zStream.c" "${THIRD_PARTY}/lzma/Bcj2.c" "${THIRD_PARTY}/lzma/Bra.c" "${THIRD_PARTY}/lzma/Bra86.c"
    "${THIRD_PARTY}/lzma/BraIA64.c" "${THIRD_PARTY}/lzma/CpuArch.c" "${THIRD_PARTY}/lzma/Delta.c" "${THIRD_PARTY}/lzma/Lzma2Dec.c"
    "${THIRD_PARTY}/lzma/LzmaDec.c" "${THIRD_PARTY}/lzma/Ppmd7.c" "${THIRD_PARTY}/lzma/Ppmd7Dec.c")
# as the SDK's own 7z decoder sample builds it (C/Util/7z/makefile.gcc)
target_compile_definitions(lzma7z PRIVATE Z7_PPMD_SUPPORT Z7_EXTRACT_ONLY)

# rcheevos' ROM hashes (rc_hash, the Nintendo DS method the library uses), which the app's rcheevos target leaves out: hash.c and
# hash_rom.c from the same vendored copy, without the disc, zip and encrypted hashers they'd need the rest of rhash for
set(RCHEEVOS_DIR "${APP_CPP}/rcheevos")
add_library(rhash-rom STATIC "${RCHEEVOS_DIR}/src/rhash/hash.c" "${RCHEEVOS_DIR}/src/rhash/hash_rom.c")
target_include_directories(rhash-rom PRIVATE "${RCHEEVOS_DIR}/include" "${RCHEEVOS_DIR}/src")
target_compile_definitions(rhash-rom PRIVATE RC_DISABLE_LUA=1 RC_HASH_NO_DISC RC_HASH_NO_ZIP RC_HASH_NO_ENCRYPTED)
# md5, rc_path_* and the rest it calls are in rcheevos
target_link_libraries(rhash-rom PRIVATE rcheevos)

add_executable(melonade-desktop
    src/main.cpp
    src/ArtImage.cpp
    src/ArtImage.h
    src/Audio.cpp
    src/Audio.h
    src/Controls.cpp
    src/Controls.h
    src/DesktopEventMessenger.cpp
    src/DesktopEventMessenger.h
    src/DesktopLog.cpp
    src/DesktopPlatform.cpp
    src/EmulatorThread.cpp
    src/EmulatorThread.h
    src/FileDialog.cpp
    src/FileDialog.h
    src/Http.cpp
    src/Http.h
    src/ImageFile.cpp
    src/ImageFile.h
    src/Input.cpp
    src/Input.h
    src/Library.cpp
    src/Library.h
    src/LibraryDb.cpp
    src/LibraryDb.h
    src/Metadata.h
    src/MetadataSync.cpp
    src/MetadataSync.h
    src/PlatformBase.cpp
    src/Frontend.h
    src/Presenter.cpp
    src/Presenter.h
    src/render/PresentFrame.h
    src/render/Shader.cpp
    src/render/Shader.h
    src/render/ShaderLibrary.cpp
    src/render/ShaderLibrary.h
    src/render/VideoFilterPipeline.cpp
    src/render/VideoFilterPipeline.h
    src/render/ScreenRenderer.cpp
    src/render/ScreenRenderer.h
    "${CMAKE_BINARY_DIR}/generated/shader_files.inc"
    src/Resources.h
    src/resources.rc
    src/RomFile.cpp
    src/RomFile.h
    src/RomConfig.cpp
    src/RomConfig.h
    src/RunOptions.h
    src/SaveSlots.cpp
    src/SaveSlots.h
    src/ScreenWindows.cpp
    src/ScreenWindows.h
    src/Secret.cpp
    src/Secret.h
    src/Settings.cpp
    src/Settings.h
    src/UiScript.cpp
    src/UiScript.h
    src/ui/GameDetailsScreen.cpp
    src/ui/GameSettingsScreen.cpp
    src/ui/Host.h
    src/ui/Icons.h
    src/ui/InputScreen.cpp
    src/ui/KeyboardTestScreen.cpp
    src/ui/LibraryCards.cpp
    src/ui/LibraryCards.h
    src/ui/LibraryScreen.cpp
    src/ui/MosaicLayout.cpp
    src/ui/MosaicLayout.h
    src/ui/PauseScreen.cpp
    src/ui/Screen.h
    src/ui/Screens.h
    src/ui/SettingsScreen.cpp
    src/ui/StatesScreen.cpp
    src/ui/Theme.cpp
    src/ui/Theme.h
    src/ui/Ui.cpp
    src/ui/Ui.h
    src/ui/Widgets.cpp
    src/ui/Widgets.h
    gl/pc_gl_loader.cpp
    gl/pc_gl.h)

# the app's version (versionName and versionCode in buildSrc/src/main/kotlin/AppConfig.kt, the Gradle config), for the exe's version info
# and --help: generated/version.h
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../buildSrc/src/main/kotlin/AppConfig.kt" APP_CONFIG)
string(REGEX MATCH "versionName = \"([0-9.]+)\"" _match "${APP_CONFIG}")
set(MELONADE_VERSION_NAME "${CMAKE_MATCH_1}")
string(REGEX MATCH "versionCode = ([0-9]+)" _match "${APP_CONFIG}")
set(MELONADE_VERSION_CODE "${CMAKE_MATCH_1}")
if (NOT MELONADE_VERSION_NAME OR NOT MELONADE_VERSION_CODE)
    message(FATAL_ERROR "can't read versionName and versionCode from AppConfig.kt")
endif()
string(REPLACE "." "," MELONADE_VERSION_COMMAS "${MELONADE_VERSION_NAME}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../buildSrc/src/main/kotlin/AppConfig.kt")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/version.h" CONTENT
"// generated by CMakeLists.txt from AppConfig.kt
#pragma once
#define MELONADE_VERSION_NAME \"${MELONADE_VERSION_NAME}\"
#define MELONADE_VERSION_COMMAS ${MELONADE_VERSION_COMMAS},${MELONADE_VERSION_CODE}
")
message(STATUS "melonadeDS ${MELONADE_VERSION_NAME} (${MELONADE_VERSION_CODE})")

# the app's shaders as desktop/shaders has them (exported from ShaderProgramSource.kt by DesktopShaderExportTest, README.md), embedded so the
# exe needs no files next to it: generated/shader_files.inc, made again when a file there changes
file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shaders.json")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/generated/shader_files.inc"
    COMMAND ${CMAKE_COMMAND} "-DSHADER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/shaders" "-DOUTPUT=${CMAKE_BINARY_DIR}/generated/shader_files.inc"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/EmbedShaders.cmake"
    DEPENDS ${SHADER_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/EmbedShaders.cmake"
    COMMENT "Embedding desktop/shaders")

# the resource script embeds these, so it's compiled again when one changes
set_source_files_properties(src/resources.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app.ico;${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest;${CMAKE_BINARY_DIR}/generated/version.h;${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/roboto/Roboto-Regular.ttf;${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/roboto/Roboto-Medium.ttf;${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/material-symbols/MaterialSymbolsOutlined-subset.ttf")

# third_party/: vendored libraries (third_party/README.md)
target_include_directories(melonade-desktop PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CORE_LIB}/src" "${GL_DIR}/shim"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party" "${CMAKE_BINARY_DIR}/generated")
# Windows 10 APIs (SetProcessInformation for power throttling)
target_compile_definitions(melonade-desktop PRIVATE _WIN32_WINNT=0x0A00)
# our own code builds with these warnings, and changes add none (docs/coding-standards.md, rule 7)
target_compile_options(melonade-desktop PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
# ole32/uuid: COM for the file dialog (shell32) and the slot screenshots (windowscodecs, WIC); winhttp for the metadata sync's requests,
# bcrypt for the ROMs' MD5 and SHA1, crypt32 for the Web API key (DPAPI); all system DLLs
# rhash-rom before melonade-shared, whose rcheevos it calls into
target_link_libraries(melonade-desktop PRIVATE rhash-rom melonade-shared core imgui sqlite3 miniz lzma7z SDL2::SDL2main SDL2::SDL2 winmm ole32 uuid shell32 windowscodecs winhttp bcrypt crypt32)

if (MINGW)
    # a GUI program (no console window of its own; a run from a console writes to it, see AttachParentConsole in main.cpp); static so the
    # exe only needs SDL2.dll next to it
    target_link_options(melonade-desktop PRIVATE -static-libgcc -static-libstdc++ -mwindows)
    # std::thread pulls in winpthread; link it statically too, or the exe won't start without MSYS2 on PATH
    target_link_libraries(melonade-desktop PRIVATE -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
    # SDL2.dll next to the exe so it runs outside the MSYS2 shell, and the controller mappings (third_party/gamecontrollerdb)
    add_custom_command(TARGET melonade-desktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:melonade-desktop>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gamecontrollerdb/gamecontrollerdb.txt"
            $<TARGET_FILE_DIR:melonade-desktop>)
endif()
