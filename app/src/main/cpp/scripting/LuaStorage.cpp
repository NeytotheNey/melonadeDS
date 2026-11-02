#include "LuaStorage.h"
#include <cstdint>
#include <cstring>
#include <mutex>
#include "lua.hpp"

namespace MelonDSAndroid::LuaStorage
{

namespace
{

// Value tags, with table keys only Integer or String
enum Tag : uint8_t
{
    False = 0,
    True = 1,
    Integer = 2,
    Float = 3,
    String = 4,
    Table = 5,
};

const char kBlobMagic[4] = { 'M', 'L', 'S', 'D' };
const uint8_t kBlobVersion = 1;

void writeU32(std::string& out, uint32_t value)
{
    for (int i = 0; i < 4; i++)
        out.push_back((char) (value >> (8 * i)));
}

void writeU64(std::string& out, uint64_t value)
{
    for (int i = 0; i < 8; i++)
        out.push_back((char) (value >> (8 * i)));
}

void patchU32(std::string& out, size_t offset, uint32_t value)
{
    for (int i = 0; i < 4; i++)
        out[offset + i] = (char) (value >> (8 * i));
}

uint32_t readU32(const unsigned char* p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

uint64_t readU64(const unsigned char* p)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; i++)
        value |= (uint64_t) p[i] << (8 * i);
    return value;
}

/**
 * Only uses Lua functions that can't raise errors, so that no C++ object is skipped by a longjmp
 */
struct Encoder
{
    lua_State* L;
    std::string& out;
    size_t limit;
    int badType = LUA_TNIL;
    // The tables being encoded, outermost first, to catch cycles
    const void* path[kMaxDepth] = {};
    int depth = 0;

    bool hasRoom(size_t bytes) const { return out.size() <= limit && bytes <= limit - out.size(); }

    EncodeError encodeString(int index)
    {
        size_t length;
        const char* chars = lua_tolstring(L, index, &length);
        if (!hasRoom(5) || !hasRoom(5 + length))
            return EncodeError::TooLarge;
        out.push_back((char) Tag::String);
        writeU32(out, (uint32_t) length);
        out.append(chars, length);
        return EncodeError::None;
    }

    EncodeError encode(int index)
    {
        switch (lua_type(L, index))
        {
            case LUA_TBOOLEAN:
                out.push_back((char) (lua_toboolean(L, index) ? Tag::True : Tag::False));
                break;
            case LUA_TNUMBER:
                if (lua_isinteger(L, index))
                {
                    out.push_back((char) Tag::Integer);
                    writeU64(out, (uint64_t) lua_tointeger(L, index));
                }
                else
                {
                    double number = (double) lua_tonumber(L, index);
                    uint64_t bits;
                    memcpy(&bits, &number, sizeof(bits));
                    out.push_back((char) Tag::Float);
                    writeU64(out, bits);
                }
                break;
            case LUA_TSTRING:
            {
                EncodeError error = encodeString(index);
                if (error != EncodeError::None)
                    return error;
                break;
            }
            case LUA_TTABLE:
            {
                const void* table = lua_topointer(L, index);
                for (int i = 0; i < depth; i++)
                {
                    if (path[i] == table)
                        return EncodeError::Cycle;
                }
                // Room for a key and a value on the stack
                if (depth >= kMaxDepth || !lua_checkstack(L, 3))
                    return EncodeError::TooDeep;
                path[depth++] = table;

                out.push_back((char) Tag::Table);
                size_t countOffset = out.size();
                writeU32(out, 0);
                uint32_t count = 0;

                lua_pushnil(L);
                while (lua_next(L, index))
                {
                    int keyIndex = lua_gettop(L) - 1;
                    int keyType = lua_type(L, keyIndex);
                    // Lua turns float keys with an integer value into integers, so only real fractions and other types end up here
                    if (keyType == LUA_TSTRING)
                    {
                        EncodeError error = encodeString(keyIndex);
                        if (error != EncodeError::None)
                            return error;
                    }
                    else if (keyType == LUA_TNUMBER && lua_isinteger(L, keyIndex))
                    {
                        out.push_back((char) Tag::Integer);
                        writeU64(out, (uint64_t) lua_tointeger(L, keyIndex));
                    }
                    else
                    {
                        return EncodeError::KeyType;
                    }

                    EncodeError error = encode(lua_gettop(L));
                    if (error != EncodeError::None)
                        return error;
                    // Every entry adds bytes, so a huge table runs into the limit here instead of being walked to the end
                    if (out.size() > limit)
                        return EncodeError::TooLarge;

                    lua_pop(L, 1);
                    count++;
                }

                patchU32(out, countOffset, count);
                depth--;
                break;
            }
            default:
                badType = lua_type(L, index);
                return EncodeError::ValueType;
        }

        return out.size() > limit ? EncodeError::TooLarge : EncodeError::None;
    }
};

/**
 * Checks values from a blob that was saved to a file, so every length is checked against what's left
 */
struct Validator
{
    const unsigned char* p;
    const unsigned char* end;

    bool has(size_t bytes) const { return (size_t) (end - p) >= bytes; }

    bool validateKey()
    {
        if (!has(1))
            return false;
        uint8_t tag = *p;
        return (tag == Tag::Integer || tag == Tag::String) && validate(0);
    }

    bool validate(int depth)
    {
        if (!has(1))
            return false;

        switch (*p++)
        {
            case Tag::False:
            case Tag::True:
                return true;
            case Tag::Integer:
            case Tag::Float:
                if (!has(8))
                    return false;
                p += 8;
                return true;
            case Tag::String:
            {
                if (!has(4))
                    return false;
                uint32_t length = readU32(p);
                p += 4;
                if (!has(length))
                    return false;
                p += length;
                return true;
            }
            case Tag::Table:
            {
                if (depth >= kMaxDepth || !has(4))
                    return false;
                uint32_t count = readU32(p);
                p += 4;
                // Each entry takes at least 2 bytes, which also keeps a bad count from looping for long
                if (count > (size_t) (end - p) / 2)
                    return false;
                for (uint32_t i = 0; i < count; i++)
                {
                    if (!validateKey() || !validate(depth + 1))
                        return false;
                }
                return true;
            }
            default:
                return false;
        }
    }
};

/**
 * Pushes values that were checked (or made by Encoder) without checking lengths again, and may raise memory errors since nothing here needs
 * cleaning up
 */
struct Decoder
{
    lua_State* L;
    const unsigned char* p;

    void push()
    {
        switch (*p++)
        {
            case Tag::False:
                lua_pushboolean(L, 0);
                break;
            case Tag::True:
                lua_pushboolean(L, 1);
                break;
            case Tag::Integer:
                lua_pushinteger(L, (lua_Integer) readU64(p));
                p += 8;
                break;
            case Tag::Float:
            {
                uint64_t bits = readU64(p);
                double number;
                memcpy(&number, &bits, sizeof(number));
                lua_pushnumber(L, (lua_Number) number);
                p += 8;
                break;
            }
            case Tag::String:
            {
                uint32_t length = readU32(p);
                p += 4;
                lua_pushlstring(L, (const char*) p, length);
                p += length;
                break;
            }
            case Tag::Table:
            {
                uint32_t count = readU32(p);
                p += 4;
                luaL_checkstack(L, 3, "stored table too deep");
                lua_createtable(L, 0, (int) count);
                for (uint32_t i = 0; i < count; i++)
                {
                    push();
                    push();
                    lua_rawset(L, -3);
                }
                break;
            }
            default:
                // Can't happen with checked values
                lua_pushnil(L);
                break;
        }
    }
};

std::mutex& getOutboxMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::vector<Snapshot>& getOutbox()
{
    static std::vector<Snapshot> outbox;
    return outbox;
}

}

void Store::clear()
{
    entries.clear();
    size = kBlobHeaderSize;
}

EncodeError encodeValue(lua_State* L, int index, std::string& out, size_t limit, int& badType)
{
    out.clear();
    int top = lua_gettop(L);
    Encoder encoder { L, out, limit };
    EncodeError error = encoder.encode(lua_absindex(L, index));
    // Popping never raises an error
    lua_settop(L, top);
    badType = encoder.badType;
    return error;
}

const char* getErrorMessage(EncodeError error)
{
    switch (error)
    {
        case EncodeError::None:
            return "";
        case EncodeError::ValueType:
            return "can't store a %s value";
        case EncodeError::KeyType:
            return "can't store a table with keys that aren't strings or integers";
        case EncodeError::TooDeep:
            return "can't store tables more than 16 deep";
        case EncodeError::Cycle:
            return "can't store a table that contains itself";
        case EncodeError::TooLarge:
            return "storage is full (the limit is 256 KB per script)";
    }
    return "";
}

void pushValue(lua_State* L, std::string_view encoded)
{
    Decoder decoder { L, (const unsigned char*) encoded.data() };
    decoder.push();
}

bool isValidValue(std::string_view encoded)
{
    Validator validator { (const unsigned char*) encoded.data(), (const unsigned char*) encoded.data() + encoded.size() };
    return validator.validate(0) && validator.p == validator.end;
}

std::string encodeBlob(const Store& store)
{
    if (store.entries.empty())
        return {};

    std::string blob;
    blob.reserve(store.size);
    blob.append(kBlobMagic, sizeof(kBlobMagic));
    blob.push_back((char) kBlobVersion);
    writeU32(blob, (uint32_t) store.entries.size());
    for (const auto& [key, value] : store.entries)
    {
        writeU32(blob, (uint32_t) key.size());
        blob.append(key);
        writeU32(blob, (uint32_t) value.size());
        blob.append(value);
    }
    return blob;
}

bool decodeBlob(std::string_view blob, Store& store)
{
    store.clear();
    if (blob.empty())
        return true;

    auto fail = [&store]() {
        store.clear();
        return false;
    };

    if (blob.size() > kMaxSize || blob.size() < kBlobHeaderSize || memcmp(blob.data(), kBlobMagic, sizeof(kBlobMagic)) != 0 || (uint8_t) blob[4] != kBlobVersion)
        return fail();

    const auto* p = (const unsigned char*) blob.data();
    const unsigned char* end = p + blob.size();
    uint32_t count = readU32(p + 5);
    p += kBlobHeaderSize;

    for (uint32_t i = 0; i < count; i++)
    {
        if (end - p < 4)
            return fail();
        uint32_t keyLength = readU32(p);
        p += 4;
        if (keyLength > kMaxKeyLength || (size_t) (end - p) < keyLength)
            return fail();
        std::string_view key((const char*) p, keyLength);
        p += keyLength;

        if (end - p < 4)
            return fail();
        uint32_t valueLength = readU32(p);
        p += 4;
        if ((size_t) (end - p) < valueLength)
            return fail();
        std::string_view value((const char*) p, valueLength);
        p += valueLength;

        if (!isValidValue(value) || !store.entries.emplace(std::string(key), std::string(value)).second)
            return fail();
    }

    if (p != end)
        return fail();

    store.size = blob.size();
    return true;
}

bool pushSnapshot(Snapshot snapshot)
{
    std::lock_guard lock(getOutboxMutex());
    auto& outbox = getOutbox();
    bool wasEmpty = outbox.empty();
    for (auto it = outbox.begin(); it != outbox.end(); ++it)
    {
        if (it->gameKey == snapshot.gameKey && it->scriptName == snapshot.scriptName)
        {
            // The older one is out of date, and dropping it keeps the outbox to one snapshot per script
            outbox.erase(it);
            break;
        }
    }
    outbox.push_back(std::move(snapshot));
    return wasEmpty;
}

std::vector<Snapshot> drainSnapshots()
{
    std::lock_guard lock(getOutboxMutex());
    std::vector<Snapshot> snapshots;
    snapshots.swap(getOutbox());
    return snapshots;
}

void restoreSnapshots(std::vector<Snapshot> snapshots)
{
    std::lock_guard lock(getOutboxMutex());
    auto& outbox = getOutbox();
    std::vector<Snapshot> restored;
    restored.reserve(snapshots.size() + outbox.size());
    for (auto& snapshot : snapshots)
    {
        bool isOutdated = false;
        for (const auto& newer : outbox)
        {
            if (newer.gameKey == snapshot.gameKey && newer.scriptName == snapshot.scriptName)
            {
                isOutdated = true;
                break;
            }
        }
        if (!isOutdated)
            restored.push_back(std::move(snapshot));
    }
    for (auto& snapshot : outbox)
        restored.push_back(std::move(snapshot));
    outbox.swap(restored);
}

}
