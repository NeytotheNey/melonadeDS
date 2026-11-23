#include "SaveSlots.h"

#include <windows.h>
#include <shellapi.h>

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "ImageFile.h"
#include "Settings.h"

using namespace melonDS;
namespace fs = std::filesystem;

namespace Frontend
{

namespace
{

constexpr int kShotW = 256, kShotH = 384;

// Java's String.hashCode over the path's UTF-16, the same kind of number the app names its screenshot folders with (the ROM URI's hash)
std::string FolderName(const fs::path& path)
{
    std::wstring text = path.wstring();
    int32_t hash = 0;
    for (wchar_t c : text)
        hash = (int32_t)(31u * (uint32_t)hash + (uint16_t)c);
    return std::to_string(hash);
}

std::string FormatTime(time_t when)
{
    tm local {};
    if (localtime_s(&local, &when) != 0)
        return "";
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int hour = local.tm_hour % 12;
    char text[64];
    snprintf(text, sizeof(text), "%s %d, %d, %d:%02d %s", kMonths[local.tm_mon], local.tm_mday, local.tm_year + 1900, hour ? hour : 12,
        local.tm_min, local.tm_hour < 12 ? "AM" : "PM");
    return text;
}

}

std::string SlotName(int slot)
{
    return slot == 0 ? "Quick slot" : "Slot " + std::to_string(slot);
}

void SaveSlots::SetGame(const fs::path& rom, const fs::path& stateDir, const fs::path& profileDir)
{
    Rom = rom;
    Dir = stateDir.empty() ? rom.parent_path() : stateDir;
    if (Dir.empty())
        Dir = ".";
    // per slot folder, so a run with another --state-dir doesn't show the pictures of other files. The same folder however the ROM's
    // path was written (.\game.nds, the recent list's absolute path, another case, a link): resolved, with backslashes, in lowercase, as
    // Windows compares paths
    std::error_code ec;
    fs::path where = fs::absolute(Dir / rom.stem(), ec);
    fs::path resolved = fs::weakly_canonical(where, ec);
    std::wstring key = (ec ? where : resolved).lexically_normal().make_preferred().wstring();
    if (!key.empty())
        CharLowerBuffW(key.data(), (DWORD)key.size());
    ShotDir = profileDir / "ss_screenshots" / FolderName(fs::path(key));
}

fs::path SaveSlots::StatePath(int slot) const
{
    fs::path name = Rom.stem();
    name += ".ml" + std::to_string(std::clamp(slot, 0, kCount - 1));
    return Dir / name;
}

fs::path SaveSlots::ScreenshotPath(int slot) const
{
    return ShotDir / (std::to_string(slot) + ".png");
}

std::string SaveSlots::FileNames() const
{
    return PathToUtf8(Rom.stem()) + ".ml0 \xe2\x80\x93 .ml8";
}

std::vector<SaveSlots::Slot> SaveSlots::List() const
{
    std::vector<Slot> slots;
    for (int i = 0; i < kCount; i++)
    {
        Slot slot;
        slot.Number = i;
        slot.File = StatePath(i);
        struct _stat64 st {};
        if (!Rom.empty() && _wstat64(slot.File.c_str(), &st) == 0 && (st.st_mode & _S_IFREG))
        {
            slot.Exists = true;
            // the write time to the 100 ns, so a slot saved twice in a second still gets its new picture
            std::error_code timeError;
            slot.Stamp = (long long)fs::last_write_time(slot.File, timeError).time_since_epoch().count();
            slot.When = FormatTime(st.st_mtime);
            std::error_code ec;
            fs::path shot = ScreenshotPath(i);
            if (fs::is_regular_file(shot, ec))
                slot.Screenshot = shot;
        }
        slots.push_back(std::move(slot));
    }
    return slots;
}

bool SaveSlots::WriteScreenshot(int slot, const std::vector<u8>& frame, int width, int height, int topWidth, int topMargin, int nativeLeft,
    int nativeTop) const
{
    if (Rom.empty() || topWidth < 256 || width <= 0 || width % topWidth != 0)
        return false;
    if (nativeLeft < 0 || nativeLeft > topWidth - 256 || nativeTop < 0 || nativeTop > 2 * topMargin)
        return false;
    int scale = width / topWidth;
    int rows = (386 + 2 * topMargin) * scale;
    if (height < rows || frame.size() < (size_t)width * height * 4)
        return false;
    // where each screen's 256 x 192 starts in the frame, in its pixels
    int topX = nativeLeft * scale, topY = nativeTop * scale;
    int bottomX = nativeLeft * scale, bottomY = (194 + 2 * topMargin) * scale;
    std::vector<u8> shot((size_t)kShotW * kShotH * 4);
    for (int y = 0; y < kShotH; y++)
    {
        bool top = y < 192;
        int x0 = top ? topX : bottomX, y0 = (top ? topY : bottomY) + (y % 192) * scale;
        for (int x = 0; x < kShotW; x++)
        {
            // the average of the scale x scale block the DS pixel covers
            unsigned sum[4] = {};
            for (int dy = 0; dy < scale; dy++)
            {
                const u8* p = &frame[((size_t)(y0 + dy) * width + x0 + x * scale) * 4];
                for (int dx = 0; dx < scale; dx++, p += 4)
                {
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                }
            }
            u8* out = &shot[((size_t)y * kShotW + x) * 4];
            unsigned n = (unsigned)(scale * scale);
            out[0] = (u8)((sum[0] + n / 2) / n);
            out[1] = (u8)((sum[1] + n / 2) / n);
            out[2] = (u8)((sum[2] + n / 2) / n);
            out[3] = 255;
        }
    }
    std::error_code ec;
    fs::create_directories(ShotDir, ec);
    return WritePng(ScreenshotPath(slot), shot, kShotW, kShotH);
}

void SaveSlots::DeleteScreenshot(int slot) const
{
    std::error_code ec;
    fs::remove(ScreenshotPath(slot), ec);
}

bool SaveSlots::Delete(int slot) const
{
    fs::path file = StatePath(slot);
    std::error_code ec;
    if (file.empty() || !fs::exists(file, ec))
        return false;
    // To the recycle bin, so a slot deleted by mistake can come back. SHFileOperation only recycles a fully qualified path (a relative
    // one, from a ROM or --state-dir given as one, is deleted for good), and the list ends with two zeros. Where there's no bin (a network
    // share, the bin turned off, a file too big for it) Windows asks before it deletes for good (FOF_WANTNUKEWARNING), which no other
    // question would otherwise; saying no there keeps the slot
    std::wstring from = fs::absolute(file, ec).lexically_normal().wstring();
    if (ec || from.empty())
        return false;
    from.push_back(L'\0');
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING | FOF_NOERRORUI | FOF_SILENT;
    if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted || fs::exists(file, ec))
    {
        fprintf(stderr, "slot %d: can't delete %s\n", slot, PathToUtf8(file).c_str());
        return false;
    }
    DeleteScreenshot(slot);
    printf("slot %d deleted: %s (in the recycle bin)\n", slot, PathToUtf8(file).c_str());
    return true;
}

}
