# The app's portable native code as the static library melonade-shared, for the Android app (app/CMakeLists.txt) and the desktop
# (desktop/CMakeLists.txt): MelonInstance and what it runs (saves, rewind, RetroAchievements, the Lua engine, the game state reads, the frame
# queue and screenshots). Included, not added as a subdirectory, so it gets the includer's compile flags; the core (the core and net-utils
# targets) must be added first.
#
# Each frontend brings the rest: the core's Platform layer (Platform.h, plus OpenInternalFile and GetThreadTid from PlatformAndroid.h), the
# MelonEventMessenger it fills FrontendContext with, and debug/: DebugLog and LogQueue (logcat, Oboe and system properties on Android; a
# small stand-in on the desktop), with MelonLog.h's log function on the desktop.

set(MELONADE_SHARED_DIR "${CMAKE_CURRENT_LIST_DIR}")

if(NOT TARGET rcheevos)
    add_subdirectory("${MELONADE_SHARED_DIR}/rcheevos" "${CMAKE_CURRENT_BINARY_DIR}/rcheevos")
endif()

# Lua library only (no lua.c/luac.c); lstrlib.c comes from lua/patched/, see lua/LICENSE
file(GLOB LUA_SOURCES "${MELONADE_SHARED_DIR}/lua/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "/lstrlib\.c$")
add_library(lua STATIC ${LUA_SOURCES} "${MELONADE_SHARED_DIR}/lua/patched/lstrlib.c")
if(NOT WIN32)
    target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
endif()
target_include_directories(lua PUBLIC "${MELONADE_SHARED_DIR}/lua")

add_library(
        melonade-shared

        STATIC

        "${MELONADE_SHARED_DIR}/CartLoader.cpp"
        "${MELONADE_SHARED_DIR}/DSiSupport.cpp"
        "${MELONADE_SHARED_DIR}/EmulatorArgsBuilder.cpp"
        "${MELONADE_SHARED_DIR}/FrontendContext.cpp"
        "${MELONADE_SHARED_DIR}/MelonInstance.cpp"
        "${MELONADE_SHARED_DIR}/RewindManager.cpp"
        "${MELONADE_SHARED_DIR}/RomIconBuilder.cpp"
        "${MELONADE_SHARED_DIR}/SDCardArgsBuilder.cpp"
        "${MELONADE_SHARED_DIR}/SaveManager.cpp"
        "${MELONADE_SHARED_DIR}/gamestate/GameState.cpp"
        "${MELONADE_SHARED_DIR}/gamestate/pk_nds_field.cpp"
        "${MELONADE_SHARED_DIR}/gamestate/pk_nds_field_gpu.cpp"
        "${MELONADE_SHARED_DIR}/renderer/FrameQueue.cpp"
        "${MELONADE_SHARED_DIR}/renderer/FramePacing.cpp"
        "${MELONADE_SHARED_DIR}/renderer/ScreenshotRenderer.cpp"
        "${MELONADE_SHARED_DIR}/renderer/SunGuard.cpp"
        "${MELONADE_SHARED_DIR}/retroachievements/RetroAchievementsManager.cpp"
        "${MELONADE_SHARED_DIR}/scripting/LuaDraw.cpp"
        "${MELONADE_SHARED_DIR}/scripting/LuaDrawApi.cpp"
        "${MELONADE_SHARED_DIR}/scripting/LuaEngine.cpp"
        "${MELONADE_SHARED_DIR}/scripting/LuaFolderApi.cpp"
        "${MELONADE_SHARED_DIR}/scripting/LuaGameApi.cpp"
        "${MELONADE_SHARED_DIR}/scripting/LuaMemoryApi.cpp"
        "${MELONADE_SHARED_DIR}/scripting/LuaSandbox.cpp"
        "${MELONADE_SHARED_DIR}/scripting/LuaStateApi.cpp"
        "${MELONADE_SHARED_DIR}/scripting/LuaStorage.cpp"
        "${MELONADE_SHARED_DIR}/scripting/LuaStorageApi.cpp"
        "${MELONADE_SHARED_DIR}/scripting/LuaWideApi.cpp"
)

target_include_directories(melonade-shared PUBLIC "${MELONADE_SHARED_DIR}" "${MELONADE_SHARED_DIR}/rcheevos/include")
target_link_libraries(melonade-shared PUBLIC core net-utils rcheevos lua)

# Our own code builds with these warnings, and changes add none (docs/coding-standards.md, rule 7)
target_compile_options(melonade-shared PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
