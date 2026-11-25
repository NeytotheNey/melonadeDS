#ifndef RETROACHIEVEMENTSMANAGER_H
#define RETROACHIEVEMENTSMANAGER_H

#include <list>
#include <vector>
#include "MelonEventMessenger.h"
#include "NDS.h"
#include "ProgressHandler.h"
#include "RAAchievement.h"
#include "RALeaderboard.h"
#include "rcheevos.h"
#include "Savestate.h"

namespace MelonDSAndroid
{
namespace RetroAchievements
{

class RetroAchievementsManager
{
public:
    RetroAchievementsManager(melonDS::NDS* nds);
    ~RetroAchievementsManager();
    bool LoadAchievements(std::list<RAAchievement> achievements);
    bool LoadLeaderboards(std::list<RALeaderboard> leaderboards);
    void UnloadEverything();
    void SetupRichPresence(std::string richPresenceScript);
    std::string GetRichPresenceStatus();
    std::vector<RARuntimeAchievement> GetRuntimeAchievements();
    bool DoSavestate(melonDS::Savestate* savestate);
    void Reset();
    void FrameUpdate();
    /**
     * [handler] (null for none, the default) owns the RCHV section from now on: its progress is saved, a state's is handed to it (a state
     * without the section, or with progress it can't take, resets it instead of failing), and Reset() resets it too. It must outlive the
     * manager or be cleared first; emulator thread, or before it starts
     */
    void SetProgressHandler(ProgressHandler* handler);

    static void CheevosEventHandler(const rc_runtime_event_t* runtime_event);

    static std::weak_ptr<MelonEventMessenger> EventMessenger;

private:

    static std::string GetLeaderboardFormattedValue(int leaderboardId, int value);
    static unsigned PeekMemory(unsigned address, unsigned numBytes, void* ud);
    void FlushPeekWarnings();
    // DoSavestate()'s RCHV section with the progress handler, the lock held
    bool SaveHandlerProgress(melonDS::Savestate* savestate);
    bool LoadHandlerProgress(melonDS::Savestate* savestate);

    melonDS::NDS* nds;
    rc_runtime_t rcheevosRuntime;
    std::mutex runtimeLock;

    std::list<RAAchievement> loadedAchievements;
    std::list<RALeaderboard> loadedLeaderboards;
    bool isRichPresenceEnabled;

    // Failed peeks, logged once per session and counted; guarded by runtimeLock
    unsigned unmappedPeekCount = 0;
    unsigned unsupportedPeekSizeCount = 0;

    // guarded by runtimeLock; the buffer is kept between states, as the rewind saves one every few frames
    ProgressHandler* progressHandler = nullptr;
    std::vector<melonDS::u8> progressBuffer;

    static RetroAchievementsManager* activeInstance;
    static std::mutex activeInstanceLock;
};

}
}

#endif //RETROACHIEVEMENTSMANAGER_H
