// nanosvg's parser and rasterizer (third_party/nanosvg, unchanged), built once here as C, in a library of its own without the warnings our
// code builds with, like the other vendored code (desktop/CMakeLists.txt)
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"
