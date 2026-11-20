cmake_minimum_required(VERSION 3.4.1)

project(melonDS-android-frontend)

add_compile_definitions(MELONDS_GL_HEADER=<GLES3/gl32.h>)
add_compile_definitions(EGL_EGLEXT_PROTOTYPES)

# Set by the debug build type in build.gradle.kts
if(MELONADE_DEBUG_TOOLS)
    add_compile_definitions(MELONADE_DEBUG_TOOLS)
endif()

# Opt-in flags from build.gradle.kts (docs/plan-optimization.md), set before the subdirectories so the core and the libraries get them too
option(MELONADE_O3 "Compile native code at -O3" OFF)
option(MELONADE_LTO "Link native code with ThinLTO" OFF)
option(MELONADE_ARMV82 "Target armv8.2-a on arm64" OFF)
if(MELONADE_O3)
    # Compile options come after the build type's flags, so this replaces its -O2
    add_compile_options(-O3)
endif()
if(MELONADE_LTO)
    add_compile_options(-flto=thin)
    add_link_options(-flto=thin)
endif()
if(MELONADE_ARMV82 AND ANDROID_ABI STREQUAL "arm64-v8a")
    add_compile_options(-march=armv8.2-a)
endif()

# Profile-guided optimization, arm64-v8a only (tools/pgo/README.md): generate instruments every native target and links the profile runtime, use
# builds with the merged profile. Other ABIs build as if it were off
set(MELONADE_PGO "off" CACHE STRING "Profile-guided optimization: off, generate or use")
set(MELONADE_PGO_PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/pgo/arm64-v8a.profdata" CACHE FILEPATH "Merged profile for MELONADE_PGO=use")
if(NOT MELONADE_PGO MATCHES "^(off|generate|use)$")
    message(FATAL_ERROR "MELONADE_PGO must be off, generate or use, not '${MELONADE_PGO}'")
endif()
if(NOT MELONADE_PGO STREQUAL "off" AND NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(STATUS "MELONADE_PGO=${MELONADE_PGO} ignored on ${ANDROID_ABI} (arm64-v8a only)")
elseif(MELONADE_PGO STREQUAL "generate")
    # The frontend dumps the counters itself on pause and stop (perf/PgoDump.cpp), since Android kills the process before atexit runs
    add_compile_options(-fprofile-generate)
    # The profile names static functions <source file>;<name>, and the source file is an absolute path, so a profile merged here would miss them in
    # another checkout (CI). Name them by the file name alone; use builds must do the same
    add_compile_options("SHELL:-mllvm -static-func-full-module-prefix=false")
    add_link_options(-fprofile-generate)
    add_compile_definitions(MELONADE_PGO_GENERATE)
elseif(MELONADE_PGO STREQUAL "use")
    file(TO_CMAKE_PATH "${MELONADE_PGO_PROFILE}" MELONADE_PGO_PROFILE_PATH)
    if(NOT EXISTS "${MELONADE_PGO_PROFILE_PATH}")
        message(FATAL_ERROR "MELONADE_PGO=use, but the profile ${MELONADE_PGO_PROFILE_PATH} doesn't exist. Collect and merge one first (tools/pgo/README.md), "
                "or pass -PnativePgoProfile=<path>")
    endif()
    # The profile is applied at compile time (before ThinLTO, which keeps the branch weights in the bitcode), so the link doesn't need it. Clang doesn't
    # list the profile as a dependency, so a new profile re-runs this configure and its hash in the flags makes ninja recompile everything
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MELONADE_PGO_PROFILE_PATH}")
    file(MD5 "${MELONADE_PGO_PROFILE_PATH}" MELONADE_PGO_PROFILE_MD5)
    add_compile_options("-fprofile-use=${MELONADE_PGO_PROFILE_PATH}" -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date
            -Wno-profile-instr-missing -Wno-backend-plugin)
    # As in generate, so static functions match in any checkout
    add_compile_options("SHELL:-mllvm -static-func-full-module-prefix=false")
    add_compile_definitions(MELONADE_PGO_PROFILE_MD5=${MELONADE_PGO_PROFILE_MD5})
    message(STATUS "MELONADE_PGO=use with ${MELONADE_PGO_PROFILE_PATH} (md5 ${MELONADE_PGO_PROFILE_MD5})")
endif()

set(BUILD_QT_SDL OFF)
set(ENABLE_GDBSTUB OFF)

set(ANDROID-CPP src/main/cpp)
set(CORE-LIB ../melonDS-android-lib)
set(OBOE ${ANDROID-CPP}/oboe)
set(FAAD2 ${ANDROID-CPP}/faad2)
set(RCHEEVOS ${ANDROID-CPP}/rcheevos)

add_subdirectory(${CORE-LIB} ./melonDS-android-lib)
add_subdirectory(${OBOE} ./oboe)
add_subdirectory(${FAAD2} ./faad2)
add_subdirectory(${ANDROID-CPP}/enet ./enet)
target_include_directories(enet INTERFACE ${ANDROID-CPP}/enet/include)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${CORE-LIB}/cmake")
add_subdirectory(${CORE-LIB}/src/net ./net-utils)

# The portable native code the desktop shares (melonade-shared), with rcheevos and Lua
include(${ANDROID-CPP}/SharedNative.cmake)

add_library(
        melonDS-android-frontend

        SHARED

        ${ANDROID-CPP}/AndroidMelonEventMessenger.cpp
        ${ANDROID-CPP}/EmulatorMessageQueueJNI.cpp
        ${ANDROID-CPP}/JniEnvHandler.cpp
        ${ANDROID-CPP}/MelonDS.cpp
        ${ANDROID-CPP}/MelonDSAndroidCameraHandler.cpp
        ${ANDROID-CPP}/MelonDSAndroidConfiguration.cpp
        ${ANDROID-CPP}/MelonDSAndroidInterface.cpp
        ${ANDROID-CPP}/MelonDSAndroidJNI.cpp
        ${ANDROID-CPP}/MelonDSAudio.cpp
        ${ANDROID-CPP}/MelonDSNandJNI.cpp
        ${ANDROID-CPP}/MicInputOboeCallback.cpp
        ${ANDROID-CPP}/NativeGlContext.cpp
        ${ANDROID-CPP}/OboeCallback.cpp
        ${ANDROID-CPP}/OpenGLContext.cpp
        ${ANDROID-CPP}/PlatformAndroid.cpp
        ${ANDROID-CPP}/PlatformAndroid_AAC.cpp
        ${ANDROID-CPP}/RetroAchievementsMapper.cpp
        ${ANDROID-CPP}/UriFileHandler.cpp
        ${ANDROID-CPP}/debug/AudioDiagnostics.cpp
        ${ANDROID-CPP}/debug/DebugLog.cpp
        ${ANDROID-CPP}/debug/DebugProperties.cpp
        ${ANDROID-CPP}/debug/LogQueue.cpp
        ${ANDROID-CPP}/perf/PgoDump.cpp
        ${ANDROID-CPP}/performancehint/JniPerformanceHintManager.cpp
        ${ANDROID-CPP}/performancehint/NdkPerformanceHintManager.cpp
        ${ANDROID-CPP}/performancehint/PerformanceHintManagerFactory.cpp
        ${ANDROID-CPP}/performancehint/ThreadSafePerformanceHintSession.cpp
        ${ANDROID-CPP}/scripting/LuaScriptingJNI.cpp
)

target_include_directories(melonDS-android-frontend PUBLIC
        ${ANDROID-CPP}/
        ${CORE-LIB}/src/frontend
        ${OBOE}/include
        ${FAAD2}/include
        ${RCHEEVOS}/include
)
target_link_libraries(melonDS-android-frontend android melonade-shared core net-utils oboe faad enet rcheevos lua EGL GLESv3)

# -Bsymbolic binds the library's own calls at link time so the core's hot paths skip the PLT; JNI exports are unaffected
target_link_options(melonDS-android-frontend PRIVATE -Wl,-Bsymbolic)