// Downloaded art (MetadataSync.h) read with stb_image (third_party/stb) and, for WebP, which stb_image can't read, with Windows' own WIC
// decoder when this Windows has one (Windows 11, and Windows 10 with the WebP Image Extensions): whether some bytes are an image at all,
// before they're saved, and an image file decoded and shrunk for the menus' tiles. Images over kMaxArtSide pixels a side or kMaxArtPixels
// in all don't count as images, so a hostile file can't make the decoder allocate gigabytes. Any thread
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Frontend
{

constexpr int kMaxArtSide = 4096;
constexpr int64_t kMaxArtPixels = 2048ll * 2048;

enum class ImageCheck
{
    Ok,
    NotAnImage,   // not one stb_image or WIC can read, or too big
    Unsupported,  // a WebP and this Windows has no WebP decoder: says nothing about the image
};
ImageCheck CheckImage(const uint8_t* data, size_t size);
// Windows can decode WebP (WIC has a decoder for it); asked once
bool WebpSupported();

struct ArtPixels
{
    std::vector<uint8_t> Rgba; // Width x Height RGBA rows, top row first
    int Width = 0, Height = 0;
};

// [file] decoded, then shrunk (an area average) so neither side is over [maxSide], keeping its shape. A DS title screen of both screens
// stacked (256 x 384, as libretro's are) gives only its top screen when [topScreenOnly]. False when it can't be read or is over the caps
bool LoadArt(const std::filesystem::path& file, int maxSide, bool topScreenOnly, ArtPixels& out);

}
