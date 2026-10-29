cmake_minimum_required(VERSION 3.4.1)

project(melonDS-android-frontend)

add_compile_definitions(MELONDS_GL_HEADER=<GLES3/gl32.h>)
add_compile_definitions(EGL_EGLEXT_PROTOTYPES)

# Set by the debug build type in build.gradle.kts
if(MELONADE_DEBUG_TOOLS)
    add_compile_definitions(MELONADE_DEBUG_TOOLS)
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

# Lua for per-game scripts. Only the library is built (no lua.c/luac.c), and the sources are kept unchanged, except for
# lstrlib.c: the copy in lua/patched/ is built instead (see lua/LICENSE)
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
        ${ANDROID-CPP}/debug/DebugLog.cpp
        ${ANDROID-CPP}/performancehint/JniPerformanceHintManager.cpp
        ${ANDROID-CPP}/performancehint/NdkPerformanceHintManager.cpp
        ${ANDROID-CPP}/performancehint/PerformanceHintManagerFactory.cpp
        ${ANDROID-CPP}/performancehint/ThreadSafePerformanceHintSession.cpp
        ${ANDROID-CPP}/renderer/FrameQueue.cpp
        ${ANDROID-CPP}/renderer/ScreenshotRenderer.cpp
        ${ANDROID-CPP}/retroachievements/RetroAchievementsManager.cpp
        ${ANDROID-CPP}/scripting/LuaDraw.cpp
        ${ANDROID-CPP}/scripting/LuaEngine.cpp
        ${ANDROID-CPP}/scripting/LuaScriptingJNI.cpp
        ${ANDROID-CPP}/scripting/LuaStorage.cpp
)

target_include_directories(melonDS-android-frontend PUBLIC
        ${ANDROID-CPP}/
        ${CORE-LIB}/src/frontend
        ${OBOE}/include
        ${FAAD2}/include
        ${RCHEEVOS}/include
)
target_link_libraries(melonDS-android-frontend android core net-utils oboe faad enet rcheevos lua EGL GLESv3)

# Bind the library's own symbols at link time, so calls between its functions (the core's hot paths included)
# don't go through PLT stubs. Symbols stay exported, so the JNI entry points are unaffected
target_link_options(melonDS-android-frontend PRIVATE -Wl,-Bsymbolic)