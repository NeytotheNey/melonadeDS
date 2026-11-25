#include "NDS.h"
#include "rcheevos.h"
#include "RetroAchievementsManager.h"
#include "types.h"

using namespace melonDS;


namespace MelonDSAndroid
{
namespace RetroAchievements
{

std::weak_ptr<MelonEventMessenger> RetroAchievementsManager::EventMessenger;
RetroAchievementsManager* RetroAchievementsManager::activeInstance = nullptr;
std::mutex RetroAchievementsManager::activeInstanceLock;

RetroAchievementsManager::RetroAchievementsManager(melonDS::NDS* nds) : nds(nds)
{
    rc_runtime_init(&rcheevosRuntime);
    isRichPresenceEnabled = false;
}

RetroAchievementsManager::~RetroAchievementsManager()
{
    FlushPeekWarnings();
    rc_runtime_destroy(&rcheevosRuntime);
}

bool RetroAchievementsManager::LoadAchievements(std::list<RAAchievement> achievements)
{
    std::unique_lock lock(runtimeLock);

    // A new achievement set starts a new session for the peek warnings
    FlushPeekWarnings();

    for (const auto &achievement : achievements) {
        int result = rc_runtime_activate_achievement(&rcheevosRuntime, achievement.id, achievement.memoryAddress.c_str(), nullptr, 0);
        if (result != RC_OK)
            return false;

        loadedAchievements.push_back(achievement);
    }

    return true;
}

bool RetroAchievementsManager::LoadLeaderboards(std::list<RALeaderboard> leaderboards)
{
    std::unique_lock lock(runtimeLock);

    for (auto &leaderboard : leaderboards) {
        int result = rc_runtime_activate_lboard(&rcheevosRuntime, leaderboard.id, leaderboard.memoryAddress.c_str(), nullptr, 0);
        if (result != RC_OK)
            return false;

        int rcheevosLeaderboardType = rc_parse_format(leaderboard.format.c_str());
        leaderboard.rcheevosFormat = rcheevosLeaderboardType;

        loadedLeaderboards.push_back(leaderboard);
    }

    return true;
}

void RetroAchievementsManager::UnloadEverything()
{
    std::unique_lock lock(runtimeLock);

    for (const auto &achievement : loadedAchievements) {
        rc_runtime_deactivate_achievement(&rcheevosRuntime, achievement.id);
    }
    for (const auto &leaderboard : loadedLeaderboards) {
        rc_runtime_deactivate_lboard(&rcheevosRuntime, leaderboard.id);
    }

    loadedAchievements.clear();
    loadedLeaderboards.clear();
    FlushPeekWarnings();
}

void RetroAchievementsManager::SetupRichPresence(std::string richPresenceScript)
{
    std::unique_lock lock(runtimeLock);

    rc_runtime_activate_richpresence(&rcheevosRuntime, richPresenceScript.c_str(), nullptr, 0);
    isRichPresenceEnabled = true;
}

std::string RetroAchievementsManager::GetRichPresenceStatus()
{
    std::unique_lock lock(runtimeLock);

    if (!isRichPresenceEnabled)
        return "";

    char buffer[512];
    rc_runtime_get_richpresence(&rcheevosRuntime, buffer, 512, PeekMemory, this, nullptr);

    return buffer;
}

std::vector<RARuntimeAchievement> RetroAchievementsManager::GetRuntimeAchievements()
{
    std::vector<RARuntimeAchievement> achievements(loadedAchievements.size());
    int index = 0;
    for (const auto &item: loadedAchievements)
    {
        RARuntimeAchievement& runtimeAchievement = achievements[index++];
        runtimeAchievement.id = item.id;
        rc_runtime_get_achievement_measured(&rcheevosRuntime, item.id, &runtimeAchievement.value, &runtimeAchievement.target);
    }

    return achievements;
}

bool RetroAchievementsManager::DoSavestate(Savestate* savestate)
{
    std::unique_lock lock(runtimeLock);

    // With a handler, a state without the section (an older desktop one) resets its progress and still loads
    if (progressHandler && !savestate->Saving && !savestate->Error && !savestate->HasSection("RCHV"))
    {
        progressHandler->ResetProgress();
        return true;
    }

    savestate->Section("RCHV");
    // A handler with nothing to save (no game loaded) saves the empty runtime's progress, as before, which the app can read
    if (savestate->Saving && progressHandler && progressHandler->ProgressSize() > 0)
        return SaveHandlerProgress(savestate);
    if (!savestate->Saving && !savestate->Error && progressHandler)
        return LoadHandlerProgress(savestate);

    if (savestate->Saving)
    {
        u32 rcheevosStateSize = (u32) rc_runtime_progress_size(&rcheevosRuntime, nullptr);
        u8* rcheevosStateBuffer = new u8[rcheevosStateSize];
        int result = rc_runtime_serialize_progress_sized(rcheevosStateBuffer, rcheevosStateSize, &rcheevosRuntime, nullptr);
        if (result != RC_OK)
        {
            delete[] rcheevosStateBuffer;
            return false;
        }

        savestate->Var32(&rcheevosStateSize);
        savestate->VarArray(rcheevosStateBuffer, rcheevosStateSize);
        delete[] rcheevosStateBuffer;
    }
    else if (savestate->Error)
    {
        // RCHV section was not found
        return false;
    }
    else
    {
        u32 rcheevosStateSize;
        savestate->Var32(&rcheevosStateSize);
        u8* rcheevosStateBuffer = new u8[rcheevosStateSize];
        savestate->VarArray(rcheevosStateBuffer, rcheevosStateSize);

        int result = rc_runtime_deserialize_progress(&rcheevosRuntime, rcheevosStateBuffer, nullptr);
        delete[] rcheevosStateBuffer;

        if (result != RC_OK)
            return false;
    }

    return true;
}

bool RetroAchievementsManager::SaveHandlerProgress(Savestate* savestate)
{
    size_t size = progressHandler->ProgressSize();
    if (size > UINT32_MAX)
        return false;

    progressBuffer.resize(size);
    if (!progressHandler->SerializeProgress(progressBuffer.data(), size))
        return false;

    u32 stateSize = (u32) size;
    savestate->Var32(&stateSize);
    savestate->VarArray(progressBuffer.data(), stateSize);
    return !savestate->Error;
}

bool RetroAchievementsManager::LoadHandlerProgress(Savestate* savestate)
{
    u32 stateSize = 0;
    savestate->Var32(&stateSize);
    if (savestate->Error)
        return false;

    // The size comes from the file: one past the rest of the state is a damaged section, whose progress is dropped
    if (stateSize > savestate->BufferLength() - savestate->Length())
    {
        Platform::Log(Platform::LogLevel::Warn, "RCHV section claims %u bytes, more than the state has; achievement progress reset\n", stateSize);
        progressHandler->ResetProgress();
        return true;
    }

    progressBuffer.resize(stateSize);
    savestate->VarArray(progressBuffer.data(), stateSize);
    if (savestate->Error)
        return false;

    if (stateSize == 0 || !progressHandler->DeserializeProgress(progressBuffer.data(), stateSize))
    {
        Platform::Log(Platform::LogLevel::Info, "The state's achievement progress doesn't fit the game loaded; it's reset\n");
        progressHandler->ResetProgress();
    }
    return true;
}

void RetroAchievementsManager::SetProgressHandler(ProgressHandler* handler)
{
    std::unique_lock lock(runtimeLock);
    progressHandler = handler;
}

void RetroAchievementsManager::Reset()
{
    std::unique_lock lock(runtimeLock);
    rc_runtime_reset(&rcheevosRuntime);
    if (progressHandler)
        progressHandler->ResetProgress();
    FlushPeekWarnings();
}

void RetroAchievementsManager::FrameUpdate()
{
    std::unique_lock lock(runtimeLock);
    std::unique_lock instanceLock(activeInstanceLock);

    activeInstance = this;
    rc_runtime_do_frame(&rcheevosRuntime, &CheevosEventHandler, &PeekMemory, this, nullptr);
    activeInstance = nullptr;
}

void RetroAchievementsManager::CheevosEventHandler(const rc_runtime_event_t* runtime_event)
{
    auto eventMessenger = RetroAchievementsManager::EventMessenger.lock();
    if (!eventMessenger)
        return;

    switch (runtime_event->type)
    {
        case RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED:
            Platform::Log(Platform::LogLevel::Debug, "Achievement triggered: %d", runtime_event->id);
            eventMessenger->onAchievementTriggered(runtime_event->id);
            break;
        case RC_RUNTIME_EVENT_ACHIEVEMENT_PRIMED:
            Platform::Log(Platform::LogLevel::Debug, "Achievement primed: %d", runtime_event->id);
            eventMessenger->onAchievementPrimed(runtime_event->id);
            break;
        case RC_RUNTIME_EVENT_ACHIEVEMENT_UNPRIMED:
            Platform::Log(Platform::LogLevel::Debug, "Achievement unprimed: %d", runtime_event->id);
            eventMessenger->onAchievementUnprimed(runtime_event->id);
            break;
        case RC_RUNTIME_EVENT_ACHIEVEMENT_PROGRESS_UPDATED:
            unsigned int value;
            unsigned int target;

            rc_runtime_get_achievement_measured(&activeInstance->rcheevosRuntime, runtime_event->id, &value, &target);
            // Do not notify of achievements with no progress. Weird, but it happens
            if (value > 0)
            {
                char buffer[32];
                rc_runtime_format_achievement_measured(&activeInstance->rcheevosRuntime, runtime_event->id, buffer, sizeof(buffer));
                Platform::Log(Platform::LogLevel::Debug, "Achievement progress update: %d (%d/%d)", runtime_event->id, value, target);
                eventMessenger->onAchievementProgressUpdated(runtime_event->id, value, target, buffer);
            }
            break;
        case RC_RUNTIME_EVENT_LBOARD_STARTED:
            Platform::Log(Platform::LogLevel::Debug, "Leaderboard attempt started: %d", runtime_event->id);
            eventMessenger->onLeaderboardAttemptStarted(runtime_event->id);
            break;
        case RC_RUNTIME_EVENT_LBOARD_CANCELED:
            Platform::Log(Platform::LogLevel::Debug, "Leaderboard attempt canceled: %d", runtime_event->id);
            eventMessenger->onLeaderboardAttemptCanceled(runtime_event->id);
            break;
        case RC_RUNTIME_EVENT_LBOARD_TRIGGERED:
        {
            std::string formattedValue = GetLeaderboardFormattedValue(runtime_event->id, runtime_event->value);
            Platform::Log(Platform::LogLevel::Debug, "Leaderboard attempt completed: %d (%d|%s)", runtime_event->id, runtime_event->value, formattedValue.c_str());
            eventMessenger->onLeaderboardAttemptCompleted(runtime_event->id, runtime_event->value, formattedValue);
            break;
        }
        case RC_RUNTIME_EVENT_LBOARD_UPDATED:
        {
            std::string formattedValue = GetLeaderboardFormattedValue(runtime_event->id, runtime_event->value);
            eventMessenger->onLeaderboardAttemptUpdated(runtime_event->id, formattedValue);
            break;
        }
    }
}

std::string RetroAchievementsManager::GetLeaderboardFormattedValue(int leaderboardId, int value)
{
    auto leaderboard = std::find_if(activeInstance->loadedLeaderboards.begin(), activeInstance->loadedLeaderboards.end(), [=](RALeaderboard l){ return l.id == leaderboardId; });
    char buffer[32];
    if (leaderboard != activeInstance->loadedLeaderboards.end())
        rc_runtime_format_lboard_value(buffer, sizeof(buffer), value, leaderboard->rcheevosFormat);
    else
        buffer[0] = '\0';

    return buffer;
}

void RetroAchievementsManager::FlushPeekWarnings()
{
    if (unmappedPeekCount > 0)
        Platform::Log(Platform::LogLevel::Warn, "Unknown memory peek region: %u peeks returned 0 this session", unmappedPeekCount);
    if (unsupportedPeekSizeCount > 0)
        Platform::Log(Platform::LogLevel::Warn, "Unsupported memory peek size: %u peeks returned 0 this session", unsupportedPeekSizeCount);

    unmappedPeekCount = 0;
    unsupportedPeekSizeCount = 0;
}

unsigned RetroAchievementsManager::PeekMemory(unsigned address, unsigned numBytes, void* ud)
{
    auto manager = (RetroAchievementsManager*) ud;
    NDS* nds = manager->nds;
    u8* memoryRegion;
    u32 memoryMask;

    // Obtain target region as defined in consoleinfo.c
    unsigned region = address & 0xFF000000;
    switch (region)
    {
        case 0x00000000U: // Main RAM
            memoryRegion = nds->MainRAM;
            memoryMask = nds->MainRAMMask;
            break;
        case 0x01000000U: // Data TCM
            memoryRegion = nds->ARM9.DTCM;
            memoryMask = DTCMPhysicalSize - 1;
            break;
        default:
            // Expected when a pointer chain follows a null or stale pointer (loading, cutscenes), so only log the first one
            if (manager->unmappedPeekCount++ == 0)
                Platform::Log(Platform::LogLevel::Warn, "Unknown memory peek region. Address: 0x%08X (further ones are suppressed this session)", address);
            return 0;
    }

    switch (numBytes)
    {
        case 1:
        case 2:
        case 4:
        {
            // Read byte by byte (little-endian) with the mask applied to each byte, so reads never leave the region
            unsigned value = 0;
            for (unsigned i = 0; i < numBytes; i++)
                value |= (unsigned) memoryRegion[(address + i) & memoryMask] << (i * 8);

            return value;
        }
        default:
            if (manager->unsupportedPeekSizeCount++ == 0)
                Platform::Log(Platform::LogLevel::Warn, "Unsupported memory peek size: %u (further ones are suppressed this session)", numBytes);
            return 0;
    }
}

}
}