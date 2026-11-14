# T2: a decoded texel heap for the GL 3D renderer

Design from 11-13 (planning pass, code reading only). Motivation: PERF-NOTES "T2 probe" — the per-fragment texture decode is the biggest 3D GPU cost (probe: −45% / −26% GPU cycles in W2 title / town on the Fold, −16% / −20% on the Pixel; Mali is CVT-bound). Switch `GPU::GLTexCache`, debug `gltexcache`, default 0 until phone-tested.

## 0. What's there today

**Shader** (`src/GPU3D_OpenGL_shaders.h`): every textured fragment runs `TextureLookup_Nearest`: unpack `fPolygonAttr.y/z`, `TexcoordWrap` both axes, format-specific TexMem address and byte fetch(es) from `TexMem` (R8UI, 1024x512), then the palette address and a fetch from `TexPalMem`, which is **GL_RGB5_A1**, 1024x48, declared `uniform sampler2D` with no precision, so it gets the **default lowp** (fp16 on Mali).

Float tails of the fetch functions:
- Palette formats: rgb from that fetch. Alpha is the literal `1.0`/`alpha0` (I2/I4/I8), or `float(int)/31.0` (A3I5: `(a3<<2)+(a3>>1)`; A5I3: `a5`).
- Direct: each channel is `float(int)/31.0` in the shader; alpha is `float(bit)`.
- 4x4: rgb from palette fetches, alpha 1.0. Mode 1 val 2 is `(c0+c1)/2.0`; mode 3 is `(c0*5+c1*3)/8.0` and `(c0*3+c1*5)/8.0`, all float, no truncation. Index 3 in modes 0/1 is `vec4(0)`.

`FinalColor` then does modulate/decal/toon in float, and the RGBA8 color target quantizes. **`TextureLookup_Linear` is dead code** (commented out in `FinalColor`; `ClassifyTransAlpha`'s comment relies on that). Leave it untouched.

**`GPU3D_Texcache.*` / `GPU3D_TexcacheOpenGL.*`** are used only by the Compute renderer and can't be reused: they decode to RGB6A5 (`c*2+1`, alpha 0..31 in RGBA8UI) with truncating integer 4x4 interpolation, so every format differs from GL; the palette is baked into the key (palette animation re-decodes every 4x4 texture); `Update()` consumes the VRAM dirty bits itself (one consumer per frame); and it uses texture arrays per size class, which would split batches. Kept from it: the key masking idea and "hash the source bytes on a dirty hit".

## 1. Recommendation

A single **linear texel heap**: one RGBA8UI 2D texture, 1024 texels wide like TexMem, holding decoded **texel records**: the *integer inputs* of today's float tails (absolute palette coordinate, integer alpha, a kind tag), not final colors.

- Batching: the heap and a second palette texture are bound once per frame (units 2 and 3). The per-polygon heap offset replaces `vramOffset` in `fPolygonAttr.y`. Render keys and draw calls are unchanged.
- The shader keeps today's `TexcoordWrap` and indexing; the format decode and palette address math are gone.
- Palette animation is free: records hold palette *coordinates*, so palette VRAM writes only re-upload palette rows, as today.
- Exact: the GPU applies the same float operations to the same integer and texel inputs.

Why not final colors: RGBA8 gives k/255 and c/31 = k/255 only for 0 and 31; RGB5_A1 can't hold 5-bit alpha, interpolated 4x4 colors or direct texels bit-exactly; RGBA32F from the CPU can't match Mali's fp16 palette fetch. Why not per-size arrays: 64 size classes don't fit in 16 units, so draws split; they only pay off with hardware wrap, which isn't exact.

## 2. Data layout

- **`TexHeap`**: `glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8UI, 1024, 4096)`, NEAREST min/mag (required for an integer texture). 4M texels, 16 MB. If `GL_MAX_TEXTURE_SIZE` < 4096, 2048 rows (8 MB).
- **Allocation**: a buddy allocator over power-of-two blocks from 64 texels (8x8) to 1M (1024x1024). w·h is always a power of two, and blocks are aligned to their size, so each block is one upload rectangle: n ≥ 1024: `(0, o>>10, 1024, n>>10)`; n < 1024: `(o&0x3FF, o>>10, n, 1)`.
- Texel (x, y) is at `offset + y*w + x`; the shader addresses it as `ivec2(a & 0x3FF, a >> 10)`, like TexMem.
- **`TexPalLinear`**: RGB5_A1, 256x192 (96 KB), the same 16-bit words as TexPalMem, 256 per row. Entry p is at `(p & 0xFF, p >> 8)`, so the record holds the coordinate directly. Uploads come from the existing `TexPalConvBuffer`: 1024-wide row r is 256-wide rows 4r..4r+3, one more `glTexSubImage2D(0, 4*start, 256, 4*(row-start))` per dirty run. One 256-wide row is one 512 B dirty chunk.
- **Record** (RGBA8UI, `A & 7` = kind):

| kind | from | R, G | B | A bits 3–7 |
|---|---|---|---|---|
| 0 PAL | I2/I4/I8, 4x4 val 0/1, mode-0/2 val 2, mode-2 val 3 | palette x, y | literal alpha 0/1 (`idx>0 \|\| !bit29`, 1 for 4x4) | – |
| 1 PALA | A3I5, A5I3 | palette x, y | alpha 0..31 (same int as today's `pixel.a`) | – |
| 2 DIRECT | direct | r5, g5 | b5 | bit 7 = alpha bit |
| 3 HALF | 4x4 mode 1 val 2 | c0 x, y (c1 = x+1, same row since c0 is even) | – | – |
| 4 W53 / 5 W35 | 4x4 mode 3 val 2 / val 3 | c0 x, y | – | – |
| 6 ZERO | 4x4 val 3 in mode 0/1 | – | – | – |

- **Per polygon**: `fPolygonAttr.y = 0x40000000 | heapOffset` (offset < 2^22) when served by the cache; otherwise `TexParam & 0xFFFF` and the old path. `.z` unchanged. VBO stride stays 7 u32.

## 3. Shader change (only with `#define TexCache`)

```glsl
#ifdef TexCache
uniform usampler2D TexHeap;      // unit 2 (mediump usampler, as declared above)
uniform sampler2D TexPalLinear;  // unit 3, declared exactly like TexPalMem (default lowp!)

vec4 TextureLookup_Cached(vec2 st)
{
    int attr = int(fPolygonAttr.z << 16);
    int tw = 8 << ((attr >> 20) & 0x7);
    int th = 8 << ((attr >> 23) & 0x7);
    int wrapmode = (attr >> 16);
    ivec2 c = ivec2(st);
    c.x = TexcoordWrap(c.x, tw, wrapmode>>0);
    c.y = TexcoordWrap(c.y, th, wrapmode>>1);
    int a = (fPolygonAttr.y & 0x3FFFFF) + c.y * tw + c.x;
    ivec4 rec = ivec4(texelFetch(TexHeap, ivec2(a & 0x3FF, a >> 10), 0)); // highp int local, as today's `pixel`
    int kind = rec.a & 0x7;
    if (kind == 6) return vec4(0.0);
    if (kind == 2)
    {
        vec4 color;
        color.r = float(rec.r) / 31.0;
        color.g = float(rec.g) / 31.0;
        color.b = float(rec.b) / 31.0;
        color.a = float(rec.a >> 7);
        return color;
    }
    vec4 color = texelFetch(TexPalLinear, rec.rg, 0);
    if (kind == 0) return vec4(color.rgb, (rec.b != 0) ? 1.0 : 0.0);
    if (kind == 1) return vec4(color.rgb, float(rec.b)/31.0);
    vec4 color0 = color;
    vec4 color1 = texelFetch(TexPalLinear, rec.rg + ivec2(1, 0), 0);
    if (kind == 3) return vec4((color0.rgb + color1.rgb) / 2.0, 1.0);
    if (kind == 4) return vec4((color0.rgb*5.0 + color1.rgb*3.0) / 8.0, 1.0);
    return vec4((color0.rgb*3.0 + color1.rgb*5.0) / 8.0, 1.0);
}
#endif
```

In `TextureLookup_Nearest`, after the TexProbe `#if`: `#ifdef TexCache  if (fPolygonAttr.y >= 0x40000000) return TextureLookup_Cached(st);  #endif`, then the old body, unchanged. Programs built without the define are byte-identical to today's.

Precision rules: fetch into highp locals (`ivec4 rec = ivec4(...)`, `vec4 color = ...`) as today, so `float(int)/31.0` and the 4x4 mixes run at today's precision; declare `TexPalLinear` with the same default precision as `TexPalMem`; copy the expression text exactly (match today's code where it differs from the sketch above, e.g. how direct and 4x4 write their tails); don't refactor the old path into shared helpers.

## 4. Exactness argument

1. **Integer part**: the CPU decoder is a line-by-line transcription of each `TextureFetch_*`'s integer code, run for every (x, y) in [0,w)×[0,h). `TexcoordWrap`'s output is always in that range, so the record at (x, y) holds exactly the integers the old shader would feed its float tail. Copy exactly: 4x4 block address `((y&0x3FC)*(w>>2)) + (x&0x3FC) + (y&3)`; slot 1 `0x20000 + ((addr&0x1FFFC)>>1) (+0x10000 if addr ≥ 0x40000)`, per texel; I2 `((y*w+x)>>2)` with shift `2*(x&3)`; I4 nibble by `x&1`; palette base `pal<<2` for I2 and `pal<<3` otherwise, plus `(palinfo&0x3FFF)<<1` for 4x4.
2. **Float part**: same operations on the same inputs (RGB5_A1 texelFetch of identical words; same alpha literals or `float(int)/31.0`; direct `float(int)/31.0`; 4x4 mixes on highp locals).
3. **Out-of-range reads aren't reproduced; those textures fall back**: a texture whose decode would read past TexMem's 512 KB or palette index ≥ 0xC000 is Uncacheable and takes the old path.
4. **Residual risk**: different codegen for the same expression in two places (FMA contraction, reciprocal-multiply). Not proven; hash3d on each GPU family is the check.

## 5. CPU flow per frame (`GLRenderer::RenderFrame`)

1. `DeriveState` / `MakeVRAMFlat_*Coherent` / `TexMemDirtyAll`: existing.
2. Build the dirty-chunk prefix array once (factor it out of `InvalidateOpaqueTex` so both it and the new `TexCacheInvalidate(textureDirty)` use it). Overlapping entries become **Stale** (keep their block). Runs every frame while the cache is active.
3. Identical-frame skip: unchanged.
4. Program variant check next to the `GLTexProbe` one: `if (gpu.GLTexCache != TexCacheAsked) SetRenderVariant(...)`. First time on: create `TexHeap` and `TexPalLinear` and fill `TexPalLinear` from `TexPalConvBuffer`. Off: clear the cache, delete both.
5. Bind units 0–3 every frame (the compositor and fog pass rebind 0–7).
6. TexMem upload (existing); TexPalMem upload plus the `TexPalLinear` rows from the same data.
7. Clear (existing).
8. Polygon loop (existing `SetupPolygon` and NoDiscard marking), then **`AssignTexCache(gpu, texturing)`** before `BuildPolygons`. Skip masks, texturing off and format 0. Per polygon: key → entry, `LastUsed = frame`. Valid: `rp.TexWord = 0x40000000 | Offset`. Stale: hash the source bytes (XXH3-64, in tree); unchanged → Valid, else re-decode into the same block if the budget allows. Pending (new or evicted): allocate (evict LRU entries with `LastUsed < frame`), decode and upload within the budget. Uncacheable, Volatile, budget spent or heap full: old `TexWord = TexParam & 0xFFFF`. Decode via a staging buffer (strips ≤64K texels → 256 KB), `glTexSubImage2D(GL_RGBA_INTEGER, GL_UNSIGNED_BYTE)` on unit 2.
9. `BuildPolygons` / `SetupVertex` write `rp->TexWord` into `.y` (`SetupVertex` and the BetterPolygons center vertex).

## 6. Key, invalidation, bounding

- **Key (u64)**: `TexParam & 0x3FF0FFFF` (drops wrap/flip 16–19 and texgen 30–31); bit 29 cleared for formats 1, 5, 6, 7, kept for I2/I4/I8; plus `TexPalette << 32` for formats other than 7.
- **Entry** (≤4096, open-addressed index like `OpaqueTexIndex`): Key, Offset, Order, State, LastUsed, First0/Last0 (texel-byte chunks), First1/Last1 (4x4 slot-1 chunks), SrcHash, change-history bits, VolatileUntil.
- **Invalidation**: texture VRAM via the shared dirty prefix array → Stale → rehash on next use (covers re-DMA of identical data and bank remaps). Palette VRAM: no cache action, only `TexPalLinear` rows. Bit 29: in the key. Savestate load: every chunk dirty at the next `DeriveState` → all Stale → rehashed. Reset: `ClearTexCache()` next to `ClearOpaqueTex()`. Renderer switch: new `GLRenderer`, destructor deletes the textures. Switch toggle: clear.
- **Bounding**: `TexCacheBudget` decoded texels per frame, default 256K (~1 MB upload). Volatile rule: an entry that needed a content-changing re-decode in ≥3 of the last 8 frames it was used goes Volatile for 120 frames (old path, block freed, no decode).
- **Cost estimates (not measured)**: decode ~1–2 ns/texel scalar on the phones' big cores; a 256x256 texture ~0.1 ms plus a 256 KB upload; full budget ~0.4–0.8 ms in a burst frame; rehash ~10 µs per 64 KB; steady state ~0.

## 7. Shader wrap

Keep `TexcoordWrap` in the shader (exact, same source). Hardware wrap isn't exact (`texture()` nearest floors where `ivec2(st)` truncates; large 12.4 texcoords; per-polygon wrap modes break batching). An exact cheaper later step (step 3): precompute per-polygon constants in the VS into a flat varying and use a branchless wrap (`(m - (c & m)) == (~c & m)` for m = 2^k−1); measure the varying cost.

## 8. Switch and coexistence

- `GPU::GLTexCache` (default 0), debug `gltexcache`, desktop `--set GLTexCache=N`. Bits: 1 on; 2 check (re-decode every used texture every frame, no reuse, no budget); 4 tiny budget (4K texels) and a small heap, so old and new paths mix within a frame; 8 no budget and no volatile rule.
- Generalize `SetTexProbe` into `SetRenderVariant(probe, cache)`: rebuild `RenderShader[0]`/`[1]` (counting builds while SCENEFRAGS runs), drop `[2]`/`[3]`/`[4]` with `FetchShadersTried`/`NoDiscardTried` reset, fall back and log once on failure (`GLTEXCACHE: ... stays off`). `BuildRenderShader` adds `#define TexCache` and sets `TexHeap`=2 / `TexPalLinear`=3, so every build gets it.
- NoDiscard: unaffected (identical texel alpha). TexProbe: while nonzero, the cache define is left out and `AssignTexCache` doesn't run. Clear bitmap: still TexMem. Compute renderer / Texcache: untouched. Off: no new GL objects, one compare per frame, programs identical to today's.

## 9. Checking exactness

- Desktop `hash3d=1`, `GLTexCache` 0 vs 1, 3 and 5: W2 title and town, Platinum overworld, SoulSilver overworld, a 4x4-heavy scene (Mario Kart DS, Zelda), Dark Dawn's fog title, a palette-animation scene, an A3I5/A5I3 effects scene. Also with `glnodiscard` 0/3, a savestate load and reset mid-run, a runtime toggle. SCENEFRAGS and GLOPAQUE `draws` equal.
- Phones: Fold and Pixel hash3d pairs in W2 title, W2 town and Platinum, plus `gltrans` bit 2 where the extension exists.
- A debug-only `GLTEXCACHE` stats line: served/fallback polys and area by reason, entries, heap use, decoded texels, upload bytes, decode µs.

## 10. Remaining ALU (rough)

Today I8 ~35–40 int-ish ops per textured fragment, 4x4 ~60–80. Step 1 ~25–28 (two dependent fetches). Step 3 ~12–15. Probe 1 ~4. Expect roughly 40–60% of the probe's gain from step 1. Step 2 (if registers/occupancy stay high): a `TexCacheOnly` build without the old path for cached and untextured polygons; fallback polygons get a `RenderKey` bit (0x40000 free).

## 11. Risks

Codegen/precision difference between the two paths (hash mismatch; switch stays default 0 until the phones agree); Mali resource ghosting when `glTexSubImage2D` hits the 16 MB heap the previous frame still reads (watch upload and CPU time; volatile rule, PBO, smaller heap or a compute decode later); gain below the probe's; dependent-fetch latency; XXH3 collisions (bit 2 is the check); burst CPU at scene loads (budget, old path covers the gap).

## 12. Files and size

- New `src/GPU3D_OpenGLTexHeap.h/.cpp` (~450 lines): entry table, buddy allocator, transcribed decoder, invalidation against the prefix array, rehash, LRU eviction, volatile rule. No GL calls; hands back upload rectangles. Add to `src/CMakeLists.txt` next to `GPU3D_OpenGL.cpp`.
- `src/GPU3D_OpenGL.cpp` (~200): `BuildRenderShader`, `SetTexProbe` → `SetRenderVariant`, `New()` / destructor, `Reset`, `SavestateLoaded`, `RenderFrame` steps 2/4/5/6/8, `AssignTexCache`, shared prefix array for `InvalidateOpaqueTex`, `TexWord` in `SetupPolygon` / `SetupVertex` / `BuildPolygons`.
- `src/GPU3D_OpenGL.h` (~50), `src/GPU3D_OpenGL_shaders.h` (~55), `src/GPU.h` (~10).
- Switch wiring: `app/src/main/cpp/debug/DebugProperties.cpp/.h`, `desktop/src/main.cpp`, `docs/debug-switches.md`.
- Debug stats: `src/GPU3D_GLStats.h` (~80).
- Step 1 ~850–950 lines; step 2 ~+100; step 3 ~+60. Memory: 16 MB heap + 96 KB palette texture on the GPU, ~450 KB on the CPU, all freed when off.
