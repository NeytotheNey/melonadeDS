# T2: a decoded texel heap for the GL 3D renderer

Design from 11-13, built 11-13 to 11-14. Motivation: PERF-NOTES "T2 probe": the per-fragment texture decode is the biggest 3D GPU cost (probe: −45% / −26% GPU cycles in W2 title / town on the Fold, −16% / −20% on the Pixel; Mali is CVT-bound). Switch `GPU::GLTexCache`, debug `gltexcache`, default `449` since 11-14 (steps 2, 3 and 4 with the record path left out of the main programs). The bits are in `docs/debug-switches.md`; results in PERF-NOTES "T2 step 1" through "T2 step 4b".

## 0. What was there before

**Shader** (`src/GPU3D_OpenGL_shaders.h`): every textured fragment runs `TextureLookup_Nearest`: unpack `fPolygonAttr.y/z`, `TexcoordWrap` both axes, format-specific TexMem address and byte fetch(es) from `TexMem` (R8UI, 1024x512), then the palette address and a fetch from `TexPalMem`, which is **GL_RGB5_A1**, 1024x48, declared `uniform sampler2D` with no precision, so it gets the **default lowp** (fp16 on Mali).

Float tails of the fetch functions:
- Palette formats: rgb from that fetch. Alpha is the literal `1.0`/`alpha0` (I2/I4/I8), or `float(int)/31.0` (A3I5: `(a3<<2)+(a3>>1)`; A5I3: `a5`).
- Direct: each channel is `float(int)/31.0` in the shader; alpha is `float(bit)`.
- 4x4: rgb from palette fetches, alpha 1.0. Mode 1 val 2 is `(c0+c1)/2.0`; mode 3 is `(c0*5+c1*3)/8.0` and `(c0*3+c1*5)/8.0`, all float, no truncation. Index 3 in modes 0/1 is `vec4(0)`.

`FinalColor` then does modulate/decal/toon in float, and the RGBA8 color target quantizes. **`TextureLookup_Linear` is dead code** (commented out in `FinalColor`; `ClassifyTransAlpha`'s comment relies on that). Leave it untouched.

**`GPU3D_Texcache.*` / `GPU3D_TexcacheOpenGL.*`** are used only by the Compute renderer and can't be reused: they decode to RGB6A5 (`c*2+1`, alpha 0..31 in RGBA8UI) with truncating integer 4x4 interpolation, so every format differs from GL; the palette is baked into the key (palette animation re-decodes every 4x4 texture); `Update()` consumes the VRAM dirty bits itself (one consumer per frame); and it uses texture arrays per size class, which would split batches. Kept from it: the key masking idea and "hash the source bytes on a dirty hit".

## 1. Recommendation

A single **linear texel heap**: one RGBA8UI 2D texture, 1024 texels wide like TexMem, holding decoded **texel records**: the *integer inputs* of the float tails (absolute palette coordinate, integer alpha, a kind tag), not final colors.

- Batching: the heap and a second palette texture are bound once per frame (units 2 and 3). The per-polygon heap offset replaces `vramOffset` in `fPolygonAttr.y`. Render keys and draw calls are unchanged.
- The shader keeps `TexcoordWrap` and indexing; the format decode and palette address math are gone.
- Palette animation is free: records hold palette *coordinates*, so palette VRAM writes only re-upload palette rows.
- Exact: the GPU applies the same float operations to the same integer and texel inputs.

Why not final colors: RGBA8 gives k/255 and c/31 = k/255 only for 0 and 31; RGB5_A1 can't hold 5-bit alpha, interpolated 4x4 colors or direct texels bit-exactly; RGBA32F from the CPU can't match Mali's fp16 palette fetch. Why not per-size arrays: 64 size classes don't fit in 16 units, so draws split; they only pay off with hardware wrap, which isn't exact.

## 2. Data layout

- **`TexHeap`**: `glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8UI, 1024, 4096)`, NEAREST min/mag (required for an integer texture). 4M texels, 16 MB. If `GL_MAX_TEXTURE_SIZE` < 4096, 2048 rows (8 MB).
- **Allocation**: a buddy allocator over power-of-two blocks from 64 texels (8x8) to 1M (1024x1024). w·h is always a power of two, and blocks are aligned to their size, so each block is one upload rectangle: n ≥ 1024: `(0, o>>10, 1024, n>>10)`; n < 1024: `(o&0x3FF, o>>10, n, 1)`.
- Texel (x, y) is at `offset + y*w + x`; the shader addresses it as `ivec2(a & 0x3FF, a >> 10)`, like TexMem.
- **`TexPalLinear`**: RGB5_A1, 256x192 (96 KB), the same 16-bit words as TexPalMem, 256 per row. Entry p is at `(p & 0xFF, p >> 8)`, so the record holds the coordinate directly. Uploads come from the existing `TexPalConvBuffer`: 1024-wide row r is 256-wide rows 4r..4r+3. One 256-wide row is one 512 B dirty chunk.
- **Record** (RGBA8UI, `A & 7` = kind):

| kind | from | R, G | B | A bits 3–7 |
|---|---|---|---|---|
| 0 PAL | I2/I4/I8, 4x4 val 0/1, mode-0/2 val 2, mode-2 val 3 | palette x, y | literal alpha 0/1 (`idx>0 \|\| !bit29`, 1 for 4x4) | – |
| 1 PALA | A3I5, A5I3 | palette x, y | alpha 0..31 (the same int as the old `pixel.a`) | – |
| 2 DIRECT | direct | r5, g5 | b5 | bit 7 = alpha bit |
| 3 HALF | 4x4 mode 1 val 2 | c0 x, y (c1 = x+1, same row since c0 is even) | – | – |
| 4 W53 / 5 W35 | 4x4 mode 3 val 2 / val 3 | c0 x, y | – | – |
| 6 ZERO | 4x4 val 3 in mode 0/1 | – | – | – |

- **Per polygon**: `fPolygonAttr.y = 0x40000000 | heapOffset` (offset < 2^22) when served by the cache; otherwise `TexParam & 0xFFFF` and the old path. `.z` unchanged. VBO stride stays 7 u32.

## 3. Shader change

`TextureLookup_Cached` (under `#define TexCache`) reads the record and runs the old float tails on it; `TextureLookup_Nearest` returns it when `fPolygonAttr.y >= 0x40000000`. Programs built without the define are byte-identical to the old ones.

Precision rules (they're what keeps it exact): fetch into highp locals (`ivec4 rec = ivec4(...)`, `vec4 color = ...`), so `float(int)/31.0` and the 4x4 mixes run at the old precision; declare `TexPalLinear` with the same default precision as `TexPalMem`; copy the expression text of the old tails exactly; don't refactor the old path into shared helpers.

## 4. Exactness argument

1. **Integer part**: the CPU decoder is a line-by-line transcription of each `TextureFetch_*`'s integer code, run for every (x, y) in [0,w)×[0,h). `TexcoordWrap`'s output is always in that range, so the record at (x, y) holds exactly the integers the old shader would feed its float tail. Copy exactly: 4x4 block address `((y&0x3FC)*(w>>2)) + (x&0x3FC) + (y&3)`; slot 1 `0x20000 + ((addr&0x1FFFC)>>1) (+0x10000 if addr ≥ 0x40000)`, per texel; I2 `((y*w+x)>>2)` with shift `2*(x&3)`; I4 nibble by `x&1`; palette base `pal<<2` for I2 and `pal<<3` otherwise, plus `(palinfo&0x3FFF)<<1` for 4x4.
2. **Float part**: same operations on the same inputs (RGB5_A1 texelFetch of identical words; same alpha literals or `float(int)/31.0`; direct `float(int)/31.0`; 4x4 mixes on highp locals).
3. **Out-of-range reads aren't reproduced; those textures fall back**: a texture whose decode would read past TexMem's 512 KB or palette index ≥ 0xC000 is Uncacheable and takes the old path.
4. **Residual risk**: different codegen for the same expression in two places (FMA contraction, reciprocal-multiply). Not proven; hash3d on each GPU family is the check, and it matched on the desktop, Fold, Pixel and OnePlus.

## 5. CPU flow per frame (`GLRenderer::RenderFrame`)

1. The dirty-chunk prefix array is built once (shared with `InvalidateOpaqueTex`); overlapping entries become **Stale** (keep their block). Runs every frame while the cache is active.
2. Program variant check next to the `GLTexProbe` one (`SetRenderVariant`). First time on: create `TexHeap` and `TexPalLinear` and fill `TexPalLinear` from `TexPalConvBuffer`. Off: clear the cache, delete both.
3. Bind units 0–3 every frame (the compositor and fog pass rebind 0–7). TexPalMem upload plus the `TexPalLinear` rows from the same data.
4. Polygon loop, then **`AssignTexCache(gpu, texturing)`** before `BuildPolygons`. Skips masks, texturing off and format 0. Per polygon: key → entry, `LastUsed = frame`. Valid: `rp.TexWord = 0x40000000 | Offset`. Stale: hash the source bytes (XXH3-64); unchanged → Valid, else re-decode into the same block if the budget allows. Pending (new or evicted): allocate (evict LRU entries with `LastUsed < frame`), decode and upload within the budget. Uncacheable, Volatile, budget spent or heap full: old `TexWord = TexParam & 0xFFFF`. Decode via a staging buffer (strips ≤64K texels → 256 KB), `glTexSubImage2D(GL_RGBA_INTEGER, GL_UNSIGNED_BYTE)` on unit 2.
5. `BuildPolygons` / `SetupVertex` write `rp->TexWord` into `.y` (`SetupVertex` and the BetterPolygons center vertex).

## 6. Key, invalidation, bounding

- **Key (u64)**: `TexParam & 0x3FF0FFFF` (drops wrap/flip 16–19 and texgen 30–31); bit 29 cleared for formats 1, 5, 6, 7, kept for I2/I4/I8; plus `TexPalette << 32` for formats other than 7.
- **Entry** (≤4096, open-addressed index like `OpaqueTexIndex`): Key, Offset, Order, State, LastUsed, First0/Last0 (texel-byte chunks), First1/Last1 (4x4 slot-1 chunks), SrcHash, change-history bits, VolatileUntil.
- **Invalidation**: texture VRAM via the shared dirty prefix array → Stale → rehash on next use (covers re-DMA of identical data and bank remaps). Bit 29: in the key. Savestate load: every chunk dirty at the next `DeriveState` → all Stale → rehashed. Reset: the heap is cleared next to `ClearOpaqueTex()`. Renderer switch: new `GLRenderer`, destructor deletes the textures. Switch change: clear. Palette VRAM: see 13, 4b.
- **Bounding**: a decode budget per frame (`DefaultBudget`, 256K texels, ~1 MB upload). Volatile rule: an entry that needed a content-changing re-decode in ≥3 of the last 8 frames it was used goes Volatile for 120 frames (old path, block freed, no decode). Steady-state CPU measured on the phones: ~2–50 µs/frame.

## 7. Shader wrap

`TexcoordWrap` stays in the shader (exact, same source). Hardware wrap isn't exact (`texture()` nearest floors where `ivec2(st)` truncates; large 12.4 texcoords; per-polygon wrap modes break batching). Step 3 (bit 64, part of `449`) precomputes per-polygon wrap constants in the VS into one `flat mediump ivec4` and uses a branchless exact wrap (`(m - (c & m)) == (~c & m)` for m = 2^k−1). On its own it was only −2.1% / −1.4% Mcyc on the Fold (title / town) and neutral on the Pixel (varying work +23%).

## 8. Switch and coexistence

- `GPU::GLTexCache`, debug `gltexcache`, desktop `--set GLTexCache=N`. The bits (check mode, tiny budget, no budget, step 1, forced twin failure, steps 3 and 4) are in `docs/debug-switches.md`.
- `SetRenderVariant` rebuilds `RenderShader[0]`/`[1]` (counting builds while SCENEFRAGS runs), drops the fetch and no-discard programs built from them (`FetchShadersTried`/`NoDiscardTried` reset), and falls back and logs once on failure (`GLTEXCACHE: ... stays off`). `BuildRenderShader` adds `#define TexCache` and sets `TexHeap`=2 / `TexPalLinear`=3.
- NoDiscard: unaffected (identical texel alpha). TexProbe: while nonzero, the cache define is left out and `AssignTexCache` doesn't run. Clear bitmap: still TexMem. Compute renderer / Texcache: untouched. Off (`0`): no new GL objects, one compare per frame, programs identical to the old ones.

## 9. Checking exactness

- Desktop `hash3d=1`, `gltexcache` values against each other: W2 title and town, Platinum overworld, SoulSilver overworld, a 4x4-heavy scene (Mario Kart DS, Zelda), Dark Dawn's fog title, a palette-animation scene, an A3I5/A5I3 effects scene. Also with `glnodiscard` 0/3, a savestate load and reset mid-run, a runtime toggle. SCENEFRAGS and GLOPAQUE `draws` equal.
- Phones: Fold and Pixel hash3d pairs in W2 title, W2 town and Platinum, plus `gltrans` bit 2 where the extension exists.
- `scenestats=1` prints a `GLTEXCACHE` stats line (fields in the comments above `GLTexCacheStats` in `melonDS-android-lib/src/GPU3D_GLStats.h`).

## 10. Step 2

Step 1 (one program with both paths, now `17`) was exact but only −2 to −5%. Step 2 builds the programs most polygons draw with (served, untextured, texturing off) without the old decode path (`TexCacheOnly`). Polygons the heap can't serve (out of range, budget, volatile, heap full) get `RenderKey_TexFull` (0x40000) and draw with full-program twins (`RenderShader[FullShader + id]`), built lazily. A failed twin build turns the cache off until the switch changes. Fold −19% title / −10% town; Pixel −5.5% core-active.

## 11. Risks and what's not checked

- Not checked: a game that streams textures every frame, on a phone (the volatile rule and budget should handle it); memory on the Fold and Pixel (OnePlus graphics +0.7–1.9 MB); Mali resource ghosting when `glTexSubImage2D` hits the heap the previous frame still reads (watch upload and CPU time; volatile rule, PBO, smaller heap or a compute decode later).
- Palette changes (4b) were only exercised on the desktop: the phones' palette counters stayed 0, since their SoulSilver states are near-static.
- Untuned: the palette volatile rule (no scene animated palettes often enough to trigger it). The re-resolve CPU cost on phones isn't measured.
- XXH3 collisions (bit 2, the check mode, is the check); burst CPU at scene loads (budget, old path covers the gap).

## 12. Files

- `src/GPU3D_OpenGLTexHeap.h/.cpp`: entry table, buddy allocator, transcribed decoder, invalidation against the prefix array, rehash, LRU eviction, volatile rules. No GL calls; hands back upload rectangles.
- `src/GPU3D_OpenGL.cpp/.h`: `BuildRenderShader`, `SetRenderVariant`, `AssignTexCache`, the shared prefix array, `TexWord` in `SetupPolygon` / `SetupVertex` / `BuildPolygons`. Shaders in `src/GPU3D_OpenGL_shaders.h`, the switch in `src/GPU.h`, stats in `src/GPU3D_GLStats.h`.
- Switch wiring: `app/src/main/cpp/debug/DebugProperties.cpp/.h`, `desktop/src/main.cpp`, `docs/debug-switches.md`.
- Memory: 16 MB heap + 96 KB palette texture + 8 MB color plane on the GPU, ~450 KB on the CPU, all freed when off.

## 13. Step 4 (design B): a color plane next to the records

What's left after step 3 is the record path itself: a 4-byte record fetch, int conversions, the kind and alpha code, and the record → `TexPalLinear` dependency. In W2 every served polygon is I2/I4/I8 (kind 0) or A3I5/A5I3 (kind 1), so the kind branches are uniform per polygon; the cost is the fetch chain.

**4a (11-13, `gltexcache` bit 128).**
- `TexColor`: RGB5_A1, the heap's size (1024 x 4096, 8 MB; 4 MB at 2048 rows, 256 KB in the tiny mode), the same block offsets as the records, made and deleted with the heap, on unit 4 (bound every frame like units 2 and 3). Same internal format, format and type as `TexPalLinear`.
- The decoder writes, next to each record of an I2/I4/I8/A3I5/A5I3 entry, the texel's palette word as `TexPalConvBuffer` holds it (entry R + 256 G), with A (bit 0) set to kind 0's alpha, `B != 0`. 4x4 and direct stay records only. Uploads are 6 bytes a texel instead of 4.
- `TexWord` flags (offsets are below 2^22, bits 22–29 were free): bit 22 the polygon has a color plane, bit 23 its alpha comes from the record (A3I5, A5I3).
- Shader (`#define TexColorPlane`), after the address: with bit 22, `color = texelFetch(TexColor, ta, 0)`, and with bit 23 `return vec4(color.rgb, float(rec.b)/31.0)` with the record fetched at the same address (independent, not dependent), else `return color`. Exactness: rgb is the same 16-bit word in the same format fetched with the same default precision as the record path's `TexPalLinear` fetch; A is 0 or 1 exactly, as kind 0 returns; kind 1's alpha is the same expression on the same record. The direct and 4x4 tails are untouched (a unorm fetch and `float(i)/31.0` aren't interchangeable).
- **Bit 256:** with 128 and step 2, the cache-only programs are built without the record path (`#define TexColorOnly`, the color fetch plus the bit-23 record fetch only). Served textures without a color plane (4x4, direct) get `RenderKey_TexFull` and draw with the twins, whose heap lookup still has the record path, so the frame stays exact; in W2 there are none. `449` = 256 + 193, `385` = 256 + 129. Ignored without 128 or with step 1 (bit 16).
- 4a had no palette tracking, so SoulSilver and Dark Dawn's title (which animate palettes) went stale; 4b fixed that.

**4b, palette tracking (11-14, same bit 128, exact; `449` is the default since 11-14).** Range = lowest to highest palette entry the records point at (already includes the format's base); chunks = entry >> 8; hash = XXH3 over those `TexPalConvBuffer` words. `TexPalDirtyPrefix` is built right after the texture `Invalidate`, before the identical-frame skip (which needs `!texPalChanged` anyway). An entry whose palette chunks changed is rehashed at next use; unchanged keeps the plane, changed re-resolves the color plane only (`Decode(records=false, color=true)`, only reached from Valid or Kept, so VRAM matches the records; it counts against the decode budget). `PalHistory` (8 bits) drives the palette volatile rule (3 of 8, 120 frames; off in modes 2 and 8): those entries drop their color bit and take the record path, which follows the palette exactly. `Use` returns a `Plane` (None/Ok/Volatile/Budget) and only `PlaneOk` sets the color bits. State loads don't clear the heap: `ResetVRAMCache` sets every mapping to 0x8000, so the next `DeriveState` dirties every palette chunk. Known waste, still exact: a change to an unused word inside the range re-resolves (and counts toward volatile).

Results (PERF-NOTES "T2 step 4a", "T2 step 4b"): 0 mismatches on the desktop in all 7 scenes incl. SoulSilver and Dark Dawn, and on the Fold, Pixel and OnePlus. Fold cold A/B 449 vs 1: GLTIME µs −8.2% title / −5.2% town, opaque pass −14% / −16%; Pixel core-active −8.3% / −4.9%.
