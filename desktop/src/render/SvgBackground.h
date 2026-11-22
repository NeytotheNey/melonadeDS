// SVG backgrounds rasterized with nanosvg (third_party/nanosvg), the way the app's BackgroundDecoder.kt sizes them with AndroidSVG
// (SvgRasterSize: the fit modes get the largest bitmap of the SVG's shape that fits the surface, the stretch mode one of the surface's
// size; an SVG with no shape of its own, which is what the built-in backgrounds are, gets the surface's size and its percentages and
// pixels are the surface's). nanosvg reads a subset of SVG, so the document goes through a few rewrites first, done here so the vendored
// code stays as it is: <use> is expanded into copies of what it refers to, a shape filled with a <pattern> is drawn as that pattern's tile
// repeated (each tile clipped to its cell, as SVG clips it) between the layers above and below it, and gradients in objectBoundingBox
// units on rects, circles and ellipses become user-space ones with the box as their transform (nanosvg takes their plain-number
// coordinates as pixels and draws radial ones as circles). <text> isn't drawn (nanosvg has no text). Any thread
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "layout/LayoutModel.h"

namespace Frontend
{

// SvgRasterSize.looksLikeSvg: the start of a file has "<svg" in it
bool LooksLikeSvg(const uint8_t* data, size_t size);

// SvgRasterSize.fitInside: the largest size with [aspectRatio] (none: the whole size) that fits [maxWidth] x [maxHeight]; false if empty
bool FitInside(std::optional<float> aspectRatio, int maxWidth, int maxHeight, int& width, int& height);
// SvgRasterSize.clampToMaxSize: scaled down, keeping its ratio, until neither side is over [maxSize]
void ClampToMaxSize(int& width, int& height, int maxSize);

// [svg] (the file's text) rasterized for a [surfaceWidth] x [surfaceHeight] surface shown with [mode], no side over [maxSize], into
// premultiplied RGBA rows, top row first (as Android uploads a Bitmap). False (with the reason in [error]) when it can't be read, when it
// would take more than a set budget of layers, tiles and parsing, or when [cancel] is set
bool RasterizeSvg(const std::string& svg, int surfaceWidth, int surfaceHeight, BackgroundMode mode, int maxSize, std::vector<uint8_t>& rgba,
    int& width, int& height, std::string& error, const std::atomic<bool>* cancel = nullptr);

}
