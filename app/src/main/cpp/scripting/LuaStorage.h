#ifndef LUASTORAGE_H
#define LUASTORAGE_H

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// From lua.h, which is only included by the .cpp files
struct lua_State;

namespace MelonDSAndroid::LuaStorage
{

/**
 * Most a script can store, encoded: the size of its whole blob (see [encodeBlob]), header included
 */
constexpr size_t kMaxSize = 256 * 1024;
constexpr size_t kMaxKeyLength = 256;
/**
 * How deep tables can go: a table in a stored value is level 1, a table in that one level 2, and so on
 */
constexpr int kMaxDepth = 16;

/**
 * A blob is "MLSD", a version byte, an entry count (u32), then per entry its key's length (u32), the key, its value's length (u32) and the
 * value (see [encodeValue]). Numbers are little-endian
 */
constexpr size_t kBlobHeaderSize = 9;
constexpr size_t kBlobEntryOverhead = 8;

/**
 * A script's melon.storage: key -> encoded value. It lives outside the script's Lua state, so it survives reset reloads and switching the
 * script off and on. Sorted, so that the same data always gives the same blob
 */
struct Store
{
    std::map<std::string, std::string, std::less<>> entries;
    // The size [encodeBlob] would give, kept up to date as entries change
    size_t size = kBlobHeaderSize;
    // Changed since the last snapshot
    bool isDirty = false;
    // flush() was called in this frame
    bool isFlushRequested = false;
    // A value being encoded. A member rather than a local, so that a Lua error (a longjmp) can't leak it
    std::string scratch;

    void clear();
};

enum class EncodeError
{
    None,
    // A value of a type that can't be stored (functions, userdata, threads). [encodeValue] gives the type
    ValueType,
    // A table key that isn't a string or an integer
    KeyType,
    TooDeep,
    // A table that contains itself
    Cycle,
    // Over the size limit given to [encodeValue]
    TooLarge,
};

/**
 * Encodes the value at [index] of L's stack into [out] (cleared first). Never raises a Lua error, so that callers can clean up first. Tables
 * are copied deeply with their raw content (metatables aren't kept). Stops as soon as [out] grows past [limit].
 *
 * @param badType Set to the Lua type that couldn't be stored, for EncodeError::ValueType
 */
EncodeError encodeValue(lua_State* L, int index, std::string& out, size_t limit, int& badType);

/**
 * The message a script sees for [error]. ValueType's has a %s for the type's name
 */
const char* getErrorMessage(EncodeError error);

/**
 * Pushes a copy of an encoded value. It must be one [encodeValue] made, or one that [decodeBlob] checked. Can raise memory errors
 */
void pushValue(lua_State* L, std::string_view encoded);

/**
 * Whether [encoded] is exactly one well-formed value within the depth limit
 */
bool isValidValue(std::string_view encoded);

/**
 * The whole store as a blob, or an empty string when it has no entries (the saved file can go then)
 */
std::string encodeBlob(const Store& store);

/**
 * Replaces [store]'s entries with the blob's. Returns false (and leaves [store] empty) if the blob is corrupt: a wrong header or version, a
 * length that runs past the end, a bad value, bytes left over, or more than [kMaxSize]. An empty blob is an empty store
 */
bool decodeBlob(std::string_view blob, Store& store);

/**
 * A snapshot of one script's store, waiting for the app to save it. [data] is empty when the store is
 */
struct Snapshot
{
    std::string gameKey;
    std::string scriptName;
    std::string data;
};

/**
 * Adds a snapshot to the outbox, replacing an older one of the same script. The outbox is global (it outlives the emulator instance, so the
 * app can drain it after the emulator stopped) and thread safe. Returns true if the outbox was empty, in which case the app should be woken up
 */
bool pushSnapshot(Snapshot snapshot);

/**
 * Takes everything out of the outbox, oldest first
 */
std::vector<Snapshot> drainSnapshots();

/**
 * Puts drained snapshots back in front of the outbox, for when the app couldn't take them. A script that pushed a newer snapshot meanwhile
 * keeps that one
 */
void restoreSnapshots(std::vector<Snapshot> snapshots);

}

#endif //LUASTORAGE_H
