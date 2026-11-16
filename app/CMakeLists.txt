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
add_subdirectory(${RCHEEVOS} ./rcheevos)
add_subdirectory(${ANDROID-CPP}/enet ./enet)
target_include_directories(enet INTERFACE ${ANDROID-CPP}/enet/include)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${CORE-LIB}/cmake")
add_subdirectory(${CORE-LIB}/src/net ./net-utils)

# Lua library only (no lua.c/luac.c); lstrlib.c comes from lua/patched/, see lua/LICENSE
file(GLOB LUA_SOURCES ${ANDROID-CPP}/lua/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX "/lstrlib\.c$")
add_library(lua STATIC ${LUA_SOURCES} ${ANDROID-CPP}/lua/patched/lstrlib.c)
target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
target_include_directories(lua PUBLIC ${ANDROID-CPP}/lua)

add_library(
        melonDS-android-frontend

        SHARED

        ${ANDROID-CPP}/AndroidMelonEventMessenger.cpp
        ${ANDROID-CPP}/CartLoader.cpp
        ${ANDROID-CPP}/DSiSupport.cpp
        ${ANDROID-CPP}/EmulatorArgsBuilder.cpp
        ${ANDROID-CPP}/EmulatorMessageQueueJNI.cpp
        ${ANDROID-CPP}/JniEnvHandler.cpp
        ${ANDROID-CPP}/MelonDS.cpp
        ${ANDROID-CPP}/MelonDSAndroidCameraHandler.cpp
        ${ANDROID-CPP}/MelonDSAndroidConfiguration.cpp
        ${ANDROID-CPP}/MelonDSAndroidInterface.cpp
        ${ANDROID-CPP}/MelonDSAndroidJNI.cpp
        ${ANDROID-CPP}/MelonDSAudio.cpp
        ${ANDROID-CPP}/MelonDSNandJNI.cpp
        ${ANDROID-CPP}/MelonInstance.cpp
        ${ANDROID-CPP}/MicInputOboeCallback.cpp
        ${ANDROID-CPP}/NativeGlContext.cpp
        ${ANDROID-CPP}/OboeCallback.cpp
        ${ANDROID-CPP}/OpenGLContext.cpp
        ${ANDROID-CPP}/PlatformAndroid.cpp
        ${ANDROID-CPP}/PlatformAndroid_AAC.cpp
        ${ANDROID-CPP}/RetroAchievementsMapper.cpp
        ${ANDROID-CPP}/RewindManager.cpp
        ${ANDROID-CPP}/RomIconBuilder.cpp
        ${ANDROID-CPP}/SDCardArgsBuilder.cpp
        ${ANDROID-CPP}/SaveManager.cpp
        ${ANDROID-CPP}/UriFileHandler.cpp
        ${ANDROID-CPP}/debug/AudioDiagnostics.cpp
        ${ANDROID-CPP}/debug/DebugLog.cpp
        ${ANDROID-CPP}/debug/DebugProperties.cpp
        ${ANDROID-CPP}/debug/LogQueue.cpp
        ${ANDROID-CPP}/performancehint/JniPerformanceHintManager.cpp
        ${ANDROID-CPP}/performancehint/NdkPerformanceHintManager.cpp
        ${ANDROID-CPP}/performancehint/PerformanceHintManagerFactory.cpp
        ${ANDROID-CPP}/performancehint/ThreadSafePerformanceHintSession.cpp
        ${ANDROID-CPP}/renderer/FrameQueue.cpp
        ${ANDROID-CPP}/renderer/FramePacing.cpp
        ${ANDROID-CPP}/renderer/ScreenshotRenderer.cpp
        ${ANDROID-CPP}/retroachievements/RetroAchievementsManager.cpp
        ${ANDROID-CPP}/scripting/LuaDraw.cpp
        ${ANDROID-CPP}/scripting/LuaDrawApi.cpp
        ${ANDROID-CPP}/scripting/LuaEngine.cpp
        ${ANDROID-CPP}/scripting/LuaFolderApi.cpp
        ${ANDROID-CPP}/scripting/LuaMemoryApi.cpp
        ${ANDROID-CPP}/scripting/LuaSandbox.cpp
        ${ANDROID-CPP}/scripting/LuaScriptingJNI.cpp
        ${ANDROID-CPP}/scripting/LuaStateApi.cpp
        ${ANDROID-CPP}/scripting/LuaStorage.cpp
        ${ANDROID-CPP}/scripting/LuaStorageApi.cpp
        ${ANDROID-CPP}/scripting/LuaWideApi.cpp
)

target_include_directories(melonDS-android-frontend PUBLIC
        ${ANDROID-CPP}/
        ${CORE-LIB}/src/frontend
        ${OBOE}/include
        ${FAAD2}/include
        ${RCHEEVOS}/include
)
target_link_libraries(melonDS-android-frontend android core net-utils oboe faad enet rcheevos lua EGL GLESv3)

# -Bsymbolic binds the library's own calls at link time so the core's hot paths skip the PLT; JNI exports are unaffected
target_link_options(melonDS-android-frontend PRIVATE -Wl,-Bsymbolic)