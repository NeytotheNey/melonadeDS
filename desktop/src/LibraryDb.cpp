#include "LibraryDb.h"

#include <cstdio>
#include <cstring>

#include "Library.h"
#include "Settings.h"
#include "sqlite/sqlite3.h"

namespace Frontend
{

namespace
{

// Each schema version's changes, in order; a database at an older version gets the ones after it, each in a transaction with the new
// user_version. Step C (metadata and art sync) adds version 3 here
struct Migration { int Version; const char* Sql; };
const Migration kMigrations[] = {
    {1,
        "CREATE TABLE roms ("
        "  id INTEGER PRIMARY KEY,"
        "  path TEXT NOT NULL COLLATE NOCASE,"   // the file (UTF-8, absolute)
        "  entry TEXT NOT NULL DEFAULT '',"      // the ROM inside a zip or 7z, '' for a plain file
        "  file_size INTEGER NOT NULL,"          // what a rescan compares: the file's size
        "  file_time INTEGER NOT NULL,"          // and its last write time (FILETIME)
        "  state INTEGER NOT NULL,"              // 0 read, 1 not a DS ROM, 2 an online-only file not read yet, 3 the read failed
        "  rom_size INTEGER,"                    // the ROM's own size (an archive's entry uncompressed)
        "  game_code TEXT,"
        "  region TEXT,"
        "  header_title TEXT,"
        "  title TEXT,"                          // the banner's title, NULL when it has none
        "  title_language INTEGER,"              // the banner language asked for when it was read (0 Japanese, 1 English, ...)
        "  developer TEXT,"
        "  dsiware INTEGER NOT NULL DEFAULT 0,"
        "  ra_hash TEXT,"                        // rc_hash's Nintendo DS hash, as the app's RomProcessor
        "  crc32 TEXT, md5 TEXT, sha1 TEXT,"     // the whole ROM's hashes, for step C's metadata match; NULL until then
        "  icon BLOB,"                           // 32 x 32 RGBA, NULL without one
        "  scanned_at INTEGER NOT NULL,"         // when it was read (ms since 1970)
        "  UNIQUE (path, entry)"
        ");"
        "CREATE TABLE play_history ("
        "  path TEXT NOT NULL PRIMARY KEY COLLATE NOCASE,"
        "  last_played INTEGER NOT NULL"         // ms since 1970
        ");"},
    {2,
        "CREATE TABLE rom_config ("
        "  game TEXT NOT NULL COLLATE NOCASE,"   // the ROM's RetroAchievements hash, or its path when rc_hash refused it
        "  key TEXT NOT NULL,"                   // the app's RomConfigDto field: runtimeConsoleType, customName, gbaSlotConfig.type, ...
        "  value TEXT NOT NULL,"                 // its value as the app writes it (DS, DSi, RumblePak, ...)
        "  PRIMARY KEY (game, key)"
        ");"},
};

struct Statement
{
    Statement(sqlite3* db, const char* sql)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &S, nullptr) != SQLITE_OK)
            fprintf(stderr, "library: %s: %s\n", sql, sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(S); }
    explicit operator bool() const { return S != nullptr; }
    void Text(int i, const std::string& v) { sqlite3_bind_text(S, i, v.c_str(), (int)v.size(), SQLITE_TRANSIENT); }
    void TextOrNull(int i, const std::string& v) { v.empty() ? (void)sqlite3_bind_null(S, i) : Text(i, v); }
    void Int(int i, int64_t v) { sqlite3_bind_int64(S, i, v); }
    std::string ColText(int i)
    {
        const unsigned char* t = sqlite3_column_text(S, i);
        return t ? std::string((const char*)t, sqlite3_column_bytes(S, i)) : std::string();
    }
    int64_t ColInt(int i) { return sqlite3_column_int64(S, i); }
    sqlite3_stmt* S = nullptr;
};

}

bool LibraryDb::Open(const std::filesystem::path& file)
{
    std::lock_guard guard(Lock);
    if (sqlite3_open_v2(PathToUtf8(file).c_str(), &Db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        fprintf(stderr, "library: can't open %s: %s\n", PathToUtf8(file).c_str(), Db ? sqlite3_errmsg(Db) : "out of memory");
        sqlite3_close(Db);
        Db = nullptr;
        return false;
    }
    sqlite3_busy_timeout(Db, 2000);
    Exec("PRAGMA journal_mode = WAL");
    Exec("PRAGMA synchronous = NORMAL");
    if (!Migrate())
    {
        sqlite3_close(Db);
        Db = nullptr;
        return false;
    }
    return true;
}

void LibraryDb::Close()
{
    std::lock_guard guard(Lock);
    if (Db)
        sqlite3_close(Db);
    Db = nullptr;
}

bool LibraryDb::Exec(const char* sql)
{
    char* error = nullptr;
    if (sqlite3_exec(Db, sql, nullptr, nullptr, &error) != SQLITE_OK)
    {
        fprintf(stderr, "library: %s\n", error ? error : sqlite3_errmsg(Db));
        sqlite3_free(error);
        return false;
    }
    return true;
}

bool LibraryDb::Migrate()
{
    {
        Statement version(Db, "PRAGMA user_version");
        Version = version && sqlite3_step(version.S) == SQLITE_ROW ? (int)version.ColInt(0) : 0;
    }
    int latest = kMigrations[sizeof(kMigrations) / sizeof(kMigrations[0]) - 1].Version;
    if (Version > latest)
    {
        // made by a newer build: its tables are a superset, so it's used as it is
        printf("library: library.db is at schema version %d, newer than this build's %d\n", Version, latest);
        return true;
    }
    for (const Migration& m : kMigrations)
    {
        if (m.Version <= Version)
            continue;
        char setVersion[64];
        snprintf(setVersion, sizeof(setVersion), "PRAGMA user_version = %d", m.Version);
        if (!Exec("BEGIN") || !Exec(m.Sql) || !Exec(setVersion) || !Exec("COMMIT"))
        {
            Exec("ROLLBACK");
            fprintf(stderr, "library: the update to schema version %d failed\n", m.Version);
            return false;
        }
        printf("library: schema version %d\n", m.Version);
        Version = m.Version;
    }
    return true;
}

bool LibraryDb::LoadRoms(std::vector<LibraryEntry>& out)
{
    std::lock_guard guard(Lock);
    if (!Db)
        return false;
    Statement q(Db, "SELECT id, path, entry, file_size, file_time, state, rom_size, game_code, region, header_title, title, title_language,"
        " developer, dsiware, ra_hash, icon, scanned_at FROM roms");
    if (!q)
        return false;
    while (sqlite3_step(q.S) == SQLITE_ROW)
    {
        LibraryEntry e;
        e.Id = q.ColInt(0);
        e.Path = q.ColText(1);
        e.Entry = q.ColText(2);
        e.FileSize = (uint64_t)q.ColInt(3);
        e.FileTime = q.ColInt(4);
        e.State = (int)q.ColInt(5);
        e.RomSize = (uint64_t)q.ColInt(6);
        e.GameCode = q.ColText(7);
        e.Region = q.ColText(8);
        e.HeaderTitle = q.ColText(9);
        e.Title = q.ColText(10);
        e.TitleLanguage = sqlite3_column_type(q.S, 11) == SQLITE_NULL ? -1 : (int)q.ColInt(11);
        e.Developer = q.ColText(12);
        e.DsiWare = q.ColInt(13) != 0;
        e.RaHash = q.ColText(14);
        if (sqlite3_column_bytes(q.S, 15) == 32 * 32 * 4)
        {
            const uint8_t* blob = (const uint8_t*)sqlite3_column_blob(q.S, 15);
            e.Icon = std::make_shared<std::vector<uint8_t>>(blob, blob + 32 * 32 * 4);
        }
        e.ScannedAt = q.ColInt(16);
        out.push_back(std::move(e));
    }
    return true;
}

bool LibraryDb::LoadPlays(std::map<std::string, int64_t>& out)
{
    std::lock_guard guard(Lock);
    if (!Db)
        return false;
    Statement q(Db, "SELECT path, last_played FROM play_history");
    if (!q)
        return false;
    while (sqlite3_step(q.S) == SQLITE_ROW)
    {
        std::string path = q.ColText(0);
        for (char& c : path)
            c = (char)tolower((unsigned char)c);
        out[path] = q.ColInt(1);
    }
    return true;
}

bool LibraryDb::StoreRom(LibraryEntry& e)
{
    std::lock_guard guard(Lock);
    if (!Db)
        return false;
    if (!Exec("BEGIN IMMEDIATE"))
        return false;
    // the row to update: its own, or the one with its path
    int64_t id = e.Id > 0 ? e.Id : 0;
    if (!id)
    {
        Statement find(Db, "SELECT id FROM roms WHERE path = ?");
        find.Text(1, e.Path);
        if (find && sqlite3_step(find.S) == SQLITE_ROW)
            id = find.ColInt(0);
    }
    bool ok;
    {
        // an update keeps the row's id; the columns are the insert's, with the id last
        Statement ins(Db, id
            ? "UPDATE roms SET path = ?, entry = ?, file_size = ?, file_time = ?, state = ?, rom_size = ?, game_code = ?, region = ?,"
              " header_title = ?, title = ?, title_language = ?, developer = ?, dsiware = ?, ra_hash = ?, icon = ?, scanned_at = ?,"
              " crc32 = NULL, md5 = NULL, sha1 = NULL WHERE id = ?"
            : "INSERT INTO roms (path, entry, file_size, file_time, state, rom_size, game_code, region, header_title, title,"
              " title_language, developer, dsiware, ra_hash, icon, scanned_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        ins.Text(1, e.Path);
        ins.Text(2, e.Entry);
        ins.Int(3, (int64_t)e.FileSize);
        ins.Int(4, e.FileTime);
        ins.Int(5, e.State);
        ins.Int(6, (int64_t)e.RomSize);
        ins.TextOrNull(7, e.GameCode);
        ins.TextOrNull(8, e.Region);
        ins.TextOrNull(9, e.HeaderTitle);
        ins.TextOrNull(10, e.Title);
        if (e.TitleLanguage >= 0)
            ins.Int(11, e.TitleLanguage);
        ins.TextOrNull(12, e.Developer);
        ins.Int(13, e.DsiWare ? 1 : 0);
        ins.TextOrNull(14, e.RaHash);
        if (e.Icon)
            sqlite3_bind_blob(ins.S, 15, e.Icon->data(), (int)e.Icon->size(), SQLITE_TRANSIENT);
        ins.Int(16, e.ScannedAt);
        if (id)
            ins.Int(17, id);
        ok = ins && sqlite3_step(ins.S) == SQLITE_DONE && (!id || sqlite3_changes(Db) == 1);
    }
    if (!ok)
        fprintf(stderr, "library: can't store %s: %s\n", e.Path.c_str(), sqlite3_errmsg(Db));
    if (ok && !Exec("COMMIT"))
        ok = false;
    if (!ok)
    {
        Exec("ROLLBACK");
        return false;
    }
    e.Id = id ? id : sqlite3_last_insert_rowid(Db);
    return true;
}

bool LibraryDb::DeleteRoms(const std::vector<int64_t>& ids)
{
    std::lock_guard guard(Lock);
    if (!Db)
        return false;
    if (!Exec("BEGIN IMMEDIATE"))
        return false;
    bool ok = true;
    {
        Statement del(Db, "DELETE FROM roms WHERE id = ?");
        ok = (bool)del;
        for (int64_t id : ids)
        {
            // (a row the database never had, a negative id, is skipped)
            if (!ok || id <= 0)
                continue;
            sqlite3_reset(del.S);
            del.Int(1, id);
            ok = sqlite3_step(del.S) == SQLITE_DONE;
        }
    }
    if (ok && Exec("COMMIT"))
        return true;
    fprintf(stderr, "library: can't remove %zu rows: %s\n", ids.size(), sqlite3_errmsg(Db));
    Exec("ROLLBACK");
    return false;
}

bool LibraryDb::SetPlayed(const std::string& path, int64_t when)
{
    std::lock_guard guard(Lock);
    if (!Db)
        return false;
    Statement up(Db, "INSERT INTO play_history (path, last_played) VALUES (?, ?)"
        " ON CONFLICT (path) DO UPDATE SET last_played = excluded.last_played");
    up.Text(1, path);
    up.Int(2, when);
    return up && sqlite3_step(up.S) == SQLITE_DONE;
}

bool LibraryDb::LoadConfigs(std::map<std::string, std::map<std::string, std::string>>& out)
{
    std::lock_guard guard(Lock);
    if (!Db)
        return false;
    Statement q(Db, "SELECT game, key, value FROM rom_config");
    if (!q)
        return false;
    while (sqlite3_step(q.S) == SQLITE_ROW)
    {
        std::string path = q.ColText(0);
        for (char& c : path)
            c = (char)tolower((unsigned char)c);
        out[path][q.ColText(1)] = q.ColText(2);
    }
    return true;
}

bool LibraryDb::StoreConfig(const std::string& game, const std::map<std::string, std::string>& fields, const std::vector<std::string>& keys)
{
    std::lock_guard guard(Lock);
    if (!Db)
        return false;
    if (!Exec("BEGIN IMMEDIATE"))
        return false;
    bool ok = true;
    {
        Statement del(Db, "DELETE FROM rom_config WHERE game = ? AND key = ?");
        ok = (bool)del;
        for (const std::string& key : keys)
        {
            if (!ok)
                break;
            sqlite3_reset(del.S);
            del.Text(1, game);
            del.Text(2, key);
            ok = sqlite3_step(del.S) == SQLITE_DONE;
        }
        Statement ins(Db, "INSERT INTO rom_config (game, key, value) VALUES (?, ?, ?)");
        ok = ok && (bool)ins;
        for (const auto& [key, value] : fields)
        {
            if (!ok)
                break;
            sqlite3_reset(ins.S);
            ins.Text(1, game);
            ins.Text(2, key);
            ins.Text(3, value);
            ok = sqlite3_step(ins.S) == SQLITE_DONE;
        }
    }
    if (ok && Exec("COMMIT"))
        return true;
    fprintf(stderr, "library: can't store the settings of %s: %s\n", game.c_str(), sqlite3_errmsg(Db));
    Exec("ROLLBACK");
    return false;
}

}
