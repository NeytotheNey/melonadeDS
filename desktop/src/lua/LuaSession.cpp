#include "lua/LuaSession.h"

#include <algorithm>
#include <cstdio>
#include <memory>

#include "ImageFile.h"
#include "MelonInstance.h"
#include "lua/LuaScriptHeaderParser.h"
#include "scripting/LuaDraw.h"
#include "scripting/LuaEngine.h"
#include "scripting/LuaStorage.h"

namespace fs = std::filesystem;

namespace Frontend
{

namespace
{

// the app's lua_script_error_* strings, for a script that couldn't be read
const char* ReadError(LuaScriptStorage::ReadResult::Kind kind)
{
    using Kind = LuaScriptStorage::ReadResult::Kind;
    switch (kind)
    {
    case Kind::FolderNotFound: return "the script folder can't be found";
    case Kind::FileNotFound: return "the file isn't in the script folder";
    case Kind::TooLarge: return "the file is over 1 MB";
    default: return "the file couldn't be read";
    }
}

// the images as WIC reads them (ImageFile.h): the size from the header first, then the pixels as unpremultiplied 0xAARRGGBB
LuaScriptStorage::ImageDecoder MakeImageDecoder()
{
    LuaScriptStorage::ImageDecoder decoder;
    decoder.ReadSize = [](const fs::path& file, int& width, int& height) { return ReadImageSize(file, width, height); };
    decoder.Decode = [](const fs::path& file, int& width, int& height, std::vector<uint32_t>& pixels)
    {
        std::vector<melonDS::u8> rgba;
        if (!ReadImage(file, rgba, width, height))
            return false;
        pixels.resize((size_t)width * height);
        for (size_t i = 0; i < pixels.size(); i++)
        {
            const melonDS::u8* p = &rgba[i * 4];
            pixels[i] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        }
        return true;
    };
    return decoder;
}

// LuaScriptStorage's folder content as native takes it (LuaScriptingJNI.cpp's setupLuaScripts): modules by name, images by path with their
// pixels premultiplied
std::shared_ptr<MelonDSAndroid::LuaFolderContent> ToNative(LuaScriptStorage::FolderContent content)
{
    auto native = std::make_shared<MelonDSAndroid::LuaFolderContent>();
    for (LuaScriptStorage::FolderModule& module : content.Modules)
        native->modules.emplace(module.Name, MelonDSAndroid::LuaFolderContent::Module {module.Path, std::move(module.Source), module.LeftOutReason});
    for (LuaScriptStorage::FolderImage& image : content.Images)
    {
        MelonDSAndroid::LuaFolderContent::Image nativeImage;
        nativeImage.leftOutReason = image.LeftOutReason;
        if (image.Width > 0 && image.Height > 0 && (int64_t)image.Width * image.Height == (int64_t)image.Pixels.size())
        {
            nativeImage.width = image.Width;
            nativeImage.height = image.Height;
            nativeImage.pixels = std::move(image.Pixels);
            for (melonDS::u32& pixel : nativeImage.pixels)
                pixel = MelonDSAndroid::premultiplyColor(pixel);
        }
        else if (nativeImage.leftOutReason.empty())
            nativeImage.leftOutReason = LuaScriptFolderContent::REASON_NOT_AN_IMAGE;
        native->images.emplace(image.Path, std::move(nativeImage));
    }
    return native;
}

// the folder's modules and images for the session, or an empty set when there's no folder or it can't be read
std::shared_ptr<MelonDSAndroid::LuaFolderContent> ReadContent(const std::optional<fs::path>& folder, const std::set<std::string>& scriptPaths)
{
    std::optional<LuaScriptStorage::FolderContent> content =
        folder ? LuaScriptStorage::ReadFolderContent(*folder, scriptPaths, MakeImageDecoder()) : std::nullopt;
    if (!content)
    {
        if (folder)
            fprintf(stderr, "lua: the script folder's modules and images couldn't be read\n");
        printf("lua: folder content: 0 of 0 modules, 0 of 0 images\n");
        return std::make_shared<MelonDSAndroid::LuaFolderContent>();
    }
    if (content->GaveUp)
        fprintf(stderr, "lua: the script folder has too many folders or files: the rest are left out\n");
    size_t modules = std::count_if(content->Modules.begin(), content->Modules.end(), [](const auto& m) { return m.Source.has_value(); });
    size_t images = std::count_if(content->Images.begin(), content->Images.end(), [](const auto& i) { return !i.Pixels.empty(); });
    printf("lua: folder content: %zu of %zu modules, %zu of %zu images\n", modules, content->Modules.size(), images, content->Images.size());
    return ToNative(std::move(*content));
}

// the app's getNativeOptionValues: each declared option's value as native types it
std::vector<MelonDSAndroid::LuaOptionValue> NativeOptions(const std::vector<LuaScriptOption>& options, const std::map<std::string, std::string>& values)
{
    std::vector<MelonDSAndroid::LuaOptionValue> native;
    for (const LuaScriptOption& option : options)
    {
        auto found = values.find(option.Key);
        LuaScriptOptionValue value = option.ToNativeValue(found != values.end() ? found->second : option.Resolve(std::nullopt));
        MelonDSAndroid::LuaOptionValue nativeValue;
        nativeValue.key = value.Key;
        using Type = MelonDSAndroid::LuaOptionValue::Type;
        switch (value.OptionType)
        {
        case LuaScriptOptionType::Boolean: nativeValue.type = Type::Boolean; nativeValue.boolean = value.Integer != 0; break;
        case LuaScriptOptionType::Integer: nativeValue.type = Type::Integer; nativeValue.integer = value.Integer; break;
        case LuaScriptOptionType::Number: nativeValue.type = Type::Number; nativeValue.number = value.Number; break;
        case LuaScriptOptionType::String: nativeValue.type = Type::String; nativeValue.string = value.Text; break;
        case LuaScriptOptionType::Layout: nativeValue.type = Type::Layout; nativeValue.boolean = value.Integer != 0; break;
        }
        native.push_back(std::move(nativeValue));
    }
    return native;
}

}

LuaSession::LuaSession(const fs::path& profileDir)
    : Store(profileDir, [](const std::string& line) { fprintf(stderr, "lua: %s\n", line.c_str()); })
{
    Writer = std::thread([this] { RunWriter(); });
}

LuaSession::~LuaSession()
{
    {
        std::lock_guard guard(WriterLock);
        Stopping = true;
    }
    WriterWake.notify_one();
    Writer.join();
    // anything the writer didn't get to
    SaveData();
}

void LuaSession::RunWriter()
{
    std::unique_lock lock(WriterLock);
    for (;;)
    {
        WriterWake.wait(lock, [this] { return SaveWanted || Stopping; });
        if (Stopping)
            return;
        SaveWanted = false;
        lock.unlock();
        SaveData();
        lock.lock();
    }
}

void LuaSession::RequestDataSave()
{
    {
        std::lock_guard guard(WriterLock);
        SaveWanted = true;
    }
    WriterWake.notify_one();
}

void LuaSession::SaveData()
{
    std::lock_guard guard(DataLock);
    std::vector<MelonDSAndroid::LuaStorage::Snapshot> snapshots = MelonDSAndroid::LuaStorage::drainSnapshots();
    for (const MelonDSAndroid::LuaStorage::Snapshot& snapshot : snapshots)
        Store.Write(snapshot.gameKey, snapshot.scriptName, snapshot.data);
    if (!snapshots.empty())
        printf("lua: storage saved for %zu script(s)\n", snapshots.size());
}

void LuaSession::Start(MelonDSAndroid::MelonInstance& instance, const LuaLaunch& launch, bool screensSwapped)
{
    // before the scripts run, so the player's swap is their first requests' baseline (the app's LuaUiEvent.SessionStarted); the windows
    // show it already
    UiOverrides.OnSessionStarted({{LuaUiSetting::SwapScreens, screensSwapped}});
    Instance = &instance;
    SavesOptions = launch.SavesOptions;
    State = {};
    if (launch.Scripts.empty())
    {
        Store.SetSessionGameKey(std::nullopt);
        return;
    }

    std::vector<std::pair<std::string, std::string>> loaded;
    std::vector<std::pair<std::string, std::string>> failed;
    for (const auto& [fileName, result] : launch.Scripts)
    {
        if (result.ResultKind == LuaScriptStorage::ReadResult::Kind::Success)
            loaded.emplace_back(fileName, result.Source);
        else
        {
            printf("lua: skipping %s: %s\n", fileName.c_str(), LuaScriptStorage::ResultName(result.ResultKind));
            failed.emplace_back(fileName, ReadError(result.ResultKind));
        }
    }
    printf("lua: %zu of %zu enabled scripts loaded\n", loaded.size(), launch.Scripts.size());

    // declarations are read from the source being sent, so they match the script that runs
    for (const auto& [fileName, source] : loaded)
    {
        LuaScriptHeader header = LuaScriptHeaderParser::Parse(source);
        auto stored = launch.StoredOptions.find(fileName);
        State.OptionValues.push_back(ResolveValues(header.Options, stored != launch.StoredOptions.end() ? stored->second
                                                                                                        : std::map<std::string, std::string>()));
        State.Headers.push_back(std::move(header));
        State.Names.push_back(fileName);
    }
    State.LoadedCount = (int)loaded.size();
    for (int id = 0; id < State.LoadedCount; id++)
        State.RunningIds.insert(id);
    for (size_t i = 0; i < failed.size(); i++)
    {
        State.Names.push_back(failed[i].first);
        State.Headers.push_back(std::nullopt);
        State.OptionValues.emplace_back();
        State.Errors[State.LoadedCount + (int)i] = failed[i].second;
    }

    if (!loaded.empty())
    {
        // anything left from the last session is written first, so the data read below is the newest
        SaveData();
        Store.SetSessionGameKey(launch.DataGameKey);
        std::shared_ptr<MelonDSAndroid::LuaFolderContent> content = ReadContent(launch.ContentFolder, launch.ContentScriptPaths);
        std::vector<MelonDSAndroid::LuaScriptSource> sources;
        for (int id = 0; id < State.LoadedCount; id++)
        {
            MelonDSAndroid::LuaScriptSource source;
            source.name = loaded[id].first;
            source.source = std::move(loaded[id].second);
            if (launch.DataGameKey)
                source.savedData = Store.Read(*launch.DataGameKey, source.name).value_or("");
            source.dataGameKey = launch.DataGameKey.value_or("");
            source.options = NativeOptions(State.Headers[id]->Options, State.OptionValues[id]);
            sources.push_back(std::move(source));
        }
        instance.setLuaScripts(std::move(sources), std::move(content), launch.AreStateLoadsAllowed);
    }
    // told like a script that fails to load, so the player sees why it isn't running
    for (size_t i = 0; i < failed.size(); i++)
    {
        ScriptStoppedForRequests(State.LoadedCount + (int)i);
        if (Hooks.OnScriptSwitchedOff)
            Hooks.OnScriptSwitchedOff(failed[i].first, failed[i].second);
    }
}

void LuaSession::End()
{
    EndRequests();
    // written before the next launch reads it, so it's the newest (the app's stopEmulator)
    SaveData();
    State = {};
    Store.SetSessionGameKey(std::nullopt);
    Instance = nullptr;
    SavesOptions = false;
}

void LuaSession::OnScriptStopped(int scriptId, const std::string& error)
{
    State.RunningIds.erase(scriptId);
    State.Errors[scriptId] = error;
    ScriptStoppedForRequests(scriptId);
    if (Hooks.OnScriptSwitchedOff && scriptId >= 0 && scriptId < (int)State.Names.size())
        Hooks.OnScriptSwitchedOff(State.Names[scriptId], error);
}

void LuaSession::OnUiRequest(LuaUiSetting setting, int scriptId, bool value)
{
    bool current = Hooks.GetUiSetting && setting != LuaUiSetting::ShowControls ? Hooks.GetUiSetting(setting) : false;
    ApplyUiSetting(setting, UiOverrides.OnScriptRequest(setting, scriptId, value, current));
}

void LuaSession::OnLayoutRequest(int scriptId, const std::optional<std::string>& optionKey)
{
    if (scriptId < 0 || scriptId >= (int)State.Names.size())
        return;
    const std::optional<LuaScriptHeader>& header = State.Headers[scriptId];
    static const std::vector<LuaScriptOption> NO_OPTIONS;
    // a layout that's gone is caught when it's shown too, so without the callback every layout counts as there
    LuaLayoutRequest request = GetLuaLayoutRequest(header ? header->Options : NO_OPTIONS, State.OptionValues[scriptId], optionKey,
        [this](const std::string& id) { return !Hooks.LayoutExists || Hooks.LayoutExists(id); });
    PublishLayout(LayoutRequests.OnScriptRequest(scriptId, request));
}

void LuaSession::OnTiltShift(int scriptId, const TiltShift& request)
{
    TiltShiftRequests.OnScriptRequest(scriptId, request);
    PublishEffects();
}

void LuaSession::OnBloom(int scriptId, const Bloom& request)
{
    BloomRequests.OnScriptRequest(scriptId, request);
    PublishEffects();
}

void LuaSession::OnDepthOfField(int scriptId, const DepthOfField& request)
{
    DepthOfFieldRequests.OnScriptRequest(scriptId, request);
    PublishEffects();
}

void LuaSession::OnShade(int scriptId, const Shade& request)
{
    ShadeRequests.OnScriptRequest(scriptId, request);
    PublishEffects();
}

void LuaSession::OnSun(int scriptId, const Sun& request)
{
    SunRequests.OnScriptRequest(scriptId, request);
    PublishEffects();
}

void LuaSession::OnWideCamera(int scriptId, const WideCameraRequest& request)
{
    WideCameraRequest current = WideCameraRequests.OnScriptRequest(scriptId, request);
    if (Hooks.OnWideCameraChanged)
        Hooks.OnWideCameraChanged(current);
}

void LuaSession::OnStateLoaded()
{
    if (Hooks.OnStateLoaded)
        Hooks.OnStateLoaded();
}

void LuaSession::OnPlayerChanged(LuaUiSetting setting)
{
    UiOverrides.OnPlayerChanged(setting);
}

void LuaSession::OnPlayerPickedLayout()
{
    PublishLayout(LayoutRequests.OnPlayerChanged());
}

LuaScriptStatus LuaSession::StatusOf(int scriptId) const
{
    LuaScriptStatus status;
    status.Id = scriptId;
    status.Name = State.Names[scriptId];
    const std::optional<LuaScriptHeader>& header = State.Headers[scriptId];
    if (header)
    {
        status.Description = header->Description;
        status.Options = header->Options;
    }
    status.IsRunning = State.RunningIds.contains(scriptId);
    status.WasRead = scriptId < State.LoadedCount;
    auto error = State.Errors.find(scriptId);
    if (error != State.Errors.end())
        status.LastError = error->second;
    status.OptionValues = State.OptionValues[scriptId];
    return status;
}

std::vector<LuaScriptStatus> LuaSession::Scripts() const
{
    std::vector<LuaScriptStatus> statuses;
    for (int id = 0; id < (int)State.Names.size(); id++)
        statuses.push_back(StatusOf(id));
    return statuses;
}

std::optional<LuaScriptStatus> LuaSession::Script(int scriptId) const
{
    if (scriptId < 0 || scriptId >= (int)State.Names.size())
        return std::nullopt;
    return StatusOf(scriptId);
}

void LuaSession::Disable(int scriptId)
{
    if (!State.RunningIds.contains(scriptId) || !Instance)
        return;
    State.RunningIds.erase(scriptId);
    printf("lua: %s switched off by the player\n", State.Names[scriptId].c_str());
    Instance->disableLuaScript(scriptId);
    // native sends no stop event for this one, so what the script changed is undone here
    ScriptStoppedForRequests(scriptId);
}

bool LuaSession::Enable(int scriptId)
{
    // a script that couldn't be read at launch has nothing to run
    if (scriptId < 0 || scriptId >= State.LoadedCount || State.RunningIds.contains(scriptId) || !Instance)
        return false;
    // if loading it fails, the stop event switches it off again with the new error
    State.RunningIds.insert(scriptId);
    State.Errors.erase(scriptId);
    printf("lua: %s switched on by the player\n", State.Names[scriptId].c_str());
    Instance->enableLuaScript(scriptId);
    // before anything it asks for in on_load, which only runs once the emulator thread gets to it
    ScriptStartedForRequests(scriptId);
    return true;
}

bool LuaSession::SetOptions(int scriptId, const std::map<std::string, std::string>& values)
{
    if (scriptId < 0 || scriptId >= State.LoadedCount || !Instance)
        return false;
    bool wasRunning = State.RunningIds.contains(scriptId);
    const std::vector<LuaScriptOption>& options = State.Headers[scriptId]->Options;
    State.OptionValues[scriptId] = ResolveValues(options, values);
    State.RunningIds.insert(scriptId);
    State.Errors.erase(scriptId);
    printf("lua: %s restarted with new options by the player\n", State.Names[scriptId].c_str());
    Instance->restartLuaScript(scriptId, NativeOptions(options, State.OptionValues[scriptId]));
    // native sends no stop event for a restart, so what the script changed is undone here, as if it was switched off and on again
    if (wasRunning)
        ScriptStoppedForRequests(scriptId);
    ScriptStartedForRequests(scriptId);
    if (SavesOptions && Hooks.SaveOptions)
        Hooks.SaveOptions(State.Names[scriptId], State.OptionValues[scriptId]);
    return true;
}

bool LuaSession::MergeOptions(std::vector<RomLuaScript>& scripts, const std::string& fileName, const std::map<std::string, std::string>& values)
{
    bool changed = false;
    for (RomLuaScript& script : scripts)
    {
        if (script.FileName != fileName)
            continue;
        for (const auto& [key, value] : values)
        {
            auto stored = script.Options.find(key);
            if (stored != script.Options.end() && stored->second == value)
                continue;
            script.Options[key] = value;
            changed = true;
        }
    }
    return changed;
}

void LuaSession::ScriptStoppedForRequests(int scriptId)
{
    PublishLayout(LayoutRequests.OnScriptStopped(scriptId));
    TiltShiftRequests.OnScriptStopped(scriptId);
    BloomRequests.OnScriptStopped(scriptId);
    DepthOfFieldRequests.OnScriptStopped(scriptId);
    ShadeRequests.OnScriptStopped(scriptId);
    SunRequests.OnScriptStopped(scriptId);
    PublishEffects();
    WideCameraRequest camera = WideCameraRequests.OnScriptStopped(scriptId);
    if (Hooks.OnWideCameraChanged)
        Hooks.OnWideCameraChanged(camera);
    ApplyUiSettings(UiOverrides.OnScriptStopped(scriptId));
}

void LuaSession::ScriptStartedForRequests(int scriptId)
{
    LayoutRequests.OnScriptStarted(scriptId);
    TiltShiftRequests.OnScriptStarted(scriptId);
    BloomRequests.OnScriptStarted(scriptId);
    DepthOfFieldRequests.OnScriptStarted(scriptId);
    ShadeRequests.OnScriptStarted(scriptId);
    SunRequests.OnScriptStarted(scriptId);
    WideCameraRequests.OnScriptStarted(scriptId);
    UiOverrides.OnScriptStarted(scriptId);
}

void LuaSession::EndRequests()
{
    PublishLayout(LayoutRequests.OnSessionEnded());
    TiltShiftRequests.OnSessionEnded();
    BloomRequests.OnSessionEnded();
    DepthOfFieldRequests.OnSessionEnded();
    ShadeRequests.OnSessionEnded();
    SunRequests.OnSessionEnded();
    PublishEffects();
    WideCameraRequest camera = WideCameraRequests.OnSessionEnded();
    if (Hooks.OnWideCameraChanged)
        Hooks.OnWideCameraChanged(camera);
    ApplyUiSettings(UiOverrides.OnSessionEnded());
}

void LuaSession::ApplyUiSettings(const std::map<LuaUiSetting, bool>& values)
{
    for (const auto& [setting, value] : values)
        ApplyUiSetting(setting, value);
}

// the app's applyLuaUiSetting: changed only if it differs from what the screen shows, without counting as the player's change
void LuaSession::ApplyUiSetting(LuaUiSetting setting, bool value)
{
    // The desktop has no touch controls to show or hide, so show_controls changes nothing; its requests still go through the arbiter, which
    // keeps the other settings' rules the same as the app's
    if (setting == LuaUiSetting::ShowControls || !Hooks.ApplyUiSetting)
        return;
    if (Hooks.GetUiSetting && Hooks.GetUiSetting(setting) == value)
        return;
    Hooks.ApplyUiSetting(setting, value);
}

void LuaSession::PublishEffects()
{
    LuaEffects effects {TiltShiftRequests.Current(), BloomRequests.Current(), DepthOfFieldRequests.Current(), ShadeRequests.Current(),
        SunRequests.Current()};
    if (effects == CurrentEffects)
        return;
    CurrentEffects = effects;
    if (Hooks.OnEffectsChanged)
        Hooks.OnEffectsChanged(CurrentEffects);
}

void LuaSession::PublishLayout(std::optional<std::string> layoutId)
{
    if (layoutId == CurrentLayoutId)
        return;
    CurrentLayoutId = std::move(layoutId);
    if (Hooks.OnLayoutChanged)
        Hooks.OnLayoutChanged(CurrentLayoutId);
}

}
