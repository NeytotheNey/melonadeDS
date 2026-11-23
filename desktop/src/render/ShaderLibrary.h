// The app's shaders and filter table, from desktop/shaders (exported from ShaderProgramSource.kt and VideoFilterShaderProvider.kt by the app's
// DesktopShaderExportTest, which also fails when they drift) as the exe embeds them. Read once, on first use; any thread
#pragma once

#include <string>
#include <vector>

namespace Frontend
{

// ShaderProgramSource.TextureFiltering
enum class TextureFiltering { Nearest, Linear };

// ShaderProgramSource: one of the app's shaders, named as the Kotlin property is
struct ShaderProgramSource
{
    std::string Name;
    TextureFiltering Filtering = TextureFiltering::Nearest;
    std::string VertexShaderSource, FragmentShaderSource;
};

// VideoFiltering.kt, in its order
enum class VideoFiltering { None, Linear, Xbr2, Hq2x, Hq4x, Quilez, Lcd, Scanlines, Scale2x, Fxaa, Sharp, Mmpx, Count };

// the enum's name ("NONE", "XBR2"), as the filter table has it; the stored setting is it in lower case (video_filtering)
const char* VideoFilteringName(VideoFiltering filtering);
// [name] as an enum value, any case ("mmpx", "MMPX"); false for none
bool ParseVideoFiltering(const std::string& name, VideoFiltering& filtering);

// VideoFilter.kt's FilterPrePass: [Scale] times its input on each axis, skipped when the 3D is upscaled if [NativeResolutionOnly]
struct FilterPrePass
{
    const ShaderProgramSource* Source = nullptr;
    int Scale = 1;
    bool NativeResolutionOnly = false;
};

// VideoFilter.kt: pre-passes run in order over the whole frame texture, then the screen shader draws the last one's output; with [Deband] the
// deband pre-pass runs in front of them, or inside the screen shader when it can take it ([CanFoldDeband])
struct VideoFilter
{
    const ShaderProgramSource* ScreenShader = nullptr;
    std::vector<FilterPrePass> PrePasses;
    bool CanFoldDeband = false;
    bool Deband = false;
};

class ShaderLibrary
{
public:
    // the embedded files, read the first time; null (logged) if they're broken, which only a bad export can do
    static const ShaderLibrary* Get();

    // the shader named as the Kotlin property is (NoFilterShader, DebandPrePassShader, ...), null if there's none
    const ShaderProgramSource* Find(const std::string& name) const;
    // VideoFilterShaderProvider.getFilter: the filter for [filtering], with Deband set when [deband] is (the no-filter one for a filtering
    // the table hasn't got)
    VideoFilter GetFilter(VideoFiltering filtering, bool deband) const;

private:
    bool Load(std::string& error);

    std::vector<ShaderProgramSource> Shaders;
    VideoFilter Filters[(int)VideoFiltering::Count];
    VideoFilter NoFilter;
};

}
