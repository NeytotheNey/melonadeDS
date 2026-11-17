#include "LuaEngineInternal.h"

namespace MelonDSAndroid
{

int LuaGameApi::state(lua_State* L)
{
    LuaEngine* engine = getScript(L).engine;
    // Asking switches the per-frame read on for the rest of the game; nil for a game the field read doesn't know
    int scene = engine->gameState ? engine->gameState->getSceneForScript(engine->nds) : GameState::Unknown;
    const char* name = GameState::getSceneName(scene);
    if (name)
        lua_pushstring(L, name);
    else
        lua_pushnil(L);
    return 1;
}

}
