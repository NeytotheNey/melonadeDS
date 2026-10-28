package me.magnum.melonds.common.opengl

class ShaderProgramSource private constructor(val textureFiltering: TextureFiltering, val vertexShaderSource: String, val fragmentShaderSource: String) {
    enum class TextureFiltering {
        NEAREST,
        LINEAR
    }

    companion object {
        // The frame texture holds both screens stacked vertically, top screen first (from v = 0), with 2 empty lines between them. These sizes
        // are in DS pixels. When the OpenGL or Compute renderer is upscaled the texture is that many times bigger, so the filters that work on
        // texels (HQ2x, HQ4x, Quilez, xBR, Scale2x, FXAA, Sharp, MMPX) read the real size from the texSize uniform. Only LCD and scanlines
        // keep using these, since they draw one line/subpixel group per DS pixel. The scalers can't stay on the DS grid: upscaled 3D isn't
        // aligned to it (some games draw everything in 3D, sprites included), so DS-grid neighbours would make them copy pixels into the wrong
        // places
        private const val SCREEN_HEIGHT = 192
        private const val SCREEN_GAP = 2
        private const val LOGICAL_TEXTURE_WIDTH = 256
        private const val LOGICAL_TEXTURE_HEIGHT = SCREEN_HEIGHT * 2 + SCREEN_GAP

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

        // For pre-passes (see VideoFilter), which draw a full-target quad into a texture. No alpha, since nothing is blended
        private const val PRE_PASS_VERT_SHADER = "attribute vec2 vPos;\n" +
                "attribute vec2 vUV;\n" +
                "varying vec2 uv;\n" +
                "void main()\n" +
                "{\n" +
                "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                "    uv = vUV;\n" +
                "}"

        // Every screen shader passes its final RGB colour through finishColor(). The pre-passes don't, since they work on the raw colours.
        // It does nothing unless ShaderFactory defines COLOR_CORRECTION, for the "DS screen colours" setting. Then it applies Pokefan531's
        // nds-color profile (hunterk's Color Mangler, public domain), which mimics the paler, cooler colours of the original DS's LCD: to linear
        // with a gamma of 1.91, dimmed to 89%, mixed through the colour matrix, then back with the same gamma. pow() is undefined for negative
        // values, which some filters can output and the matrix can make (its red row has a negative blue term), so both pow() inputs are
        // clamped. Works in GLSL ES 1.00 and 3.00
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

        // Helpers for filters that sample far enough to reach past the gap between the screens. sampleScreen() keeps samples inside the screen
        // that the fragment belongs to, so the other screen and the empty lines never bleed in. Needs tex and texSize to be declared before it.
        // SCREEN_BAND_FUNCTIONS is for GLSL ES 1.00 shaders and SCREEN_BAND_FUNCTIONS_300 for #version 300 es ones, which have no texture2D()
        private const val SCREEN_BAND_RANGE_FUNCTIONS = "const float topScreenEnd = ${SCREEN_HEIGHT}.0 / ${LOGICAL_TEXTURE_HEIGHT}.0;\n" +
                "const float bottomScreenStart = ${SCREEN_HEIGHT + SCREEN_GAP}.0 / ${LOGICAL_TEXTURE_HEIGHT}.0;\n" +
                "" +
                "// Range of v coordinates (texel centres) of the screen that contains p\n" +
                "vec2 screenBand(vec2 p) {\n" +
                "    vec2 band = p.y < 0.5 ? vec2(0.0, topScreenEnd) : vec2(bottomScreenStart, 1.0);\n" +
                "    float halfTexel = 0.5 / texSize.y;\n" +
                "    return band + vec2(halfTexel, -halfTexel);\n" +
                "}\n"

        private const val SCREEN_BAND_FUNCTIONS = SCREEN_BAND_RANGE_FUNCTIONS +
                "" +
                "vec3 sampleScreen(vec2 p, vec2 band) {\n" +
                "    return texture2D(tex, vec2(p.x, clamp(p.y, band.x, band.y))).bgr;\n" +
                "}\n"

        private const val SCREEN_BAND_FUNCTIONS_300 = SCREEN_BAND_RANGE_FUNCTIONS +
                "" +
                "vec3 sampleScreen(vec2 p, vec2 band) {\n" +
                "    return texture(tex, vec2(p.x, clamp(p.y, band.x, band.y))).bgr;\n" +
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

        val NoFilterShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
            DEFAULT_VERT_SHADER,
            DEFAULT_FRAG_SHADER
        )

        val LinearShader = ShaderProgramSource(
            TextureFiltering.LINEAR,
            DEFAULT_VERT_SHADER,
            DEFAULT_FRAG_SHADER
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
                    "" +
                    "void main() {\n" +
                    "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                    "    uv = vUV;\n" +
                    "    alpha = vAlpha;\n" +
                    "    omega = 3.141592654 * 2.0 * vec2($LOGICAL_TEXTURE_WIDTH, $LOGICAL_TEXTURE_HEIGHT);\n" +
                    "}",
            "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" +
                    "precision highp float;\n" +
                    "#else\n" +
                    "precision mediump float;\n" +
                    "#endif\n" +
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
                    "" +
                    "vec2 inputSize = vec2($LOGICAL_TEXTURE_WIDTH, $LOGICAL_TEXTURE_HEIGHT);\n" + // What is this?
                    "vec2 outputSize = vec2($LOGICAL_TEXTURE_WIDTH, $LOGICAL_TEXTURE_HEIGHT);\n" + // What is this?
                    "" +
                    "void main()\n" +
                    "{\n" +
                    "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                    "    uv = vUV;\n" +
                    "    vec2 textureSize = vec2($LOGICAL_TEXTURE_WIDTH, $LOGICAL_TEXTURE_HEIGHT);\n" +
                    "    alpha = vAlpha;\n" +
                    "    omega = vec2(3.1415 * outputSize.x * textureSize.x / inputSize.x, 2.0 * 3.1415 * textureSize.y);\n" +
                    "}",
            "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" +
                    "precision highp float;\n" +
                    "#else\n" +
                    "precision mediump float;\n" +
                    "#endif\n" +
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
            "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" +
                    "precision highp float;\n" +
                    "#else\n" +
                    "precision mediump float;\n" +
                    "#endif\n" +
                    "uniform sampler2D tex;\n" +
                    "uniform vec2 texSize;\n" +
                    "varying vec2 uv;\n" +
                    "varying float alpha;\n" +
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
                    "" +
                    "    vec2 g1 = up * (step(0.5, fp.x) + step(0.5, fp.y) - 1.0) +\n" +
                    "            left * (step(0.5, fp.x) - step(0.5, fp.y));\n" +
                    "    vec2 g2 = up * (step(0.5, fp.y) - step(0.5, fp.x)) +\n" +
                    "            left * (step(0.5, fp.x) + step(0.5, fp.y) - 1.0);\n" +
                    "" +
                    "    vec3 B = texture2D(tex, uv + g1     ).bgr;\n" +
                    "    vec3 C = texture2D(tex, uv + g1 - g2).bgr;\n" +
                    "    vec3 D = texture2D(tex, uv      + g2).bgr;\n" +
                    "    vec3 E = texture2D(tex, uv          ).bgr;\n" +
                    "    vec3 F = texture2D(tex, uv      - g2).bgr;\n" +
                    "    vec3 G = texture2D(tex, uv - g1 + g2).bgr;\n" +
                    "    vec3 H = texture2D(tex, uv - g1     ).bgr;\n" +
                    "    vec3 I = texture2D(tex, uv - g1 - g2).bgr;\n" +
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
                    "varying vec4 uv[5];\n" +
                    "varying float alpha;\n" +
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
                    "" +
                    "    gl_Position = vec4(vPos, 0.0, 1.0);\n" +
                    "}",
            "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" +
                    "precision highp float;\n" +
                    "#else\n" +
                    "precision mediump float;\n" +
                    "#endif\n" +
                    "uniform sampler2D tex;\n" +
                    "varying vec4 uv[5];\n" +
                    "varying float alpha;\n" +
                    "" +
                    "    const float mx = 0.325;      // start smoothing wt.\n" +
                    "    const float k = -0.250;      // wt. decrease factor\n" +
                    "    const float max_w = 0.25;    // max filter weigth\n" +
                    "    const float min_w =-0.05;    // min filter weigth\n" +
                    "    const float lum_add = 0.25;  // effects smoothing\n" +
                    COLOR_FUNCTIONS +
                    "" +
                    "void main() {\n" +
                    "    vec3 c00 = texture2D(tex, uv[1].xy).xyz; \n" +
                    "    vec3 c10 = texture2D(tex, uv[1].zw).xyz; \n" +
                    "    vec3 c20 = texture2D(tex, uv[2].xy).xyz; \n" +
                    "    vec3 c01 = texture2D(tex, uv[4].zw).xyz; \n" +
                    "    vec3 c11 = texture2D(tex, uv[0].xy).xyz; \n" +
                    "    vec3 c21 = texture2D(tex, uv[2].zw).xyz; \n" +
                    "    vec3 c02 = texture2D(tex, uv[4].xy).xyz; \n" +
                    "    vec3 c12 = texture2D(tex, uv[3].zw).xyz; \n" +
                    "    vec3 c22 = texture2D(tex, uv[3].xy).xyz; \n" +
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
                    "varying vec4 uv[7];\n" +
                    "varying float alpha;\n" +
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
                    "}",
            "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" +
                    "precision highp float;\n" +
                    "#else\n" +
                    "precision mediump float;\n" +
                    "#endif\n" +
                    "uniform sampler2D tex;\n" +
                    "varying vec4 uv[7];\n" +
                    "varying float alpha;\n" +
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
                    "    vec3 c  = texture2D(tex, uv[0].xy).bgr;\n" +
                    "    vec3 i1 = texture2D(tex, uv[1].xy).bgr;\n" +
                    "    vec3 i2 = texture2D(tex, uv[2].xy).bgr;\n" +
                    "    vec3 i3 = texture2D(tex, uv[3].xy).bgr;\n" +
                    "    vec3 i4 = texture2D(tex, uv[4].xy).bgr;\n" +
                    "    vec3 o1 = texture2D(tex, uv[5].xy).bgr;\n" +
                    "    vec3 o3 = texture2D(tex, uv[6].xy).bgr;\n" +
                    "    vec3 o2 = texture2D(tex, uv[5].zw).bgr;\n" +
                    "    vec3 o4 = texture2D(tex, uv[6].zw).bgr;\n" +
                    "    vec3 s1 = texture2D(tex, uv[1].zw).bgr;\n" +
                    "    vec3 s2 = texture2D(tex, uv[2].zw).bgr;\n" +
                    "    vec3 s3 = texture2D(tex, uv[3].zw).bgr;\n" +
                    "    vec3 s4 = texture2D(tex, uv[4].zw).bgr;\n" +
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
            "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" +
                    "precision highp float;\n" +
                    "#else\n" +
                    "precision mediump float;\n" +
                    "#endif\n" +
                    "uniform sampler2D tex;\n" +
                    "uniform vec2 texSize;\n" +
                    "varying float alpha;\n" +
                    "varying vec2 uv;\n" +
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
                    "    return texture2D(tex, p);\n" +
                    "}\n" +
                    COLOR_FUNCTIONS +
                    "" +
                    "void main() {\n" +
                    "    gl_FragColor = vec4(finishColor(getTexel(uv).bgr), alpha);\n" +
                    "}"
        )

        // Scale2x (also known as EPX) by Andrea Mazzoleni
        // Algorithm description: https://www.scale2x.it/algorithm
        // Each texel is drawn as 4 quadrants. A quadrant takes the colour of the 2 neighbours next to it if they match each other and the other
        // 2 neighbours don't, which rounds off diagonal edges without blurring anything. Works on the real texture grid (see above)
        val Scale2xShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
            DEFAULT_VERT_SHADER,
            "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" +
                    "precision highp float;\n" +
                    "#else\n" +
                    "precision mediump float;\n" +
                    "#endif\n" +
                    "uniform sampler2D tex;\n" +
                    "uniform vec2 texSize;\n" +
                    "varying vec2 uv;\n" +
                    "varying float alpha;\n" +
                    SCREEN_BAND_FUNCTIONS +
                    COLOR_FUNCTIONS +
                    "" +
                    "void main() {\n" +
                    "    vec2 ps = 1.0 / texSize;\n" +
                    "    vec2 band = screenBand(uv);\n" +
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
                    "    // Neighbours next to the quadrant this fragment is in, and the ones on the opposite sides\n" +
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
        // Finds the direction of an edge from the luma of the 4 diagonal neighbours and blurs along it. Like the original, the corners are
        // sampled half a texel away, so with linear filtering each one is the average of 2x2 texels and 1 pixel wide detail (like text) doesn't
        // count as an edge on its own. Texels whose neighbourhood has too little contrast are left untouched. The blur can reach up to 4 texels
        // away, more than the gap between the screens, so every sample is kept inside the screen being drawn. This is a pre-pass (see
        // VideoFilter): it has to run once per frame texel, because neighbouring screen pixels inside one texel would pick different edge
        // directions
        val FxaaPrePassShader = ShaderProgramSource(
            TextureFiltering.LINEAR,
            PRE_PASS_VERT_SHADER,
            "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" +
                    "precision highp float;\n" +
                    "#else\n" +
                    "precision mediump float;\n" +
                    "#endif\n" +
                    "uniform sampler2D tex;\n" +
                    "uniform vec2 texSize;\n" +
                    "varying vec2 uv;\n" +
                    SCREEN_BAND_FUNCTIONS +
                    "" +
                    "const float FXAA_SPAN_MAX = 8.0;\n" +
                    "const float FXAA_REDUCE_MUL = 1.0 / 8.0;\n" +
                    "const float FXAA_REDUCE_MIN = 1.0 / 128.0;\n" +
                    "// Minimum local contrast to count as an edge, relative to the brightest sample and absolute (for dark areas). High quality values\n" +
                    "const float FXAA_EDGE_THRESHOLD = 1.0 / 8.0;\n" +
                    "const float FXAA_EDGE_THRESHOLD_MIN = 1.0 / 16.0;\n" +
                    "const vec3 luma = vec3(0.299, 0.587, 0.114);\n" +
                    "" +
                    "void main() {\n" +
                    "    vec2 rcpFrame = 1.0 / texSize;\n" +
                    "    vec2 band = screenBand(uv);\n" +
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
                    "    // sampleScreen() reads the BGR texels as RGB. Swap back so the output keeps the channel order of the frame texture\n" +
                    "    gl_FragColor = vec4(color.bgr, 1.0);\n" +
                    "}"
        )

        // sharp-shimmerless
        // Author: zadpos (Woohyun Kang)
        // License: Public domain
        // Treats texels as rectangles instead of points: every texel covers exactly its own area of the screen, so all DS pixels are drawn the
        // same size (no mix of 4 and 5 pixel wide columns that shimmers when the image scrolls). Only a screen pixel that a texel boundary
        // crosses gets a blend of the 2 texels, weighted by how much of the pixel each one covers. The original gets the scale factor from
        // the output size, but here it comes from the derivatives of the texel coordinates, so it works for both screens at any size and
        // rotation without the renderers passing anything in. That needs fwidth(), which is why this one is a #version 300 es shader.
        // When texels are smaller than pixels (high internal resolutions on a small screen), the pixel is treated as 1 texel wide, which
        // turns this into plain linear filtering
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
                    "uniform sampler2D tex;\n" +
                    "uniform vec2 texSize;\n" +
                    "in vec2 uv;\n" +
                    "in float alpha;\n" +
                    "out vec4 fragColor;\n" +
                    SCREEN_BAND_FUNCTIONS_300 +
                    COLOR_FUNCTIONS +
                    "" +
                    "void main() {\n" +
                    "    // Position of the pixel centre in texels, and the pixel size in texels per axis. fwidth() has to be taken on t itself,\n" +
                    "    // which is continuous across the quad, not on anything floored or clamped. Capped at 1 texel (see above)\n" +
                    "    vec2 t = uv * texSize;\n" +
                    "    vec2 d = clamp(fwidth(t), 0.00001, 1.0);\n" +
                    "" +
                    "    // The pixel covers t - d / 2 to t + d / 2. farTexel is the texel its far edge is in. If the near edge is in the same texel,\n" +
                    "    // w is 1 or more and the sample lands on that texel centre. Otherwise the boundary at farTexel is inside the pixel, w is the\n" +
                    "    // share of the pixel past it, and sampling w texels past the centre of the texel before it makes the bilinear filter blend\n" +
                    "    // the 2 texels by area. With d = 1 this is always a sample at t, so linear filtering\n" +
                    "    vec2 farEdge = t + 0.5 * d;\n" +
                    "    vec2 farTexel = floor(farEdge);\n" +
                    "    vec2 w = min((farEdge - farTexel) / d, 1.0);\n" +
                    "    vec2 p = (farTexel - 0.5 + w) / texSize;\n" +
                    "" +
                    "    fragColor = vec4(finishColor(sampleScreen(p, screenBand(uv))), alpha);\n" +
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
        // A 2x pixel art scaler in the spirit of Scale2x: it rounds off diagonal edges and fills in 1:1 and 2:1 slopes, but keeps square
        // corners, thin lines and text intact, and never makes up new colours. It's a pre-pass (see VideoFilter) that renders at twice the
        // frame texture's size, so each fragment is one quadrant of a source texel and texSize is the source size. Only runs at the DS's own
        // resolution: when the 3D is upscaled, every DS pixel is already a block of texels and this would do nothing. Some samples reach 3
        // texels away, more than the gap between the screens, so every sample is kept inside the screen being drawn. Needs NEAREST, since it
        // compares texels for exact equality
        val MmpxPrePassShader = ShaderProgramSource(
            TextureFiltering.NEAREST,
            PRE_PASS_VERT_SHADER,
            "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" +
                    "precision highp float;\n" +
                    "#else\n" +
                    "precision mediump float;\n" +
                    "#endif\n" +
                    "uniform sampler2D tex;\n" +
                    "uniform vec2 texSize;\n" +
                    "varying vec2 uv;\n" +
                    SCREEN_BAND_FUNCTIONS +
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
                    "    vec2 band = screenBand(uv);\n" +
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
                    "    // Determine which of our 4 output pixels we need to use. This pass renders at twice the size, so each fragment is one quadrant of\n" +
                    "    // the source texel. v grows downwards like y in the original (v = 0 is the first line of the top screen), so a.y < 0.5 is the top half\n" +
                    "    vec2 a = fract(uv * texSize);\n" +
                    "    vec3 color = (a.x < 0.5) ? (a.y < 0.5 ? J : L) : (a.y < 0.5 ? K : M);\n" +
                    "    // sampleScreen() reads the BGR texels as RGB. Swap back so the output keeps the channel order of the frame texture\n" +
                    "    gl_FragColor = vec4(color.bgr, 1.0);\n" +
                    "}"
        )
    }
}