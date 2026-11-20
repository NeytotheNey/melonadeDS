# Vendored libraries

| Library | Version | Source | License | Used for |
|---|---|---|---|---|
| picojson | 1.3.0 (tag `v1.3.0`) | https://github.com/kazuho/picojson, `picojson.h` and `LICENSE` from https://raw.githubusercontent.com/kazuho/picojson/v1.3.0/ | BSD 2-clause (`picojson/LICENSE`) | `settings.json` (`src/Settings.cpp`) |
| Dear ImGui | 1.93.0 (tag `v1.93.0`, the master branch's release, not docking) | https://github.com/ocornut/imgui, https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0.tar.gz | MIT (`imgui/LICENSE.txt`) | the menus (`src/ui/`) |
| Roboto | 3.016 (release `v3.016`) | https://github.com/googlefonts/roboto-3-classic, `unhinted/static/Roboto-Regular.ttf` and `Roboto-Medium.ttf` from https://github.com/googlefonts/roboto-3-classic/releases/download/v3.016/Roboto_v3.016.zip, `OFL.txt` from the tag | SIL Open Font License 1.1 (`fonts/roboto/OFL.txt`) | the menus' text: the app has no font of its own, so it's Android's |
| Material Symbols Outlined | the variable font at commit `10ecc14790612ca8fd164e3f4ffe9c16fc50d5b7` (2026-10-23); the repo has no tagged release with Material Symbols (its last tag, 4.0.0, is from 2020) | https://github.com/google/material-design-icons, `variablefont/MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf` and `LICENSE` at that commit | Apache 2.0 (`fonts/material-symbols/LICENSE`) | the menus' icons |
| SDL_GameControllerDB | commit `4e87e3c6c2574f49e1cdf8526b6f829b61e5ccb3` (2026-11-17; the repo has no tags) | https://github.com/mdqinc/SDL_GameControllerDB, `gamecontrollerdb.txt` and `LICENSE` at that commit | zlib (`gamecontrollerdb/LICENSE`) | controller mappings SDL lacks, read at start from next to the exe (`src/main.cpp`), shipped in the release folder |

ImGui: the core files (`imgui*.cpp`, `imgui*.h`, `imconfig.h`, `imstb_*.h`, `LICENSE.txt`) and the SDL2 and OpenGL3 backends
(`backends/imgui_impl_sdl2.*`, `backends/imgui_impl_opengl3*`) as released, unchanged; the demo, docs, examples and other backends are
left out. The OpenGL3 backend is built with `IMGUI_IMPL_OPENGL_ES3` (`CMakeLists.txt`), so it uses the GLES 3 shaders on the GLES 3.2
context through `pc_gl.h`.

Roboto: the two files as released, unchanged.

Material Symbols: the 10.7 MB variable font is cut down to the icons `src/ui/Icons.h` lists, at its default instance (FILL 0, GRAD 0,
opsz 24, wght 400: the outlined look), by `fonts/material-symbols/subset.py` (fontTools), into `MaterialSymbolsOutlined-subset.ttf`
(about 6 KB). An icon added to `Icons.h` needs the script run again on the original font:

    python -I desktop/third_party/fonts/material-symbols/subset.py "MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf" desktop/src/ui/Icons.h desktop/third_party/fonts/material-symbols/MaterialSymbolsOutlined-subset.ttf

The fonts are built into the exe (`src/resources.rc`).

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
| `gamecontrollerdb.txt` | `a9994e85b0d608cfcb4df0e1424523e488829acb1017b6a2a2adf8df6478cb26` |
| `MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf` (the original) | `f7837ba8573d9a7a74931618ced8307bc81f96d49af296bf6c8052c772ed5282` |
