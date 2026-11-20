// The game's savestate slots as the app keeps them: nine files next to the ROM named after it, <rom name>.ml0 (the quick slot) to .ml8,
// so a state moves between the phone and the PC as it is. Each slot's screenshot is a 256x384 PNG (top screen over bottom screen) in the
// profile, ss_screenshots\<folder>\<slot>.png, as the app keeps them in its own files (ss_screenshots/<the ROM URI's hash>/<slot>.png);
// those stay on each device, so a state copied over has no picture until it's saved again. UI thread, except where it says otherwise
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "types.h"

namespace Frontend
{

class SaveSlots
{
public:
    static constexpr int kCount = 9;

    struct Slot
    {
        int Number = 0;
        bool Exists = false;
        std::filesystem::path File;
        std::filesystem::path Screenshot; // empty when the slot has none
        long long Stamp = 0;               // the state file's write time (newer is larger), so a changed picture is read again
        std::string When;                  // "Nov 20, 2026, 2:03 PM"
    };

    // the game: its ROM, the folder for its slots (the ROM's when empty) and the profile for the screenshots
    void SetGame(const std::filesystem::path& rom, const std::filesystem::path& stateDir, const std::filesystem::path& profileDir);
    void Clear() { Rom.clear(); }
    bool HasGame() const { return !Rom.empty(); }

    // any thread
    std::filesystem::path StatePath(int slot) const;
    std::filesystem::path ScreenshotPath(int slot) const;
    // "Pokemon Platinum.ml0 – .ml8"
    std::string FileNames() const;

    std::vector<Slot> List() const;

    // the slot's screenshot from a frame read back at the internal resolution (Presenter::ReadFrame: RGBA rows, the top screen
    // [topWidth] DS pixels wide with [topMargin] rows above and below it, the bottom one 194 rows below the top screen's start), scaled
    // down to 256x384 with the top screen's middle 256 columns, as the app's screenshot leaves the WideMelon sides and margins out
    bool WriteScreenshot(int slot, const std::vector<melonDS::u8>& frame, int width, int height, int topWidth, int topMargin) const;
    void DeleteScreenshot(int slot) const;
    // the slot's state to the recycle bin, and its screenshot deleted
    bool Delete(int slot) const;

private:
    std::filesystem::path Rom, Dir, ShotDir;
};

// "Slot 3", or "Quick slot" for 0, as the app names them
std::string SlotName(int slot);

}
