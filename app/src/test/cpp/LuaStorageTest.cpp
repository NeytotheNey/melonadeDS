/*
 * Host test for scripting/LuaStorage (the melon.storage codec) that prints each failed check and exits with 1 if any failed; it isn't part of
 * the Gradle build, and MODS.md ("Testing") has the build command.
 */

#include <cstdio>
#include <cstring>
#include <string>
#include "lua.hpp"
#include "scripting/LuaStorage.h"

using namespace MelonDSAndroid;

// lua/patched/lstrlib.c charges pattern matching to the engine's work budget, which isn't here
extern "C" void melonade_charge_work(lua_State*, lua_Integer) {}

static int failures = 0;

#define CHECK(condition) \
    do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

static void run(lua_State* L, const char* code)
{
    if (luaL_dostring(L, code) != LUA_OK)
    {
        std::printf("Lua error: %s\n", lua_tostring(L, -1));
        failures++;
        lua_pop(L, 1);
    }
}

/**
 * Encodes the global [name], decodes it again and checks the copy with the Lua function deep_equal()
 */
static void checkRoundTrip(lua_State* L, const char* name)
{
    lua_getglobal(L, name);
    std::string encoded;
    int badType = 0;
    LuaStorage::EncodeError error = LuaStorage::encodeValue(L, -1, encoded, LuaStorage::kMaxSize, badType);
    CHECK(error == LuaStorage::EncodeError::None);
    CHECK(LuaStorage::isValidValue(encoded));

    lua_getglobal(L, "deep_equal");
    lua_pushvalue(L, -2);
    LuaStorage::pushValue(L, encoded);
    lua_call(L, 2, 1);
    bool isEqual = lua_toboolean(L, -1);
    if (!isEqual)
        std::printf("round trip of %s changed the value\n", name);
    CHECK(isEqual);
    lua_pop(L, 2);
    CHECK(lua_gettop(L) == 0);
}

static LuaStorage::EncodeError encodeGlobal(lua_State* L, const char* name, size_t limit = LuaStorage::kMaxSize)
{
    lua_getglobal(L, name);
    std::string encoded;
    int badType = 0;
    LuaStorage::EncodeError error = LuaStorage::encodeValue(L, -1, encoded, limit, badType);
    lua_pop(L, 1);
    return error;
}

static void testValues(lua_State* L)
{
    run(L, R"(
        function deep_equal(a, b)
            if type(a) ~= type(b) then return false end
            if type(a) == "number" then
                if math.type(a) ~= math.type(b) then return false end
                if a ~= a then return b ~= b end
                return a == b
            end
            if type(a) ~= "table" then return a == b end
            for k, v in pairs(a) do if not deep_equal(v, b[k]) then return false end end
            for k in pairs(b) do if a[k] == nil then return false end end
            return true
        end

        v_int = 42
        v_negative = math.mininteger
        v_float = 0.5
        v_integral_float = 3.0
        v_huge = math.huge
        v_nan = 0 / 0
        v_true = true
        v_false = false
        v_string = "with \0 a NUL and \xff bytes"
        v_empty_string = ""
        v_table = {
            1, 2, 3, "four", 5.5,
            name = "melon", nested = { deeper = { list = { true, false } } }, [-7] = "negative key", [math.maxinteger] = 1,
            empty = {},
        }
        -- 16 levels: the most there can be
        v_deep = {}
        local t = v_deep
        for i = 2, 16 do t.next = {}; t = t.next end
        t.value = "bottom"
        -- The same table twice isn't a cycle: it's copied twice
        local shared = { 1 }
        v_shared = { a = shared, b = shared }
    )");

    const char* names[] = { "v_int", "v_negative", "v_float", "v_integral_float", "v_huge", "v_nan", "v_true", "v_false", "v_string",
                            "v_empty_string", "v_table", "v_deep", "v_shared" };
    for (const char* name : names)
        checkRoundTrip(L, name);

    // Changing the copy doesn't change the original: each push is a fresh table
    lua_getglobal(L, "v_shared");
    std::string encoded;
    int badType = 0;
    LuaStorage::encodeValue(L, -1, encoded, LuaStorage::kMaxSize, badType);
    LuaStorage::pushValue(L, encoded);
    lua_getfield(L, -1, "a");
    lua_getfield(L, -2, "b");
    CHECK(lua_topointer(L, -1) != lua_topointer(L, -2));
    lua_settop(L, 0);
}

static void testEncodeErrors(lua_State* L)
{
    run(L, R"(
        e_function = print
        e_in_table = { ok = 1, bad = print }
        e_cycle = {}
        e_cycle.self = e_cycle
        e_float_key = { [0.5] = 1 }
        e_bool_key = { [true] = 1 }
        e_too_deep = {}
        local t = e_too_deep
        for i = 2, 17 do t.next = {}; t = t.next end
        e_big = string.rep("x", 300 * 1024)
        e_many = {}
        for i = 1, 200000 do e_many[i] = true end
    )");

    lua_getglobal(L, "e_function");
    std::string encoded;
    int badType = 0;
    CHECK(LuaStorage::encodeValue(L, -1, encoded, LuaStorage::kMaxSize, badType) == LuaStorage::EncodeError::ValueType);
    CHECK(badType == LUA_TFUNCTION);
    lua_pop(L, 1);

    CHECK(encodeGlobal(L, "e_in_table") == LuaStorage::EncodeError::ValueType);
    CHECK(encodeGlobal(L, "e_cycle") == LuaStorage::EncodeError::Cycle);
    CHECK(encodeGlobal(L, "e_float_key") == LuaStorage::EncodeError::KeyType);
    CHECK(encodeGlobal(L, "e_bool_key") == LuaStorage::EncodeError::KeyType);
    CHECK(encodeGlobal(L, "e_too_deep") == LuaStorage::EncodeError::TooDeep);
    CHECK(encodeGlobal(L, "e_big") == LuaStorage::EncodeError::TooLarge);
    CHECK(encodeGlobal(L, "e_many") == LuaStorage::EncodeError::TooLarge);
    CHECK(encodeGlobal(L, "v_string", 10) == LuaStorage::EncodeError::TooLarge);
    // A failed encode leaves the stack as it was
    CHECK(lua_gettop(L) == 0);
}

static LuaStorage::Store makeStore(lua_State* L)
{
    LuaStorage::Store store;
    const char* names[] = { "v_int", "v_table", "v_string", "v_deep" };
    for (const char* name : names)
    {
        lua_getglobal(L, name);
        std::string encoded;
        int badType = 0;
        LuaStorage::encodeValue(L, -1, encoded, LuaStorage::kMaxSize, badType);
        lua_pop(L, 1);
        store.size += LuaStorage::kBlobEntryOverhead + strlen(name) + encoded.size();
        store.entries.emplace(name, std::move(encoded));
    }
    return store;
}

static void testBlobs(lua_State* L)
{
    LuaStorage::Store empty;
    CHECK(LuaStorage::encodeBlob(empty).empty());
    LuaStorage::Store decoded;
    CHECK(LuaStorage::decodeBlob("", decoded));
    CHECK(decoded.entries.empty());

    LuaStorage::Store store = makeStore(L);
    std::string blob = LuaStorage::encodeBlob(store);
    // The store's size is exactly what its blob takes
    CHECK(blob.size() == store.size);
    CHECK(LuaStorage::decodeBlob(blob, decoded));
    CHECK(decoded.entries == store.entries);
    CHECK(decoded.size == store.size);
    // Same data, same bytes
    CHECK(LuaStorage::encodeBlob(decoded) == blob);

    // Every shorter blob is corrupt, and leaves the store empty
    for (size_t length = 1; length < blob.size(); length++)
    {
        decoded = store;
        bool isDecoded = LuaStorage::decodeBlob(std::string_view(blob.data(), length), decoded);
        CHECK(!isDecoded);
        CHECK(decoded.entries.empty());
        if (isDecoded)
            break;
    }

    auto corrupt = [&](size_t offset, char value) {
        std::string copy = blob;
        copy[offset] = value;
        return copy;
    };
    CHECK(!LuaStorage::decodeBlob(corrupt(0, 'X'), decoded));
    CHECK(!LuaStorage::decodeBlob(corrupt(4, 2), decoded));
    // A huge entry count
    CHECK(!LuaStorage::decodeBlob(corrupt(8, (char) 0x7F), decoded));
    CHECK(!LuaStorage::decodeBlob(blob + "x", decoded));
    CHECK(!LuaStorage::decodeBlob(std::string(LuaStorage::kMaxSize + 1, 'M'), decoded));

    // A key over 256 bytes, and the same key twice
    LuaStorage::Store longKey;
    longKey.entries.emplace(std::string(257, 'k'), std::string(1, '\1'));
    CHECK(!LuaStorage::decodeBlob(LuaStorage::encodeBlob(longKey), decoded));
    std::string twice = LuaStorage::encodeBlob(makeStore(L));
    {
        LuaStorage::Store one;
        one.entries.emplace("a", std::string(1, '\1'));
        std::string single = LuaStorage::encodeBlob(one);
        // Header with a count of 2, then the same entry twice
        std::string doubled = single.substr(0, 5) + std::string("\2\0\0\0", 4) + single.substr(9) + single.substr(9);
        CHECK(!LuaStorage::decodeBlob(doubled, decoded));
    }

    // Bad values: an unknown tag, a string longer than what's left, a table 17 deep
    CHECK(!LuaStorage::isValidValue(std::string(1, '\x09')));
    CHECK(!LuaStorage::isValidValue(std::string("\4\xff\0\0\0abc", 8)));
    CHECK(!LuaStorage::isValidValue(std::string("\1\1", 2)));
    std::string deep;
    for (int i = 0; i < 17; i++)
        deep += std::string("\5\1\0\0\0\4\1\0\0\0n", 11);
    deep += '\1';
    CHECK(!LuaStorage::isValidValue(deep));
    // A table key that is a boolean
    CHECK(!LuaStorage::isValidValue(std::string("\5\1\0\0\0\1\1", 7)));

    // Flipping any single byte never crashes: the blob is either rejected or still made of valid values
    for (size_t offset = 0; offset < blob.size(); offset++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            std::string copy = blob;
            copy[offset] ^= (char) (1 << bit);
            if (LuaStorage::decodeBlob(copy, decoded))
            {
                for (const auto& entry : decoded.entries)
                {
                    LuaStorage::pushValue(L, entry.second);
                    lua_pop(L, 1);
                }
            }
        }
    }
}

static void testOutbox()
{
    LuaStorage::drainSnapshots();
    CHECK(LuaStorage::pushSnapshot({ "AXYE", "a.lua", "1" }));
    CHECK(!LuaStorage::pushSnapshot({ "AXYE", "b.lua", "2" }));
    // A newer snapshot of the same script replaces the old one
    CHECK(!LuaStorage::pushSnapshot({ "AXYE", "a.lua", "3" }));
    auto snapshots = LuaStorage::drainSnapshots();
    CHECK(snapshots.size() == 2);
    CHECK(snapshots.size() == 2 && snapshots[0].scriptName == "b.lua" && snapshots[1].data == "3");
    CHECK(LuaStorage::drainSnapshots().empty());
    CHECK(LuaStorage::pushSnapshot({ "AXYE", "a.lua", "4" }));

    // Snapshots the app couldn't take go back in front, unless the script pushed a newer one meanwhile
    snapshots = LuaStorage::drainSnapshots();
    CHECK(LuaStorage::pushSnapshot({ "AXYE", "b.lua", "5" }));
    snapshots.push_back({ "AXYE", "b.lua", "2" });
    LuaStorage::restoreSnapshots(std::move(snapshots));
    snapshots = LuaStorage::drainSnapshots();
    CHECK(snapshots.size() == 2);
    CHECK(snapshots.size() == 2 && snapshots[0].data == "4" && snapshots[1].data == "5");
}

int main()
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    testValues(L);
    testEncodeErrors(L);
    testBlobs(L);
    testOutbox();

    lua_close(L);
    if (failures == 0)
        std::printf("All LuaStorage tests passed\n");
    return failures == 0 ? 0 : 1;
}
