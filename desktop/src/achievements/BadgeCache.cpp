#include "achievements/BadgeCache.h"

#include <cstdio>
#include <fstream>

#include "ArtImage.h"
#include "Settings.h"
#include "achievements/AchievementsLogic.h"

namespace fs = std::filesystem;

namespace Frontend
{

namespace
{

constexpr int WORKER_COUNT = 2;
// a badge is 64 x 64, a game's icon 96 x 96: anything this big isn't one
constexpr size_t MAX_IMAGE_BYTES = 4u << 20;

// [bytes] to [file] through a temporary file of the worker's own ([index]), so a half-written image is never read
bool WriteImage(const fs::path& file, const std::vector<uint8_t>& bytes, int index)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    fs::path temp = file;
    temp += ".part" + std::to_string(index);
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
        if (!out)
        {
            out.close();
            fs::remove(temp, ec);
            return false;
        }
    }
    fs::rename(temp, file, ec);
    if (ec)
    {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

}

BadgeCache::~BadgeCache()
{
    Close();
}

void BadgeCache::Open(const fs::path& mediaDir, const std::string& userAgent)
{
    if (!Workers.empty())
        return;
    MediaDir = mediaDir;
    Http_ = std::make_unique<Http::Client>(userAgent);
    Stopping = false;
    for (int i = 0; i < WORKER_COUNT; i++)
        Workers.emplace_back([this, i] { WorkerLoop(i); });
}

void BadgeCache::Close()
{
    if (Workers.empty())
        return;
    {
        std::lock_guard guard(Lock);
        Stopping = true;
        Queue.clear();
    }
    Http_->Cancel();
    Wake.notify_all();
    for (std::thread& worker : Workers)
        worker.join();
    Workers.clear();
    Http_.reset();
}

const std::string* BadgeCache::File(const std::string& url, uint32_t gameId)
{
    if (url.empty() || Workers.empty())
        return nullptr;
    std::lock_guard guard(Lock);
    // (here, on the UI thread, so a pointer File() gave out is never freed while it's used)
    if (ClearPending)
    {
        Entries.clear();
        ClearPending = false;
    }
    auto it = Entries.find(url);
    if (it != Entries.end())
        return it->second.Is == State::Ready ? &it->second.Path : nullptr;
    Entry& entry = Entries[url];
    entry.GameId = gameId;
    Queue.push_back(url);
    Wake.notify_one();
    return nullptr;
}

void BadgeCache::Clear()
{
    std::lock_guard write(WriteLock);
    std::lock_guard guard(Lock);
    Generation++;
    Queue.clear();
    ClearPending = true;
    printf("ra: badges forgotten (downloaded art cleared)\n");
    fflush(stdout);
}

bool BadgeCache::Pending()
{
    std::lock_guard guard(Lock);
    return !Queue.empty() || Busy > 0;
}

void BadgeCache::WorkerLoop(int index)
{
    std::unique_lock lock(Lock);
    for (;;)
    {
        Wake.wait(lock, [&] { return Stopping || !Queue.empty(); });
        if (Stopping)
            return;
        std::string url = std::move(Queue.front());
        Queue.pop_front();
        uint32_t gameId = Entries[url].GameId;
        uint64_t generation = Generation.load();
        Busy++;
        lock.unlock();
        std::string path = Resolve(url, gameId, index, generation);
        lock.lock();
        Busy--;
        // cleared meanwhile: its entry is gone (or about to be)
        if (generation != Generation.load() || ClearPending)
            continue;
        Entry& entry = Entries[url];
        entry.Path = std::move(path);
        entry.Is = entry.Path.empty() ? State::Failed : State::Ready;
    }
}

std::string BadgeCache::Resolve(const std::string& url, uint32_t gameId, int index, uint64_t generation)
{
    std::error_code ec;
    if (gameId != 0)
    {
        fs::path icon = MediaDir / "ra" / std::to_string(gameId) / "icon.png";
        if (fs::is_regular_file(icon, ec))
            return PathToUtf8(icon);
    }
    std::string name = BadgeFileName(url);
    if (name.empty())
    {
        printf("ra: badge skipped: not an image's URL\n");
        fflush(stdout);
        return "";
    }
    fs::path file = MediaDir / "ra" / "badges" / PathFromUtf8(name);
    if (fs::is_regular_file(file, ec))
    {
        printf("ra: badge %s from the cache\n", name.c_str());
        fflush(stdout);
        return PathToUtf8(file);
    }
    Http::Request request;
    request.Url = url;
    request.MaxBytes = MAX_IMAGE_BYTES;
    Http::Response response = Http_->Get(request);
    if (response.Err == Http::Response::Error::Cancelled)
        return "";
    const char* failure = nullptr;
    if (!response.Ok())
        failure = "download failed";
    else if (CheckImage(response.Body.data(), response.Body.size()) != ImageCheck::Ok)
        failure = "not an image this Windows can read";
    else
    {
        std::lock_guard write(WriteLock);
        // the art was cleared while it downloaded: nothing is saved
        if (generation != Generation.load())
            return "";
        if (!WriteImage(file, response.Body, index))
            failure = "couldn't be saved";
    }
    if (failure)
    {
        printf("ra: badge %s: %s (%s); not tried again this run\n", name.c_str(), failure, Http::Describe(response).c_str());
        fflush(stdout);
        return "";
    }
    printf("ra: badge %s downloaded\n", name.c_str());
    fflush(stdout);
    return PathToUtf8(file);
}

}
