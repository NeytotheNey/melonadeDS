# Vendored libraries

| Library | Version | Source | License | Used for |
|---|---|---|---|---|
| picojson | 1.3.0 (tag `v1.3.0`) | https://github.com/kazuho/picojson, `picojson.h` and `LICENSE` from https://raw.githubusercontent.com/kazuho/picojson/v1.3.0/ | BSD 2-clause (`picojson/LICENSE`) | `settings.json` (`src/Settings.cpp`) |
| Dear ImGui | 1.93.0 (tag `v1.93.0`, the master branch's release, not docking) | https://github.com/ocornut/imgui, https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0.tar.gz | MIT (`imgui/LICENSE.txt`) | the menus (`src/ui/`) |
| Roboto | 3.016 (release `v3.016`) | https://github.com/googlefonts/roboto-3-classic, `unhinted/static/Roboto-Regular.ttf` and `Roboto-Medium.ttf` from https://github.com/googlefonts/roboto-3-classic/releases/download/v3.016/Roboto_v3.016.zip, `OFL.txt` from the tag | SIL Open Font License 1.1 (`fonts/roboto/OFL.txt`) | the menus' text: the app has no font of its own, so it's Android's |
| Material Symbols Outlined | the variable font at commit `10ecc14790612ca8fd164e3f4ffe9c16fc50d5b7` (2026-10-23); the repo has no tagged release with Material Symbols (its last tag, 4.0.0, is from 2020) | https://github.com/google/material-design-icons, `variablefont/MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf` and `LICENSE` at that commit | Apache 2.0 (`fonts/material-symbols/LICENSE`) | the menus' icons |
| SQLite | 3.54.1 (2026-10-16, the amalgamation) | https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip, `sqlite3.c` and `sqlite3.h` | public domain (`sqlite/PUBLIC-DOMAIN.txt`, https://www.sqlite.org/copyright.html) | the game library's `library.db` (`src/LibraryDb.cpp`) |
| miniz | 3.1.2 (release `3.1.2`, 2026-07-01) | https://github.com/richgel999/miniz/releases/download/3.1.2/miniz-3.1.2.zip, `miniz.c`, `miniz.h` and `LICENSE` | MIT (`miniz/LICENSE`) | zip files in the library and on the command line (`src/RomFile.cpp`) |
| LZMA SDK | 25.01 (2025-08) | https://www.7-zip.org/a/lzma2501.7z, the 7z decoder's C files from `C/` (the ones `C/Util/7z/makefile.gcc` builds, without `7zMain.c`) and the headers they include, plus `DOC/lzma-sdk.txt` | public domain (`lzma/lzma-sdk.txt`) | 7z files in the library and on the command line (`src/RomFile.cpp`) |
| stb_image | 2.34 (commit `1da9ade1e66195252114f5da497f244704a6c58d`, 2026-11-19, the release bump) | https://github.com/nothings/stb, `stb_image.h` and `LICENSE` at that commit | public domain or MIT, either (`stb/LICENSE`) | the downloaded art in the library (`src/ArtImage.cpp`) |
| nanosvg | commit `239e102ec2c691f2902e20ace2ed36ee4a35cfe6` (2026-07-09; the repo has no tagged releases) | https://github.com/memononen/nanosvg, `src/nanosvg.h`, `src/nanosvgrast.h` and `LICENSE.txt` at that commit | zlib (`nanosvg/LICENSE.txt`) | the layouts' SVG backgrounds (`src/render/SvgBackground.cpp`) |
| SDL_GameControllerDB | commit `4e87e3c6c2574f49e1cdf8526b6f829b61e5ccb3` (2026-11-17; the repo has no tags) | https://github.com/mdqinc/SDL_GameControllerDB, `gamecontrollerdb.txt` and `LICENSE` at that commit | zlib (`gamecontrollerdb/LICENSE`) | controller mappings SDL lacks, read at start from next to the exe (`src/main.cpp`), shipped in the release folder |

ImGui: the core files (`imgui*.cpp`, `imgui*.h`, `imconfig.h`, `imstb_*.h`, `LICENSE.txt`) and the SDL2 and OpenGL3 backends
(`backends/imgui_impl_sdl2.*`, `backends/imgui_impl_opengl3*`) as released, unchanged; the demo, docs, examples and other backends are
left out. The OpenGL3 backend is built with `IMGUI_IMPL_OPENGL_ES3` (`CMakeLists.txt`), so it uses the GLES 3 shaders on the GLES 3.2
context through `pc_gl.h`.

Roboto: the two files as released, unchanged.

SQLite, miniz and the LZMA SDK: the files as released, unchanged, each built as a static library by `CMakeLists.txt` (SQLite without
extension loading, the LZMA SDK with `Z7_PPMD_SUPPORT` and `Z7_EXTRACT_ONLY` as its own 7z decoder sample). Not vendored here: rcheevos'
ROM hashing (`rc_hash`) is built from the app's copy (`app/src/main/cpp/rcheevos/src/rhash/hash.c` and `hash_rom.c`), which the app's
rcheevos target leaves out, and the Japanese, Korean and Chinese glyphs in game titles come from Windows' own fonts (Yu Gothic or MS
Gothic, Malgun Gothic, Microsoft YaHei), read where Windows keeps them and never shipped.

Material Symbols: the 10.7 MB variable font is cut down to the icons `src/ui/Icons.h` lists, at its default instance (FILL 0, GRAD 0,
opsz 24, wght 400: the outlined look), by `fonts/material-symbols/subset.py` (fontTools), into `MaterialSymbolsOutlined-subset.ttf`
(about 14 KB). An icon added to `Icons.h` needs the script run again on the original font:

    python -I desktop/third_party/fonts/material-symbols/subset.py "MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf" desktop/src/ui/Icons.h desktop/third_party/fonts/material-symbols/MaterialSymbolsOutlined-subset.ttf

The fonts are built into the exe (`src/resources.rc`).

stb_image: the header as it is at that commit, unchanged; `src/ArtImage.cpp` builds it with PNG, JPEG, GIF and BMP only, no stdio and
no HDR, a 4096-pixel limit on each side, and (in `ArtImage.cpp`) at most 4 megapixels, checked with `stbi_info` before decoding.

nanosvg: the two headers as they are at that commit, unchanged, built once as C in `src/render/NanoSvg.c` (its own static library, without
our warnings, like the other vendored code). What it can't read of the app's built-in backgrounds is rewritten before it parses them, in
`src/render/SvgBackground.cpp` (see there and `desktop/README.md`), so the headers stay as released.

SDL_GameControllerDB: the file as it is at that commit, unchanged; `build.sh` copies it next to the exe. To update it, take the file at a newer
commit and change the commit and hash here.

SHA-256:

| File | SHA-256 |
|---|---|
| `picojson.h` | `5ddf7276d04926da7be243e7af49258e78cc27278ee9097ba45b942c7a6b5f9d` |
| `imgui-1.93.0.tar.gz` (the download) | `f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee` |
| `Roboto_v3.016.zip` (the download) | `1653dbe12f248da8fb0b9920db7b9496cd677ed3981154f6f15285c8bd4e334f` |
| `Roboto-Regular.ttf` | `39548fd9d6250717a5fcfe00b1b0ade234c1f5be8a40818c37e995c13c844df1` |
| `Roboto-Medium.ttf` | `663bedb17df44144ea2ccf4c3a3c1853547bcfed2395ad29904502ea92c74bcb` |
| `stb_image.h` | `dfc2aadda6295f67f677587773f25e5bf8c8ad427ed0db7c518525c91e72e020` |
| `nanosvg.h` | `e34fd5d084be106cea972d19ce5d27fd96d17ba89f8d06bdceee058420c8b2b0` |
| `nanosvgrast.h` | `79a9c5f4db19debf9f3a648a1589e96d92854f245a5cb4f3d823f263785234d8` |
| `gamecontrollerdb.txt` | `a9994e85b0d608cfcb4df0e1424523e488829acb1017b6a2a2adf8df6478cb26` |
| `sqlite-amalgamation-3540100.zip` (the download) | `0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9` |
| `sqlite3.c` | `0c657666d819f261cc4007526ad57e1669de9949df7a8ea847aa284a2bf0ee52` |
| `miniz-3.1.2.zip` (the download) | `f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a` |
| `miniz.c` | `e2c1aeb66eef9191d8c3feb164db2def2335a61d039bf04ed849f6b042433b30` |
| `lzma2501.7z` (the download) | `cbc3babd589d971e45971d787ff100be8aaa5eab15b2694497ec3e447009e1f2` |
| `MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf` (the original) | `f7837ba8573d9a7a74931618ced8307bc81f96d49af296bf6c8052c772ed5282` |
