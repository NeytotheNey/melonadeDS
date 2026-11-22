package me.magnum.melonds.common.opengl

import me.magnum.melonds.domain.model.FRAME_TEXTURE_HEIGHT
import me.magnum.melonds.domain.model.SCREEN_GAP
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.SCREEN_WIDTH

class ShaderProgramSource private constructor(val textureFiltering: TextureFiltering, val vertexShaderSource: String, val fragmentShaderSource: String) {
    enum class TextureFiltering {
        NEAREST,
        LINEAR
    }

    companion object {
        // Fragment shader precision: high where the GPU supports it, medium otherwise
        private const val PRECISION_HEADER = "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" +
                "precision highp float;\n" +
                "#else\n" +
                "precision mediump float;\n" +
                "#endif\n"

        private const val DEFAULT_VERT_SHADER = "attribute vec2 vUV;\n" +
                "attribute vec2 vPos;\n" +
                "attribute float vAlpha;\n" +
                "varying vec2 uv;\n" +
                "varying float alpha;\n" +
                "void main()\n" +
                "{\n" +
                "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                "    uv = vUV;\n" +
                "    alpha = vAlpha;\n" +
                "}"

        // For pre-passes (see VideoFilter), which draw a full-target quad into a texture with nothing blended
        private const val PRE_PASS_VERT_SHADER = "attribute vec2 vPos;\n" +
                "attribute vec2 vUV;\n" +
                "varying vec2 uv;\n" +
                "void main()\n" +
                "{\n" +
                "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                "    uv = vUV;\n" +
                "}"

        // finishColor() applies Pokefan531's nds-color profile (hunterk's Color Mangler, public domain) when COLOR_CORRECTION is defined; both
        // pow() inputs are clamped because filters can output negatives
        private const val COLOR_FUNCTIONS = "vec3 finishColor(vec3 rgb) {\n" +
                "#ifdef COLOR_CORRECTION\n" +
                "    vec3 c = pow(clamp(rgb, 0.0, 1.0), vec3(1.91)) * 0.89;\n" +
                "    // One column per input channel: new R = 0.87 R + 0.255 G - 0.125 B, new G = 0.1 R + 0.645 G + 0.255 B, new B = 0.1 R + 0.17 G + 0.73 B\n" +
                "    c = mat3(0.87, 0.1, 0.1, 0.255, 0.645, 0.17, -0.125, 0.255, 0.73) * c;\n" +
                "    return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 1.91));\n" +
                "#else\n" +
                "    return rgb;\n" +
                "#endif\n" +
                "}\n"

        private const val DEFAULT_FRAG_SHADER = "precision mediump float;\n" +
                "uniform sampler2D tex;\n" +
                "varying vec2 uv;\n" +
                "varying float alpha;\n" +
                COLOR_FUNCTIONS +
                "void main()\n" +
                "{\n" +
                "    vec4 color = texture2D(tex, uv);\n" +
                "    gl_FragColor = vec4(finishColor(color.bgr), alpha);\n" +
                "}"

        // The frame's rows (Shader.setTopScreenHeight): x the top screen's half's end and y the bottom screen's start as v coordinates, z the middle
        // of the gap between them (0.5 without the WideMelon camera's margin), w the frame's height in DS pixels (FRAME_TEXTURE_HEIGHT without it)
        private const val FRAME_LAYOUT_UNIFORM = "uniform vec4 frameLayout;\n"

        // screenColumns(p) is the u range a wide frame lets the non-wide screen sample; needs texSize and topScreenWidth declared first, and
        // declares frameLayout
        private const val SCREEN_COLUMN_RANGE_FUNCTIONS = "uniform float bottomScreenWide;\n" +
                FRAME_LAYOUT_UNIFORM +
                "vec2 screenColumns(vec2 p) {\n" +
                "    if ((p.y < frameLayout.z) == (bottomScreenWide < 0.5) || topScreenWidth <= ${SCREEN_WIDTH}.0) {\n" +
                "        return vec2(-1.0, 2.0);\n" +
                "    }\n" +
                "    float halfWidth = 0.5 * ${SCREEN_WIDTH}.0 / topScreenWidth;\n" +
                "    float halfTexel = 0.5 / texSize.x;\n" +
                "    return vec2(0.5 - halfWidth + halfTexel, 0.5 + halfWidth - halfTexel);\n" +
                "}\n"

        // Samples p unchanged when there's nothing to clamp, since any math on it can cost precision on GPUs that sample a plain varying exactly
        private const val SAMPLE_COLUMNS_FUNCTIONS = "vec4 sampleColumns(vec2 p, vec2 columns) {\n" +
                "    if (columns.x < 0.0) {\n" +
                "        return texture2D(tex, p);\n" +
                "    }\n" +
                "    return texture2D(tex, vec2(clamp(p.x, columns.x, columns.y), p.y));\n" +
                "}\n"

        // The frame texture stacks both screens top first with 2 empty lines between, FRAME_TEXTURE_HEIGHT by topScreenWidth DS pixels before
        // upscaling, or taller by the WideMelon camera's margin above and below the top screen (frameLayout has its rows); filters that need the
        // real texel size read texSize instead

        // tex, texSize and topScreenWidth (see above), as most filters declare them
        private const val SCREEN_UNIFORMS = "uniform sampler2D tex;\n" +
                "uniform vec2 texSize;\n" +
                "uniform float topScreenWidth;\n"

        // Helpers that clamp samples to the screen a fragment belongs to, so the gap and the other screen never bleed in (textureFunction is
        // texture2D, or texture for #version 300 es)
        private fun screenBandFunctions(textureFunction: String): String {
            return SCREEN_BAND_CONSTANTS +
                    SCREEN_COLUMN_RANGE_FUNCTIONS +
                    screenBandOnlyFunctions(textureFunction)
        }

        // From frameLayout, which SCREEN_COLUMN_RANGE_FUNCTIONS declares
        private const val SCREEN_BAND_CONSTANTS = "#define topScreenEnd frameLayout.x\n" +
                "#define bottomScreenStart frameLayout.y\n"

        // screenBandFunctions() without SCREEN_BAND_CONSTANTS and SCREEN_COLUMN_RANGE_FUNCTIONS, for a shader that declares those itself
        private fun screenBandOnlyFunctions(textureFunction: String): String {
            return "// Range of v (xy) and u (zw) coordinates (texel centers) of the screen that contains p\n" +
                    "vec4 screenBand(vec2 p) {\n" +
                    "    vec2 band = p.y < frameLayout.z ? vec2(0.0, topScreenEnd) : vec2(bottomScreenStart, 1.0);\n" +
                    "    float halfTexel = 0.5 / texSize.y;\n" +
                    "    return vec4(band + vec2(halfTexel, -halfTexel), screenColumns(p));\n" +
                    "}\n" +
                    "vec3 sampleScreen(vec2 p, vec4 band) {\n" +
                    "    return $textureFunction(tex, vec2(clamp(p.x, band.z, band.w), clamp(p.y, band.x, band.y))).bgr;\n" +
                    "}\n"
        }

        // Deband, after mpv's deband (itself based on flash3kyuu), with a per-sample threshold so that detail isn't blurred. Three rings of 4
        // samples around p, each twice as far as the last (a random angle and distance per output pixel); the farthest ring whose samples are
        // all within DEBAND_THRESHOLD of center replaces it with their average, so the 1 or 2 level steps of DS gradients blend and anything
        // with more contrast is left alone. Radii are in DS pixels (texSize is the sampled texture's size, frameLayout.w DS pixels high), so it
        // reaches as far at every internal resolution and output size. A little grain hides the 8 bit steps of the result. Needs
        // screenBandFunctions() first. DebandPrePassShader runs it once per frame texel; the Sharp and Linear screen shaders run it once per
        // screen pixel when DEBAND is defined (debug.melonade.debandfold)
        private const val DEBAND_FUNCTIONS = "// A bit over two 6 bit levels (one 5 bit level), per channel\n" +
                "const float DEBAND_THRESHOLD = 2.5 / 63.0;\n" +
                "const float DEBAND_RANGE = 3.0;\n" +
                "const float DEBAND_GRAIN = 1.0 / 255.0;\n" +
                "" +
                "// Hash without sine by Dave Hoskins (MIT), since sin() of big values is imprecise on mobile GPUs\n" +
                "float hash12(vec2 p) {\n" +
                "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n" +
                "    p3 += dot(p3, p3.yzx + 33.33);\n" +
                "    return fract((p3.x + p3.y) * p3.z);\n" +
                "}\n" +
                "" +
                "// Whether all 4 samples around p (offset and perpendicular, both ways) are close to center, with their average in average\n" +
                "bool debandRing(vec2 p, vec3 center, vec2 offset, vec2 perpendicular, vec4 band, out vec3 average) {\n" +
                "    vec3 s1 = sampleScreen(p + offset, band);\n" +
                "    vec3 s2 = sampleScreen(p - offset, band);\n" +
                "    vec3 s3 = sampleScreen(p + perpendicular, band);\n" +
                "    vec3 s4 = sampleScreen(p - perpendicular, band);\n" +
                "    vec3 d = max(max(abs(s1 - center), abs(s2 - center)), max(abs(s3 - center), abs(s4 - center)));\n" +
                "    average = (s1 + s2 + s3 + s4) * 0.25;\n" +
                "    return max(d.r, max(d.g, d.b)) < DEBAND_THRESHOLD;\n" +
                "}\n" +
                "" +
                "// center (RGB, as sampleScreen() reads it) debanded with the rings around p, clamped to 0-1\n" +
                "vec3 deband(vec2 p, vec3 center, vec4 band) {\n" +
                "    // Texels per DS pixel; the frame texture is frameLayout.w DS pixels high at any internal resolution (the constant without a\n" +
                "    // margin, as before it)\n" +
                "    float scale = frameLayout.w == ${FRAME_TEXTURE_HEIGHT}.0 ? texSize.y / ${FRAME_TEXTURE_HEIGHT}.0 : texSize.y / frameLayout.w;\n" +
                "    float angle = 6.2831853 * hash12(gl_FragCoord.xy);\n" +
                "    float radius = DEBAND_RANGE * scale * (0.5 + 0.5 * hash12(gl_FragCoord.yx + 17.0));\n" +
                "    vec2 offset = vec2(cos(angle), sin(angle)) * radius / texSize;\n" +
                "    // Rotated in texels, since the frame texture isn't square; scaling both by 2 or 4 later is exact\n" +
                "    vec2 texels = offset * texSize;\n" +
                "    vec2 perpendicular = vec2(-texels.y, texels.x) / texSize;\n" +
                "    vec3 result = center;\n" +
                "    vec3 average;\n" +
                "    // Farthest ring first, since it blends the most: the first that passes wins, so flat areas never sample the nearer ones\n" +
                "    if (debandRing(p, center, offset * 4.0, perpendicular * 4.0, band, average)) {\n" +
                "        result = average;\n" +
                "    } else if (debandRing(p, center, offset * 2.0, perpendicular * 2.0, band, average)) {\n" +
                "        result = average;\n" +
                "    } else if (debandRing(p, center, offset, perpendicular, band, average)) {\n" +
                "        result = average;\n" +
                "    }\n" +
                "    // Grain only where the color was changed, so flat areas and untouched detail stay clean\n" +
                "    if (result != center) {\n" +
                "        result += (hash12(gl_FragCoord.xy + 101.0) - 0.5) * DEBAND_GRAIN;\n" +
                "    }\n" +
                "    return clamp(result, 0.0, 1.0);\n" +
                "}\n"

        val BackgroundShader = ShaderProgramSource(
            TextureFiltering.LINEAR,
            "attribute vec2 vUV;\n" +
                    "attribute vec2 vPos;\n" +
                    "varying vec2 uv;\n" +
                    "void main()\n" +
                    "{\n" +
                    "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                    "    uv = vUV;\n" +
                    "}",
            "precision mediump float;\n" +
                    "uniform sampler2D tex;\n" +
                    "varying vec2 uv;\n" +
                    "void main()\n" +
                    "{\n" +
                    "    vec4 color = texture2D(tex, uv);\n" +
                    "    gl_FragColor = vec4(color.rgb, 1);\n" +
                    "}"
        )

        // Soft shadow around each screen; uv is the distance outside the screen in radii
        val ScreenShadowShader = ShaderProgramSource(
            TextureFiltering.LINEAR,
            DEFAULT_VERT_SHADER,
            "precision mediump float;\n" +
                    "varying vec2 uv;\n" +
                    "varying float alpha;\n" +
                    "void main()\n" +
                    "{\n" +
                    "    float dist = length(uv);\n" +
                    "    gl_FragColor = vec4(0.0, 0.0, 0.0, alpha * (1.0 - smoothstep(0.0, 1.0, dist)));\n" +
                    "}"
        )

        // Draws the premultiplied Lua overlay texture over a screen quad, blended with GL_ONE, GL_ONE_MINUS_SRC_ALPHA
        val OverlayShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
            DEFAULT_VERT_SHADER,
            PRECISION_HEADER +
                    "uniform sampler2D tex;\n" +
                    "varying vec2 uv;\n" +
                    "varying float alpha;\n" +
                    "void main()\n" +
                    "{\n" +
                    "    gl_FragColor = texture2D(tex, uv).bgra * alpha;\n" +
                    "}"
        )

        val NoFilterShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
            DEFAULT_VERT_SHADER,
            DEFAULT_FRAG_SHADER
        )

        // debug.melonade.comp1x: where a texel of the full frame is in a frame with screens the compositor drew at 1x (PresentFrameWrapper.halvesAt1x):
        // a texel in the rows of such a screen is its DS pixel in the 1x image at the start of those rows, any other one (the other screen, the
        // gap) is where it is. With filtered 2D those screens are drawn at the filter map's scale (halfScaleAt1x) instead, so the image is that
        // many times bigger and a texel is its pixel there, scale / halfScale times smaller. Needs texSize (the frame's size) declared first, and
        // highp ints
        // The top screen's half is taller by the WideMelon camera's margin (frameLayout.w is the frame's rows), and its image at 1x covers the
        // whole half
        private const val HALVES_AT_1X_FUNCTIONS = "// x: the screens at 1x (bit 0 the top one), y: the frame's internal scale, z: the scale they were drawn at\n" +
                "uniform ivec3 halvesAt1x;\n" +
                FRAME_LAYOUT_UNIFORM +
                "int topHalfRows() {\n" +
                "    return int(frameLayout.w + 0.5) - ${SCREEN_GAP + SCREEN_HEIGHT};\n" +
                "}\n" +
                "bool isInScreenAt1x(ivec2 texel) {\n" +
                "    int scale = halvesAt1x.y;\n" +
                "    int topRows = topHalfRows();\n" +
                "    int screen = texel.y >= (topRows + ${SCREEN_GAP}) * scale ? 1 : 0;\n" +
                "    int rows = screen == 0 ? topRows : ${SCREEN_HEIGHT};\n" +
                "    return texel.y - screen * (topRows + ${SCREEN_GAP}) * scale < rows * scale && (halvesAt1x.x & (1 << screen)) != 0;\n" +
                "}\n" +
                "ivec2 sourceAt1x(ivec2 texel) {\n" +
                "    int scale = halvesAt1x.y;\n" +
                "    int up = scale / max(halvesAt1x.z, 1);\n" +
                "    int topRows = topHalfRows();\n" +
                "    int screen = texel.y >= (topRows + ${SCREEN_GAP}) * scale ? 1 : 0;\n" +
                "    int start = screen * (topRows + ${SCREEN_GAP}) * scale;\n" +
                "    return ivec2(min(texel.x / up, int(texSize.x) / up - 1), start + (texel.y - start) / up);\n" +
                "}\n"

        // GLSL ES 3.00 versions of DEFAULT_VERT_SHADER and PRE_PASS_VERT_SHADER, for the 1x shaders below (texelFetch())
        private const val DEFAULT_VERT_SHADER_300 = "#version 300 es\n" +
                "in vec2 vUV;\n" +
                "in vec2 vPos;\n" +
                "in float vAlpha;\n" +
                "out vec2 uv;\n" +
                "out float alpha;\n" +
                "void main()\n" +
                "{\n" +
                "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                "    uv = vUV;\n" +
                "    alpha = vAlpha;\n" +
                "}"

        private const val PRE_PASS_VERT_SHADER_300 = "#version 300 es\n" +
                "in vec2 vPos;\n" +
                "in vec2 vUV;\n" +
                "out vec2 uv;\n" +
                "void main()\n" +
                "{\n" +
                "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                "    uv = vUV;\n" +
                "}"

        // debug.melonade.comp1x: the no-filter shader for a frame with screens the compositor drew at 1x. A pixel takes the texel the default
        // shader's nearest sample at uv picks: the sampler's own pick, read from coordXTex (the frame's width x 1, texel x holding x) and
        // coordYTex (1 x its height) with uv as the default shader declares and samples it (VideoFilterPipeline.bindHalvesAt1xCoordinates). Working
        // it out with floor(uv * texSize) isn't exact: on the edge between two texels (an integer ratio between the screen and the frame puts
        // samples there) it can choose the other one, which a screen drawn at the filter map's scale can show. In a screen at 1x it reads that
        // texel's DS pixel from the 1x image instead, and anywhere else it samples as the default shader does
        val NoFilterHalvesAt1xShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
            DEFAULT_VERT_SHADER_300,
            "#version 300 es\n" +
                    "precision mediump float;\n" +
                    "precision highp int;\n" +
                    "uniform sampler2D tex;\n" +
                    "uniform highp vec2 texSize;\n" +
                    "uniform highp usampler2D coordXTex;\n" +
                    "uniform highp usampler2D coordYTex;\n" +
                    HALVES_AT_1X_FUNCTIONS +
                    "// mediump, as the default shader's\n" +
                    "in vec2 uv;\n" +
                    "in float alpha;\n" +
                    "out vec4 fragColor;\n" +
                    COLOR_FUNCTIONS +
                    "void main()\n" +
                    "{\n" +
                    "    ivec2 texel = ivec2(int(texture(coordXTex, uv).r), int(texture(coordYTex, uv).r));\n" +
                    "    vec4 color = isInScreenAt1x(texel) ? texelFetch(tex, sourceAt1x(texel), 0) : texture(tex, uv);\n" +
                    "    fragColor = vec4(finishColor(color.bgr), alpha);\n" +
                    "}"
        )

        // debug.melonade.comp1x: pre-pass that turns a frame with screens at 1x into the full frame the compositor would have drawn, texel for
        // texel, so any filter and pre-pass can take it as it is. Its target is the frame's size, and the pre-pass quad puts uv on the target's
        // texel centers, so floor(uv * texSize) is each texel's own coordinate
        val ExpandHalvesAt1xPrePassShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
            PRE_PASS_VERT_SHADER_300,
            "#version 300 es\n" +
                    "precision highp float;\n" +
                    "precision highp int;\n" +
                    "uniform sampler2D tex;\n" +
                    "uniform vec2 texSize;\n" +
                    HALVES_AT_1X_FUNCTIONS +
                    "in vec2 uv;\n" +
                    "out vec4 fragColor;\n" +
                    "void main()\n" +
                    "{\n" +
                    "    ivec2 texel = ivec2(floor(uv * texSize));\n" +
                    "    fragColor = texelFetch(tex, isInScreenAt1x(texel) ? sourceAt1x(texel) : texel, 0);\n" +
                    "}"
        )

        // The default shader with bilinear filtering, which blends in the next texel, so it's kept off the black bars of a wide frame. With
        // DEBAND defined it debands its sample (DEBAND_FUNCTIONS) in place of the pre-pass (debug.melonade.debandfold)
        val LinearShader = ShaderProgramSource(
            TextureFiltering.LINEAR,
            DEFAULT_VERT_SHADER,
            PRECISION_HEADER +
                    SCREEN_UNIFORMS +
                    "varying vec2 uv;\n" +
                    "varying float alpha;\n" +
                    SCREEN_COLUMN_RANGE_FUNCTIONS +
                    SAMPLE_COLUMNS_FUNCTIONS +
                    "#ifdef DEBAND\n" +
                    SCREEN_BAND_CONSTANTS +
                    screenBandOnlyFunctions("texture2D") +
                    DEBAND_FUNCTIONS +
                    "#endif\n" +
                    COLOR_FUNCTIONS +
                    "void main()\n" +
                    "{\n" +
                    "    vec4 color = sampleColumns(uv, screenColumns(uv));\n" +
                    "    vec3 rgb = color.bgr;\n" +
                    "#ifdef DEBAND\n" +
                    "    rgb = deband(uv, rgb, screenBand(uv));\n" +
                    "#endif\n" +
                    "    gl_FragColor = vec4(finishColor(rgb), alpha);\n" +
                    "}"
        )

        // Author: Gigaherz
        // License: Public domain
        val LcdShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
                "attribute vec2 vPos;\n" +
                    "attribute vec2 vUV;\n" +
                    "attribute float vAlpha;\n" +
                    "varying vec2 uv;\n" +
                    "varying float alpha;\n" +
                    "varying vec2 omega;\n" +
                    "uniform float topScreenWidth;\n" +
                    FRAME_LAYOUT_UNIFORM +
                    "" +
                    "void main() {\n" +
                    "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                    "    uv = vUV;\n" +
                    "    alpha = vAlpha;\n" +
                    "    // The frame's rows, the constant without the WideMelon camera's margin as before it\n" +
                    "    if (frameLayout.w == ${FRAME_TEXTURE_HEIGHT}.0) {\n" +
                    "        omega = 3.141592654 * 2.0 * vec2(topScreenWidth, $FRAME_TEXTURE_HEIGHT);\n" +
                    "    } else {\n" +
                    "        omega = 3.141592654 * 2.0 * vec2(topScreenWidth, frameLayout.w);\n" +
                    "    }\n" +
                    "}",
            PRECISION_HEADER +
                    "uniform sampler2D tex;\n" +
                    "varying vec2 uv;\n" +
                    "varying float alpha;\n" +
                    "varying vec2 omega;\n" +
                    "" +
                    "/* configuration (higher values mean brighter image but reduced effect depth) */\n" +
                    "const float brighten_scanlines = 16.0;\n" +
                    "const float brighten_lcd = 4.0;\n" +
                    "" +
                    "const vec3 offsets = 3.141592654 * vec3(1.0/2.0,1.0/2.0 - 2.0/3.0,1.0/2.0-4.0/3.0);\n" +
                    COLOR_FUNCTIONS +
                    "" +
                    "void main() {\n" +
                    "    vec2 angle = uv * omega;\n" +
                    "" +
                    "    float yfactor = (brighten_scanlines + sin(angle.y)) / (brighten_scanlines + 1.0);\n" +
                    "    vec3 xfactors = (brighten_lcd + sin(angle.x + offsets)) / (brighten_lcd + 1.0);\n" +
                    "" +
                    "    gl_FragColor.rgb = finishColor(yfactor * xfactors * texture2D(tex, uv).bgr);\n" +
                    "    gl_FragColor.a = alpha;\n" +
                    "}"
        )

        // Author: Themaister
        // This code is hereby placed in the public domain.
        val ScanlinesShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
                "attribute vec2 vPos;\n" +
                    "attribute vec2 vUV;\n" +
                    "attribute float vAlpha;\n" +
                    "varying vec2 uv;\n" +
                    "varying float alpha;\n" +
                    "varying vec2 omega;\n" +
                    "uniform float topScreenWidth;\n" +
                    FRAME_LAYOUT_UNIFORM +
                    "" +
                    "void main()\n" +
                    "{\n" +
                    "    // In main(), since ES 1.00 globals can only start from constants. The frame's rows, the constant without the WideMelon\n" +
                    "    // camera's margin as before it\n" +
                    "    float frameRows = frameLayout.w == ${FRAME_TEXTURE_HEIGHT}.0 ? ${FRAME_TEXTURE_HEIGHT}.0 : frameLayout.w;\n" +
                    "    vec2 inputSize = vec2(topScreenWidth, frameRows);\n" +
                    "    vec2 outputSize = vec2(topScreenWidth, frameRows);\n" +
                    "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                    "    uv = vUV;\n" +
                    "    vec2 textureSize = vec2(topScreenWidth, frameRows);\n" +
                    "    alpha = vAlpha;\n" +
                    "    omega = vec2(3.1415 * outputSize.x * textureSize.x / inputSize.x, 2.0 * 3.1415 * textureSize.y);\n" +
                    "}",
            PRECISION_HEADER +
                    "" +
                    "uniform sampler2D tex;\n" +
                    "varying float alpha;\n" +
                    "varying vec2 uv;\n" +
                    "varying vec2 omega;\n" +
                    "" +
                    "const float base_brightness = 0.95;\n" +
                    "const vec2 sine_comp = vec2(0.05, 0.15);\n" +
                    COLOR_FUNCTIONS +
                    "" +
                    "void main ()\n" +
                    "{\n" +
                    "    vec4 color = vec4(texture2D(tex, uv).bgr, alpha);\n" +
                    "" +
                    "    vec4 scanline = clamp(color * (base_brightness + dot(sine_comp * sin(uv * omega), vec2(1.0))), 0.0, 1.0);\n" +
                    "    gl_FragColor = vec4(finishColor(scanline.rgb), scanline.a);\n" +
                    "}"
        )

        // Hyllian's 2xBR Shader
        //
        // Copyright (C) 2011 Hyllian/Jararaca - sergiogdb@gmail.com
        //
        // This program is free software; you can redistribute it and/or
        // modify it under the terms of the GNU General Public License
        // as published by the Free Software Foundation; either version 2
        // of the License, or (at your option) any later version.
        //
        // This program is distributed in the hope that it will be useful,
        // but WITHOUT ANY WARRANTY; without even the implied warranty of
        // MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        // GNU General Public License for more details.
        //
        // You should have received a copy of the GNU General Public License
        // along with this program; if not, write to the Free Software
        // Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
        val XbrShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
            DEFAULT_VERT_SHADER,
            PRECISION_HEADER +
                    SCREEN_UNIFORMS +
                    "varying vec2 uv;\n" +
                    "varying float alpha;\n" +
                    SCREEN_COLUMN_RANGE_FUNCTIONS +
                    SAMPLE_COLUMNS_FUNCTIONS +
                    "" +
                    "const vec3 dtt = vec3(65536.0, 255.0, 1.0);\n" +
                    "" +
                    "float reduce(vec3 color) {\n" +
                    "    return dot(color, dtt);\n" +
                    "}\n" +
                    COLOR_FUNCTIONS +
                    "" +
                    "void main() {\n" +
                    "    vec2 ps = 1.0 / texSize;\n" +
                    "    vec2 up = vec2(0.0, -ps.y);\n" +
                    "    vec2 left = vec2(-ps.x, 0.0);\n" +
                    "    vec2 fp = fract(uv * texSize);\n" +
                    "    vec2 columns = screenColumns(uv);\n" +
                    "" +
                    "    vec2 g1 = up * (step(0.5, fp.x) + step(0.5, fp.y) - 1.0) +\n" +
                    "            left * (step(0.5, fp.x) - step(0.5, fp.y));\n" +
                    "    vec2 g2 = up * (step(0.5, fp.y) - step(0.5, fp.x)) +\n" +
                    "            left * (step(0.5, fp.x) + step(0.5, fp.y) - 1.0);\n" +
                    "" +
                    "    vec3 B = sampleColumns(uv + g1     , columns).bgr;\n" +
                    "    vec3 C = sampleColumns(uv + g1 - g2, columns).bgr;\n" +
                    "    vec3 D = sampleColumns(uv      + g2, columns).bgr;\n" +
                    "    vec3 E = sampleColumns(uv          , columns).bgr;\n" +
                    "    vec3 F = sampleColumns(uv      - g2, columns).bgr;\n" +
                    "    vec3 G = sampleColumns(uv - g1 + g2, columns).bgr;\n" +
                    "    vec3 H = sampleColumns(uv - g1     , columns).bgr;\n" +
                    "    vec3 I = sampleColumns(uv - g1 - g2, columns).bgr;\n" +
                    "" +
                    "    float b = reduce(B);\n" +
                    "    float c = reduce(C);\n" +
                    "    float d = reduce(D);\n" +
                    "    float e = reduce(E);\n" +
                    "    float f = reduce(F);\n" +
                    "    float g = reduce(G);\n" +
                    "    float h = reduce(H);\n" +
                    "    float i = reduce(I);\n" +
                    "" +
                    "    vec3 color = E;\n" +
                    "" +
                    "    if (h==f && h!=e && ( e==g && (h==i || e==d) || e==c && (h==i || e==b) ))\n" +
                    "    {\n" +
                    "        color = mix(E, F, 0.5);\n" +
                    "    }\n" +
                    "    gl_FragColor = vec4(finishColor(color), alpha);\n" +
                    "}"
        )

        // 2xGLSLHqFilter shader
        //
        // Copyright (C) 2005 guest(r) - guest.r@gmail.com
        //
        // This program is free software; you can redistribute it and/or
        // modify it under the terms of the GNU General Public License
        // as published by the Free Software Foundation; either version 2
        // of the License, or (at your option) any later version.
        //
        // This program is distributed in the hope that it will be useful,
        // but WITHOUT ANY WARRANTY; without even the implied warranty of
        // MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        // GNU General Public License for more details.
        //
        // You should have received a copy of the GNU General Public License
        // along with this program; if not, write to the Free Software
        // Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
        val Hq2xShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
                "attribute vec2 vPos;\n" +
                    "attribute vec2 vUV;\n" +
                    "attribute float vAlpha;\n" +
                    "uniform vec2 texSize;\n" +
                    "uniform float topScreenWidth;\n" +
                    "varying vec4 uv[5];\n" +
                    "varying float alpha;\n" +
                    "varying vec2 columns;\n" +
                    SCREEN_COLUMN_RANGE_FUNCTIONS +
                    "" +
                    "void main() {\n" +
                    "    vec2 dg1 = 0.5 / texSize;\n" +
                    "    vec2 dg2 = vec2(-dg1.x, dg1.y);\n" +
                    "    vec2 dx = vec2(dg1.x, 0.0);\n" +
                    "    vec2 dy = vec2(0.0, dg1.y);\n" +
                    "" +
                    "    uv[0].xy = vUV;\n" +
                    "    uv[1].xy = vUV - dg1;\n" +
                    "    uv[1].zw = vUV - dy;\n" +
                    "    uv[2].xy = vUV - dg2;\n" +
                    "    uv[2].zw = vUV + dx;\n" +
                    "    uv[3].xy = vUV + dg1;\n" +
                    "    uv[3].zw = vUV + dy;\n" +
                    "    uv[4].xy = vUV + dg2;\n" +
                    "    uv[4].zw = vUV - dx;\n" +
                    "    alpha = vAlpha;\n" +
                    "    // The same for the whole quad, since a quad is one screen\n" +
                    "    columns = screenColumns(vUV);\n" +
                    "" +
                    "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                    "}",
            PRECISION_HEADER +
                    "uniform sampler2D tex;\n" +
                    "varying vec4 uv[5];\n" +
                    "varying float alpha;\n" +
                    "varying vec2 columns;\n" +
                    SAMPLE_COLUMNS_FUNCTIONS +
                    "" +
                    "    const float mx = 0.325;      // start smoothing wt.\n" +
                    "    const float k = -0.250;      // wt. decrease factor\n" +
                    "    const float max_w = 0.25;    // max filter weigth\n" +
                    "    const float min_w =-0.05;    // min filter weigth\n" +
                    "    const float lum_add = 0.25;  // effects smoothing\n" +
                    COLOR_FUNCTIONS +
                    "" +
                    "void main() {\n" +
                    "    vec3 c00 = sampleColumns(uv[1].xy, columns).xyz; \n" +
                    "    vec3 c10 = sampleColumns(uv[1].zw, columns).xyz; \n" +
                    "    vec3 c20 = sampleColumns(uv[2].xy, columns).xyz; \n" +
                    "    vec3 c01 = sampleColumns(uv[4].zw, columns).xyz; \n" +
                    "    vec3 c11 = sampleColumns(uv[0].xy, columns).xyz; \n" +
                    "    vec3 c21 = sampleColumns(uv[2].zw, columns).xyz; \n" +
                    "    vec3 c02 = sampleColumns(uv[4].xy, columns).xyz; \n" +
                    "    vec3 c12 = sampleColumns(uv[3].zw, columns).xyz; \n" +
                    "    vec3 c22 = sampleColumns(uv[3].xy, columns).xyz; \n" +
                    "    vec3 dt = vec3(1.0, 1.0, 1.0);\n" +
                    "" +
                    "    float md1 = dot(abs(c00 - c22), dt);\n" +
                    "    float md2 = dot(abs(c02 - c20), dt);\n" +
                    "" +
                    "    float w1 = dot(abs(c22 - c11), dt) * md2;\n" +
                    "    float w2 = dot(abs(c02 - c11), dt) * md1;\n" +
                    "    float w3 = dot(abs(c00 - c11), dt) * md2;\n" +
                    "    float w4 = dot(abs(c20 - c11), dt) * md1;\n" +
                    "" +
                    "    float t1 = w1 + w3;\n" +
                    "    float t2 = w2 + w4;\n" +
                    "    float ww = max(t1, t2) + 0.001;\n" +
                    "" +
                    "    c11 = (w1 * c00 + w2 * c20 + w3 * c22 + w4 * c02 + ww * c11) / (t1 + t2 + ww);\n" +
                    "" +
                    "    float lc1 = k / (0.12 * dot(c10 + c12 + c11, dt) + lum_add);\n" +
                    "    float lc2 = k / (0.12 * dot(c01 + c21 + c11, dt) + lum_add);\n" +
                    "" +
                    "    w1 = clamp(lc1 * dot(abs(c11 - c10), dt) + mx, min_w, max_w);\n" +
                    "    w2 = clamp(lc2 * dot(abs(c11 - c21), dt) + mx, min_w, max_w);\n" +
                    "    w3 = clamp(lc1 * dot(abs(c11 - c12), dt) + mx, min_w, max_w);\n" +
                    "    w4 = clamp(lc2 * dot(abs(c11 - c01), dt) + mx, min_w, max_w);\n" +
                    "" +
                    "    // Worked out in the BGR order of the texture\n" +
                    "    vec3 color = w1 * c10 + w2 * c21 + w3 * c12 + w4 * c01 + (1.0 - w1 - w2 - w3 - w4) * c11;\n" +
                    "    gl_FragColor = vec4(finishColor(color.bgr), alpha);\n" +
                    "}"
        )

        // 4xGLSLHqFilter shader
        //
        // Copyright (C) 2005 guest(r) - guest.r@gmail.com
        //
        // This program is free software; you can redistribute it and/or
        // modify it under the terms of the GNU General Public License
        // as published by the Free Software Foundation; either version 2
        // of the License, or (at your option) any later version.
        //
        // This program is distributed in the hope that it will be useful,
        // but WITHOUT ANY WARRANTY; without even the implied warranty of
        // MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        // GNU General Public License for more details.
        //
        // You should have received a copy of the GNU General Public License
        // along with this program; if not, write to the Free Software
        // Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
        val Hq4xShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
                "attribute vec2 vPos;\n" +
                    "attribute vec2 vUV;\n" +
                    "attribute float vAlpha;\n" +
                    "uniform vec2 texSize;\n" +
                    "uniform float topScreenWidth;\n" +
                    "varying vec4 uv[7];\n" +
                    "varying float alpha;\n" +
                    "varying vec2 columns;\n" +
                    SCREEN_COLUMN_RANGE_FUNCTIONS +
                    "" +
                    "void main()\n" +
                    "{\n" +
                    "    vec2 dg1 = 0.5 / texSize;\n" +
                    "    vec2 dg2 = vec2(-dg1.x, dg1.y);\n" +
                    "    vec2 sd1 = dg1 * 0.5;\n" +
                    "    vec2 sd2 = dg2 * 0.5;\n" +
                    "    vec2 ddx = vec2(dg1.x, 0.0);\n" +
                    "    vec2 ddy = vec2(0.0, dg1.y);\n" +
                    "" +
                    "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                    "    uv[0].xy = vUV;\n" +
                    "    uv[1].xy = vUV - sd1;\n" +
                    "    uv[2].xy = vUV - sd2;\n" +
                    "    uv[3].xy = vUV + sd1;\n" +
                    "    uv[4].xy = vUV + sd2;\n" +
                    "    uv[5].xy = vUV - dg1;\n" +
                    "    uv[6].xy = vUV + dg1;\n" +
                    "    uv[5].zw = vUV - dg2;\n" +
                    "    uv[6].zw = vUV + dg2;\n" +
                    "    uv[1].zw = vUV - ddy;\n" +
                    "    uv[2].zw = vUV + ddx;\n" +
                    "    uv[3].zw = vUV + ddy;\n" +
                    "    uv[4].zw = vUV - ddx;\n" +
                    "    alpha = vAlpha;\n" +
                    "    // The same for the whole quad, since a quad is one screen\n" +
                    "    columns = screenColumns(vUV);\n" +
                    "}",
            PRECISION_HEADER +
                    "uniform sampler2D tex;\n" +
                    "varying vec4 uv[7];\n" +
                    "varying float alpha;\n" +
                    "varying vec2 columns;\n" +
                    SAMPLE_COLUMNS_FUNCTIONS +
                    "" +
                    "const float mx = 1.00;      // start smoothing wt.\n" +
                    "const float k = -1.10;      // wt. decrease factor\n" +
                    "const float max_w = 0.75;   // max filter weigth\n" +
                    "const float min_w = 0.03;   // min filter weigth\n" +
                    "const float lum_add = 0.33; // effects smoothing\n" +
                    COLOR_FUNCTIONS +
                    "" +
                    "void main()\n" +
                    "{\n" +
                    "    vec3 c  = sampleColumns(uv[0].xy, columns).bgr;\n" +
                    "    vec3 i1 = sampleColumns(uv[1].xy, columns).bgr;\n" +
                    "    vec3 i2 = sampleColumns(uv[2].xy, columns).bgr;\n" +
                    "    vec3 i3 = sampleColumns(uv[3].xy, columns).bgr;\n" +
                    "    vec3 i4 = sampleColumns(uv[4].xy, columns).bgr;\n" +
                    "    vec3 o1 = sampleColumns(uv[5].xy, columns).bgr;\n" +
                    "    vec3 o3 = sampleColumns(uv[6].xy, columns).bgr;\n" +
                    "    vec3 o2 = sampleColumns(uv[5].zw, columns).bgr;\n" +
                    "    vec3 o4 = sampleColumns(uv[6].zw, columns).bgr;\n" +
                    "    vec3 s1 = sampleColumns(uv[1].zw, columns).bgr;\n" +
                    "    vec3 s2 = sampleColumns(uv[2].zw, columns).bgr;\n" +
                    "    vec3 s3 = sampleColumns(uv[3].zw, columns).bgr;\n" +
                    "    vec3 s4 = sampleColumns(uv[4].zw, columns).bgr;\n" +
                    "    vec3 dt = vec3(1.0,1.0,1.0);\n" +
                    "" +
                    "    float ko1=dot(abs(o1-c),dt);\n" +
                    "    float ko2=dot(abs(o2-c),dt);\n" +
                    "    float ko3=dot(abs(o3-c),dt);\n" +
                    "    float ko4=dot(abs(o4-c),dt);\n" +
                    "" +
                    "    float k1=min(dot(abs(i1-i3),dt),max(ko1,ko3));\n" +
                    "    float k2=min(dot(abs(i2-i4),dt),max(ko2,ko4));\n" +
                    "" +
                    "    float w1 = k2; if(ko3<ko1) w1*=ko3/ko1;\n" +
                    "    float w2 = k1; if(ko4<ko2) w2*=ko4/ko2;\n" +
                    "    float w3 = k2; if(ko1<ko3) w3*=ko1/ko3;\n" +
                    "    float w4 = k1; if(ko2<ko4) w4*=ko2/ko4;\n" +
                    "" +
                    "    c=(w1*o1+w2*o2+w3*o3+w4*o4+0.001*c)/(w1+w2+w3+w4+0.001);\n" +
                    "" +
                    "    w1 = k*dot(abs(i1-c)+abs(i3-c),dt)/(0.125*dot(i1+i3,dt)+lum_add);\n" +
                    "    w2 = k*dot(abs(i2-c)+abs(i4-c),dt)/(0.125*dot(i2+i4,dt)+lum_add);\n" +
                    "    w3 = k*dot(abs(s1-c)+abs(s3-c),dt)/(0.125*dot(s1+s3,dt)+lum_add);\n" +
                    "    w4 = k*dot(abs(s2-c)+abs(s4-c),dt)/(0.125*dot(s2+s4,dt)+lum_add);\n" +
                    "" +
                    "    w1 = clamp(w1+mx,min_w,max_w); \n" +
                    "    w2 = clamp(w2+mx,min_w,max_w);\n" +
                    "    w3 = clamp(w3+mx,min_w,max_w); \n" +
                    "    w4 = clamp(w4+mx,min_w,max_w);\n" +
                    "" +
                    "    gl_FragColor = vec4(finishColor((w1*(i1+i3)+w2*(i2+i4)+w3*(s1+s3)+w4*(s2+s4)+c)/(2.0*(w1+w2+w3+w4)+1.0)), alpha);\n" +
                    "}"
        )

        // Fragment shader based on "Improved texture interpolation" by Iñigo Quílez
        // Original description: http://www.iquilezles.org/www/articles/texture/texture.htm
        val QuilezShader = ShaderProgramSource(
            TextureFiltering.LINEAR,
            DEFAULT_VERT_SHADER,
            PRECISION_HEADER +
                    SCREEN_UNIFORMS +
                    "varying float alpha;\n" +
                    "varying vec2 uv;\n" +
                    SCREEN_COLUMN_RANGE_FUNCTIONS +
                    SAMPLE_COLUMNS_FUNCTIONS +
                    "" +
                    "vec4 getTexel(vec2 p) {\n" +
                    "    p = p * texSize + vec2(0.5);\n" +
                    "" +
                    "    vec2 i = floor(p);\n" +
                    "    vec2 f = p - i;\n" +
                    "    f = f * f * f * (f * (f * 6.0 - vec2(15.0)) + vec2(10.0));\n" +
                    "    p = i + f;\n" +
                    "" +
                    "    p = (p - vec2(0.5)) / texSize;\n" +
                    "    return sampleColumns(p, screenColumns(uv));\n" +
                    "}\n" +
                    COLOR_FUNCTIONS +
                    "" +
                    "void main() {\n" +
                    "    gl_FragColor = vec4(finishColor(getTexel(uv).bgr), alpha);\n" +
                    "}"
        )

        // Scale2x (also known as EPX) by Andrea Mazzoleni
        // Algorithm description: https://www.scale2x.it/algorithm
        // Each texel is drawn as 4 quadrants, each taking the color of its 2 neighbors when they match and the other 2 don't
        val Scale2xShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
            DEFAULT_VERT_SHADER,
            PRECISION_HEADER +
                    SCREEN_UNIFORMS +
                    "varying vec2 uv;\n" +
                    "varying float alpha;\n" +
                    screenBandFunctions("texture2D") +
                    COLOR_FUNCTIONS +
                    "" +
                    "void main() {\n" +
                    "    vec2 ps = 1.0 / texSize;\n" +
                    "    vec4 band = screenBand(uv);\n" +
                    "" +
                    "    //   A\n" +
                    "    // C P B\n" +
                    "    //   D\n" +
                    "    vec3 P = sampleScreen(uv, band);\n" +
                    "    vec3 A = sampleScreen(uv - vec2(0.0, ps.y), band);\n" +
                    "    vec3 B = sampleScreen(uv + vec2(ps.x, 0.0), band);\n" +
                    "    vec3 C = sampleScreen(uv - vec2(ps.x, 0.0), band);\n" +
                    "    vec3 D = sampleScreen(uv + vec2(0.0, ps.y), band);\n" +
                    "" +
                    "    // Neighbors next to the quadrant this fragment is in, and the ones on the opposite sides\n" +
                    "    vec2 fp = fract(uv * texSize);\n" +
                    "    bool isLeft = fp.x < 0.5;\n" +
                    "    bool isUp = fp.y < 0.5;\n" +
                    "    vec3 h = isLeft ? C : B;\n" +
                    "    vec3 hOpposite = isLeft ? B : C;\n" +
                    "    vec3 v = isUp ? A : D;\n" +
                    "    vec3 vOpposite = isUp ? D : A;\n" +
                    "" +
                    "    vec3 color = P;\n" +
                    "    if (h == v && h != vOpposite && v != hOpposite) {\n" +
                    "        color = v;\n" +
                    "    }\n" +
                    "    gl_FragColor = vec4(finishColor(color), alpha);\n" +
                    "}"
        )

        // FXAA by Timothy Lottes (NVIDIA), the original simple version
        // Pre-pass; runs once per frame texel, blurring along the edge direction found from the 4 diagonal neighbors
        val FxaaPrePassShader = ShaderProgramSource(
            TextureFiltering.LINEAR,
            PRE_PASS_VERT_SHADER,
            PRECISION_HEADER +
                    SCREEN_UNIFORMS +
                    "varying vec2 uv;\n" +
                    screenBandFunctions("texture2D") +
                    "" +
                    "const float FXAA_SPAN_MAX = 8.0;\n" +
                    "const float FXAA_REDUCE_MUL = 1.0 / 8.0;\n" +
                    "const float FXAA_REDUCE_MIN = 1.0 / 128.0;\n" +
                    "// Minimum local contrast to count as an edge, relative to the brightest sample and absolute (for dark areas)\n" +
                    "const float FXAA_EDGE_THRESHOLD = 1.0 / 8.0;\n" +
                    "const float FXAA_EDGE_THRESHOLD_MIN = 1.0 / 16.0;\n" +
                    "const vec3 luma = vec3(0.299, 0.587, 0.114);\n" +
                    "" +
                    "void main() {\n" +
                    "    vec2 rcpFrame = 1.0 / texSize;\n" +
                    "    vec4 band = screenBand(uv);\n" +
                    "" +
                    "    vec3 rgbNW = sampleScreen(uv + vec2(-0.5, -0.5) * rcpFrame, band);\n" +
                    "    vec3 rgbNE = sampleScreen(uv + vec2(0.5, -0.5) * rcpFrame, band);\n" +
                    "    vec3 rgbSW = sampleScreen(uv + vec2(-0.5, 0.5) * rcpFrame, band);\n" +
                    "    vec3 rgbSE = sampleScreen(uv + vec2(0.5, 0.5) * rcpFrame, band);\n" +
                    "    vec3 rgbM = sampleScreen(uv, band);\n" +
                    "" +
                    "    float lumaNW = dot(rgbNW, luma);\n" +
                    "    float lumaNE = dot(rgbNE, luma);\n" +
                    "    float lumaSW = dot(rgbSW, luma);\n" +
                    "    float lumaSE = dot(rgbSE, luma);\n" +
                    "    float lumaM = dot(rgbM, luma);\n" +
                    "    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));\n" +
                    "    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));\n" +
                    "    if (lumaMax - lumaMin < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD)) {\n" +
                    "        gl_FragColor = vec4(rgbM.bgr, 1.0);\n" +
                    "        return;\n" +
                    "    }\n" +
                    "" +
                    "    vec2 dir;\n" +
                    "    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));\n" +
                    "    dir.y = (lumaNW + lumaSW) - (lumaNE + lumaSE);\n" +
                    "" +
                    "    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);\n" +
                    "    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);\n" +
                    "    dir = min(vec2(FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX), dir * rcpDirMin)) * rcpFrame;\n" +
                    "" +
                    "    vec3 rgbA = 0.5 * (\n" +
                    "        sampleScreen(uv + dir * (1.0 / 3.0 - 0.5), band) +\n" +
                    "        sampleScreen(uv + dir * (2.0 / 3.0 - 0.5), band));\n" +
                    "    vec3 rgbB = rgbA * 0.5 + 0.25 * (\n" +
                    "        sampleScreen(uv + dir * -0.5, band) +\n" +
                    "        sampleScreen(uv + dir * 0.5, band));\n" +
                    "    float lumaB = dot(rgbB, luma);\n" +
                    "" +
                    "    vec3 color = rgbB;\n" +
                    "    if (lumaB < lumaMin || lumaB > lumaMax) {\n" +
                    "        color = rgbA;\n" +
                    "    }\n" +
                    "    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB\n" +
                    "    gl_FragColor = vec4(color.bgr, 1.0);\n" +
                    "}"
        )

        // Deband (DEBAND_FUNCTIONS) as a pre-pass, once per frame texel before the selected filter, so it works with every filter and the
        // filter's pre-passes (like MMPX's pixel-art detection) see the smoothed frame
        val DebandPrePassShader = ShaderProgramSource(
            TextureFiltering.LINEAR,
            PRE_PASS_VERT_SHADER,
            PRECISION_HEADER +
                    SCREEN_UNIFORMS +
                    "varying vec2 uv;\n" +
                    screenBandFunctions("texture2D") +
                    DEBAND_FUNCTIONS +
                    "" +
                    "void main() {\n" +
                    "    vec4 band = screenBand(uv);\n" +
                    "    vec3 center = sampleScreen(uv, band);\n" +
                    "    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB\n" +
                    "    gl_FragColor = vec4(deband(uv, center, band).bgr, 1.0);\n" +
                    "}"
        )

        // What the tilt-shift pass that doesn't apply the color boost does with a texel it has finished
        private const val TILT_SHIFT_NO_BOOST_FUNCTION = "vec3 finishTiltShift(vec3 color) {\n" +
                "    return color;\n" +
                "}\n"

        // What the last tilt-shift pass does instead: saturation (x) as a mix with the luma and contrast (y) around 0.5, on RGB in its real order
        private const val TILT_SHIFT_BOOST_FUNCTION = "uniform vec2 tiltShiftBoost;\n" +
                "const vec3 BOOST_LUMA = vec3(0.299, 0.587, 0.114);\n" +
                "vec3 finishTiltShift(vec3 color) {\n" +
                "    vec3 saturated = mix(vec3(dot(color, BOOST_LUMA)), color, tiltShiftBoost.x);\n" +
                "    return clamp((saturated - 0.5) * tiltShiftBoost.y + 0.5, 0.0, 1.0);\n" +
                "}\n"

        // One direction of the tilt-shift blur: a pre-pass that only changes the DS top screen, where the blur's radius grows from 0 at the sharp
        // band to tiltShift.zw at the screen's edges. 13 Gaussian samples, as far apart as that row's radius needs, kept inside the part of the
        // screen that's drawn. Radii are in DS pixels, so it reaches as far at every internal resolution. finishFunction runs on every texel of the
        // screen, the sharp band's too
        private fun tiltShiftPrePassShader(direction: String, finishFunction: String): ShaderProgramSource {
            return ShaderProgramSource(
                TextureFiltering.LINEAR,
                PRE_PASS_VERT_SHADER,
                PRECISION_HEADER +
                        SCREEN_UNIFORMS +
                        "varying vec2 uv;\n" +
                        "// Sharp band's edges as fractions of the screen's height from its top (xy), and the blur's radius in DS pixels at the screen's top and bottom edges (zw)\n" +
                        "uniform vec4 tiltShift;\n" +
                        screenBandFunctions("texture2D") +
                        "" +
                        "const vec2 BLUR_DIRECTION = $direction;\n" +
                        "// Below this many texels the blur can't be seen, so the row skips it\n" +
                        "const float MIN_BLUR_RADIUS = 0.1;\n" +
                        "// A Gaussian whose sigma is the radius / 2.5 at offsets of 0 to 6 sixths of the radius, adding up to 1\n" +
                        "const float WEIGHT_0 = 0.167292;\n" +
                        "const float WEIGHT_1 = 0.153382;\n" +
                        "const float WEIGHT_2 = 0.118217;\n" +
                        "const float WEIGHT_3 = 0.076592;\n" +
                        "const float WEIGHT_4 = 0.041715;\n" +
                        "const float WEIGHT_5 = 0.019098;\n" +
                        "const float WEIGHT_6 = 0.007350;\n" +
                        finishFunction +
                        "" +
                        "float blurRadius(float y) {\n" +
                        "    // y is from 0 to just under 1, so neither division can be by 0\n" +
                        "    if (y < tiltShift.x) {\n" +
                        "        return tiltShift.z * smoothstep(0.0, 1.0, (tiltShift.x - y) / tiltShift.x);\n" +
                        "    }\n" +
                        "    if (y > tiltShift.y) {\n" +
                        "        return tiltShift.w * smoothstep(0.0, 1.0, (y - tiltShift.y) / (1.0 - tiltShift.y));\n" +
                        "    }\n" +
                        "    return 0.0;\n" +
                        "}\n" +
                        "" +
                        "void main() {\n" +
                        "    // The gap and the DS bottom screen stay as they are\n" +
                        "    if (uv.y >= topScreenEnd) {\n" +
                        "        gl_FragColor = vec4(texture2D(tex, uv).rgb, 1.0);\n" +
                        "        return;\n" +
                        "    }\n" +
                        "" +
                        "    // Texels per DS pixel; the frame texture is frameLayout.w DS pixels high at any internal resolution. The band is placed on the\n" +
                        "    // top screen's SCREEN_HEIGHT rows, inside the WideMelon camera's margin (none: the old math exactly)\n" +
                        "    float radius;\n" +
                        "    if (frameLayout.w == ${FRAME_TEXTURE_HEIGHT}.0) {\n" +
                        "        radius = blurRadius(uv.y / topScreenEnd) * texSize.y / ${FRAME_TEXTURE_HEIGHT}.0;\n" +
                        "    } else {\n" +
                        "        float margin = (frameLayout.w - ${FRAME_TEXTURE_HEIGHT}.0) * 0.5;\n" +
                        "        float screenY = clamp((uv.y * frameLayout.w - margin) / ${SCREEN_HEIGHT}.0, 0.0, 0.999);\n" +
                        "        radius = blurRadius(screenY) * texSize.y / frameLayout.w;\n" +
                        "    }\n" +
                        "    if (radius < MIN_BLUR_RADIUS) {\n" +
                        "        gl_FragColor = vec4(finishTiltShift(texture2D(tex, uv).bgr).bgr, 1.0);\n" +
                        "        return;\n" +
                        "    }\n" +
                        "" +
                        "    vec4 band = screenBand(uv);\n" +
                        "    vec2 spacing = BLUR_DIRECTION * radius / 6.0 / texSize;\n" +
                        "    vec3 color = WEIGHT_0 * sampleScreen(uv, band);\n" +
                        "    color += WEIGHT_1 * (sampleScreen(uv - spacing, band) + sampleScreen(uv + spacing, band));\n" +
                        "    color += WEIGHT_2 * (sampleScreen(uv - spacing * 2.0, band) + sampleScreen(uv + spacing * 2.0, band));\n" +
                        "    color += WEIGHT_3 * (sampleScreen(uv - spacing * 3.0, band) + sampleScreen(uv + spacing * 3.0, band));\n" +
                        "    color += WEIGHT_4 * (sampleScreen(uv - spacing * 4.0, band) + sampleScreen(uv + spacing * 4.0, band));\n" +
                        "    color += WEIGHT_5 * (sampleScreen(uv - spacing * 5.0, band) + sampleScreen(uv + spacing * 5.0, band));\n" +
                        "    color += WEIGHT_6 * (sampleScreen(uv - spacing * 6.0, band) + sampleScreen(uv + spacing * 6.0, band));\n" +
                        "    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB\n" +
                        "    gl_FragColor = vec4(finishTiltShift(color).bgr, 1.0);\n" +
                        "}"
            )
        }

        // Built when a script first asks for the blur (see VideoFilterPipeline), which runs them in this order, so the color boost comes last
        val TiltShiftHorizontalPrePassShader get() = tiltShiftPrePassShader("vec2(1.0, 0.0)", TILT_SHIFT_NO_BOOST_FUNCTION)
        val TiltShiftVerticalPrePassShader get() = tiltShiftPrePassShader("vec2(0.0, 1.0)", TILT_SHIFT_BOOST_FUNCTION)

        // What the blur's last pass does to a sharp texel, and nothing else, for when there's no blur to draw: both give the same colors, so going
        // between them shows no step. Built when a script first asks for only the color boost (see VideoFilterPipeline)
        val TiltShiftColorPrePassShader get() = ShaderProgramSource(
            TextureFiltering.LINEAR,
            PRE_PASS_VERT_SHADER,
            PRECISION_HEADER +
                    "uniform sampler2D tex;\n" +
                    "varying vec2 uv;\n" +
                    FRAME_LAYOUT_UNIFORM +
                    "#define topScreenEnd frameLayout.x\n" +
                    TILT_SHIFT_BOOST_FUNCTION +
                    "" +
                    "void main() {\n" +
                    "    vec3 texel = texture2D(tex, uv).rgb;\n" +
                    "    // The gap and the DS bottom screen stay as they are\n" +
                    "    if (uv.y >= topScreenEnd) {\n" +
                    "        gl_FragColor = vec4(texel, 1.0);\n" +
                    "        return;\n" +
                    "    }\n" +
                    "" +
                    "    gl_FragColor = vec4(finishTiltShift(texel.bgr).bgr, 1.0);\n" +
                    "}"
        )

        // The bloom's values (Shader.setBloom): strength (x), threshold (y) and radius in DS pixels (z)
        private const val BLOOM_UNIFORM = "uniform vec3 bloom;\n"

        // A texel of the frame's depth texture (PresentFrameWrapper.depthTextureId, the compositor's kCompositorDepthFS) as a depth from 0 to 1:
        // the DS's 24-bit depth in RGB, high byte in R, so 0 is 2D drawn over the 3D and 1 is where what's behind the 3D shows. A holds the
        // compositor's flags (1 for 2D, 2 for behind the 3D, 4 for a W-buffered 3D pixel, 8 for 3D drawn under another camera than
        // PresentFrameWrapper.depthCamera, 16 for a pixel in a character's shadow, which the renderer draws, 32 for one a light texture drew,
        // melon.gpu.light_textures), which the sun shadows and bloom's lights read. The
        // two ends are exact and every other depth stays below 1, however the GPU divides, so a test like depth < 1.0 tells real 3D from what's
        // behind it
        private const val DEPTH_DECODE_FUNCTION = "highp float decodeDepth(highp vec4 texel) {\n" +
                "    highp vec3 bytes = floor(texel.rgb * 255.0 + 0.5);\n" +
                "    highp float value = bytes.r * 65536.0 + bytes.g * 256.0 + bytes.b;\n" +
                "    if (value >= 16777215.0) {\n" +
                "        return 1.0;\n" +
                "    }\n" +
                "    // The largest float below 1\n" +
                "    return min(value / 16777215.0, 0.99999994);\n" +
                "}\n"

        // The same texel's 24-bit depth as a whole number (depthValue), which highp floats hold exactly up to 2^24, so depths can be averaged
        // as differences from one of them without losing steps; isValue3D() is decodeDepth()'s test for real 3D, depth > 0 and < 1. encodeDepth()
        // is the inverse of depthValue(), the value rounded to a whole number from 0 to 16777215 as RGB, exact since it only divides by powers
        // of 2. For the shading's passes, whose targets hold depths the same way
        private const val DEPTH_VALUE_FUNCTIONS = "highp float depthValue(highp vec4 texel) {\n" +
                "    highp vec3 bytes = floor(texel.rgb * 255.0 + 0.5);\n" +
                "    return bytes.r * 65536.0 + bytes.g * 256.0 + bytes.b;\n" +
                "}\n" +
                "bool isValue3D(highp float value) {\n" +
                "    return value > 0.0 && value < 16777215.0;\n" +
                "}\n" +
                "highp vec3 encodeDepth(highp float value) {\n" +
                "    value = clamp(floor(value + 0.5), 0.0, 16777215.0);\n" +
                "    highp float high = floor(value / 65536.0);\n" +
                "    highp float middle = floor((value - high * 65536.0) / 256.0);\n" +
                "    highp float low = value - high * 65536.0 - middle * 256.0;\n" +
                "    return vec3(high, middle, low) / 255.0;\n" +
                "}\n"

        // The depth of field's focus as DepthOfFieldFocusShader stores it (needs DEPTH_DECODE_FUNCTION first): its depth in RGB as the depth
        // texture has it, all 0 for none, which decodes to negative
        private const val FOCUS_DECODE_FUNCTION = "highp float decodeFocus(highp vec4 texel) {\n" +
                "    highp float depth = decodeDepth(texel);\n" +
                "    return depth > 0.0 ? depth : -1.0;\n" +
                "}\n"

        // Black, for the texels of a bloom target outside the DS top screen, which never glow
        private const val BLOOM_BLACK = "vec4(0.0, 0.0, 0.0, 1.0)"

        // The first bloom pass, at DS-native resolution (VideoFilterPipeline makes its target topScreenWidth x frameLayout.w): each texel is the
        // average of the input texels it covers (four bilinear taps, exact at 2x and 4x), then only what's brighter than the threshold is kept,
        // easing in over a soft knee so the glow has no hard edge. Only the DS top screen; the rest is black. With ONLY_3D defined
        // (VideoFilterPipeline, for bloom's only_3d) each tap counts only where the frame's depth (depthTex, unit 1, nearest, read with
        // DEPTH_DECODE_FUNCTION) is 3D, still averaged over all four so the glow fades at the 3D's edges. With LIGHTS defined (bloom's
        // lights) a tap whose depth texel is 3D with flag 32 (a light texture, melon.gpu.light_textures) leaves the luma average and glows on
        // its own instead: by its brightest channel over the same soft knee at bloomLights.x, times bloomLights.y, a quarter for each tap
        val BloomBrightPrePassShader get() = ShaderProgramSource(
            TextureFiltering.LINEAR,
            PRE_PASS_VERT_SHADER,
            PRECISION_HEADER +
                    SCREEN_UNIFORMS +
                    "varying vec2 uv;\n" +
                    BLOOM_UNIFORM +
                    screenBandFunctions("texture2D") +
                    "" +
                    "const vec3 BLOOM_LUMA = vec3(0.299, 0.587, 0.114);\n" +
                    "// Half the soft knee's width, as a luma: the glow eases in from threshold - BLOOM_KNEE to threshold + BLOOM_KNEE\n" +
                    "const float BLOOM_KNEE = 0.1;\n" +
                    "" +
                    "#ifdef LIGHTS\n" +
                    "uniform highp sampler2D depthTex;\n" +
                    "// The lights' threshold, as a brightest channel (x), and strength (y)\n" +
                    "uniform vec2 bloomLights;\n" +
                    DEPTH_DECODE_FUNCTION +
                    "// sampleScreen(p, band) for the luma average (black where the depth isn't 3D with ONLY_3D), or black for a light, whose own\n" +
                    "// glow by its brightest channel goes into lights instead\n" +
                    "vec3 sampleTap(vec2 p, vec4 band, inout vec3 lights) {\n" +
                    "    vec2 q = vec2(clamp(p.x, band.z, band.w), clamp(p.y, band.x, band.y));\n" +
                    "    highp vec4 depthTexel = texture2D(depthTex, vec2(q.x, clamp(q.y / topScreenEnd, 0.0, 1.0)));\n" +
                    "    highp float depth = decodeDepth(depthTexel);\n" +
                    "    bool is3D = depth > 0.0 && depth < 1.0;\n" +
                    "    float flags = floor(depthTexel.a * 255.0 + 0.5);\n" +
                    "    if (is3D && mod(floor(flags / 32.0), 2.0) == 1.0) {\n" +
                    "        vec3 light = texture2D(tex, q).bgr;\n" +
                    "        float peak = max(light.r, max(light.g, light.b));\n" +
                    "        float knee = clamp(peak - bloomLights.x + BLOOM_KNEE, 0.0, 2.0 * BLOOM_KNEE);\n" +
                    "        knee = knee * knee / (4.0 * BLOOM_KNEE);\n" +
                    "        lights += light * (max(knee, peak - bloomLights.x) / max(peak, 0.0001));\n" +
                    "        return vec3(0.0);\n" +
                    "    }\n" +
                    "#ifdef ONLY_3D\n" +
                    "    return is3D ? texture2D(tex, q).bgr : vec3(0.0);\n" +
                    "#else\n" +
                    "    return sampleScreen(p, band);\n" +
                    "#endif\n" +
                    "}\n" +
                    "#elif defined(ONLY_3D)\n" +
                    "uniform highp sampler2D depthTex;\n" +
                    DEPTH_DECODE_FUNCTION +
                    "// sampleScreen(p, band) where the frame's depth at that texel is 3D, black for 2D over the 3D and for what's behind it\n" +
                    "vec3 sampleTap(vec2 p, vec4 band) {\n" +
                    "    vec2 q = vec2(clamp(p.x, band.z, band.w), clamp(p.y, band.x, band.y));\n" +
                    "    highp float depth = decodeDepth(texture2D(depthTex, vec2(q.x, clamp(q.y / topScreenEnd, 0.0, 1.0))));\n" +
                    "    return depth > 0.0 && depth < 1.0 ? texture2D(tex, q).bgr : vec3(0.0);\n" +
                    "}\n" +
                    "#else\n" +
                    "vec3 sampleTap(vec2 p, vec4 band) {\n" +
                    "    return sampleScreen(p, band);\n" +
                    "}\n" +
                    "#endif\n" +
                    "" +
                    "void main() {\n" +
                    "    vec4 band = screenBand(uv);\n" +
                    "    // The gap, the DS bottom screen, and the sides beside the top screen when only the bottom one is wide\n" +
                    "    if (uv.y >= topScreenEnd || (band.z >= 0.0 && (uv.x < band.z || uv.x > band.w))) {\n" +
                    "        gl_FragColor = $BLOOM_BLACK;\n" +
                    "        return;\n" +
                    "    }\n" +
                    "" +
                    "    // A quarter of this pass's texel, which is one DS pixel\n" +
                    "    vec2 tap = 0.25 / vec2(topScreenWidth, frameLayout.w);\n" +
                    "#ifdef LIGHTS\n" +
                    "    vec3 lights = vec3(0.0);\n" +
                    "    vec3 color = 0.25 * (\n" +
                    "        sampleTap(uv - tap, band, lights) + sampleTap(uv + tap, band, lights) +\n" +
                    "        sampleTap(uv + vec2(tap.x, -tap.y), band, lights) + sampleTap(uv + vec2(-tap.x, tap.y), band, lights));\n" +
                    "#else\n" +
                    "    vec3 color = 0.25 * (\n" +
                    "        sampleTap(uv - tap, band) + sampleTap(uv + tap, band) +\n" +
                    "        sampleTap(uv + vec2(tap.x, -tap.y), band) + sampleTap(uv + vec2(-tap.x, tap.y), band));\n" +
                    "#endif\n" +
                    "    float luma = dot(color, BLOOM_LUMA);\n" +
                    "    float knee = clamp(luma - bloom.y + BLOOM_KNEE, 0.0, 2.0 * BLOOM_KNEE);\n" +
                    "    knee = knee * knee / (4.0 * BLOOM_KNEE);\n" +
                    "    float weight = max(knee, luma - bloom.y) / max(luma, 0.0001);\n" +
                    "    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB\n" +
                    "#ifdef LIGHTS\n" +
                    "    gl_FragColor = vec4((color * weight + 0.25 * bloomLights.y * lights).bgr, 1.0);\n" +
                    "#else\n" +
                    "    gl_FragColor = vec4((color * weight).bgr, 1.0);\n" +
                    "#endif\n" +
                    "}"
        )

        // One direction of the bloom's blur, at DS-native resolution like the bright pass: a Gaussian whose sigma is half the radius, from 9
        // bilinear samples a quarter of the radius apart (at most 2 texels, what bilinear filtering can bridge), kept inside the DS top screen
        private fun bloomBlurPrePassShader(direction: String): ShaderProgramSource {
            return ShaderProgramSource(
                TextureFiltering.LINEAR,
                PRE_PASS_VERT_SHADER,
                PRECISION_HEADER +
                        SCREEN_UNIFORMS +
                        "varying vec2 uv;\n" +
                        BLOOM_UNIFORM +
                        screenBandFunctions("texture2D") +
                        "" +
                        "const vec2 BLUR_DIRECTION = $direction;\n" +
                        "// A Gaussian whose sigma is 2 samples at offsets of 0 to 4 samples, adding up to 1\n" +
                        "const float WEIGHT_0 = 0.204162;\n" +
                        "const float WEIGHT_1 = 0.180174;\n" +
                        "const float WEIGHT_2 = 0.123832;\n" +
                        "const float WEIGHT_3 = 0.066282;\n" +
                        "const float WEIGHT_4 = 0.027631;\n" +
                        "" +
                        "void main() {\n" +
                        "    // Only the DS top screen has a glow\n" +
                        "    if (uv.y >= topScreenEnd) {\n" +
                        "        gl_FragColor = $BLOOM_BLACK;\n" +
                        "        return;\n" +
                        "    }\n" +
                        "" +
                        "    // This pass's texels are DS pixels\n" +
                        "    vec4 band = screenBand(uv);\n" +
                        "    vec2 spacing = BLUR_DIRECTION * bloom.z / 4.0 / texSize;\n" +
                        "    vec3 color = WEIGHT_0 * sampleScreen(uv, band);\n" +
                        "    color += WEIGHT_1 * (sampleScreen(uv - spacing, band) + sampleScreen(uv + spacing, band));\n" +
                        "    color += WEIGHT_2 * (sampleScreen(uv - spacing * 2.0, band) + sampleScreen(uv + spacing * 2.0, band));\n" +
                        "    color += WEIGHT_3 * (sampleScreen(uv - spacing * 3.0, band) + sampleScreen(uv + spacing * 3.0, band));\n" +
                        "    color += WEIGHT_4 * (sampleScreen(uv - spacing * 4.0, band) + sampleScreen(uv + spacing * 4.0, band));\n" +
                        "    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB\n" +
                        "    gl_FragColor = vec4(color.bgr, 1.0);\n" +
                        "}"
            )
        }

        // Built when a script first asks for a glow (see VideoFilterPipeline), which runs them after the bright pass in this order
        val BloomBlurHorizontalPrePassShader get() = bloomBlurPrePassShader("vec2(1.0, 0.0)")
        val BloomBlurVerticalPrePassShader get() = bloomBlurPrePassShader("vec2(0.0, 1.0)")

        // The bloom's last pass, at the input's resolution: the blurred glow on unit 1 (glowTex, DS-native, sampled linear) added at the bloom's
        // strength, then the vignette, a smooth darkening from vignette.y of the half-diagonal out to the corners, where it's vignette.x. Both
        // on the DS top screen only, its SCREEN_HEIGHT rows inside the WideMelon camera's margin for the vignette. With NO_GLOW defined
        // (VideoFilterPipeline) there's no glow to read, for the haze or the vignette without it. With HAZE defined, 3D pixels first mix towards
        // hazeColor (RGB) at up to haze.x, easing in from depth haze.y over haze.z, with the frame's depth on unit 2 (hazeDepthTex, nearest,
        // read with DEPTH_DECODE_FUNCTION)
        val BloomCombinePrePassShader get() = ShaderProgramSource(
            TextureFiltering.LINEAR,
            PRE_PASS_VERT_SHADER,
            PRECISION_HEADER +
                    SCREEN_UNIFORMS +
                    "varying vec2 uv;\n" +
                    "#ifndef NO_GLOW\n" +
                    "uniform sampler2D glowTex;\n" +
                    BLOOM_UNIFORM +
                    "#endif\n" +
                    "#ifdef HAZE\n" +
                    "uniform highp sampler2D hazeDepthTex;\n" +
                    "// The haze's strength (x), and where it starts (y) and how far it eases in (z) in raw depth\n" +
                    "uniform highp vec3 haze;\n" +
                    "uniform vec3 hazeColor;\n" +
                    DEPTH_DECODE_FUNCTION +
                    "#endif\n" +
                    "// The vignette's strength (x) and where it starts (y)\n" +
                    "uniform vec2 vignette;\n" +
                    "// The sun shadows' guard band in DS pixels: the columns on each side of the top screen that are filtered but not shown\n" +
                    "uniform float topScreenGuard;\n" +
                    "uniform float bottomScreenWide;\n" +
                    FRAME_LAYOUT_UNIFORM +
                    "#define topScreenEnd frameLayout.x\n" +
                    "" +
                    "// The DS top screen's columns (xy) and its SCREEN_HEIGHT rows inside the camera's margin (zw), as u and v coordinates\n" +
                    "vec4 topScreenRect() {\n" +
                    "    float halfWidth = bottomScreenWide > 0.5 && topScreenWidth > ${SCREEN_WIDTH}.0 ? 0.5 * ${SCREEN_WIDTH}.0 / topScreenWidth : 0.5;\n" +
                    "    float margin = (topScreenEnd * frameLayout.w - ${SCREEN_HEIGHT}.0) * 0.5;\n" +
                    "    return vec4(0.5 - halfWidth, 0.5 + halfWidth, margin / frameLayout.w, (margin + ${SCREEN_HEIGHT}.0) / frameLayout.w);\n" +
                    "}\n" +
                    "" +
                    "void main() {\n" +
                    "    vec3 texel = texture2D(tex, uv).rgb;\n" +
                    "    // The gap and the DS bottom screen stay as they are\n" +
                    "    if (uv.y >= topScreenEnd) {\n" +
                    "        gl_FragColor = vec4(texel, 1.0);\n" +
                    "        return;\n" +
                    "    }\n" +
                    "" +
                    "    vec3 color = texel.bgr;\n" +
                    "    vec4 rect = topScreenRect();\n" +
                    "    // Both in DS pixels, so the darkening is round on the screen whatever its width\n" +
                    "    vec2 dsPixels = vec2(topScreenWidth, frameLayout.w);\n" +
                    "#ifdef HAZE\n" +
                    "    highp float depth = decodeDepth(texture2D(hazeDepthTex, vec2(uv.x, clamp(uv.y / topScreenEnd, 0.0, 1.0))));\n" +
                    "    if (depth > 0.0 && depth < 1.0) {\n" +
                    "        color = mix(color, hazeColor, haze.x * smoothstep(haze.y, haze.y + max(haze.z, 0.000001), depth));\n" +
                    "    }\n" +
                    "#endif\n" +
                    "#ifndef NO_GLOW\n" +
                    "    // Kept on the glow's texel centers inside the top screen, so the black around it doesn't darken its edges\n" +
                    "    vec2 glowHalfTexel = 0.5 / dsPixels;\n" +
                    "    vec2 glowUv = clamp(uv, vec2(rect.x, 0.0) + glowHalfTexel, vec2(rect.y, topScreenEnd) - glowHalfTexel);\n" +
                    "    color += bloom.x * texture2D(glowTex, glowUv).bgr;\n" +
                    "#endif\n" +
                    "    // The vignette goes over the top screen as it's shown, without the guard band's columns (its rows are the 192 already)\n" +
                    "    vec4 shown = rect;\n" +
                    "    if (!(bottomScreenWide > 0.5 && topScreenWidth > ${SCREEN_WIDTH}.0)) {\n" +
                    "        float guardU = topScreenGuard / topScreenWidth;\n" +
                    "        shown.xy += vec2(guardU, -guardU);\n" +
                    "    }\n" +
                    "    vec2 offset = (uv - 0.5 * (shown.xz + shown.yw)) * dsPixels;\n" +
                    "    vec2 halfSize = 0.5 * (shown.yw - shown.xz) * dsPixels;\n" +
                    "    float t = clamp((length(offset) / length(halfSize) - vignette.y) / max(1.0 - vignette.y, 0.001), 0.0, 1.0);\n" +
                    "    color *= 1.0 - vignette.x * t * t * (3.0 - 2.0 * t);\n" +
                    "    gl_FragColor = vec4(clamp(color, 0.0, 1.0).bgr, 1.0);\n" +
                    "}"
        )

        // The depth of field's focus for a frame, into a 1 x 1 target that the blur passes read (focusTex): its depth in RGB as the depth
        // texture has it (0 for none) and in A how long a hold has lasted. tex (unit 0) is the last frame's, so the focus can hold, and
        // depthTex (unit 1) the frame's depth. depthFocus.x is a fixed focus depth, or negative to look at depthFocus.yz (u and v of depthTex),
        // and depthFocus.w is 1 on the frames that age a hold (every other one). Without a fixed depth the focus is, in order:
        // - The nearest flat 3D at and up to 18 DS pixels above the point: a depth that's the same 3 DS pixels lower is a quad facing the camera,
        //   as Pokemon White's characters are, where the floor, walls and roofs all change depth from row to row. That's the player standing at
        //   the point, so whatever covers her (a pillar, a roof) never takes the focus.
        // - With none, the last frame's focus, held while the nearest 3D around the point is nearer than it by more than the sharp range
        //   (depthOfField.y): the player is behind something. It holds up to HOLD_LIMIT aging frames (about 8.5 s) since the last flat 3D.
        // - Otherwise the nearest 3D at the point and the four points 3 DS pixels around it, or 6 if none of those is 3D. The nearest, as
        //   White's player walking sideways shows the floor between her legs at the point every other step, which is farther than the sharp
        //   range. With no 3D there, none, which also ends a hold (a map change goes through a frame with no 3D)
        val DepthOfFieldFocusShader get() = ShaderProgramSource(
            TextureFiltering.NEAREST,
            PRE_PASS_VERT_SHADER,
            PRECISION_HEADER +
                    "varying vec2 uv;\n" +
                    "uniform highp sampler2D tex;\n" +
                    "uniform float topScreenWidth;\n" +
                    FRAME_LAYOUT_UNIFORM +
                    "uniform vec3 depthOfField;\n" +
                    "uniform vec4 depthFocus;\n" +
                    "uniform highp sampler2D depthTex;\n" +
                    "" +
                    "// How far around the focus point the fallback looks, in DS pixels: the near ring first, and the far one only if the near one\n" +
                    "// finds no 3D\n" +
                    "const float FOCUS_NEAR = 3.0;\n" +
                    "const float FOCUS_FAR = 6.0;\n" +
                    "// The flat 3D search: rows every FLAT_STEP DS pixels from the point up FLAT_ROWS - 1 steps, in three columns FLAT_STEP apart,\n" +
                    "// each against the depth FLAT_STEP lower. Two depths count as the same within a few of the DS's 24-bit steps, where White's\n" +
                    "// floor changes by hundreds over 3 pixels\n" +
                    "const float FLAT_STEP = 3.0;\n" +
                    "const int FLAT_ROWS = 7;\n" +
                    "const highp float FLAT_TOLERANCE = 2.5 / 16777215.0;\n" +
                    "const float HOLD_LIMIT = 255.0;\n" +
                    "" +
                    DEPTH_DECODE_FUNCTION +
                    FOCUS_DECODE_FUNCTION +
                    "highp float depthAt(vec2 p) {\n" +
                    "    return decodeDepth(texture2D(depthTex, p));\n" +
                    "}\n" +
                    "" +
                    "bool is3D(highp float depth) {\n" +
                    "    return depth > 0.0 && depth < 1.0;\n" +
                    "}\n" +
                    "" +
                    "// One DS pixel as u and v of depthTex, which is the top screen's half of the frame\n" +
                    "vec2 dsPixel() {\n" +
                    "    return 1.0 / vec2(topScreenWidth, frameLayout.x * frameLayout.w);\n" +
                    "}\n" +
                    "" +
                    "// The nearer of nearest and the depth at p if that's 3D, so a negative nearest means none yet\n" +
                    "highp float nearer3D(highp float nearest, vec2 p) {\n" +
                    "    highp float depth = depthAt(p);\n" +
                    "    if (!is3D(depth)) {\n" +
                    "        return nearest;\n" +
                    "    }\n" +
                    "    return nearest < 0.0 ? depth : min(nearest, depth);\n" +
                    "}\n" +
                    "" +
                    "// The nearest of nearest and the 3D at the four points ringPixels DS pixels from the focus point, negative for none\n" +
                    "highp float nearestOnRing(highp float nearest, float ringPixels) {\n" +
                    "    vec2 ringStep = ringPixels * dsPixel();\n" +
                    "    nearest = nearer3D(nearest, depthFocus.yz + vec2(ringStep.x, 0.0));\n" +
                    "    nearest = nearer3D(nearest, depthFocus.yz - vec2(ringStep.x, 0.0));\n" +
                    "    nearest = nearer3D(nearest, depthFocus.yz + vec2(0.0, ringStep.y));\n" +
                    "    return nearer3D(nearest, depthFocus.yz - vec2(0.0, ringStep.y));\n" +
                    "}\n" +
                    "" +
                    "// The nearest flat 3D at and above the focus point (see above), negative for none\n" +
                    "highp float nearestFlat() {\n" +
                    "    vec2 flatStep = FLAT_STEP * dsPixel();\n" +
                    "    highp float nearest = -1.0;\n" +
                    "    for (int row = 0; row < FLAT_ROWS; row++) {\n" +
                    "        for (int column = -1; column <= 1; column++) {\n" +
                    "            vec2 p = depthFocus.yz + vec2(float(column) * flatStep.x, -float(row) * flatStep.y);\n" +
                    "            // Above the depth texture's top, clamping would read its top row for both and call it flat\n" +
                    "            if (p.y < 0.0) {\n" +
                    "                continue;\n" +
                    "            }\n" +
                    "            highp float depth = depthAt(p);\n" +
                    "            highp float below = depthAt(p + vec2(0.0, flatStep.y));\n" +
                    "            if (is3D(depth) && is3D(below) && abs(depth - below) <= FLAT_TOLERANCE) {\n" +
                    "                nearest = nearest < 0.0 ? depth : min(nearest, depth);\n" +
                    "            }\n" +
                    "        }\n" +
                    "    }\n" +
                    "    return nearest;\n" +
                    "}\n" +
                    "" +
                    "// The focus as stored: the depth's 24 bits in RGB, high byte in R, all 0 for none (a real depth keeps at least 1), and the hold's\n" +
                    "// age in A\n" +
                    "vec4 encodeFocus(highp float depth, float age) {\n" +
                    "    if (depth < 0.0) {\n" +
                    "        return vec4(0.0, 0.0, 0.0, age / 255.0);\n" +
                    "    }\n" +
                    "    highp float value = clamp(floor(depth * 16777215.0 + 0.5), 1.0, 16777215.0);\n" +
                    "    highp float high = floor(value / 65536.0);\n" +
                    "    highp float middle = floor((value - high * 65536.0) / 256.0);\n" +
                    "    highp float low = value - high * 65536.0 - middle * 256.0;\n" +
                    "    return vec4(vec3(high, middle, low) / 255.0, age / 255.0);\n" +
                    "}\n" +
                    "" +
                    "void main() {\n" +
                    "    if (depthFocus.x >= 0.0) {\n" +
                    "        gl_FragColor = encodeFocus(depthFocus.x, HOLD_LIMIT);\n" +
                    "        return;\n" +
                    "    }\n" +
                    "    highp float flatDepth = nearestFlat();\n" +
                    "    if (flatDepth >= 0.0) {\n" +
                    "        gl_FragColor = encodeFocus(flatDepth, 0.0);\n" +
                    "        return;\n" +
                    "    }\n" +
                    "" +
                    "    highp float nearest = nearestOnRing(nearer3D(-1.0, depthFocus.yz), FOCUS_NEAR);\n" +
                    "    if (nearest < 0.0) {\n" +
                    "        nearest = nearestOnRing(-1.0, FOCUS_FAR);\n" +
                    "    }\n" +
                    "    highp vec4 last = texture2D(tex, vec2(0.5));\n" +
                    "    highp float lastFocus = decodeFocus(last);\n" +
                    "    float age = floor(last.a * 255.0 + 0.5);\n" +
                    "    if (nearest < 0.0 || lastFocus < 0.0 || age >= HOLD_LIMIT) {\n" +
                    "        gl_FragColor = encodeFocus(nearest, HOLD_LIMIT);\n" +
                    "        return;\n" +
                    "    }\n" +
                    "    // Still within a hold's time: something nearer at the point than the sharp range keeps the last focus, and anything else is taken\n" +
                    "    // as it is, but the hold's time keeps running\n" +
                    "    age = min(age + depthFocus.w, HOLD_LIMIT);\n" +
                    "    gl_FragColor = encodeFocus(nearest < lastFocus - depthOfField.y ? lastFocus : nearest, age);\n" +
                    "}"
        )

        // One direction of the depth of field's blur, over the DS top screen like the tilt-shift's: each texel's radius follows how far its depth is
        // from the focus, up to depthOfField.x DS pixels. depthTex (unit 1, nearest) is the frame's top screen half's depth from the compositor
        // (PresentFrameWrapper.depthTextureId, read with DEPTH_DECODE_FUNCTION): 0 for 2D drawn over the 3D, which stays sharp, 1 where what's
        // behind the 3D shows, which blurs as the farthest depth. depthOfField.yz are the sharp range around the focus and the falloff past it,
        // in depth from 0 to 1. focusTex (unit 2) is the focus DepthOfFieldFocusShader picked for this frame; with none, nothing blurs. A tap only
        // counts as much as its own blur reaches it, so a sharp texel never spreads over a blurred one. 13 Gaussian taps like the tilt-shift's
        private fun depthOfFieldPrePassShader(direction: String): ShaderProgramSource {
            return ShaderProgramSource(
                TextureFiltering.LINEAR,
                PRE_PASS_VERT_SHADER,
                PRECISION_HEADER +
                        SCREEN_UNIFORMS +
                        "varying vec2 uv;\n" +
                        "uniform vec3 depthOfField;\n" +
                        "uniform highp sampler2D depthTex;\n" +
                        "uniform highp sampler2D focusTex;\n" +
                        screenBandFunctions("texture2D") +
                        "" +
                        "const vec2 BLUR_DIRECTION = $direction;\n" +
                        "// Below this many texels the blur can't be seen, so the texel skips it\n" +
                        "const float MIN_BLUR_RADIUS = 0.1;\n" +
                        "" +
                        DEPTH_DECODE_FUNCTION +
                        FOCUS_DECODE_FUNCTION +
                        "// The depth at p (u and v of the frame), from 0 to 1\n" +
                        "highp float frameDepth(vec2 p) {\n" +
                        "    return decodeDepth(texture2D(depthTex, vec2(p.x, clamp(p.y / topScreenEnd, 0.0, 1.0))));\n" +
                        "}\n" +
                        "" +
                        "// The blur's radius in DS pixels at a depth, 0 in focus and for 2D\n" +
                        "float blurRadius(highp float depth, highp float focus) {\n" +
                        "    if (depth <= 0.0 || focus < 0.0) {\n" +
                        "        return 0.0;\n" +
                        "    }\n" +
                        "    highp float offset = abs(depth - focus);\n" +
                        "    return depthOfField.x * smoothstep(depthOfField.y, depthOfField.y + max(depthOfField.z, 0.0001), offset);\n" +
                        "}\n" +
                        "" +
                        "void main() {\n" +
                        "    // The gap and the DS bottom screen stay as they are\n" +
                        "    if (uv.y >= topScreenEnd) {\n" +
                        "        gl_FragColor = vec4(texture2D(tex, uv).rgb, 1.0);\n" +
                        "        return;\n" +
                        "    }\n" +
                        "" +
                        "    // Texels per DS pixel; the frame texture is frameLayout.w DS pixels high at any internal resolution\n" +
                        "    float scale = texSize.y / frameLayout.w;\n" +
                        "    highp float focus = decodeFocus(texture2D(focusTex, vec2(0.5)));\n" +
                        "    float radius = blurRadius(frameDepth(uv), focus) * scale;\n" +
                        "    if (radius < MIN_BLUR_RADIUS) {\n" +
                        "        gl_FragColor = vec4(texture2D(tex, uv).rgb, 1.0);\n" +
                        "        return;\n" +
                        "    }\n" +
                        "" +
                        "    vec4 band = screenBand(uv);\n" +
                        "    vec2 spacing = BLUR_DIRECTION * radius / 6.0 / texSize;\n" +
                        "    vec3 color = sampleScreen(uv, band);\n" +
                        "    float total = 1.0;\n" +
                        "    for (int i = 1; i <= 6; i++) {\n" +
                        "        // A Gaussian whose sigma is the radius / 2.5, as the tilt-shift's\n" +
                        "        float x = float(i) * 2.5 / 6.0;\n" +
                        "        float weight = exp(-0.5 * x * x);\n" +
                        "        float tapDistance = float(i) * radius / 6.0;\n" +
                        "        vec2 before = uv - spacing * float(i);\n" +
                        "        vec2 after = uv + spacing * float(i);\n" +
                        "        float beforeWeight = weight * clamp(blurRadius(frameDepth(before), focus) * scale / tapDistance, 0.0, 1.0);\n" +
                        "        float afterWeight = weight * clamp(blurRadius(frameDepth(after), focus) * scale / tapDistance, 0.0, 1.0);\n" +
                        "        color += beforeWeight * sampleScreen(before, band) + afterWeight * sampleScreen(after, band);\n" +
                        "        total += beforeWeight + afterWeight;\n" +
                        "    }\n" +
                        "    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB\n" +
                        "    gl_FragColor = vec4((color / total).bgr, 1.0);\n" +
                        "}"
            )
        }

        // Built when a script first asks for a depth of field (see VideoFilterPipeline), which runs them in this order
        val DepthOfFieldHorizontalPrePassShader get() = depthOfFieldPrePassShader("vec2(1.0, 0.0)")
        val DepthOfFieldVerticalPrePassShader get() = depthOfFieldPrePassShader("vec2(0.0, 1.0)")

        // The shading's values (Shader.setShade): its strength (x), the depth blur's radius in DS pixels (y) and its range in raw depth (z)
        private const val SHADE_UNIFORM = "uniform highp vec3 shade;\n"

        // The shading's first pass, at DS resolution (VideoFilterPipeline makes its target topScreenWidth x the top screen's half's rows, the
        // depth texture's size over the internal scale). tex (unit 0) is the frame's depth texture (PresentFrameWrapper.depthTextureId); each
        // target texel takes the four depth texels at the quarter points of its DS pixel's footprint and averages the ones that are 3D. Out: that
        // mean as encodeDepth() has it in RGB, and the fraction of the four that were 3D in A, all 0 for none
        val ShadeDownsamplePrePassShader get() = ShaderProgramSource(
            TextureFiltering.NEAREST,
            PRE_PASS_VERT_SHADER,
            PRECISION_HEADER +
                    "varying vec2 uv;\n" +
                    "uniform highp sampler2D tex;\n" +
                    "uniform vec2 texSize;\n" +
                    "uniform float topScreenWidth;\n" +
                    FRAME_LAYOUT_UNIFORM +
                    DEPTH_VALUE_FUNCTIONS +
                    "" +
                    "// Counts the depth texel at p (in DS pixels) if it's 3D, with scale depth texels per DS pixel, as a difference from the first 3D\n" +
                    "// one (ref) so the sum stays exact\n" +
                    "void addTap(vec2 p, vec2 scale, inout highp float ref, inout highp float sum, inout float count) {\n" +
                    "    highp vec2 texel = min(floor(p * scale), texSize - 1.0) + 0.5;\n" +
                    "    highp float value = depthValue(texture2D(tex, texel / texSize));\n" +
                    "    if (!isValue3D(value)) {\n" +
                    "        return;\n" +
                    "    }\n" +
                    "    if (count == 0.0) {\n" +
                    "        ref = value;\n" +
                    "    }\n" +
                    "    sum += value - ref;\n" +
                    "    count += 1.0;\n" +
                    "}\n" +
                    "" +
                    "void main() {\n" +
                    "    // The top screen's half in DS pixels and the depth texels per DS pixel, both whole numbers, so the taps land inside texels\n" +
                    "    vec2 dsSize = floor(vec2(topScreenWidth, frameLayout.x * frameLayout.w) + 0.5);\n" +
                    "    vec2 scale = max(floor(texSize / dsSize + 0.5), 1.0);\n" +
                    "    vec2 dsPixel = floor(uv * dsSize);\n" +
                    "    highp float ref = 0.0;\n" +
                    "    highp float sum = 0.0;\n" +
                    "    float count = 0.0;\n" +
                    "    addTap(dsPixel + vec2(0.25, 0.25), scale, ref, sum, count);\n" +
                    "    addTap(dsPixel + vec2(0.75, 0.25), scale, ref, sum, count);\n" +
                    "    addTap(dsPixel + vec2(0.25, 0.75), scale, ref, sum, count);\n" +
                    "    addTap(dsPixel + vec2(0.75, 0.75), scale, ref, sum, count);\n" +
                    "    if (count == 0.0) {\n" +
                    "        gl_FragColor = vec4(0.0);\n" +
                    "        return;\n" +
                    "    }\n" +
                    "    gl_FragColor = vec4(encodeDepth(ref + sum / count), count / 4.0);\n" +
                    "}"
        )

        // One direction of the shading's depth blur, at DS resolution like the first pass, whose output (or the horizontal pass's) is tex (unit
        // 0, nearest): a Gaussian whose sigma is half the radius, with shade.y taps on each side (at most SHADE_MAX_RADIUS, the Lua API's
        // largest), as a normalized convolution. Each tap counts by its kernel weight times its A, so only 3D counts, and a tap outside the
        // target not at all. Out: the weighted mean depth as encodeDepth() has it in RGB, and in A the taps' weights over the kernel's, all 0
        // with none
        private fun shadeBlurPrePassShader(direction: String): ShaderProgramSource {
            return ShaderProgramSource(
                TextureFiltering.NEAREST,
                PRE_PASS_VERT_SHADER,
                PRECISION_HEADER +
                        "varying vec2 uv;\n" +
                        "uniform highp sampler2D tex;\n" +
                        "uniform vec2 texSize;\n" +
                        SHADE_UNIFORM +
                        DEPTH_VALUE_FUNCTIONS +
                        "" +
                        "const vec2 BLUR_DIRECTION = $direction;\n" +
                        "const int SHADE_MAX_RADIUS = 16;\n" +
                        "" +
                        "// Counts the texel at p by kernel times its A, as a difference from the first counted one (ref) so the sum keeps its precision\n" +
                        "void addTap(vec2 p, float kernel, inout highp float ref, inout highp float sum, inout float weights) {\n" +
                        "    // Clamping would read the edge texel again\n" +
                        "    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) {\n" +
                        "        return;\n" +
                        "    }\n" +
                        "    highp vec4 texel = texture2D(tex, p);\n" +
                        "    float weight = kernel * texel.a;\n" +
                        "    if (weight <= 0.0) {\n" +
                        "        return;\n" +
                        "    }\n" +
                        "    highp float value = depthValue(texel);\n" +
                        "    if (weights == 0.0) {\n" +
                        "        ref = value;\n" +
                        "    }\n" +
                        "    sum += weight * (value - ref);\n" +
                        "    weights += weight;\n" +
                        "}\n" +
                        "" +
                        "void main() {\n" +
                        "    vec2 tapStep = BLUR_DIRECTION / texSize;\n" +
                        "    float sigma = max(0.5 * shade.y, 0.5);\n" +
                        "    highp float ref = 0.0;\n" +
                        "    highp float sum = 0.0;\n" +
                        "    float weights = 0.0;\n" +
                        "    float kernels = 1.0;\n" +
                        "    addTap(uv, 1.0, ref, sum, weights);\n" +
                        "    for (int i = 1; i <= SHADE_MAX_RADIUS; i++) {\n" +
                        "        float x = float(i);\n" +
                        "        if (x > shade.y) {\n" +
                        "            break;\n" +
                        "        }\n" +
                        "        float kernel = exp(-0.5 * (x / sigma) * (x / sigma));\n" +
                        "        addTap(uv - tapStep * x, kernel, ref, sum, weights);\n" +
                        "        addTap(uv + tapStep * x, kernel, ref, sum, weights);\n" +
                        "        kernels += 2.0 * kernel;\n" +
                        "    }\n" +
                        "    if (weights <= 0.0) {\n" +
                        "        gl_FragColor = vec4(0.0);\n" +
                        "        return;\n" +
                        "    }\n" +
                        "    gl_FragColor = vec4(encodeDepth(ref + sum / weights), weights / kernels);\n" +
                        "}"
            )
        }

        // Built when a script first asks for shading (see VideoFilterPipeline), which runs them after the downsample pass in this order
        val ShadeBlurHorizontalPrePassShader get() = shadeBlurPrePassShader("vec2(1.0, 0.0)")
        val ShadeBlurVerticalPrePassShader get() = shadeBlurPrePassShader("vec2(0.0, 1.0)")

        // With LIGHTS defined (bloom's lights, VideoFilterPipeline), whether a pixel is a light that's on, which the shading and the sun
        // shadows leave as it is, as it gives off its own light: its depth texel has flag 32 (melon.gpu.light_textures) and its color's
        // brightest channel is at least the bloom's light threshold (bloomLights.x, Shader.setBloom)
        private const val LIGHT_EMISSIVE_FUNCTION = "#ifdef LIGHTS\n" +
                "uniform vec2 bloomLights;\n" +
                "bool isLitLight(highp vec4 depthTexel, vec3 color) {\n" +
                "    float flags = floor(depthTexel.a * 255.0 + 0.5);\n" +
                "    return mod(floor(flags / 32.0), 2.0) == 1.0 && max(color.r, max(color.g, color.b)) >= bloomLights.x;\n" +
                "}\n" +
                "#endif\n"

        // The shading's last pass, at the input's resolution: unsharp masking of the depth (Luft, Colditz and Deussen 2006). A 3D pixel's own
        // depth, from the frame's depth texture (shadeDepthTex, unit 1, nearest), is compared with the blurred depth around it (shadeBlurTex,
        // unit 2, nearest, the vertical blur's target at DS resolution), read bilinearly by hand: its four nearest texels by their bilinear
        // weights times their A. Comparing with the pixel's own depth at full resolution keeps the shading's edges as sharp as the depth, with
        // no halo at any scale. A pixel farther than its neighborhood is mixed towards itself times shadeColor (RGB) by shade.x, fully once
        // it's shade.z farther in raw depth, faded out where the blur covered less than all of the neighborhood (next to 2D or the screen's
        // edge), down to nothing at half. 2D over the 3D, what's behind the 3D, a pixel with no 3D around it, the gap and the DS bottom
        // screen stay as they are, and so does a light that's on with LIGHTS defined (LIGHT_EMISSIVE_FUNCTION)
        val ShadeApplyPrePassShader get() = ShaderProgramSource(
            TextureFiltering.NEAREST,
            PRE_PASS_VERT_SHADER,
            PRECISION_HEADER +
                    "varying vec2 uv;\n" +
                    "uniform sampler2D tex;\n" +
                    "uniform float topScreenWidth;\n" +
                    "uniform highp sampler2D shadeDepthTex;\n" +
                    "uniform highp sampler2D shadeBlurTex;\n" +
                    SHADE_UNIFORM +
                    "uniform vec3 shadeColor;\n" +
                    FRAME_LAYOUT_UNIFORM +
                    "#define topScreenEnd frameLayout.x\n" +
                    DEPTH_VALUE_FUNCTIONS +
                    LIGHT_EMISSIVE_FUNCTION +
                    "" +
                    "// Counts the blurred depth's texel p (clamped onto its target of dsSize texels) by bilinear times its A, as a difference from\n" +
                    "// the pixel's own depth\n" +
                    "void addTap(vec2 p, vec2 dsSize, float bilinear, highp float depth, inout highp float sum, inout float weights) {\n" +
                    "    highp vec4 texel = texture2D(shadeBlurTex, (clamp(p, vec2(0.0), dsSize - 1.0) + 0.5) / dsSize);\n" +
                    "    float weight = bilinear * texel.a;\n" +
                    "    if (weight <= 0.0) {\n" +
                    "        return;\n" +
                    "    }\n" +
                    "    sum += weight * (depthValue(texel) - depth);\n" +
                    "    weights += weight;\n" +
                    "}\n" +
                    "" +
                    "void main() {\n" +
                    "    vec4 texel = texture2D(tex, uv);\n" +
                    "    // The gap and the DS bottom screen stay as they are\n" +
                    "    if (uv.y >= topScreenEnd) {\n" +
                    "        gl_FragColor = texel;\n" +
                    "        return;\n" +
                    "    }\n" +
                    "    vec2 depthUv = vec2(uv.x, clamp(uv.y / topScreenEnd, 0.0, 1.0));\n" +
                    "    highp vec4 depthTexel = texture2D(shadeDepthTex, depthUv);\n" +
                    "    highp float depth = depthValue(depthTexel);\n" +
                    "    if (!isValue3D(depth)) {\n" +
                    "        gl_FragColor = texel;\n" +
                    "        return;\n" +
                    "    }\n" +
                    "#ifdef LIGHTS\n" +
                    "    // The frame texture's texels are BGR, which doesn't change the brightest channel\n" +
                    "    if (isLitLight(depthTexel, texel.rgb)) {\n" +
                    "        gl_FragColor = texel;\n" +
                    "        return;\n" +
                    "    }\n" +
                    "#endif\n" +
                    "" +
                    "    vec2 dsSize = floor(vec2(topScreenWidth, topScreenEnd * frameLayout.w) + 0.5);\n" +
                    "    vec2 p = depthUv * dsSize - 0.5;\n" +
                    "    vec2 base = floor(p);\n" +
                    "    vec2 f = p - base;\n" +
                    "    highp float sum = 0.0;\n" +
                    "    float weights = 0.0;\n" +
                    "    addTap(base, dsSize, (1.0 - f.x) * (1.0 - f.y), depth, sum, weights);\n" +
                    "    addTap(base + vec2(1.0, 0.0), dsSize, f.x * (1.0 - f.y), depth, sum, weights);\n" +
                    "    addTap(base + vec2(0.0, 1.0), dsSize, (1.0 - f.x) * f.y, depth, sum, weights);\n" +
                    "    addTap(base + vec2(1.0, 1.0), dsSize, f.x * f.y, depth, sum, weights);\n" +
                    "    if (weights <= 0.0) {\n" +
                    "        gl_FragColor = texel;\n" +
                    "        return;\n" +
                    "    }\n" +
                    "" +
                    "    // How much farther this pixel is than the blurred depth around it, in the depth's 24-bit steps\n" +
                    "    highp float farther = -sum / weights;\n" +
                    "    // weights is the blur's coverage, as the bilinear weights add up to 1. Next to 2D or the screen's edge only the 3D on one\n" +
                    "    // side counts, which reads as farther wherever the depth slopes, so the shading fades out from full coverage to half\n" +
                    "    float coverage = clamp(2.0 * weights - 1.0, 0.0, 1.0);\n" +
                    "    float darkening = shade.x * coverage * clamp(farther / max(shade.z * 16777215.0, 1.0), 0.0, 1.0);\n" +
                    "    // The frame texture's texels are BGR\n" +
                    "    vec3 color = texel.bgr;\n" +
                    "    gl_FragColor = vec4(mix(color, color * shadeColor, darkening).bgr, texel.a);\n" +
                    "}"
        )

        // The sun shadows' camera and values (Shader.setSun): sun is the strength (x), the march's length in view-space units (y), its steps
        // (z) and its bias in units (w). sunInverse is the inverse of the depth camera's projection (PresentFrameWrapper.depthCamera), and
        // sunViewport its viewport's left, top, width and height in DS pixels of the depth texture's top half, so with the WideMelon wings and
        // margin before the native view's 0
        private const val SUN_UNIFORMS = "uniform highp vec4 sun;\n" +
                "uniform highp mat4 sunInverse;\n" +
                "uniform highp vec4 sunViewport;\n"

        // A depth texel's flags byte (DEPTH_DECODE_FUNCTION's A) has [bit] (1, 2, 4, 8 or 16). sunNdc() is a point's NDC from where it is in
        // DS pixels of the depth's top half and its 24-bit Z-buffer depth (the core's z/w * 2^23 + 0x7FFE00 inverted), sunViewDistance() how
        // far in front of the camera it is in view space, and sunScreen() a clip position back in DS pixels. Needs SUN_UNIFORMS first
        private const val SUN_CAMERA_FUNCTIONS = "bool hasFlag(highp vec4 texel, float bit) {\n" +
                "    float flags = floor(texel.a * 255.0 + 0.5);\n" +
                "    return mod(floor(flags / bit), 2.0) == 1.0;\n" +
                "}\n" +
                "highp vec4 sunNdc(highp vec2 p, highp float value) {\n" +
                "    return vec4((p.x - sunViewport.x) * 2.0 / sunViewport.z - 1.0, 1.0 - (p.y - sunViewport.y) * 2.0 / sunViewport.w,\n" +
                "        (value - 8388096.0) / 8388608.0, 1.0);\n" +
                "}\n" +
                "highp float sunViewDistance(highp vec2 p, highp float value) {\n" +
                "    highp vec4 view = sunInverse * sunNdc(p, value);\n" +
                "    return -view.z / view.w;\n" +
                "}\n" +
                "highp vec2 sunScreen(highp vec4 clip) {\n" +
                "    highp vec2 ndc = clip.xy / clip.w;\n" +
                "    return vec2(sunViewport.x + (ndc.x + 1.0) * 0.5 * sunViewport.z, sunViewport.y + (1.0 - ndc.y) * 0.5 * sunViewport.w);\n" +
                "}\n" +
                "// How far apart two depths of one surface can be, in view-space units, at [dist] and [pixels] DS pixels apart: about what\n" +
                "// a surface 16 degrees off the line of sight changes by over a DS pixel of White's camera, plus a pixel's worth for the DS's\n" +
                "// rounding\n" +
                "highp float sunSurfaceTolerance(highp float dist, float pixels) {\n" +
                "    return 0.0125 * dist * (pixels + 1.0);\n" +
                "}\n"

        // The sun shadows' march, at DS resolution (VideoFilterPipeline makes its target topScreenWidth x the top screen's half's rows, the
        // depth texture's size over the internal scale). tex (unit 0, nearest) is the frame's depth texture (PresentFrameWrapper.depthTextureId).
        // Each target texel takes the depth texel in the middle of its DS pixel, rebuilds its view-space position, and steps sun.z times over
        // sun.y units towards sunDirection; it's in shadow where the depth at a step is nearer than the step by more than sun.w units. A step
        // that lands on 2D, on 3D drawn under another camera (flag 8, White's characters, which would cast shadows tilted towards the camera),
        // on nothing, off the depth or behind the camera finds no shadow there. The shadow fades out over SUN_EDGE_FADE DS pixels before the
        // edge the march leaves the depth through, where what would cast it is off screen. 3D nearer the camera than sunNear units (when it's
        // over 0) casts nothing either. Out: the texel's depth as encodeDepth() has it in RGB and the shadow (0 to 1) in A, all 0 where it
        // isn't 3D
        val SunMarchPrePassShader get() = ShaderProgramSource(
            TextureFiltering.NEAREST,
            PRE_PASS_VERT_SHADER,
            PRECISION_HEADER +
                    "varying vec2 uv;\n" +
                    "uniform highp sampler2D tex;\n" +
                    "uniform vec2 texSize;\n" +
                    "uniform float topScreenWidth;\n" +
                    "uniform highp vec3 sunDirection;\n" +
                    "uniform highp mat4 sunProjection;\n" +
                    "uniform highp float sunNear;\n" +
                    FRAME_LAYOUT_UNIFORM +
                    SUN_UNIFORMS +
                    DEPTH_VALUE_FUNCTIONS +
                    SUN_CAMERA_FUNCTIONS +
                    "" +
                    "const int SUN_MAX_STEPS = 128;\n" +
                    "const float SUN_EDGE_FADE = 32.0;\n" +
                    "" +
                    "// Whether depth texel t shades a step dist units in front of the camera: it's 3D, not under another camera, nearer than\n" +
                    "// dist - bias, and not nearer the camera than sunNear when that's over 0, which keeps what a game draws right in front of\n" +
                    "// the camera (Pokemon White's speech bubble tails) from casting across the scene\n" +
                    "bool isOccluder(highp vec2 t, highp vec2 scale, highp float dist) {\n" +
                    "    highp vec4 depth = texture2D(tex, (t + 0.5) / texSize);\n" +
                    "    highp float value = depthValue(depth);\n" +
                    "    if (!isValue3D(value) || hasFlag(depth, 8.0)) {\n" +
                    "        return false;\n" +
                    "    }\n" +
                    "    highp float occluderDistance = sunViewDistance((t + 0.5) / scale, value);\n" +
                    "    return occluderDistance < dist - sun.w && (sunNear <= 0.0 || occluderDistance >= sunNear);\n" +
                    "}\n" +
                    "" +
                    "// How far from p (DS pixels) along dir the march leaves the dsSize rect\n" +
                    "highp float exitDistance(highp vec2 p, highp vec2 dir, highp vec2 dsSize) {\n" +
                    "    highp vec2 room = vec2(dir.x > 0.0 ? dsSize.x - p.x : p.x, dir.y > 0.0 ? dsSize.y - p.y : p.y);\n" +
                    "    highp vec2 along = abs(dir);\n" +
                    "    highp float exit = 1.0e9;\n" +
                    "    if (along.x > 1.0e-6) {\n" +
                    "        exit = min(exit, room.x / along.x);\n" +
                    "    }\n" +
                    "    if (along.y > 1.0e-6) {\n" +
                    "        exit = min(exit, room.y / along.y);\n" +
                    "    }\n" +
                    "    return exit;\n" +
                    "}\n" +
                    "" +
                    "void main() {\n" +
                    "    // The top screen's half in DS pixels and the depth texels per DS pixel, both whole numbers\n" +
                    "    highp vec2 dsSize = floor(vec2(topScreenWidth, frameLayout.x * frameLayout.w) + 0.5);\n" +
                    "    highp vec2 scale = max(floor(texSize / dsSize + 0.5), 1.0);\n" +
                    "    highp vec2 texel = min(floor((floor(uv * dsSize) + 0.5) * scale), texSize - 1.0);\n" +
                    "    highp vec4 depth = texture2D(tex, (texel + 0.5) / texSize);\n" +
                    "    highp float value = depthValue(depth);\n" +
                    "    // A W-buffered pixel's depth isn't z/w (its scene has no camera, so this pass doesn't run then)\n" +
                    "    if (!isValue3D(value) || hasFlag(depth, 4.0)) {\n" +
                    "        gl_FragColor = vec4(0.0);\n" +
                    "        return;\n" +
                    "    }\n" +
                    "" +
                    "    highp vec2 p = (texel + 0.5) / scale;\n" +
                    "    highp vec4 view = sunInverse * sunNdc(p, value);\n" +
                    "    highp vec3 origin = view.xyz / view.w;\n" +
                    "    // The steps are evenly spaced in view space, so they are in clip space too\n" +
                    "    highp vec3 stride = sunDirection * (sun.y / sun.z);\n" +
                    "    highp vec4 clipOrigin = sunProjection * vec4(origin, 1.0);\n" +
                    "    highp vec4 clipStep = sunProjection * vec4(stride, 0.0);\n" +
                    "    float shadow = 0.0;\n" +
                    "    for (int i = 1; i <= SUN_MAX_STEPS; i++) {\n" +
                    "        float n = float(i);\n" +
                    "        highp vec4 clip = clipOrigin + clipStep * n;\n" +
                    "        highp float dist = -(origin.z + stride.z * n);\n" +
                    "        // A line leaves the screen or passes the camera only once, so no later step can find a shadow either\n" +
                    "        if (n > sun.z || dist <= 1.0 || clip.w <= 0.0) {\n" +
                    "            break;\n" +
                    "        }\n" +
                    "        highp vec2 q = sunScreen(clip);\n" +
                    "        if (q.x < 0.0 || q.y < 0.0 || q.x >= dsSize.x || q.y >= dsSize.y) {\n" +
                    "            break;\n" +
                    "        }\n" +
                    "        if (isOccluder(min(floor(q * scale), texSize - 1.0), scale, dist)) {\n" +
                    "            shadow = 1.0;\n" +
                    "            break;\n" +
                    "        }\n" +
                    "    }\n" +
                    "" +
                    "    // The fade, over SUN_EDGE_FADE or the march's own length on screen if that's shorter, so a march that stays on screen\n" +
                    "    // doesn't fade\n" +
                    "    highp vec4 clipFirst = clipOrigin + clipStep;\n" +
                    "    highp vec2 first = clipFirst.w > 0.0 ? sunScreen(clipFirst) - p : vec2(0.0);\n" +
                    "    highp vec4 clipEnd = clipOrigin + clipStep * sun.z;\n" +
                    "    highp float onScreen = clipEnd.w > 0.0 ? length(sunScreen(clipEnd) - p) : 1.0e9;\n" +
                    "    highp float fade = 1.0;\n" +
                    "    if (length(first) > 1.0e-6) {\n" +
                    "        highp float fadeLength = clamp(onScreen, 1.0, SUN_EDGE_FADE);\n" +
                    "        fade = clamp(exitDistance(p, normalize(first), dsSize) / fadeLength, 0.0, 1.0);\n" +
                    "    }\n" +
                    "    gl_FragColor = vec4(encodeDepth(value), shadow * fade);\n" +
                    "}"
        )

        // One direction of the sun shadows' softening blur, at DS resolution like the march, whose output (or the horizontal pass's) is tex
        // (unit 0, nearest): a Gaussian whose sigma is half of sunSoftness, with that many taps on each side (at most SUN_MAX_SOFTNESS, the Lua
        // API's largest), over A. Only taps that are 3D and on the same surface as this texel count (their view distances within
        // sunSurfaceTolerance), as a normalized convolution, so a lit roof doesn't take the shadow of the ground below it. Out: this texel's
        // RGB and the blurred shadow in A
        private fun sunBlurPrePassShader(direction: String): ShaderProgramSource {
            return ShaderProgramSource(
                TextureFiltering.NEAREST,
                PRE_PASS_VERT_SHADER,
                PRECISION_HEADER +
                        "varying vec2 uv;\n" +
                        "uniform highp sampler2D tex;\n" +
                        "uniform vec2 texSize;\n" +
                        "uniform float sunSoftness;\n" +
                        SUN_UNIFORMS +
                        DEPTH_VALUE_FUNCTIONS +
                        SUN_CAMERA_FUNCTIONS +
                        "" +
                        "const vec2 BLUR_DIRECTION = $direction;\n" +
                        "const int SUN_MAX_SOFTNESS = 8;\n" +
                        "" +
                        "// Counts the texel x taps away by kernel, if it's 3D and within the tolerance of dist\n" +
                        "void addTap(float x, float kernel, highp float dist, inout float sum, inout float weights) {\n" +
                        "    highp vec2 p = floor(uv * texSize) + 0.5 + BLUR_DIRECTION * x;\n" +
                        "    // Clamping would read the edge texel again\n" +
                        "    if (p.x < 0.0 || p.y < 0.0 || p.x > texSize.x || p.y > texSize.y) {\n" +
                        "        return;\n" +
                        "    }\n" +
                        "    highp vec4 texel = texture2D(tex, p / texSize);\n" +
                        "    highp float value = depthValue(texel);\n" +
                        "    if (!isValue3D(value)) {\n" +
                        "        return;\n" +
                        "    }\n" +
                        "    highp float apart = abs(sunViewDistance(p, value) - dist);\n" +
                        "    float weight = kernel * clamp(1.0 - apart / sunSurfaceTolerance(dist, abs(x)), 0.0, 1.0);\n" +
                        "    sum += weight * texel.a;\n" +
                        "    weights += weight;\n" +
                        "}\n" +
                        "" +
                        "void main() {\n" +
                        "    highp vec4 center = texture2D(tex, uv);\n" +
                        "    highp float value = depthValue(center);\n" +
                        "    if (!isValue3D(value)) {\n" +
                        "        gl_FragColor = center;\n" +
                        "        return;\n" +
                        "    }\n" +
                        "    highp float dist = sunViewDistance(floor(uv * texSize) + 0.5, value);\n" +
                        "    float sigma = max(0.5 * sunSoftness, 0.5);\n" +
                        "    float sum = center.a;\n" +
                        "    float weights = 1.0;\n" +
                        "    for (int i = 1; i <= SUN_MAX_SOFTNESS; i++) {\n" +
                        "        float x = float(i);\n" +
                        "        if (x > sunSoftness) {\n" +
                        "            break;\n" +
                        "        }\n" +
                        "        float kernel = exp(-0.5 * (x / sigma) * (x / sigma));\n" +
                        "        addTap(-x, kernel, dist, sum, weights);\n" +
                        "        addTap(x, kernel, dist, sum, weights);\n" +
                        "    }\n" +
                        "    gl_FragColor = vec4(center.rgb, sum / weights);\n" +
                        "}"
            )
        }

        // Built when a script first asks for sun shadows with some softness (see VideoFilterPipeline), which runs them after the march
        val SunBlurHorizontalPrePassShader get() = sunBlurPrePassShader("vec2(1.0, 0.0)")
        val SunBlurVerticalPrePassShader get() = sunBlurPrePassShader("vec2(0.0, 1.0)")

        // The sun shadows' last pass, at the input's resolution. A 3D pixel's own depth, from the frame's depth texture (sunDepthTex, unit 1,
        // nearest), picks which of the four nearest DS-resolution shadow texels (sunMaskTex, unit 2, nearest: the march's or the blur's
        // target) it takes: by their bilinear weights times how close their depth is to its own (within sunSurfaceTolerance), or the closest
        // one's if none is close. So the shadow's edges follow the full-resolution depth at any scale, with no halo where a DS pixel straddles
        // an edge. A pixel with flag 16 (in a character's shadow, which the renderer marks) is in shadow too. The shadow mixes the pixel towards
        // itself times sunColor (RGB) by sun.x. 2D over the 3D, what's behind the 3D, the gap and the DS bottom screen stay as they are, and so
        // does a light that's on with LIGHTS defined (LIGHT_EMISSIVE_FUNCTION)
        val SunApplyPrePassShader get() = ShaderProgramSource(
            TextureFiltering.NEAREST,
            PRE_PASS_VERT_SHADER,
            PRECISION_HEADER +
                    "varying vec2 uv;\n" +
                    "uniform sampler2D tex;\n" +
                    "uniform float topScreenWidth;\n" +
                    "uniform highp sampler2D sunDepthTex;\n" +
                    "uniform highp sampler2D sunMaskTex;\n" +
                    "uniform vec3 sunColor;\n" +
                    FRAME_LAYOUT_UNIFORM +
                    "#define topScreenEnd frameLayout.x\n" +
                    SUN_UNIFORMS +
                    DEPTH_VALUE_FUNCTIONS +
                    SUN_CAMERA_FUNCTIONS +
                    LIGHT_EMISSIVE_FUNCTION +
                    "" +
                    "// Counts the shadow texel t (clamped onto its target of dsSize texels) by bilinear times how near its depth is to dist, and\n" +
                    "// keeps the nearest one's shadow in closest (x the shadow, y how far apart)\n" +
                    "void addTap(highp vec2 t, highp vec2 p, highp vec2 dsSize, float bilinear, highp float dist, inout float sum,\n" +
                    "        inout float weights, inout highp vec2 closest) {\n" +
                    "    t = clamp(t, vec2(0.0), dsSize - 1.0);\n" +
                    "    highp vec4 texel = texture2D(sunMaskTex, (t + 0.5) / dsSize);\n" +
                    "    highp float value = depthValue(texel);\n" +
                    "    if (!isValue3D(value)) {\n" +
                    "        return;\n" +
                    "    }\n" +
                    "    highp float apart = abs(sunViewDistance(t + 0.5, value) - dist);\n" +
                    "    float weight = bilinear * clamp(1.0 - apart / sunSurfaceTolerance(dist, length(t + 0.5 - p)), 0.0, 1.0);\n" +
                    "    sum += weight * texel.a;\n" +
                    "    weights += weight;\n" +
                    "    if (apart < closest.y) {\n" +
                    "        closest = vec2(texel.a, apart);\n" +
                    "    }\n" +
                    "}\n" +
                    "" +
                    "void main() {\n" +
                    "    vec4 texel = texture2D(tex, uv);\n" +
                    "    // The gap and the DS bottom screen stay as they are\n" +
                    "    if (uv.y >= topScreenEnd) {\n" +
                    "        gl_FragColor = texel;\n" +
                    "        return;\n" +
                    "    }\n" +
                    "    vec2 depthUv = vec2(uv.x, clamp(uv.y / topScreenEnd, 0.0, 1.0));\n" +
                    "    highp vec4 depth = texture2D(sunDepthTex, depthUv);\n" +
                    "    highp float value = depthValue(depth);\n" +
                    "    if (!isValue3D(value)) {\n" +
                    "        gl_FragColor = texel;\n" +
                    "        return;\n" +
                    "    }\n" +
                    "#ifdef LIGHTS\n" +
                    "    // The frame texture's texels are BGR, which doesn't change the brightest channel\n" +
                    "    if (isLitLight(depth, texel.rgb)) {\n" +
                    "        gl_FragColor = texel;\n" +
                    "        return;\n" +
                    "    }\n" +
                    "#endif\n" +
                    "" +
                    "    float shadow = 1.0;\n" +
                    "    if (!hasFlag(depth, 16.0)) {\n" +
                    "        highp vec2 dsSize = floor(vec2(topScreenWidth, topScreenEnd * frameLayout.w) + 0.5);\n" +
                    "        highp vec2 p = depthUv * dsSize;\n" +
                    "        highp float dist = sunViewDistance(p, value);\n" +
                    "        highp vec2 base = floor(p - 0.5);\n" +
                    "        vec2 f = p - 0.5 - base;\n" +
                    "        float sum = 0.0;\n" +
                    "        float weights = 0.0;\n" +
                    "        highp vec2 closest = vec2(0.0, 1.0e9);\n" +
                    "        addTap(base, p, dsSize, (1.0 - f.x) * (1.0 - f.y), dist, sum, weights, closest);\n" +
                    "        addTap(base + vec2(1.0, 0.0), p, dsSize, f.x * (1.0 - f.y), dist, sum, weights, closest);\n" +
                    "        addTap(base + vec2(0.0, 1.0), p, dsSize, (1.0 - f.x) * f.y, dist, sum, weights, closest);\n" +
                    "        addTap(base + vec2(1.0, 1.0), p, dsSize, f.x * f.y, dist, sum, weights, closest);\n" +
                    "        shadow = weights > 0.001 ? sum / weights : closest.x;\n" +
                    "    }\n" +
                    "" +
                    "    // The frame texture's texels are BGR\n" +
                    "    vec3 color = texel.bgr;\n" +
                    "    gl_FragColor = vec4(mix(color, color * sunColor, sun.x * shadow).bgr, texel.a);\n" +
                    "}"
        )

        // sharp-shimmerless
        // Author: zadpos (Woohyun Kang)
        // License: Public domain
        // Draws every texel as a rectangle of the same size, blending only the pixels a texel boundary crosses, with the scale taken from fwidth().
        // With DEBAND defined it debands its sample (DEBAND_FUNCTIONS) in place of the pre-pass (debug.melonade.debandfold)
        val SharpShader = ShaderProgramSource(
            TextureFiltering.LINEAR,
            "#version 300 es\n" +
                    "in vec2 vUV;\n" +
                    "in vec2 vPos;\n" +
                    "in float vAlpha;\n" +
                    "out vec2 uv;\n" +
                    "out float alpha;\n" +
                    "void main()\n" +
                    "{\n" +
                    "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                    "    uv = vUV;\n" +
                    "    alpha = vAlpha;\n" +
                    "}",
            "#version 300 es\n" +
                    "precision highp float;\n" +
                    SCREEN_UNIFORMS +
                    "in vec2 uv;\n" +
                    "in float alpha;\n" +
                    "out vec4 fragColor;\n" +
                    screenBandFunctions("texture") +
                    "#ifdef DEBAND\n" +
                    DEBAND_FUNCTIONS +
                    "#endif\n" +
                    COLOR_FUNCTIONS +
                    "" +
                    "void main() {\n" +
                    "    // Pixel center and size in texels, with fwidth() taken on t itself since it's continuous across the quad\n" +
                    "    vec2 t = uv * texSize;\n" +
                    "    vec2 d = clamp(fwidth(t), 0.00001, 1.0);\n" +
                    "" +
                    "    // Samples w texels past the center of the texel before farTexel, so the bilinear filter blends the 2 texels by area\n" +
                    "    vec2 farEdge = t + 0.5 * d;\n" +
                    "    vec2 farTexel = floor(farEdge);\n" +
                    "    vec2 w = min((farEdge - farTexel) / d, 1.0);\n" +
                    "    vec2 p = (farTexel - 0.5 + w) / texSize;\n" +
                    "" +
                    "    vec4 band = screenBand(uv);\n" +
                    "    vec3 color = sampleScreen(p, band);\n" +
                    "#ifdef DEBAND\n" +
                    "    color = deband(p, color, band);\n" +
                    "#endif\n" +
                    "    fragColor = vec4(finishColor(color), alpha);\n" +
                    "}"
        )

        // MMPX
        // by Morgan McGuire and Mara Gagiu
        // https://casual-effects.com/research/McGuire2021PixelArt/
        // License: MIT
        //
        // Copyright 2020 Morgan McGuire & Mara Gagiu
        //
        // Permission is hereby granted, free of charge, to any person obtaining a copy
        // of this software and associated documentation files (the "Software"), to deal
        // in the Software without restriction, including without limitation the rights
        // to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
        // copies of the Software, and to permit persons to whom the Software is
        // furnished to do so, subject to the following conditions:
        //
        // The above copyright notice and this permission notice shall be included in
        // all copies or substantial portions of the Software.
        //
        // THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
        // IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
        // FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
        // AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
        // LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
        // OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
        // THE SOFTWARE.
        //
        // adapted for glsl by hunterk
        //
        // Pre-pass at twice the frame texture's size, so texSize is the source size; needs NEAREST since it compares texels for equality
        val MmpxPrePassShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
            PRE_PASS_VERT_SHADER,
            PRECISION_HEADER +
                    SCREEN_UNIFORMS +
                    "varying vec2 uv;\n" +
                    screenBandFunctions("texture2D") +
                    "" +
                    "float luma(vec3 col) {\n" +
                    "    return dot(col, vec3(0.2126, 0.7152, 0.0722));\n" +
                    "}\n" +
                    "" +
                    "bool same(vec3 B, vec3 A0) {\n" +
                    "    return all(equal(B, A0));\n" +
                    "}\n" +
                    "" +
                    "bool notsame(vec3 B, vec3 A0) {\n" +
                    "    return any(notEqual(B, A0));\n" +
                    "}\n" +
                    "" +
                    "bool all_eq2(vec3 B, vec3 A0, vec3 A1) {\n" +
                    "    return (same(B, A0) && same(B, A1));\n" +
                    "}\n" +
                    "" +
                    "bool all_eq3(vec3 B, vec3 A0, vec3 A1, vec3 A2) {\n" +
                    "    return (same(B, A0) && same(B, A1) && same(B, A2));\n" +
                    "}\n" +
                    "" +
                    "bool all_eq4(vec3 B, vec3 A0, vec3 A1, vec3 A2, vec3 A3) {\n" +
                    "    return (same(B, A0) && same(B, A1) && same(B, A2) && same(B, A3));\n" +
                    "}\n" +
                    "" +
                    "bool any_eq3(vec3 B, vec3 A0, vec3 A1, vec3 A2) {\n" +
                    "    return (same(B, A0) || same(B, A1) || same(B, A2));\n" +
                    "}\n" +
                    "" +
                    "bool none_eq2(vec3 B, vec3 A0, vec3 A1) {\n" +
                    "    return (notsame(B, A0) && notsame(B, A1));\n" +
                    "}\n" +
                    "" +
                    "bool none_eq4(vec3 B, vec3 A0, vec3 A1, vec3 A2, vec3 A3) {\n" +
                    "    return (notsame(B, A0) && notsame(B, A1) && notsame(B, A2) && notsame(B, A3));\n" +
                    "}\n" +
                    "" +
                    "// The source texel x, y texels away from this one (y grows downwards), kept inside the screen being drawn\n" +
                    "#define src(x, y) sampleScreen(uv + vec2(x, y) * ps, band)\n" +
                    "" +
                    "void main() {\n" +
                    "    vec2 ps = 1.0 / texSize;\n" +
                    "    vec4 band = screenBand(uv);\n" +
                    "" +
                    "    // Our current pixel\n" +
                    "    vec3 E = src(0.0, 0.0);\n" +
                    "" +
                    "    // Input: A-I central 3x3 grid\n" +
                    "    vec3 A = src(-1.0, -1.0);\n" +
                    "    vec3 B = src(0.0, -1.0);\n" +
                    "    vec3 C = src(1.0, -1.0);\n" +
                    "" +
                    "    vec3 D = src(-1.0, 0.0);\n" +
                    "    vec3 F = src(1.0, 0.0);\n" +
                    "" +
                    "    vec3 G = src(-1.0, 1.0);\n" +
                    "    vec3 H = src(0.0, 1.0);\n" +
                    "    vec3 I = src(1.0, 1.0);\n" +
                    "" +
                    "    // Default to nearest magnification\n" +
                    "    vec3 J = E;\n" +
                    "    vec3 K = E;\n" +
                    "    vec3 L = E;\n" +
                    "    vec3 M = E;\n" +
                    "" +
                    "    // Skip constant 3x3 centers and just use nearest-neighbor them\n" +
                    "    if (same(E, A) && same(E, B) && same(E, C) && same(E, D) && same(E, F) && same(E, G) && same(E, H) && same(E, I)) {\n" +
                    "        gl_FragColor = vec4(E.bgr, 1.0);\n" +
                    "        return;\n" +
                    "    }\n" +
                    "" +
                    "    // Read additional values at the tips of the diamond pattern\n" +
                    "    vec3 P = src(0.0, -2.0);\n" +
                    "    vec3 Q = src(-2.0, 0.0);\n" +
                    "    vec3 R = src(2.0, 0.0);\n" +
                    "    vec3 S = src(0.0, 2.0);\n" +
                    "" +
                    "    // Precompute luminances\n" +
                    "    float Bl = luma(B);\n" +
                    "    float Dl = luma(D);\n" +
                    "    float El = luma(E);\n" +
                    "    float Fl = luma(F);\n" +
                    "    float Hl = luma(H);\n" +
                    "" +
                    "    // Round some corners and fill in 1:1 slopes, but preserve sharp right angles. In each expression the 1st clause is from EPX, the 2nd\n" +
                    "    // avoids double diagonal filling on 1:1 slopes (luminance breaks the tie for thick features), the 3rd preserves square corners and the\n" +
                    "    // 4th keeps 1-pixel bumps on straight lines that are darker than their background (like the tail of a pixel art 4)\n" +
                    "    if (((same(D, B) && notsame(D, H) && notsame(D, F))) && ((El >= Dl) || same(E, A)) && any_eq3(E, A, C, G) && ((El < Dl) || notsame(A, D) || notsame(E, P) || notsame(E, Q))) J = D;\n" +
                    "    if (((same(B, F) && notsame(B, D) && notsame(B, H))) && ((El >= Bl) || same(E, C)) && any_eq3(E, A, C, I) && ((El < Bl) || notsame(C, B) || notsame(E, P) || notsame(E, R))) K = B;\n" +
                    "    if (((same(H, D) && notsame(H, F) && notsame(H, B))) && ((El >= Hl) || same(E, G)) && any_eq3(E, A, G, I) && ((El < Hl) || notsame(G, H) || notsame(E, S) || notsame(E, Q))) L = H;\n" +
                    "    if (((same(F, H) && notsame(F, B) && notsame(F, D))) && ((El >= Fl) || same(E, I)) && any_eq3(E, C, G, I) && ((El < Fl) || notsame(I, H) || notsame(E, R) || notsame(E, S))) M = F;\n" +
                    "" +
                    "    // Clean up disconnected line intersections. The 1st clause recognizes being on the inside of a diagonal corner, the 2nd makes sure\n" +
                    "    // that it is not a notch at the edge of a checkerboard dithering pattern\n" +
                    "    if ((notsame(E, F) && all_eq4(E, C, I, D, Q) && all_eq2(F, B, H)) && notsame(F, src(3.0, 0.0))) K = M = F;\n" +
                    "    if ((notsame(E, D) && all_eq4(E, A, G, F, R) && all_eq2(D, B, H)) && notsame(D, src(-3.0, 0.0))) J = L = D;\n" +
                    "    if ((notsame(E, H) && all_eq4(E, G, I, B, P) && all_eq2(H, D, F)) && notsame(H, src(0.0, 3.0))) L = M = H;\n" +
                    "    if ((notsame(E, B) && all_eq4(E, A, C, H, S) && all_eq2(B, D, F)) && notsame(B, src(0.0, -3.0))) J = K = B;\n" +
                    "" +
                    "    // Remove tips of bright triangles on dark backgrounds\n" +
                    "    if ((Bl < El) && all_eq4(E, G, H, I, S) && none_eq4(E, A, D, C, F)) J = K = B;\n" +
                    "    if ((Hl < El) && all_eq4(E, A, B, C, P) && none_eq4(E, D, G, I, F)) L = M = H;\n" +
                    "    if ((Fl < El) && all_eq4(E, A, D, G, Q) && none_eq4(E, B, C, I, H)) K = M = F;\n" +
                    "    if ((Dl < El) && all_eq4(E, C, F, I, R) && none_eq4(E, B, A, G, H)) J = L = D;\n" +
                    "" +
                    "    // Further neighborhood peeking to identify 2:1 and 1:2 slopes of constant color. These extend a color assigned by the rules above\n" +
                    "    // instead of drawing from the source image\n" +
                    "    if (notsame(H, B)) {\n" +
                    "        // Above a 2:1 slope or -2:1 slope\n" +
                    "        if (notsame(H, A) && notsame(H, E) && notsame(H, C)) {\n" +
                    "            if (all_eq3(H, G, F, R) && none_eq2(H, D, src(2.0, -1.0))) L = M;\n" +
                    "            if (all_eq3(H, I, D, Q) && none_eq2(H, F, src(-2.0, -1.0))) M = L;\n" +
                    "        }\n" +
                    "" +
                    "        // Below a 2:1 or -2:1 slope (the above patterns reflected vertically)\n" +
                    "        if (notsame(B, I) && notsame(B, G) && notsame(B, E)) {\n" +
                    "            if (all_eq3(B, A, F, R) && none_eq2(B, D, src(2.0, 1.0))) J = K;\n" +
                    "            if (all_eq3(B, C, D, Q) && none_eq2(B, F, src(-2.0, 1.0))) K = J;\n" +
                    "        }\n" +
                    "    }\n" +
                    "" +
                    "    if (notsame(F, D)) {\n" +
                    "        // Right of a -1:2 or 1:2 slope (the left patterns below reflected horizontally)\n" +
                    "        if (notsame(D, I) && notsame(D, E) && notsame(D, C)) {\n" +
                    "            if (all_eq3(D, A, H, S) && none_eq2(D, B, src(1.0, 2.0))) J = L;\n" +
                    "            if (all_eq3(D, G, B, P) && none_eq2(D, H, src(1.0, -2.0))) L = J;\n" +
                    "        }\n" +
                    "" +
                    "        // Left of a 1:2 or -1:2 slope (the 2:1 patterns above transposed)\n" +
                    "        if (notsame(F, E) && notsame(F, A) && notsame(F, G)) {\n" +
                    "            if (all_eq3(F, C, H, S) && none_eq2(F, B, src(-1.0, 2.0))) K = M;\n" +
                    "            if (all_eq3(F, I, B, P) && none_eq2(F, H, src(-1.0, -2.0))) M = K;\n" +
                    "        }\n" +
                    "    }\n" +
                    "" +
                    "    // Determine which of our 4 output pixels we need to use (v grows downwards, so a.y < 0.5 is the top half)\n" +
                    "    vec2 a = fract(uv * texSize);\n" +
                    "    vec3 color = (a.x < 0.5) ? (a.y < 0.5 ? J : L) : (a.y < 0.5 ? K : M);\n" +
                    "    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB\n" +
                    "    gl_FragColor = vec4(color.bgr, 1.0);\n" +
                    "}"
        )
    }
}