// PNG files through the Windows Imaging Component (a system DLL), for the savestate slots' screenshots: written as the app writes them
// (256x384, top screen over bottom screen) and read back for the slot list's thumbnails, and the Lua scripts' images. Any thread (one
// without COM gets it set up)
#pragma once

#include <filesystem>
#include <vector>

#include "types.h"

namespace Frontend
{

// [rgba] is width x height RGBA rows, top row first
bool WritePng(const std::filesystem::path& path, const std::vector<melonDS::u8>& rgba, int width, int height);
// the size of an image WIC can decode, from its header, without decoding its pixels
bool ReadImageSize(const std::filesystem::path& path, int& width, int& height);
// any image WIC can decode (up to 8192 pixels a side), as unpremultiplied RGBA rows, top row first
bool ReadImage(const std::filesystem::path& path, std::vector<melonDS::u8>& rgba, int& width, int& height);

}
