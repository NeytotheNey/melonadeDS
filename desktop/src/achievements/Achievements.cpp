#include "achievements/Achievements.h"

#include <chrono>
#include <cstdio>

#include "Platform.h"
#include "rc_api_runtime.h"

using namespace melonDS;

namespace Frontend
{

namespace
{

constexpr int WORKER_COUNT = 2;
// Close(): how long the requests in flight (an unlock sent as the game stopped) get before they're cancelled
constexpr auto CLOSE_GRACE = std::chrono::seconds(2);
// the events waiting for the UI thread at most; past it new ones are dropped (and counted in the log)
constexpr size_t MAX_EVENTS = 1024;
// a request's reply at most (a big set's achievementsets is a few hundred KB)
constexpr size_t MAX_REPLY_BYTES = 16u << 20;

Achievements* Owner(const rc_client_t* client)
{
    return (Achievements*)rc_client_get_userdata(client);
}

// a WinHTTP failure as rc_client takes it: the error's text as the body, retryable for the network's failures (so an unlock is tried again
// and the disconnect events come), not for the rest
rc_api_server_response_t FailedResponse(const Http::Response& reply, std::string& body)
{
    body = Http::Describe(reply);
    rc_api_server_response_t response {};
    bool retry = reply.Err == Http::Response::Error::Network || reply.Err == Http::Response::Error::Timeout;
    response.http_status_code = retry ? RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR : RC_API_SERVER_RESPONSE_CLIENT_ERROR;
    response.body = body.c_str();
    response.body_length = body.size();
    return response;
}

// An unlock's reply that says none are left (AchievementsRemaining 0), made safe for a game that's gone: rc_client_award_achievement_
// callback then walks client->game's sets for the mastery without checking there is a game, so the reply goes without the count (rc_api
// reads a missing one as unknown, which skips that walk) and with the scores and the id it uses. True when [response] was changed, [body]
// holding its new text; any other reply takes no path into the game and stays as it is
bool DropMasteryCount(rc_api_server_response_t& response, std::string& body)
{
    rc_api_award_achievement_response_t award;
    int result = rc_api_process_award_achievement_server_response(&award, &response);
    bool last = result == RC_OK && award.response.succeeded && award.achievements_remaining == 0;
    char text[128];
    if (last)
        snprintf(text, sizeof(text), "{\"Success\":true,\"Score\":%u,\"SoftcoreScore\":%u,\"AchievementID\":%u}", award.new_player_score,
            award.new_player_score_softcore, award.awarded_achievement_id);
    rc_api_destroy_award_achievement_response(&award);
    if (!last)
        return false;
    body = text;
    response.body = body.c_str();
    response.body_length = body.size();
    return true;
}

// [text]'s bytes zeroed (a request body: the token, and a login's password), then emptied; through a volatile pointer, so the compiler
// can't drop the stores to a string that's about to go
void Wipe(std::string& text)
{
    volatile char* bytes = text.data();
    for (size_t i = 0; i < text.size(); i++)
        bytes[i] = 0;
    text.clear();
}

}

Achievements::~Achievements()
{
    Close();
}

void Achievements::Open(const Config& config, Hooks hooks)
{
    if (config.Disabled || Client_)
        return;
    Client_ = rc_client_create(ReadMemory, ServerCall);
    if (!Client_)
    {
        Platform::Log(Platform::LogLevel::Error, "ra: rc_client_create failed; no RetroAchievements this run\n");
        return;
    }
    Hook = std::move(hooks);
    HardcoreRun = config.HardcoreRun;
    RichPresence = config.RichPresence;
    rc_client_set_userdata(Client_, this);
    rc_client_enable_logging(Client_, RC_CLIENT_LOG_LEVEL_INFO, LogMessage);
    rc_client_set_event_handler(Client_, HandleEvent);
    // only the emulator thread reads the console, inside DoFrame() and Idle()
    rc_client_set_allow_background_memory_reads(Client_, 0);
    rc_client_set_unofficial_enabled(Client_, 0);
    rc_client_set_encore_mode_enabled(Client_, 0);
    rc_client_set_spectator_mode_enabled(Client_, 0);
    // rc_client starts in hardcore; each session sets its own before its game loads
    rc_client_set_hardcore_enabled(Client_, 0);
    if (!config.Host.empty())
        rc_client_set_host(Client_, config.Host.c_str());

    char clause[128];
    clause[0] = '\0';
    rc_client_get_user_agent_clause(Client_, clause, sizeof(clause));
    Http_ = std::make_unique<Http::Client>(std::string(USER_AGENT_PRODUCT) + " " + clause);
    for (int i = 0; i < WORKER_COUNT; i++)
        Workers.emplace_back([this] { WorkerLoop(); });

    // the run's token over the saved one
    bool run = !config.RunUserName.empty() && !config.RunToken.empty();
    const std::string& userName = run ? config.RunUserName : config.UserName;
    const std::string& token = run ? config.RunToken : config.Token;
    bool login = !userName.empty() && !token.empty();
    {
        std::lock_guard guard(UserLock);
        UserInfo = User();
        UserInfo.UserName = userName;
        UserInfo.State = login ? LoginState::LoggingIn : LoginState::LoggedOut;
        RunTokenLogin = run;
    }
    Platform::Log(Platform::LogLevel::Info, "ra: client ready%s%s\n", config.Host.empty() ? "" : " (a test host)",
        login ? (run ? ", logging in with the run's token" : ", logging in with the saved token") : ", logged out");
    if (login)
        rc_client_begin_login_with_token(Client_, userName.c_str(), token.c_str(), OnTokenLogin, this);
}

void Achievements::Close()
{
    if (!Client_)
        return;
    {
        std::unique_lock lock(QueueLock);
        QueueWake.wait_for(lock, CLOSE_GRACE, [this] { return Queue.empty() && InFlight == 0; });
        Stopping = true;
    }
    QueueWake.notify_all();
    // the requests still in flight fail at once; the workers answer them and everything queued with an error, then leave
    Http_->Cancel();
    for (std::thread& worker : Workers)
        worker.join();
    Workers.clear();
    // what those answers queued in turn, answered here, until nothing's left; the client never hears from a request after it's gone
    for (;;)
    {
        Job job;
        {
            std::lock_guard guard(QueueLock);
            if (Queue.empty())
                break;
            job = std::move(Queue.front());
            Queue.pop_front();
        }
        Wipe(job.Body);
        Http::Response cancelled;
        cancelled.Err = Http::Response::Error::Cancelled;
        Finish(job, cancelled);
    }
    rc_client_destroy(Client_);
    Client_ = nullptr;
    Http_.reset();
    if (DroppedEvents > 0)
        Platform::Log(Platform::LogLevel::Warn, "ra: %u events were dropped (the UI didn't take them)\n", DroppedEvents);
}

void Achievements::LoginWithPassword(const std::string& userName, const std::string& password)
{
    if (!Client_)
        return;
    {
        std::lock_guard guard(UserLock);
        UserInfo = User();
        UserInfo.UserName = userName;
        UserInfo.State = LoginState::LoggingIn;
    }
    rc_client_begin_login_with_password(Client_, userName.c_str(), password.c_str(), OnPasswordLogin, this);
}

void Achievements::Logout()
{
    if (!Client_)
        return;
    bool clearToken;
    {
        std::lock_guard guard(UserLock);
        std::string userName = UserInfo.UserName;
        UserInfo = User();
        UserInfo.UserName = userName;
        clearToken = !RunTokenLogin;
        RunTokenLogin = false;
    }
    bool active;
    {
        std::lock_guard guard(SessionLock);
        active = SessionInfo.Active;
        // logged out, the session stays softcore, also after a login (which loads the game in softcore)
        SessionInfo.Hardcore = false;
        SessionInfo.Loaded = false;
        SessionInfo.GameId = 0;
        SessionInfo.Title.clear();
        SessionInfo.IconUrl.clear();
        // a login after this loads the game again
        SessionLoadStarted = false;
    }
    SessionHardcore = false;
    if (clearToken && Hook.ClearToken)
        Hook.ClearToken();
    // the logout unloads the game, which mustn't race the emulator thread's frame, nor a server reply's callback (CallbackLock)
    auto logout = [this]
    {
        std::lock_guard guard(CallbackLock);
        rc_client_logout(Client_);
    };
    if (active && Hook.PostToEmulator)
        Hook.PostToEmulator(logout);
    else
        logout();
    Platform::Log(Platform::LogLevel::Info, "ra: logged out\n");
}

Achievements::User Achievements::GetUser() const
{
    std::lock_guard guard(UserLock);
    return UserInfo;
}

bool Achievements::WouldStartHardcore(bool hardcoreSetting) const
{
    if (!Client_)
        return false;
    LoginState login;
    {
        std::lock_guard guard(UserLock);
        login = UserInfo.State;
    }
    // BeginSession's rule
    bool signedIn = login == LoginState::LoggedIn || login == LoginState::LoggingIn;
    return signedIn && HardcoreRun.value_or(hardcoreSetting);
}

bool Achievements::IsHardcoreSession() const
{
    if (!Client_)
        return false;
    LoginState login;
    {
        std::lock_guard guard(UserLock);
        login = UserInfo.State;
    }
    if (login != LoginState::LoggedIn && login != LoginState::LoggingIn)
        return false;
    std::lock_guard guard(SessionLock);
    return SessionInfo.Active && SessionInfo.Hardcore;
}

void Achievements::DropSessionHardcore()
{
    {
        std::lock_guard guard(SessionLock);
        SessionInfo.Hardcore = false;
    }
    SessionHardcore = false;
}

void Achievements::BeginSession(const std::string& raHash, bool hardcoreSetting)
{
    if (!Client_)
        return;
    LoginState login;
    {
        std::lock_guard guard(UserLock);
        login = UserInfo.State;
    }
    // A login still on its way at the start counts: the game waits for it (rc_client's AWAIT_LOGIN), and a failed one loads nothing
    bool signedIn = login == LoginState::LoggedIn || login == LoginState::LoggingIn;
    bool hardcore = signedIn && WouldStartHardcore(hardcoreSetting);
    bool load = signedIn && !raHash.empty();
    {
        std::lock_guard guard(SessionLock);
        SessionInfo = Session();
        SessionInfo.Active = true;
        SessionInfo.Hardcore = hardcore;
        SessionHash = raHash;
        SessionLoadStarted = load;
        SessionNumber++;
    }
    SessionHardcore = hardcore;
    rc_client_set_hardcore_enabled(Client_, hardcore ? 1 : 0);
    Platform::Log(Platform::LogLevel::Info, "ra: session for %s, %s%s\n", raHash.empty() ? "a game without a hash" : raHash.c_str(),
        hardcore ? "hardcore" : "softcore", load ? "" : signedIn ? " (nothing to load)" : " (logged out)");
    if (load)
    {
        // a load unloads what was there first
        std::lock_guard guard(CallbackLock);
        rc_client_begin_load_game(Client_, raHash.c_str(), OnGameLoaded, this);
    }
}

void Achievements::EndSession()
{
    if (!Client_)
        return;
    {
        std::lock_guard guard(SessionLock);
        if (!SessionInfo.Active)
            return;
        SessionInfo = Session();
        SessionHash.clear();
        SessionLoadStarted = false;
    }
    SessionHardcore = false;
    std::lock_guard guard(CallbackLock);
    rc_client_unload_game(Client_);
}

Achievements::Session Achievements::GetSession() const
{
    std::lock_guard guard(SessionLock);
    return SessionInfo;
}

void Achievements::BeginMemoryAccess(const NdsMemoryView& memory)
{
    Memory = memory;
    MemoryThread = std::this_thread::get_id();
}

void Achievements::EndMemoryAccess()
{
    MemoryThread = std::thread::id();
}

void Achievements::DoFrame(const NdsMemoryView& memory, uint32_t frame)
{
    if (!Client_)
        return;
    LastFrame = frame;
    BeginMemoryAccess(memory);
    rc_client_do_frame(Client_);
    EndMemoryAccess();
}

void Achievements::Idle(const NdsMemoryView& memory)
{
    if (!Client_)
        return;
    // a game's activation can happen here too (it waits for the emulator thread), so the memory's there as in a frame
    BeginMemoryAccess(memory);
    rc_client_idle(Client_);
    EndMemoryAccess();
}

size_t Achievements::ProgressSize()
{
    return Client_ ? rc_client_progress_size(Client_) : 0;
}

bool Achievements::SerializeProgress(uint8_t* buffer, size_t size)
{
    return Client_ && rc_client_serialize_progress_sized(Client_, buffer, size) == RC_OK;
}

bool Achievements::DeserializeProgress(const uint8_t* buffer, size_t size)
{
    if (!Client_)
        return false;
    if (!buffer || size == 0)
    {
        rc_client_reset(Client_);
        return true;
    }
    return rc_client_deserialize_progress_sized(Client_, buffer, size) == RC_OK;
}

void Achievements::ResetProgress()
{
    if (Client_)
        rc_client_reset(Client_);
}

void Achievements::DrainEvents(std::vector<AchievementEvent>& out)
{
    out.clear();
    std::lock_guard guard(EventLock);
    out.swap(Events);
}

void Achievements::Push(AchievementEvent event)
{
    using T = AchievementEvent::Type;
    std::lock_guard guard(EventLock);
    // a tracker or a progress indicator that changes every frame keeps one waiting update, the newest
    bool update = event.Kind == T::LeaderboardTrackerUpdate || event.Kind == T::ProgressIndicatorUpdate;
    if (update && !Events.empty() && Events.back().Kind == event.Kind && Events.back().Id == event.Id)
    {
        Events.back() = std::move(event);
        return;
    }
    if (Events.size() >= MAX_EVENTS)
    {
        DroppedEvents++;
        return;
    }
    Events.push_back(std::move(event));
}

void Achievements::ServerCall(const rc_api_request_t* request, rc_client_server_callback_t callback, void* callbackData, rc_client_t* client)
{
    Achievements* self = Owner(client);
    Job job;
    // copied: rc_client frees the request as soon as this returns
    job.Url = request->url ? request->url : "";
    job.Body = request->post_data ? request->post_data : "";
    job.ContentType = request->content_type ? request->content_type : "";
    job.Callback = callback;
    job.CallbackData = callbackData;
    job.Api = RequestApi(job.Body);
    // rc_client always sends the rich presence; a softcore session with ra_rich_presence off leaves it out (hardcore always sends it)
    if (job.Api == "ping" && !self->SessionHardcore.load() && !self->RichPresence.load())
        job.Body = StripPingRichPresence(job.Body);
    self->Enqueue(std::move(job));
}

void Achievements::Enqueue(Job job)
{
    // also after Close() stopped the workers (an answer's follow-up request), which then answers it
    {
        std::lock_guard guard(QueueLock);
        Queue.push_back(std::move(job));
    }
    QueueWake.notify_one();
}

void Achievements::WorkerLoop()
{
    for (;;)
    {
        Job job;
        bool stopping;
        {
            std::unique_lock lock(QueueLock);
            QueueWake.wait(lock, [this] { return Stopping || !Queue.empty(); });
            if (Queue.empty())
                return;
            job = std::move(Queue.front());
            Queue.pop_front();
            InFlight++;
            stopping = Stopping;
        }
        Http::Response reply;
        if (stopping)
            reply.Err = Http::Response::Error::Cancelled;
        else
        {
            Http::Request request;
            request.Url = job.Url;
            request.Method = job.Body.empty() ? "GET" : "POST";
            request.Body = job.Body;
            request.ContentType = job.Body.empty() ? "" : job.ContentType;
            // the API's URLs hold nothing secret, but a redirect could take the POST's token elsewhere
            request.FollowRedirects = false;
            request.MaxBytes = MAX_REPLY_BYTES;
            reply = Http_->Send(request);
            Wipe(request.Body);
        }
        // sent: its secrets aren't needed any more
        Wipe(job.Body);
        Finish(job, reply);
        {
            std::lock_guard guard(QueueLock);
            InFlight--;
        }
        QueueWake.notify_all();
    }
}

void Achievements::Finish(const Job& job, const Http::Response& reply)
{
    std::string body;
    rc_api_server_response_t response {};
    if (reply.Err == Http::Response::Error::None)
    {
        body = reply.BodyText();
        response.http_status_code = reply.Status;
        response.body = body.c_str();
        response.body_length = body.size();
    }
    else
        response = FailedResponse(reply, body);
    // the API's name only: the URL and the body have the token or the password
    if (!reply.Ok())
        Platform::Log(Platform::LogLevel::Warn, "ra: %s request: %s\n", job.Api.empty() ? "a" : job.Api.c_str(), Http::Describe(reply).c_str());

    // Every call that unloads or replaces the game (EndSession, Logout, a load) holds this lock too, so a game that's there when a callback
    // starts stays until it returns
    std::lock_guard guard(CallbackLock);
    // an unlock sent before its game was stopped or logged out of: rc_client doesn't cancel it, and its last one would crash
    if (job.Api == "awardachievement" && !rc_client_get_game_info(Client_) && DropMasteryCount(response, body))
        Platform::Log(Platform::LogLevel::Info, "ra: the last unlock's reply came after its game was unloaded\n");
    job.Callback(&response, job.CallbackData);
    // an unlock's reply has the user's new score, which only these callbacks write
    if (job.Api == "awardachievement" && Client_)
    {
        if (const rc_client_user_t* user = rc_client_get_user_info(Client_))
        {
            std::lock_guard userGuard(UserLock);
            if (UserInfo.State == LoginState::LoggedIn)
            {
                UserInfo.Score = user->score;
                UserInfo.SoftcoreScore = user->score_softcore;
            }
        }
    }
}

void Achievements::OnPasswordLogin(int result, const char* error, rc_client_t* client, void* userdata)
{
    ((Achievements*)userdata)->FinishLogin(result, error, false);
}

void Achievements::OnTokenLogin(int result, const char* error, rc_client_t* client, void* userdata)
{
    ((Achievements*)userdata)->FinishLogin(result, error, true);
}

void Achievements::FinishLogin(int result, const char* error, bool withToken)
{
    using T = AchievementEvent::Type;
    // a logout while it was on its way
    if (result == RC_ABORTED)
        return;
    std::string message = error ? error : rc_error_str(result);
    AchievementEvent event;
    event.Frame = LastFrame.load();
    std::string savedUser, savedToken;
    bool clearToken = false;
    if (result == RC_OK)
    {
        const rc_client_user_t* user = rc_client_get_user_info(Client_);
        std::lock_guard guard(UserLock);
        UserInfo = User();
        UserInfo.State = LoginState::LoggedIn;
        if (user)
        {
            UserInfo.UserName = user->username ? user->username : "";
            UserInfo.DisplayName = user->display_name ? user->display_name : "";
            UserInfo.AvatarUrl = user->avatar_url ? user->avatar_url : "";
            UserInfo.Score = user->score;
            UserInfo.SoftcoreScore = user->score_softcore;
            if (!withToken && user->token)
            {
                savedUser = UserInfo.UserName;
                savedToken = user->token;
            }
        }
        if (!withToken)
            RunTokenLogin = false;
        event.Kind = T::LoginSucceeded;
        event.Title = UserInfo.DisplayName;
        event.ImageUrl = UserInfo.AvatarUrl;
        event.Points = UserInfo.Score;
        event.SoftcorePoints = UserInfo.SoftcoreScore;
    }
    else if (withToken && (result == RC_EXPIRED_TOKEN || result == RC_INVALID_CREDENTIALS))
    {
        // the token's no good: logged out with the name kept, and a saved token forgotten
        std::lock_guard guard(UserLock);
        UserInfo.State = LoginState::Expired;
        UserInfo.Message = message;
        clearToken = !RunTokenLogin;
        event.Kind = T::LoginExpired;
        event.Message = message;
    }
    else
    {
        std::lock_guard guard(UserLock);
        UserInfo.State = LoginState::Failed;
        UserInfo.Message = message;
        event.Kind = T::LoginFailed;
        event.Message = message;
        event.Result = result;
    }
    Push(std::move(event));
    // a session that started hardcore while this login was on its way is softcore from here on
    if (result != RC_OK)
        DropSessionHardcore();

    if (!savedToken.empty() && Hook.PostToUi && Hook.SaveToken)
        Hook.PostToUi([this, savedUser, savedToken] { Hook.SaveToken(savedUser, savedToken); });
    if (clearToken && Hook.PostToUi && Hook.ClearToken)
        Hook.PostToUi([this] { Hook.ClearToken(); });
    if (result != RC_OK)
        return;

    // A login during a session that loaded nothing (it started logged out): its game, in softcore, on the emulator thread so it's ordered
    // after a logout posted there
    bool load = false;
    uint32_t session = 0;
    {
        std::lock_guard guard(SessionLock);
        if (SessionInfo.Active && !SessionLoadStarted && !SessionHash.empty())
        {
            SessionLoadStarted = true;
            session = SessionNumber;
            load = true;
        }
    }
    if (!load)
        return;
    auto start = [this, session]
    {
        std::string hash;
        {
            std::lock_guard guard(SessionLock);
            // that game stopped before this ran (the next one's session may have begun, and loads its own game)
            if (!IsSameSession(SessionInfo.Active, SessionNumber, session))
                return;
            SessionInfo.Hardcore = false;
            hash = SessionHash;
        }
        SessionHardcore = false;
        rc_client_set_hardcore_enabled(Client_, 0);
        Platform::Log(Platform::LogLevel::Info, "ra: logged in mid-game: %s loads in softcore\n", hash.c_str());
        rc_client_begin_load_game(Client_, hash.c_str(), OnGameLoaded, this);
    };
    // A load unloads what was there first, so it holds CallbackLock: posted, it takes it; run here, it's inside a reply's callback, which
    // holds it already (a login only succeeds there)
    if (Hook.PostToEmulator)
        Hook.PostToEmulator([this, start]
        {
            std::lock_guard guard(CallbackLock);
            start();
        });
    else
        start();
}

void Achievements::OnGameLoaded(int result, const char* error, rc_client_t* client, void* userdata)
{
    using T = AchievementEvent::Type;
    Achievements* self = (Achievements*)userdata;
    // unloaded (the game stopped, a logout) or loaded again over it
    if (result == RC_ABORTED)
        return;
    AchievementEvent event;
    event.Frame = self->LastFrame.load();
    if (result == RC_OK)
    {
        const rc_client_game_t* game = rc_client_get_game_info(client);
        rc_client_user_game_summary_t summary;
        rc_client_get_user_game_summary(client, &summary);
        event.Id = game ? game->id : 0;
        event.Title = game && game->title ? game->title : "";
        event.ImageUrl = game && game->badge_url ? game->badge_url : "";
        event.Unlocked = summary.num_unlocked_achievements;
        event.Total = summary.num_core_achievements;
        event.Points = summary.points_unlocked;
        event.TotalPoints = summary.points_core;
        event.Hardcore = rc_client_get_hardcore_enabled(client) != 0;
        event.Kind = summary.num_core_achievements > 0 ? T::GameLoaded : T::GameHasNoAchievements;
        std::lock_guard guard(self->SessionLock);
        if (self->SessionInfo.Active)
        {
            self->SessionInfo.Loaded = true;
            self->SessionInfo.GameId = event.Id;
            self->SessionInfo.Title = event.Title;
            self->SessionInfo.IconUrl = event.ImageUrl;
        }
    }
    else if (result == RC_NO_GAME_LOADED)
    {
        // a hash RetroAchievements doesn't know
        event.Kind = T::GameHasNoAchievements;
        event.Message = error ? error : rc_error_str(result);
    }
    else
    {
        event.Kind = T::GameLoadFailed;
        event.Message = error ? error : rc_error_str(result);
        event.Result = result;
        // a later login tries again
        std::lock_guard guard(self->SessionLock);
        self->SessionLoadStarted = false;
    }
    self->Push(std::move(event));
}

uint32_t Achievements::ReadMemory(uint32_t address, uint8_t* buffer, uint32_t numBytes, rc_client_t* client)
{
    Achievements* self = Owner(client);
    // only inside DoFrame() or Idle(), on the emulator thread, with the view that call set
    if (!self || self->MemoryThread.load() != std::this_thread::get_id())
        return 0;
    return ReadNdsMemory(self->Memory, address, buffer, numBytes);
}

void Achievements::HandleEvent(const rc_client_event_t* event, rc_client_t* client)
{
    using T = AchievementEvent::Type;
    Achievements* self = Owner(client);
    AchievementEvent copy;
    if (!self || !event || !CopyClientEvent(*event, self->LastFrame.load(), copy))
        return;
    if (copy.Kind == T::Disconnected)
        self->PendingUnlocks = true;
    else if (copy.Kind == T::Reconnected)
        self->PendingUnlocks = false;
    else if (copy.Kind == T::Reset)
        // only asked for when hardcore is turned on mid-game, which never happens here
        Platform::Log(Platform::LogLevel::Warn, "ra: rc_client asked for a reset (hardcore turned on mid-game?); ignored\n");
    self->Push(std::move(copy));
}

void Achievements::LogMessage(const char* message, const rc_client_t* client)
{
    Platform::Log(Platform::LogLevel::Info, "ra: %s\n", message);
}

}
