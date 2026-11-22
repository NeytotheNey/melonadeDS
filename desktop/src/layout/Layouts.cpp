#include "layout/Layouts.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "Settings.h"

namespace fs = std::filesystem;

namespace Frontend
{

namespace
{

// a phone's file is a few tens of KB; anything this big isn't a layouts file (rule 2)
constexpr uintmax_t MAX_FILE_SIZE = 32u << 20;

struct BuiltInInfo
{
    BuiltInLayout Kind;
    const char* Id;
    const char* Name;
    const char* Alias;
};

const BuiltInInfo kBuiltIns[] = {
    {BuiltInLayout::AppDefault, Layouts::DEFAULT_ID, "Default", "default"},
    {BuiltInLayout::Vertical, "d14ab1b1-cf2d-3221-a418-d697866c9d3b", "Top over bottom", "vertical"},
    {BuiltInLayout::Horizontal, "5befeeaf-4817-399e-baa4-2c1661a9331b", "Side by side", "horizontal"},
    {BuiltInLayout::TopOnly, "20df458f-dbd1-3082-8054-ff0df692b20a", "Top screen only", "top"},
    {BuiltInLayout::BottomOnly, "41ab902c-52d9-36db-9693-ee94dcfe54ff", "Bottom screen only", "bottom"},
};

std::string Lower(const std::string& text)
{
    std::string out = text;
    for (char& c : out)
        c = (char)std::tolower((unsigned char)c);
    return out;
}

bool IsBlank(const std::optional<std::string>& text)
{
    if (!text)
        return true;
    for (char c : *text)
    {
        if (!std::isspace((unsigned char)c))
            return false;
    }
    return true;
}

}

const char* Layouts::DesktopLayoutId(BuiltInLayout layout)
{
    for (const BuiltInInfo& b : kBuiltIns)
    {
        if (b.Kind == layout)
            return b.Id;
    }
    return DEFAULT_ID;
}

bool Layouts::Load(const fs::path& profileDir)
{
    Dir = profileDir;
    Custom.clear();
    FileBroken = false;
    bool ok = true;
    fs::path file = Dir / FILE_NAME;
    std::error_code ec;
    if (!Dir.empty() && fs::is_regular_file(file, ec))
    {
        uintmax_t size = fs::file_size(file, ec);
        std::string text;
        if (!ec && size <= MAX_FILE_SIZE)
        {
            std::ifstream in(file, std::ios::binary);
            std::ostringstream buffer;
            buffer << in.rdbuf();
            if (in.good() || in.eof())
                text = buffer.str();
            else
                ec = std::make_error_code(std::errc::io_error);
        }
        std::string error;
        if (ec || size > MAX_FILE_SIZE)
        {
            fprintf(stderr, "layouts: can't read %s (%s)\n", PathToUtf8(file).c_str(), ec ? ec.message().c_str() : "too big");
            FileBroken = true;
            ok = false;
        }
        else if (!ParseLayouts(text, Custom, error))
        {
            // a file that isn't a list at all is never written over; a list with a layout the app can't read either keeps it as it was
            FileBroken = Custom.empty();
            fprintf(stderr, "layouts: %s: %s%s\n", PathToUtf8(file).c_str(), error.c_str(),
                FileBroken ? " (left alone; only the built-in layouts for now)" : " (that layout is left out, and kept in the file as it is)");
            ok = false;
        }
    }
    Rebuild();
    printf("layouts: %d from %s, %d built in\n", CustomCount(), PathToUtf8(file).c_str(), (int)(sizeof(kBuiltIns) / sizeof(kBuiltIns[0])));
    return ok;
}

void Layouts::Rebuild()
{
    Choices.clear();
    for (const BuiltInInfo& b : kBuiltIns)
    {
        LayoutChoice c;
        c.Config.Id = b.Id;
        c.Config.Name = b.Name;
        c.Config.Type = "DEFAULT";
        c.BuiltIn = b.Kind;
        c.DisplayName = b.Name;
        c.Alias = b.Alias;
        Choices.push_back(std::move(c));
    }
    // the app's numbering of unnamed layouts (toLuaLayoutChoices): only when there are several
    int unnamed = 0;
    for (const LayoutConfiguration& config : Custom)
        unnamed += !config.Unreadable && config.Id && IsBlank(config.Name);
    int number = 0;
    for (const LayoutConfiguration& config : Custom)
    {
        // a layout without an id can't be picked (the app gives one when it saves a layout); an unreadable one isn't shown
        if (config.Unreadable || !config.Id || Find(*config.Id))
            continue;
        LayoutChoice c;
        c.Config = config;
        if (!IsBlank(config.Name))
            c.DisplayName = *config.Name;
        else
            c.DisplayName = unnamed > 1 ? "Unnamed layout " + std::to_string(++number) : "Unnamed layout";
        Choices.push_back(std::move(c));
    }
}

const LayoutChoice* Layouts::Find(const std::string& id) const
{
    std::string wanted = Lower(id);
    for (const LayoutChoice& c : Choices)
    {
        if (Lower(c.Id()) == wanted)
            return &c;
    }
    return nullptr;
}

const LayoutChoice* Layouts::FindByName(const std::string& text) const
{
    if (const LayoutChoice* byId = Find(text))
        return byId;
    std::string wanted = Lower(text);
    for (const LayoutChoice& c : Choices)
    {
        if (Lower(c.DisplayName) == wanted || (!c.Alias.empty() && c.Alias == wanted))
            return &c;
    }
    return nullptr;
}

bool Layouts::Write(const fs::path& file) const
{
    std::string text = WriteLayouts(Custom);
    std::error_code ec;
    if (!file.parent_path().empty())
        fs::create_directories(file.parent_path(), ec);
    fs::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            fprintf(stderr, "layouts: can't write %s\n", PathToUtf8(temp).c_str());
            return false;
        }
        out << text;
        // the last of it is only written when the file closes, so that's what's checked: a partial file never replaces the old one
        out.close();
        if (out.fail())
        {
            fprintf(stderr, "layouts: can't write %s\n", PathToUtf8(temp).c_str());
            fs::remove(temp, ec);
            return false;
        }
    }
    // MoveFileEx replaces the old file in one step
    if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        fprintf(stderr, "layouts: can't replace %s (error %lu)\n", PathToUtf8(file).c_str(), GetLastError());
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

bool Layouts::Save() const
{
    if (FileBroken || Dir.empty())
        return false;
    return Write(Dir / FILE_NAME);
}

}
