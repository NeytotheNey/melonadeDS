// RetroAchievements' images for the popups, the indicators and the achievement list (the badges, and the game's icon): kept by URL in
// <profile>\media\ra\badges\<the URL's file name> (BadgeFileName), the game's icon taken from media\ra\<game id>\icon.png when the
// library's sync has it there. Looked for on disk and downloaded on two worker threads of its own with their own Http::Client (GET), so at
// most two at a time and never on the UI or emulator thread; a URL that fails isn't tried again this run.
//
// Threads: the UI thread owns it and calls everything; the workers only touch the queue and the entries under Lock
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Http.h"

namespace Frontend
{

class BadgeCache
{
public:
    BadgeCache() = default;
    ~BadgeCache();
    BadgeCache(const BadgeCache&) = delete;
    BadgeCache& operator=(const BadgeCache&) = delete;

    // [mediaDir]: <profile>\media; the workers start (with [userAgent] for their requests)
    void Open(const std::filesystem::path& mediaDir, const std::string& userAgent);
    // the downloads in flight cancelled and the workers stopped
    void Close();

    // The image at [url] (UTF-8 path) once it's on disk, else null: asked for the first time it's wanted (looked for, then downloaded),
    // null for good after a failure. [gameId], for the game's icon: the library's copy in media\ra\<gameId>\icon.png is used when it's
    // there. The pointer stays valid until the next File() or Close()
    const std::string* File(const std::string& url, uint32_t gameId = 0);
    // Clear downloaded art (MetadataSync::OnArtCleared, after the media folder went): everything known forgotten, so each image is
    // looked for and downloaded again, and a download in flight saves nothing. Any thread; the entries go at the UI thread's next File()
    void Clear();
    // images asked for and not on disk or failed yet
    bool Pending();

private:
    enum class State { Queued, Ready, Failed };
    struct Entry
    {
        State Is = State::Queued;
        uint32_t GameId = 0;
        std::string Path; // set before Is becomes Ready, then never changed
    };

    void WorkerLoop(int index);
    // [url]'s file: the library's icon, the cache's copy, or a download; empty when there's none
    std::string Resolve(const std::string& url, uint32_t gameId, int index, uint64_t generation);

    std::filesystem::path MediaDir;
    std::unique_ptr<Http::Client> Http_;
    std::mutex Lock;
    std::condition_variable Wake;
    std::unordered_map<std::string, Entry> Entries; // by URL, under Lock
    std::deque<std::string> Queue;                  // URLs to resolve, under Lock
    int Busy = 0;                                   // workers resolving one, under Lock
    bool Stopping = false;
    bool ClearPending = false;                      // Clear() asked; File() drops the entries, under Lock
    // bumped by Clear() (under Lock and WriteLock); a worker saves only while it's the one its job started with, under WriteLock
    std::atomic<uint64_t> Generation = 0;
    std::mutex WriteLock;
    std::vector<std::thread> Workers;
};

}
