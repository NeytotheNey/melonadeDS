#include "ShaderLibrary.h"

#include <cstdio>
#include <cstring>
#include <mutex>

#include "picojson/picojson.h"

namespace Frontend
{

namespace
{

struct EmbeddedShaderFile
{
    const char* Name;
    const unsigned char* Data;
    size_t Size;
};

#include "shader_files.inc"

// [file]'s text with any CR left out: the export writes LF (.gitattributes keeps it so, and the drift check compares exact bytes), and
// ForDesktop matches the LF precision block, so a file saved with CRLF still draws as exported
std::string Text(const EmbeddedShaderFile& file)
{
    std::string text;
    text.reserve(file.Size);
    for (size_t i = 0; i < file.Size; i++)
    {
        if (file.Data[i] != '\r')
            text += (char)file.Data[i];
    }
    return text;
}

const char* const FILTERING_NAMES[] = {"NONE", "LINEAR", "XBR2", "HQ2X", "HQ4X", "QUILEZ", "LCD", "SCANLINES", "SCALE2X", "FXAA", "SHARP", "MMPX"};
static_assert(sizeof(FILTERING_NAMES) / sizeof(FILTERING_NAMES[0]) == (size_t)VideoFiltering::Count);

const EmbeddedShaderFile* FindFile(const std::string& name)
{
    for (const EmbeddedShaderFile& file : EMBEDDED_SHADER_FILES)
    {
        if (name == file.Name)
            return &file;
    }
    return nullptr;
}

}

const char* VideoFilteringName(VideoFiltering filtering)
{
    int index = (int)filtering;
    return index >= 0 && index < (int)VideoFiltering::Count ? FILTERING_NAMES[index] : "NONE";
}

bool ParseVideoFiltering(const std::string& name, VideoFiltering& filtering)
{
    for (int i = 0; i < (int)VideoFiltering::Count; i++)
    {
        if (_stricmp(name.c_str(), FILTERING_NAMES[i]) == 0)
        {
            filtering = (VideoFiltering)i;
            return true;
        }
    }
    return false;
}

const ShaderLibrary* ShaderLibrary::Get()
{
    static ShaderLibrary library;
    static bool loaded = false;
    static std::once_flag once;
    std::call_once(once, []
    {
        std::string error;
        loaded = library.Load(error);
        if (!loaded)
            fprintf(stderr, "the embedded shaders are broken (%s): export them again (desktop/README.md)\n", error.c_str());
    });
    return loaded ? &library : nullptr;
}

const ShaderProgramSource* ShaderLibrary::Find(const std::string& name) const
{
    for (const ShaderProgramSource& shader : Shaders)
    {
        if (shader.Name == name)
            return &shader;
    }
    return nullptr;
}

VideoFilter ShaderLibrary::GetFilter(VideoFiltering filtering, bool deband) const
{
    int index = (int)filtering;
    VideoFilter filter = index >= 0 && index < (int)VideoFiltering::Count && Filters[index].ScreenShader ? Filters[index] : NoFilter;
    filter.Deband = deband;
    return filter;
}

bool ShaderLibrary::Load(std::string& error)
{
    const EmbeddedShaderFile* table = FindFile("shaders.json");
    if (!table)
    {
        error = "no shaders.json";
        return false;
    }
    picojson::value root;
    std::string parseError = picojson::parse(root, Text(*table));
    if (!parseError.empty() || !root.is<picojson::object>())
    {
        error = "shaders.json: " + parseError;
        return false;
    }
    const picojson::value& shaders = root.get("shaders");
    const picojson::value& filters = root.get("filters");
    if (!shaders.is<picojson::array>() || !filters.is<picojson::array>())
    {
        error = "shaders.json has no shaders or filters";
        return false;
    }

    // all of them first, since the filters point into the list
    for (const picojson::value& entry : shaders.get<picojson::array>())
    {
        ShaderProgramSource shader;
        shader.Name = entry.get("name").to_str();
        shader.Filtering = entry.get("textureFiltering").to_str() == "LINEAR" ? TextureFiltering::Linear : TextureFiltering::Nearest;
        const EmbeddedShaderFile* vertex = FindFile(shader.Name + ".vert");
        const EmbeddedShaderFile* fragment = FindFile(shader.Name + ".frag");
        if (!vertex || !fragment)
        {
            error = shader.Name + " has no .vert or .frag";
            return false;
        }
        shader.VertexShaderSource = Text(*vertex);
        shader.FragmentShaderSource = Text(*fragment);
        Shaders.push_back(std::move(shader));
    }

    NoFilter.ScreenShader = Find("NoFilterShader");
    if (!NoFilter.ScreenShader)
    {
        error = "no NoFilterShader";
        return false;
    }
    for (const picojson::value& entry : filters.get<picojson::array>())
    {
        VideoFiltering filtering = VideoFiltering::None;
        if (!ParseVideoFiltering(entry.get("name").to_str(), filtering))
        {
            // a filter the app has and this build doesn't know yet draws as none
            fprintf(stderr, "shaders.json: filter %s isn't known here\n", entry.get("name").to_str().c_str());
            continue;
        }
        VideoFilter filter;
        filter.ScreenShader = Find(entry.get("screenShader").to_str());
        filter.CanFoldDeband = entry.get("canFoldDeband").evaluate_as_boolean();
        if (!filter.ScreenShader)
        {
            error = std::string(FILTERING_NAMES[(int)filtering]) + "'s screen shader isn't there";
            return false;
        }
        const picojson::value& prePasses = entry.get("prePasses");
        if (prePasses.is<picojson::array>())
        {
            for (const picojson::value& pass : prePasses.get<picojson::array>())
            {
                FilterPrePass prePass;
                prePass.Source = Find(pass.get("shader").to_str());
                prePass.Scale = pass.get("scale").is<double>() ? (int)pass.get("scale").get<double>() : 1;
                prePass.NativeResolutionOnly = pass.get("nativeResolutionOnly").evaluate_as_boolean();
                // a scale is the target's size times it, so it's held to what a texture can take
                if (!prePass.Source || prePass.Scale < 1 || prePass.Scale > 4)
                {
                    error = std::string(FILTERING_NAMES[(int)filtering]) + " has a bad pre-pass";
                    return false;
                }
                filter.PrePasses.push_back(prePass);
            }
        }
        Filters[(int)filtering] = filter;
    }
    return true;
}

}
