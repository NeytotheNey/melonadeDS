// Settings, on six tabs (L / R switch them): Video, Display, Emulation, Audio, Controls and Library (the ROM folders, and the metadata and art
// sync: the RetroAchievements Web API key, the libretro thumbnails, Refresh metadata, Clear downloaded art). Each change goes through Host::ChangeSetting,
// which saves it and applies it at once where the running game allows it; the rest (JIT, the console, the BIOS) wait for the next game
// that starts, and the rows say so. The keys are the app's preference names where it has the setting, desktop_ ones otherwise
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <string>

#include "Icons.h"
#include "Input.h"
#include "Library.h"
#include "MetadataSync.h"
#include "Screens.h"
#include "Settings.h"
#include "Ui.h"
#include "Widgets.h"
#include "layout/Layouts.h"

namespace Ui
{

namespace
{

struct Option { const char* Value; const char* Name; };

const Option kRenderers[] = {{"software", "Software"}, {"opengl", "OpenGL"}, {"compute", "Compute"}};
const Option kThemes[] = {{"system", "System"}, {"light", "Light"}, {"dark", "Dark"}};
const Option kScreens[] = {{"together", "Together"}, {"detached", "Detached (two windows)"}};
const Option kFits[] = {{"aspect", "Keep aspect"}, {"integer", "Integer scale"}, {"stretch", "Stretch"}};
const Option kFullscreen[] = {{"borderless", "Borderless"}, {"exclusive", "Exclusive"}};
// the app's video_filtering list (arrays.xml's video_filtering_values and strings.xml's video_filtering_options), which leaves out the
// enum's SCANLINES; --filter scanlines shows it by name
const Option kFiltering[] = {{"none", "None"}, {"linear", "Linear"}, {"xbr2", "2xBR"}, {"hq2x", "HQ2X"}, {"hq4x", "HQ4X"},
    {"quilez", "Quilez"}, {"lcd", "LCD"}, {"scale2x", "Scale2x"}, {"fxaa", "FXAA"}, {"sharp", "Sharp (no shimmer)"}, {"mmpx", "MMPX"}};
const Option kConsoles[] = {{"ds", "DS"}, {"dsi", "DSi"}};
const Option kLatency[] = {{"low", "Low (Slower)"}, {"medium", "Medium"}, {"high", "High (Faster)"}};
const Option kSpeeds[] = {{"-1", "Unlimited"}, {"1.5", "1.5x"}, {"2", "2x"}, {"3", "3x"}, {"4", "4x"}, {"8", "8x"}};

const char* const kTabNames[] = {"Video", "Display", "Emulation", "Audio", "Controls", "Library"};
const char* const kTabIds[] = {"video", "display", "emulation", "audio", "controls", "library"};
constexpr int kTabCount = 6;

const char* kNextGame = "Applies when a game starts";

template <size_t N> int IndexOf(const Option (&options)[N], const std::string& value)
{
    for (size_t i = 0; i < N; i++)
    {
        if (value == options[i].Value)
            return (int)i;
    }
    return -1;
}

class SettingsScreen : public Screen
{
public:
    explicit SettingsScreen(const std::string& tab)
    {
        for (int i = 0; i < kTabCount; i++)
        {
            if (tab == kTabIds[i])
                TabIndex = i;
        }
    }

    const char* Id() const override { return "settings"; }

    void Tab(App& app, int direction) override
    {
        SetTab(app, (TabIndex + direction + kTabCount) % kTabCount);
    }

    void Clear(App& app) override
    {
        // X on the key's row: the key removed
        if (FocusedKey == "ra_key")
        {
            Host& host = app.GetHost();
            if (host.RaKeySource && !host.RaKeySource().empty() && host.RemoveRaKey)
            {
                host.RemoveRaKey();
                app.Toast("Web API key removed");
            }
            return;
        }
        // X on a folder row: back to none; on a ROM folder, out of the library
        if (FocusedKey == "bios_dir" || FocusedKey == "dsi_bios_dir")
            Change(app, FocusedKey, picojson::value(std::string()));
        else if (FocusedKey.rfind("rom_folder:", 0) == 0)
        {
            std::vector<std::string> folders = Store(app).GetStringList("rom_search_dirs");
            size_t index = (size_t)atoi(FocusedKey.c_str() + 11);
            if (index < folders.size())
            {
                app.Toast("Removed " + folders[index] + " from the library");
                folders.erase(folders.begin() + index);
                SetRomFolders(app, folders);
                // the focus to the row that's there now
                WantFocus = true;
                FocusId = 0;
                FocusRow = folders.empty() ? std::string("addfolder") : "rom_folder:" + std::to_string(std::min(index, folders.size() - 1));
            }
        }
    }

    void Draw(App& app) override
    {
        const Palette& p = app.Colors();
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        float footer = FooterHeight(app);

        if (ScreenHeader(app, "Settings", true))
            app.Pop();
        int clicked = TabStrip(app, ImVec2(win.x + app.Px(40), win.y + app.Px(88) + app.Px(4)), kTabNames, kTabCount, TabIndex);
        if (clicked >= 0 && clicked != TabIndex)
            SetTab(app, clicked);

        // one column, at most 760 wide, centered on a wide window; it scrolls with the focus
        float pad = app.Px(40);
        float colW = std::min(size.x - 2 * pad, app.Px(760));
        float x = win.x + (size.x - colW) / 2;
        float top = win.y + app.Px(88) + app.Px(56);
        float ring = app.Px(12);
        ImGui::SetCursorScreenPos(ImVec2(x - ring, top - ring));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ring, ring));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, app.Px(4)));
        if (ResetScroll)
        {
            ImGui::SetNextWindowScroll(ImVec2(0, 0));
            ResetScroll = false;
        }
        ImGui::BeginChild(kTabIds[TabIndex], ImVec2(colW + 2 * ring, win.y + size.y - footer - top + ring),
            ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
        FocusedKey.clear();
        First = true;
        switch (TabIndex)
        {
        case 0: DrawVideo(app); break;
        case 1: DrawDisplay(app); break;
        case 2: DrawEmulation(app); break;
        case 3: DrawAudio(app); break;
        case 4: DrawControls(app); break;
        default: DrawLibrary(app); break;
        }
        ImGui::Dummy(ImVec2(1, app.Px(24)));
        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        bool folder = FocusedKey == "bios_dir" || FocusedKey == "dsi_bios_dir";
        if (FocusedKey.rfind("rom_folder:", 0) == 0)
            FooterHints(app, {{"A|Enter", "Change folder"}, {"X|Del", "Remove"}, {"L/R|Q/W", "Tabs"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
        else if (FocusedKey == "ra_key")
            FooterHints(app, {{"A|Enter", "Enter a key"}, {"X|Del", "Remove"}, {"L/R|Q/W", "Tabs"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
        else if (folder)
            FooterHints(app, {{"A|Enter", "Choose folder"}, {"X|Del", "Clear"}, {"L/R|Q/W", "Tabs"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
        else
            FooterHints(app, {{"A|Enter", "Change"}, {"\xe2\x80\xb9 \xe2\x80\xba", "Previous / next"}, {"L/R|Q/W", "Tabs"}, {"B|Esc", "Back"}},
                p.SurfaceContainerLowest);
    }

private:
    void SetTab(App& app, int tab)
    {
        TabIndex = tab;
        WantFocus = true;
        FocusId = 0;
        ResetScroll = true;
        printf("ui: settings/%s\n", kTabIds[TabIndex]);
        fflush(stdout);
    }

    Frontend::Settings& Store(App& app) { return *app.GetHost().Settings; }

    void Change(App& app, const std::string& key, const picojson::value& value)
    {
        Host& host = app.GetHost();
        bool live = host.ChangeSetting ? host.ChangeSetting(key, value) : true;
        if (!live && host.GameRunning && host.GameRunning())
            app.Toast("Applies the next time a game starts");
    }

    // the first row of a tab has the focus when it opens (or the row FocusRow names)
    bool TakeFirst(const char* key = nullptr)
    {
        if (!FocusRow.empty())
        {
            bool mine = key && FocusRow == key;
            if (mine)
                FocusRow.clear();
            return mine;
        }
        bool first = First;
        First = false;
        return first;
    }

    void Track(const char* key)
    {
        if (ImGui::IsItemFocused())
            FocusedKey = key;
    }

    template <size_t N> void Choice(App& app, const char* key, ImWchar icon, const char* label, const Option (&options)[N],
        const char* fallback, const char* detail = nullptr)
    {
        int index = IndexOf(options, Store(app).GetString(key, fallback));
        if (index < 0)
            index = std::max(IndexOf(options, fallback), 0);
        std::string id = std::string("##") + key;
        if (int change = ChoiceRow(app, id.c_str(), icon, label, options[index].Name, TakeFirst(), detail))
        {
            index = (index + change + (int)N) % (int)N;
            Change(app, key, picojson::value(std::string(options[index].Value)));
        }
        Track(key);
    }

    void Toggle(App& app, const char* key, ImWchar icon, const char* label, const char* detail, bool fallback)
    {
        bool value = Store(app).GetBool(key, fallback);
        std::string id = std::string("##") + key;
        if (ToggleRow(app, id.c_str(), icon, label, detail, value, TakeFirst()))
            Change(app, key, picojson::value(!value));
        Track(key);
    }

    void Folder(App& app, const char* key, const char* label, const char* files)
    {
        std::string value = Store(app).GetString(key, "");
        std::string detail = value.empty() ? std::string("Not set: ") + files : value;
        std::string id = std::string("##") + key;
        if (DetailRow(app, id.c_str(), Icon::Folder, label, detail.c_str(), TakeFirst()))
        {
            Host& host = app.GetHost();
            if (host.Hidden || !host.PickFolder)
                app.Toast("The folder picker doesn't open in a hidden run");
            else if (std::optional<std::string> picked = host.PickFolder(value))
                Change(app, key, picojson::value(*picked));
        }
        Track(key);
    }

    void Note(App& app, const char* text)
    {
        float w = ImGui::GetContentRegionAvail().x - app.Px(40);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(1, app.Px(6)));
        ImVec2 s = TextAt(app, app.Regular, 14, ImVec2(pos.x + app.Px(20), pos.y + app.Px(6)), app.Colors().OnSurfaceVariant, text, w);
        ImGui::Dummy(ImVec2(1, s.y));
    }

    void DrawVideo(App& app)
    {
        Frontend::Settings& s = Store(app);
        bool soft = s.GetString("video_renderer", "compute") == "software";
        bool gl = s.GetString("video_renderer", "compute") == "opengl";
        SectionLabel(app, "Renderer");
        Choice(app, "video_renderer", Icon::Memory, "Renderer", kRenderers, "compute");
        {
            int scale = std::clamp(s.GetInt("video_internal_resolution", 4), 1, 16);
            char value[16];
            snprintf(value, sizeof(value), "%dx", scale);
            if (int change = ChoiceRow(app, "##video_internal_resolution", Icon::HighQuality, "Internal resolution", value, TakeFirst(),
                    soft ? "OpenGL and Compute only" : nullptr))
            {
                scale = (scale - 1 + change + 16) % 16 + 1;
                Change(app, "video_internal_resolution", picojson::value(std::to_string(scale)));
            }
            Track("video_internal_resolution");
        }
        SectionLabel(app, "Enhancements");
        Toggle(app, "video_hires_capture", Icon::AutoAwesome, "Upscaled display capture",
            "Keeps copied screens, like the White 2 title, sharp when upscaled", false);
        Toggle(app, "video_filter_2d", Icon::BlurOn, "Smooth 2D edges", "Smooths jagged 2D edges at 2x and above", false);
        Toggle(app, "video_sharper_rotated_2d", Icon::Texture, "Sharper rotated 2D", "Draws rotated and scaled 2D at full resolution", false);
        SectionLabel(app, "Accuracy");
        Toggle(app, "video_edge_marking", Icon::ViewInAr, "Edge marking",
            soft ? "OpenGL and Compute (Software always draws it)" : "Draws the outlines games add around 3D models", true);
        Toggle(app, "video_precise_depth", Icon::Layers, "Precise depth (Experimental)", "Fixes overlap flickering in some games", false);
        Toggle(app, "video_gl_seams", Icon::GridOn, "Fix floor seams",
            gl ? "Recommended for Pok\xc3\xa9mon Gen 4 games" : "OpenGL only", false);
        SectionLabel(app, "Image");
        {
            std::string filtering = s.GetString("video_filtering", "none");
            int index = IndexOf(kFiltering, filtering);
            // a filter the list leaves out (--filter scanlines) shows by its name, and steps into the list at either end
            const char* name = index >= 0 ? kFiltering[index].Name : filtering == "scanlines" ? "Scanlines" : kFiltering[0].Name;
            if (int change = ChoiceRow(app, "##video_filtering", Icon::Contrast, "Filter", name, TakeFirst()))
            {
                int count = (int)(sizeof(kFiltering) / sizeof(kFiltering[0]));
                index = index < 0 ? (change > 0 ? 0 : count - 1) : (index + change + count) % count;
                Change(app, "video_filtering", picojson::value(std::string(kFiltering[index].Value)));
            }
            Track("video_filtering");
        }
        Toggle(app, "video_deband", Icon::Tune, "Smooth gradients", "Smooths banding in gradients and backgrounds", false);
        Toggle(app, "video_ds_screen_colors", Icon::Palette, "DS screen colors", "The colors of the DS's own screen", false);
        Toggle(app, "video_screen_shadow", Icon::Monitor, "Screen shadow", "Draws a soft outer shadow around the screens", false);
        SectionLabel(app, "Performance");
        Toggle(app, "enable_jit", Icon::Bolt, "Enable JIT", "Slower than the interpreter on PC. Applies when a game starts", false);
        Note(app, "Everything here but the JIT applies to a running game at once.");
    }

    void DrawDisplay(App& app)
    {
        Frontend::Settings& s = Store(app);
        bool detached = s.GetString("desktop_screens", "together") == "detached";
        SectionLabel(app, "Screens");
        Choice(app, "desktop_screens", Icon::OpenInNew, "Screens", kScreens, "together");
        {
            // the global layout (input_layout_id, the app's key); a game with its own shows that one instead
            Host& host = app.GetHost();
            std::string name = "Default";
            if (host.Layouts)
            {
                std::string id = s.GetString("input_layout_id", "");
                const Frontend::LayoutChoice* c = id.empty() ? nullptr : host.Layouts->Find(id);
                if (!c && id.empty() && s.Find("desktop_layout"))
                    c = host.Layouts->FindByName(s.GetString("desktop_layout", ""));
                name = c ? c->DisplayName : host.Layouts->Default().DisplayName;
            }
            std::string detail = name + (detached ? " (when they're together; its detached layouts when it has them)" : "");
            if (DetailRow(app, "##layout", Icon::Dashboard, "Layout", detail.c_str(), TakeFirst()))
                app.Push(MakeLayoutsScreen(LayoutsFor::Global));
            Track("input_layout_id");
        }
        Toggle(app, "desktop_swap_screens", Icon::SwapVert, "Swap screens", "The bottom screen where the top one is", false);
        SectionLabel(app, "Detached screens");
        Choice(app, "desktop_fit_top", Icon::FitScreen, "Top screen fit", kFits, "aspect");
        Choice(app, "desktop_fit_bottom", Icon::FitScreen, "Bottom screen fit", kFits, "aspect");
        Toggle(app, "desktop_bottom_no_title", Icon::Title, "Bottom window without a title bar", "Drag it by the black bars or with Alt held", false);
        Toggle(app, "desktop_bottom_on_top", Icon::PushPin, "Bottom window always on top", nullptr, false);
        SectionLabel(app, "Window");
        Choice(app, "desktop_fullscreen_mode", Icon::Fullscreen, "Fullscreen", kFullscreen, "borderless", "What F11 does to the main window");
        Toggle(app, "desktop_vsync", Icon::Sync, "VSync", "The main window waits for the monitor", false);
        SectionLabel(app, "Look");
        int theme = std::max(IndexOf(kThemes, s.GetString("desktop_theme", "system")), 0);
        Choice(app, "desktop_theme", theme == 1 ? Icon::LightMode : theme == 2 ? Icon::DarkMode : Icon::BrightnessAuto, "Theme", kThemes, "system");
    }

    void DrawEmulation(App& app)
    {
        Frontend::Settings& s = Store(app);
        bool custom = s.GetBool("use_custom_bios", false);
        SectionLabel(app, "System");
        Choice(app, "console_type", Icon::DeveloperBoard, "Console", kConsoles, "ds",
            "Applies when a game starts. DSi needs its BIOS, firmware and NAND");
        Toggle(app, "use_custom_bios", Icon::SdCard, "Use custom BIOS and firmware",
            custom ? kNextGame : "Off: the built-in FreeBIOS. Applies when a game starts", false);
        Folder(app, "bios_dir", "DS BIOS folder", "bios9.bin, bios7.bin, firmware.bin");
        Folder(app, "dsi_bios_dir", "DSi BIOS folder", "bios9.bin, bios7.bin, firmware.bin, nand.bin");
        Toggle(app, "show_bios", Icon::PlayArrow, "Show boot screen", custom ? kNextGame : "Needs the custom BIOS and firmware", false);
        SectionLabel(app, "Speed");
        Choice(app, "fast_forward_speed_multiplier", Icon::FastForward, "Fast-forward max speed", kSpeeds, "-1");
    }

    void DrawAudio(App& app)
    {
        Frontend::Settings& s = Store(app);
        bool on = s.GetBool("sound_enabled", true);
        SectionLabel(app, "Sound");
        Toggle(app, "sound_enabled", on ? Icon::VolumeUp : Icon::VolumeOff, "Enable sound", nullptr, true);
        {
            int volume = std::clamp(s.GetInt("volume", 256), 0, 256);
            int percent = (int)std::lround(volume * 100.0 / 256);
            char value[16];
            snprintf(value, sizeof(value), "%d%%", percent);
            if (int change = ChoiceRow(app, "##volume", Icon::GraphicEq, "Volume", value, TakeFirst()))
            {
                // 10% steps, wrapping around
                int step = (int)std::lround(percent / 10.0) + change;
                step = (step + 11) % 11;
                Change(app, "volume", picojson::value((double)std::lround(step * 256 / 10.0)));
            }
            Track("volume");
        }
        Choice(app, "audio_latency", Icon::Timer, "Audio latency", kLatency, "medium");
        Toggle(app, "mute_fast_forward_audio", Icon::FastForward, "Mute audio while fast forwarding", nullptr, true);
        Note(app, "Unmuted, fast forward plays its sound sped up at the max speed, or none when that's unlimited.");
    }

    void DrawControls(App& app)
    {
        Host& host = app.GetHost();
        SectionLabel(app, "Input");
        std::string detail = "Keyboard";
        if (host.Input)
        {
            for (const Frontend::Input::Pad& pad : host.Input->Pads())
                detail += ", " + pad.Name;
            if (host.Input->Pads().empty())
                detail += " (no controller connected)";
        }
        if (DetailRow(app, "##input", Icon::SportsEsports, "Buttons and hotkeys", detail.c_str(), TakeFirst()))
            app.Push(MakeInputScreen());
        Track("input");
        Toggle(app, "desktop_rumble", Icon::Vibration, "Controller rumble", "When a game rumbles (with the Rumble Pak)", true);
        Note(app, "The right stick moves a touch cursor over the bottom screen; the right trigger taps it. The mouse touches too.");
    }

    void SetRomFolders(App& app, const std::vector<std::string>& folders)
    {
        picojson::array list;
        for (const std::string& f : folders)
            list.emplace_back(f);
        Change(app, "rom_search_dirs", picojson::value(list));
    }

    // the ROM folders (rom_search_dirs, the app's key), each one changed with A or removed with X, then adding one and a rescan
    void DrawLibrary(App& app)
    {
        Host& host = app.GetHost();
        Frontend::Settings& s = Store(app);
        std::vector<std::string> folders = s.GetStringList("rom_search_dirs");
        SectionLabel(app, "ROM folders");
        for (size_t i = 0; i < folders.size(); i++)
        {
            std::string key = "rom_folder:" + std::to_string(i);
            std::string name = Frontend::PathToUtf8(Frontend::PathFromUtf8(folders[i]).filename());
            if (name.empty())
                name = folders[i];
            std::string id = "##" + key;
            if (DetailRow(app, id.c_str(), Icon::Folder, name.c_str(), folders[i].c_str(), TakeFirst(key.c_str())))
            {
                if (host.Hidden || !host.PickFolder)
                    app.Toast("The folder picker doesn't open in a hidden run");
                else if (std::optional<std::string> picked = host.PickFolder(folders[i]))
                {
                    bool known = false;
                    for (size_t j = 0; j < folders.size(); j++)
                        known = known || (j != i && Frontend::Library::SameFolder(folders[j], *picked));
                    if (known)
                        app.Toast("That folder is in the library already");
                    else
                    {
                        folders[i] = *picked;
                        SetRomFolders(app, folders);
                    }
                }
            }
            if (ImGui::IsItemFocused())
                FocusedKey = key;
        }
        if (MenuRow(app, "##addfolder", Icon::CreateNewFolder, "Add a ROM folder", nullptr, TakeFirst("addfolder")))
        {
            if (host.Hidden || !host.PickFolder)
                app.Toast("The folder picker doesn't open in a hidden run");
            else if (std::optional<std::string> picked = host.PickFolder(folders.empty() ? "" : folders.back()))
            {
                bool known = std::any_of(folders.begin(), folders.end(), [&](const std::string& f) { return Frontend::Library::SameFolder(f, *picked); });
                if (known)
                    app.Toast("That folder is in the library already");
                else
                {
                    folders.push_back(*picked);
                    SetRomFolders(app, folders);
                }
            }
        }
        Track("addfolder");
        SectionLabel(app, "Library");
        std::string status;
        if (host.Library)
        {
            Frontend::Library::Progress progress = host.Library->GetProgress();
            size_t games = host.Library->Entries()->size();
            if (progress.Scanning)
                status = progress.Found ? "Scanning " + std::to_string(progress.Done) + " of " + std::to_string(progress.Found) : std::string("Scanning");
            else
                status = std::to_string(games) + (games == 1 ? " game" : " games");
        }
        if (MenuRow(app, "##rescan", Icon::Refresh, "Rescan the library", status.c_str(), TakeFirst("rescan")) && host.Library)
        {
            host.Library->Rescan();
            app.Toast("Scanning the ROM folders again");
        }
        Track("rescan");
        Note(app, "The library looks in these folders and their subfolders for .nds and .dsi files, and for .zip and .7z archives with "
                  "one in them. A rescan reads only new and changed files. Files that are online-only (OneDrive) are listed without "
                  "being downloaded.");
        if (s.IsOverridden("rom_search_dirs"))
            Note(app, "--rom-folder set these folders for this run: changes here last until it ends.");
        DrawMetadata(app);
    }

    // the metadata and art sync: the Web API key (typed on the keyboard, shown as dots, stored encrypted), its test, the libretro
    // thumbnails, Refresh metadata, Clear downloaded art, and what the sync is doing
    void DrawMetadata(App& app)
    {
        Host& host = app.GetHost();
        Frontend::MetadataSync* sync = host.Metadata;
        // the key typed, stored once the keyboard closes (blank changes nothing, as the app's dialog)
        if (EditingKey && !app.KeyboardOpen())
        {
            EditingKey = false;
            std::string key = KeyBuffer;
            KeyBuffer.assign(KeyBuffer.size(), '\0');
            KeyBuffer.clear();
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            if (!key.empty() && host.SaveRaKey)
            {
                bool stored = host.SaveRaKey(key);
                if (sync)
                    sync->ResetKeyTest();
                if (stored)
                    app.Toast("Web API key saved");
            }
        }
        SectionLabel(app, "Metadata and art");
        // what the sync has done and is doing
        if (sync && host.Library)
        {
            Frontend::MetadataSync::Status st = sync->GetStatus();
            size_t games = 0, withArt = 0;
            for (const Frontend::LibraryEntry& e : *host.Library->Entries())
            {
                if (e.State != Frontend::LibraryEntry::kRead)
                    continue;
                games++;
                withArt += e.ArtFile.empty() ? 0 : 1;
            }
            std::string line = "Art: " + std::to_string(withArt) + " of " + std::to_string(games) + (games == 1 ? " game" : " games");
            if (st.LastSync > 0)
            {
                time_t t = (time_t)(st.LastSync / 1000);
                tm local {};
                localtime_s(&local, &t);
                char when[64];
                strftime(when, sizeof(when), "%b %d, %I:%M %p", &local);
                line += std::string(", last synced ") + when;
            }
            else
                line += ", never synced";
            if (st.Off)
                line += ". Off for this run (--no-sync)";
            else if (st.Running)
                line += ". " + (st.Activity.empty() ? std::string("Syncing") : st.Activity) + "\xe2\x80\xa6";
            else if (st.Waiting)
                line += ". Waiting until the game is closed";
            else if (st.RetryLater)
                line += ". Stopped early, it tries again later" + (st.LastStop.empty() ? std::string() : " (" + st.LastStop + ")");
            else if (!st.LastStop.empty())
                line += ". Last sync: " + st.LastStop;
            Note(app, line.c_str());
        }
        std::string source = host.RaKeySource ? host.RaKeySource() : "";
        std::string keyDetail = source.empty() ? "Not set: details and art then come from the libretro thumbnails only"
            : source == "saved" ? "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2  Saved, encrypted for your Windows account"
            : "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2  From the command line, for this run only";
        if (DetailRow(app, "##ra_key", Icon::Key, "RetroAchievements Web API key", keyDetail.c_str(), TakeFirst("ra_key")))
        {
            KeyBuffer.clear();
            EditingKey = true;
            app.OpenKeyboard(&KeyBuffer, "RetroAchievements Web API key", 64, true);
        }
        Track("ra_key");
        if (sync)
        {
            Frontend::MetadataSync::Status st = sync->GetStatus();
            using T = Frontend::MetadataSync::KeyTest;
            const char* test = st.Test == T::Testing ? "Testing\xe2\x80\xa6" : st.Test == T::Valid ? "The key works"
                : st.Test == T::Rejected ? "RetroAchievements rejected the key" : st.Test == T::Failed ? "Couldn't reach RetroAchievements"
                : st.Test == T::Empty ? "No key to test" : nullptr;
            if (MenuRow(app, "##ra_key_test", Icon::TaskAlt, "Test the key", test, TakeFirst("ra_key_test")))
                sync->TestKey("");
            Track("ra_key_test");
        }
        Toggle(app, "desktop_libretro_thumbnails", Icon::Image, "Use libretro thumbnails",
            "Box art and title screens by No-Intro name, for games RetroAchievements has no art for; no account needed", true);
        if (MenuRow(app, "##refresh_metadata", Icon::CloudDownload, "Refresh metadata", "Over two weeks old, or art that failed",
                TakeFirst("refresh_metadata")) && sync)
        {
            sync->RefreshMetadata();
            app.Toast(sync->GetStatus().Off ? "Marked for a refresh: --no-sync holds it until the next run" : "Refreshing what's due");
        }
        Track("refresh_metadata");
        if (MenuRow(app, "##clear_art", Icon::DeleteSweep, "Clear downloaded art", "The next sync downloads it again", TakeFirst("clear_art")) && sync)
        {
            sync->ClearArt();
            app.Toast("Downloaded art cleared");
        }
        Track("clear_art");

        Note(app, "Game details and art come from RetroAchievements with your Web API key (on retroachievements.org, under Settings), "
                  "and from the libretro thumbnails, which need no account. Each game is looked up once and its art kept in the profile's "
                  "media folder; nothing syncs while a game runs.");
    }

    std::string FocusRow;
    std::string KeyBuffer;
    bool EditingKey = false;
    int TabIndex = 0;
    bool ResetScroll = true;
    bool First = true;
    std::string FocusedKey;
};

}

std::unique_ptr<Screen> MakeSettingsScreen(const std::string& tab)
{
    return std::make_unique<SettingsScreen>(tab);
}

}
