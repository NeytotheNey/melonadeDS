// RetroAchievements on the desktop: one rc_client (rcheevos' client, which loads a game's achievements, leaderboards and rich presence
// from the server, runs them each frame and sends unlocks and scores back), with what it needs from this frontend:
//
// - Its server calls, queued for a pool of two worker threads of its own with their own Http::Client (POSTs to dorequest.php). Each
//   request's callback runs exactly once, on a worker (one at a time), also on a network failure (as rc_client's retryable error, so its own
//   retries and disconnect events work) and at Close() (as a plain error, before the client goes).
// - The user: a password login (whose token is handed to Hooks::SaveToken), a token login at Open() with the saved one, an expired token
//   (LoginState::Expired, the user name kept, the token cleared), and the logout (the token cleared, the game unloaded).
// - A session per game run: BeginSession() right after the emulator thread starts loads the game by its RetroAchievements hash, with
//   hardcore fixed for the session (logged in, or logging in, at its start, and ra_hardcore_enabled or --ra-hardcore); EndSession()
//   unloads it after the thread stopped. A login made mid-game loads the running game in softcore, and a logout mid-game unloads it, both
//   on the emulator thread (Hooks::PostToEmulator) so they can't race a frame. Unofficial, encore and spectator modes are off; rc_client
//   itself keeps leaderboards off in softcore. The rich presence goes out with each ping unless ra_rich_presence is off in softcore.
// - The emulator thread: DoFrame() after each frame and Idle() while it's parked (rc_client_do_frame and rc_client_idle), the only places
//   rc_client reads the console's memory (background reads are off), and the savestates' progress (it's the instance's ProgressHandler).
// - Events: rc_client's (from the emulator thread or a worker) and Achievements' own (login, game loaded, ...) are copied into
//   AchievementEvent (AchievementsLogic.h) and queued; the UI thread takes them with DrainEvents().
//
// Threads: the UI thread owns the object and calls everything not marked otherwise. Client() is for the UI's queries, see there.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "Http.h"
#include "achievements/AchievementsLogic.h"
#include "rc_client.h"
#include "retroachievements/ProgressHandler.h"
#include "version.h"

namespace Frontend
{

class Achievements : public MelonDSAndroid::RetroAchievements::ProgressHandler
{
public:
    enum class LoginState { LoggedOut, LoggingIn, LoggedIn, Expired, Failed };

    // The user as the UI shows them; a copy
    struct User
    {
        LoginState State = LoginState::LoggedOut;
        std::string UserName;      // ra_username, also while logged out (expired, failed)
        std::string DisplayName;
        std::string AvatarUrl;
        uint32_t Score = 0;        // hardcore points
        uint32_t SoftcoreScore = 0;
        std::string Message;       // why the last login failed (Failed)
    };

    // The running game's session; a copy
    struct Session
    {
        bool Active = false;       // between BeginSession() and EndSession()
        bool Hardcore = false;     // fixed at its start
        bool Loaded = false;       // rc_client has the game (achievements may still be none)
        uint32_t GameId = 0;
        std::string Title, IconUrl;
    };

    // What Open() is given: the settings and the command line's run-only flags
    struct Config
    {
        bool Disabled = false;               // --no-ra: no client at all this run
        std::string Host;                    // --ra-host: rc_client_set_host (a fake server); empty for retroachievements.org
        std::string UserName;                // ra_username
        std::string Token;                   // desktop_ra_token, already decrypted; empty for none
        std::string RunUserName, RunToken;   // --ra-user and --ra-token: logged in with for the run, never saved
        std::optional<bool> HardcoreRun;     // --ra-hardcore, over ra_hardcore_enabled for every session of the run
        bool RichPresence = true;            // ra_rich_presence
    };

    // What Achievements asks of main; SaveToken and ClearToken always run on the UI thread (posted there when a worker asks)
    struct Hooks
    {
        // runs [task] on the UI thread at its next turn (DesktopEventMessenger::Post); any thread
        std::function<void(std::function<void()>)> PostToUi;
        // runs [task] on the emulator thread between frames (EmulatorThread::Post) while a session is active; any thread (the UI thread's
        // logout, a worker's mid-game login)
        std::function<void(std::function<void()>)> PostToEmulator;
        // a password login's user name and token, to keep (ra_username, and desktop_ra_token through Secret.h)
        std::function<void(const std::string& userName, const std::string& token)> SaveToken;
        // the saved token isn't any good any more (expired, or a logout); the user name stays
        std::function<void()> ClearToken;
    };

    // The user agent's product, followed by rc_client's clause (rc_client_get_user_agent_clause)
    static constexpr const char* USER_AGENT_PRODUCT = "melonadeDS-desktop/" MELONADE_VERSION_NAME;

    Achievements() = default;
    ~Achievements() override;
    Achievements(const Achievements&) = delete;
    Achievements& operator=(const Achievements&) = delete;

    // The client and its workers, and the token login when there's a token (the run's, else the saved one); nothing with Disabled
    void Open(const Config& config, Hooks hooks);
    // Waits a moment for the requests in flight (an unlock), cancels the rest (their callbacks get an error), stops the workers and
    // destroys the client; after the emulator thread stopped and EndSession()
    void Close();
    bool IsEnabled() const { return Client_ != nullptr; }

    void LoginWithPassword(const std::string& userName, const std::string& password);
    void Logout();
    User GetUser() const;

    // ra_rich_presence, for the pings from now on; any thread
    void SetRichPresence(bool enabled) { RichPresence = enabled; }

    // Right after the emulator thread started: the session for the game with [raHash] (none when empty: nothing loads, also after a
    // login), hardcore when [hardcoreSetting] (ra_hardcore_enabled; --ra-hardcore over it) and the user is logged in or logging in
    void BeginSession(const std::string& raHash, bool hardcoreSetting);
    // Whether a session BeginSession() started now would be hardcore, by the same rule (for what's decided before the emulator thread
    // starts, the Lua scripts); [hardcoreSetting] as BeginSession's. Any thread
    bool WouldStartHardcore(bool hardcoreSetting) const;
    // The running session is hardcore: active, hardcore at its start, and the login hasn't failed, expired or been logged out since (no
    // state loads, no Lua). Any thread
    bool IsHardcoreSession() const;
    // --ra-hardcore, when the run has it (over ra_hardcore_enabled)
    std::optional<bool> HardcoreRunOverride() const { return HardcoreRun; }
    // After the emulator thread stopped (no DoFrame() can run): the game unloaded
    void EndSession();
    Session GetSession() const;
    // an unlock or a score that couldn't be sent yet waits (between rc_client's disconnected and reconnected events); any thread
    bool HasPendingUnlocks() const { return PendingUnlocks.load(); }

    // Emulator thread: after the frame numbered [frame] / while parked between frames, with the console's memory as it is then
    // (MemoryViewOf in main.cpp: main RAM and the ARM9's DTCM)
    void DoFrame(const NdsMemoryView& memory, uint32_t frame);
    void Idle(const NdsMemoryView& memory);

    // ProgressHandler, for MelonInstance::setAchievementsProgressHandler: the RCHV section is rc_client's progress; emulator thread
    size_t ProgressSize() override;
    bool SerializeProgress(uint8_t* buffer, size_t size) override;
    bool DeserializeProgress(const uint8_t* buffer, size_t size) override;
    void ResetProgress() override;

    // UI thread: the events queued since the last call, oldest first, in [out] (cleared first; its buffer is reused for the next ones)
    void DrainEvents(std::vector<AchievementEvent>& out);

    // The client, null when disabled, for the UI's lists. rc_client locks itself in rc_client_create_achievement_list,
    // rc_client_create_leaderboard_list, rc_client_create_subset_list, rc_client_has_achievements, rc_client_has_leaderboards and
    // rc_client_get_rich_presence_message, so the UI thread may call those (and destroy the lists they make). The lists point into the
    // game, though, which a logout mid-game frees on the emulator thread: read them under HoldGame(). rc_client_get_game_info,
    // rc_client_get_user_info, rc_client_get_achievement_info and rc_client_get_user_game_summary return or read the client's own data without
    // its lock while a worker or the emulator thread may change or free it: use GetUser(), GetSession() and the events instead. Nothing
    // that loads, unloads or logs in goes through here
    rc_client_t* Client() const { return Client_; }
    // Holds off everything that unloads or replaces the game (CallbackLock) until the lock goes: around making a list, copying what's
    // needed out of it and destroying it. Not while calling Logout(), BeginSession() or EndSession(), which take it. UI thread
    std::unique_lock<std::mutex> HoldGame() { return std::unique_lock(CallbackLock); }

private:
    struct Job
    {
        std::string Url, Body, ContentType, Api;
        rc_client_server_callback_t Callback = nullptr;
        void* CallbackData = nullptr;
    };

    static void ServerCall(const rc_api_request_t* request, rc_client_server_callback_t callback, void* callbackData, rc_client_t* client);
    static uint32_t ReadMemory(uint32_t address, uint8_t* buffer, uint32_t numBytes, rc_client_t* client);
    static void HandleEvent(const rc_client_event_t* event, rc_client_t* client);
    static void LogMessage(const char* message, const rc_client_t* client);
    static void OnPasswordLogin(int result, const char* error, rc_client_t* client, void* userdata);
    static void OnTokenLogin(int result, const char* error, rc_client_t* client, void* userdata);
    static void OnGameLoaded(int result, const char* error, rc_client_t* client, void* userdata);

    void Enqueue(Job job);
    void WorkerLoop();
    // [job]'s callback with [response]; one at a time (CallbackLock), so the copies after it see what it changed
    void Finish(const Job& job, const Http::Response& response);
    // a login's end: the user, the event, the token kept or cleared, and a session that waits for a login loaded
    void FinishLogin(int result, const char* error, bool withToken);
    // the session isn't hardcore any more (a logout, a login that failed or expired)
    void DropSessionHardcore();
    void Push(AchievementEvent event);
    // [memory] and this thread as the one rc_client may read through, around a call into it
    void BeginMemoryAccess(const NdsMemoryView& memory);
    void EndMemoryAccess();

    rc_client_t* Client_ = nullptr;
    Hooks Hook;
    // set at Open(), then read only
    std::optional<bool> HardcoreRun;

    // the workers' queue (guarded by QueueLock) and their HTTP client; Stopping makes the workers answer every job with an error
    std::unique_ptr<Http::Client> Http_;
    std::mutex QueueLock;
    std::condition_variable QueueWake;
    std::deque<Job> Queue;
    int InFlight = 0;
    bool Stopping = false;
    std::vector<std::thread> Workers;
    // the workers' calls into rc_client, one at a time, and everything that unloads or replaces the game (EndSession, the logout, a
    // load), which rc_client doesn't order against a reply's callback; taken before any other lock here
    std::mutex CallbackLock;

    // the user and the session: UserLock guards UserInfo; SessionLock guards SessionInfo, the session's hash, number and whether its load
    // started, and orders a login's load against EndSession()
    mutable std::mutex UserLock;
    User UserInfo;
    // logged in with --ra-token: its token is never saved, and a logout or its expiry leaves the saved one alone; under UserLock
    bool RunTokenLogin = false;
    mutable std::mutex SessionLock;
    Session SessionInfo;
    std::string SessionHash;
    bool SessionLoadStarted = false;
    // counted up by each BeginSession, so a task made for one session can tell (IsSameSession) when it runs in another
    uint32_t SessionNumber = 0;

    std::atomic_bool RichPresence = true;
    // the session's hardcore, for the ping shim on the workers
    std::atomic_bool SessionHardcore = false;
    std::atomic_bool PendingUnlocks = false;
    // the last frame the emulator thread ran, for the events
    std::atomic<uint32_t> LastFrame = 0;

    // the thread rc_client may read memory on (the emulator thread inside DoFrame() or Idle()), and its view, which only that thread
    // touches
    std::atomic<std::thread::id> MemoryThread;
    NdsMemoryView Memory;

    // the events for the UI thread, guarded by EventLock; capped, so a UI that doesn't drain can't grow it for ever
    std::mutex EventLock;
    std::vector<AchievementEvent> Events;
    uint32_t DroppedEvents = 0;
};

}
