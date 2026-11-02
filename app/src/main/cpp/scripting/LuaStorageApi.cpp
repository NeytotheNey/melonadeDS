#include <string>
#include <string_view>
#include "LuaEngineInternal.h"

namespace MelonDSAndroid
{

/**
 * The key at index 1, which must be a real string (numbers aren't turned into keys)
 */
static std::string_view checkStorageKey(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);
    size_t length;
    const char* key = lua_tolstring(L, 1, &length);
    if (length > LuaStorage::kMaxKeyLength)
        luaL_error(L, "storage keys are limited to 256 bytes");
    return { key, length };
}

static size_t getStorageEntrySize(std::string_view key, std::string_view value)
{
    return LuaStorage::kBlobEntryOverhead + key.size() + value.size();
}

// Lua errors are longjmps, which skip C++ destructors, so these functions keep no C++ object with memory of its own across a call that can
// raise one: encoding goes into the store's scratch string, and the error is raised once nothing is left to clean up

int LuaStorageApi::get(lua_State* L)
{
    std::string_view key = checkStorageKey(L);
    LuaStorage::Store& store = getScript(L).storage;
    auto entry = store.entries.find(key);
    if (entry == store.entries.end())
    {
        lua_pushnil(L);
        return 1;
    }

    // A fresh copy each time, so changing it doesn't change what's stored
    melonade_charge_work(L, (lua_Integer) entry->second.size() + 1);
    LuaStorage::pushValue(L, entry->second);
    return 1;
}

int LuaStorageApi::set(lua_State* L)
{
    std::string_view key = checkStorageKey(L);
    LuaStorage::Store& store = getScript(L).storage;
    auto entry = store.entries.find(key);

    if (lua_isnoneornil(L, 2))
    {
        if (entry != store.entries.end())
        {
            store.size -= getStorageEntrySize(entry->first, entry->second);
            store.entries.erase(entry);
            store.isDirty = true;
        }
        return 0;
    }

    // The room left for the value once the key's old value is gone
    size_t sizeWithoutEntry = store.size - (entry != store.entries.end() ? getStorageEntrySize(entry->first, entry->second) : 0);
    size_t sizeWithEmptyEntry = sizeWithoutEntry + getStorageEntrySize(key, {});
    size_t limit = sizeWithEmptyEntry < LuaStorage::kMaxSize ? LuaStorage::kMaxSize - sizeWithEmptyEntry : 0;

    int badType = 0;
    LuaStorage::EncodeError error = LuaStorage::encodeValue(L, 2, store.scratch, limit, badType);
    size_t encodedSize = store.scratch.size();
    if (error != LuaStorage::EncodeError::None)
        store.scratch.clear();

    // Charged before the value is kept, so that the storage stays as it was if the budget runs out here
    melonade_charge_work(L, (lua_Integer) encodedSize + 1);
    if (error == LuaStorage::EncodeError::ValueType)
        return luaL_error(L, LuaStorage::getErrorMessage(error), lua_typename(L, badType));
    if (error != LuaStorage::EncodeError::None)
        return luaL_error(L, "%s", LuaStorage::getErrorMessage(error));

    if (entry != store.entries.end())
    {
        store.size -= getStorageEntrySize(entry->first, entry->second);
        entry->second.swap(store.scratch);
        store.size += getStorageEntrySize(entry->first, entry->second);
    }
    else
    {
        store.size += getStorageEntrySize(key, store.scratch);
        store.entries.emplace(std::string(key), std::move(store.scratch));
    }
    store.scratch.clear();
    // Don't keep a big buffer around for one big value
    if (store.scratch.capacity() > 16 * 1024)
        std::string().swap(store.scratch);
    store.isDirty = true;
    return 0;
}

int LuaStorageApi::keys(lua_State* L)
{
    LuaStorage::Store& store = getScript(L).storage;
    lua_Integer work = (lua_Integer) store.entries.size() + 1;
    for (const auto& entry : store.entries)
        work += (lua_Integer) entry.first.size();
    melonade_charge_work(L, work);

    // In key order
    lua_createtable(L, (int) store.entries.size(), 0);
    lua_Integer index = 1;
    for (const auto& entry : store.entries)
    {
        lua_pushlstring(L, entry.first.data(), entry.first.size());
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

int LuaStorageApi::clear(lua_State* L)
{
    LuaStorage::Store& store = getScript(L).storage;
    store.clear();
    // Also when it was empty already: data that couldn't be read is replaced too
    store.isDirty = true;
    return 0;
}

int LuaStorageApi::flush(lua_State* L)
{
    // Saved at the end of the frame, if anything changed
    getScript(L).storage.isFlushRequested = true;
    return 0;
}

int LuaStorageApi::size(lua_State* L)
{
    const LuaStorage::Store& store = getScript(L).storage;
    lua_pushinteger(L, store.entries.empty() ? 0 : (lua_Integer) store.size);
    lua_pushinteger(L, (lua_Integer) LuaStorage::kMaxSize);
    return 2;
}

const luaL_Reg LuaStorageApi::functions[] = {
    { "get", get },
    { "set", set },
    { "keys", keys },
    { "clear", clear },
    { "flush", flush },
    { "size", size },
    { nullptr, nullptr },
};

}
