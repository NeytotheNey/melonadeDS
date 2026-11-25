// A game session's Lua scripts, as the app's AndroidEmulatorManager (setupLuaScripts, saveLuaData, the LuaScriptsState it keeps,
// enable/disable/restartLuaScript) and EmulatorViewModel (sendLuaUiEvent and the arbiters it feeds, setLuaScriptOptions) have them: the
// enabled scripts read and set up on the instance with their headers, option values, saved data and folder content; the scripts' requests
// picked by the arbiters in lua/requests and camera/LuaWideCameraRequests and handed to the callbacks; the storage snapshots written to
// LuaScriptDataStore on a writer thread of its own (the app's IO-thread save); and what the Mods menu needs (Scripts, Enable, Disable,
// SetOptions).
//
// UI thread, except RequestDataSave (any thread): the messenger's posted events, the boot and stop and the menus all run there, so the
// script state needs no lock of its own
#pragma once

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "camera/LuaWideCameraRequests.h"
#include "lua/LuaScriptDataStore.h"
#include "lua/LuaScriptFolderSync.h"
#include "lua/LuaScriptOption.h"
#include "lua/LuaScriptStorage.h"
#include "lua/requests/LuaEffectRequests.h"
#include "lua/requests/LuaLayoutRequests.h"
#include "lua/requests/LuaUiOverrides.h"

namespace MelonDSAndroid { class MelonInstance; }

namespace Frontend
{

// A session script's header data, running state, last error and option values (the app's LuaScriptStatus), with its [Id]: the scripts sent
// to the instance first, in launch order (the id native uses), then the ones that couldn't be read, which native never sees
struct LuaScriptStatus
{
    int Id = 0;
    std::string Name;
    std::optional<std::string> Description;
    bool IsRunning = false;
    // false for a script that couldn't be read at launch, which can't be switched on
    bool WasRead = true;
    std::optional<std::string> LastError;
    std::vector<LuaScriptOption> Options;
    std::map<std::string, std::string> OptionValues;
};

// the screen effects the scripts ask for now, each none when no running script asks for it
struct LuaEffects
{
    std::optional<TiltShift> TiltShiftEffect;
    std::optional<Bloom> BloomEffect;
    std::optional<DepthOfField> DepthOfFieldEffect;
    std::optional<Shade> ShadeEffect;
    std::optional<Sun> SunEffect;

    bool operator==(const LuaEffects&) const = default;
};

// What a launch runs: the enabled scripts in order with how they read, each one's stored option values by file name, the folder whose
// modules and images they get (none for none) with the game's scripts in it (its whole list, enabled or not, which aren't modules), the key their saved data is kept under (none: it lasts for the session only), whether
// melon.state may load states (the app's areSaveStateLoadsAllowed), and whether option changes are saved to the game's settings (not for
// the command line's --lua-script, whose scripts aren't the game's)
struct LuaLaunch
{
    std::vector<std::pair<std::string, LuaScriptStorage::ReadResult>> Scripts;
    std::map<std::string, std::map<std::string, std::string>> StoredOptions;
    std::optional<std::filesystem::path> ContentFolder;
    std::set<std::string> ContentScriptPaths;
    std::optional<std::string> DataGameKey;
    bool AreStateLoadsAllowed = true;
    bool SavesOptions = true;
};

class LuaSession
{
public:
    // UI thread; any may be empty
    struct Callbacks
    {
        // the effects changed (each effect's arbiter, as the app's luaTiltShift and the other flows)
        std::function<void(const LuaEffects& effects)> OnEffectsChanged;
        // the layout a script holds now, or none for the player's
        std::function<void(const std::optional<std::string>& layoutId)> OnLayoutChanged;
        // what the scripts ask of the WideMelon camera now
        std::function<void(const WideCameraRequest& request)> OnWideCameraChanged;
        // the swap or fast forward as the screen shows it now, and set for a script (not the player's change: not stored)
        std::function<bool(LuaUiSetting setting)> GetUiSetting;
        std::function<void(LuaUiSetting setting, bool value)> ApplyUiSetting;
        // whether a layout with that id exists, so a script asking for a deleted one changes nothing
        std::function<bool(const std::string& layoutId)> LayoutExists;
        // a script was switched off by an error (the app's "Script x was switched off: error" toast)
        std::function<void(const std::string& scriptName, const std::string& error)> OnScriptSwitchedOff;
        // a script's options changed from the menu: [fileName]'s stored values merged with [values] in the game's settings
        std::function<void(const std::string& fileName, const std::map<std::string, std::string>& values)> SaveOptions;
        // a script loaded a state with melon.state (the app's RAEventUi.Reset, which the desktop has no achievements for yet)
        std::function<void()> OnStateLoaded;
    };

    // [profileDir] holds the scripts' saved data (lua-data)
    explicit LuaSession(const std::filesystem::path& profileDir);
    ~LuaSession();
    LuaSession(const LuaSession&) = delete;
    LuaSession& operator=(const LuaSession&) = delete;

    Callbacks Hooks;

    LuaScriptDataStore& DataStore() { return Store; }

    // A session starts, before the instance's emulator thread: the scripts' arbiters with the player's swap as their baseline, then
    // [launch]'s scripts read into [instance] (MelonInstance::setLuaScripts, which its start() loads), and the ones that couldn't be read
    // reported as stopped. No scripts clears what an earlier session left
    void Start(MelonDSAndroid::MelonInstance& instance, const LuaLaunch& launch, bool screensSwapped);
    // the session ended, after the instance stopped (its scripts' last storage is in the outbox): saved, forgotten, and what the scripts
    // changed put back
    void End();

    // the messenger's events
    void OnScriptStopped(int scriptId, const std::string& error);
    void OnUiRequest(LuaUiSetting setting, int scriptId, bool value);
    void OnLayoutRequest(int scriptId, const std::optional<std::string>& optionKey);
    void OnTiltShift(int scriptId, const TiltShift& request);
    void OnBloom(int scriptId, const Bloom& request);
    void OnDepthOfField(int scriptId, const DepthOfField& request);
    void OnShade(int scriptId, const Shade& request);
    void OnSun(int scriptId, const Sun& request);
    void OnWideCamera(int scriptId, const WideCameraRequest& request);
    void OnStateLoaded();
    // any thread: snapshots are waiting in the outbox, which the writer thread saves
    void RequestDataSave();
    // writes the waiting snapshots now, on this thread
    void SaveData();

    // the player changed [setting] by hand (a hotkey, the pause menu), so no script holds it anymore
    void OnPlayerChanged(LuaUiSetting setting);
    // the player picked a layout by hand: no script holds the layout anymore (OnLayoutChanged gets none if one did)
    void OnPlayerPickedLayout();
    // the layout a script holds now, or none for the player's
    std::optional<std::string> LayoutId() const { return LayoutRequests.LayoutId(); }
    // what scripts hold [setting] at, or none
    std::optional<bool> ScriptValue(LuaUiSetting setting) const { return UiOverrides.ScriptValue(setting); }
    const LuaEffects& Effects() const { return CurrentEffects; }

    // The Mods menu's (the app's EmulatorViewModel and AndroidEmulatorManager): whether this session loaded any script (the pause menu's
    // Mods entry shows while it did, even when all were switched off since), whether one runs, the scripts and one of them by id
    bool HasScripts() const { return !State.Names.empty(); }
    bool AreScriptsRunning() const { return !State.RunningIds.empty(); }
    std::vector<LuaScriptStatus> Scripts() const;
    std::optional<LuaScriptStatus> Script(int scriptId) const;
    // switches a running script off for this session only (the stored switch stays)
    void Disable(int scriptId);
    // switches a stopped script back on for this session only; false if it can't run (it couldn't be read) or already runs
    bool Enable(int scriptId);
    // restarts (or starts) the script with [values] resolved against its options, and saves them to the game's settings as the pause
    // menu's Options dialog does; false if it can't run
    bool SetOptions(int scriptId, const std::map<std::string, std::string>& values);

    // [scripts]'s entry for [fileName] with [values] over its stored options (the app's `it.options + savedValues`); false if nothing changed
    static bool MergeOptions(std::vector<RomLuaScript>& scripts, const std::string& fileName, const std::map<std::string, std::string>& values);

private:
    // the app's LuaScriptsState
    struct ScriptsState
    {
        std::vector<std::string> Names;
        // how many of Names were sent to native
        int LoadedCount = 0;
        std::set<int> RunningIds;
        // the errors that switched scripts off, by id
        std::map<int, std::string> Errors;
        // from each script's header, read from the source sent at launch; none for the ones that couldn't be read
        std::vector<std::optional<LuaScriptHeader>> Headers;
        // the option values each script runs with: set at launch and changed by SetOptions
        std::vector<std::map<std::string, std::string>> OptionValues;
    };

    LuaScriptStatus StatusOf(int scriptId) const;
    // the arbiters' share of the app's sendLuaUiEvent: a script stopped (also by the player), started, the session ended
    void ScriptStoppedForRequests(int scriptId);
    void ScriptStartedForRequests(int scriptId);
    void EndRequests();
    void ApplyUiSettings(const std::map<LuaUiSetting, bool>& values);
    void ApplyUiSetting(LuaUiSetting setting, bool value);
    void PublishEffects();
    void PublishLayout(std::optional<std::string> layoutId);
    void RunWriter();

    LuaScriptDataStore Store;
    ScriptsState State;
    bool SavesOptions = false;
    // native's instance while a session runs, for switching its scripts
    MelonDSAndroid::MelonInstance* Instance = nullptr;

    LuaUiOverrides UiOverrides;
    LuaLayoutRequests LayoutRequests;
    LuaTiltShiftRequests TiltShiftRequests;
    LuaBloomRequests BloomRequests;
    LuaDepthOfFieldRequests DepthOfFieldRequests;
    LuaShadeRequests ShadeRequests;
    LuaSunRequests SunRequests;
    LuaWideCameraRequests WideCameraRequests;
    LuaEffects CurrentEffects;
    std::optional<std::string> CurrentLayoutId;

    // drained and written under DataLock, so an older snapshot can't overwrite a newer one (the app's luaDataLock)
    std::mutex DataLock;
    // the writer thread's wake-up: guards SaveWanted and Stopping
    std::mutex WriterLock;
    std::condition_variable WriterWake;
    bool SaveWanted = false;
    bool Stopping = false;
    std::thread Writer;
};

}
