#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex;
uniform vec2 texSize;
uniform float topScreenWidth;
varying vec2 uv;
#define topScreenEnd frameLayout.x
#define bottomScreenStart frameLayout.y
uniform float bottomScreenWide;
uniform vec4 frameLayout;
uniform vec4 topScreenGuard;
vec2 screenColumns(vec2 p) {
    if ((p.y < frameLayout.z) == (bottomScreenWide < 0.5) || topScreenWidth <= 256.0) {
        return vec2(-1.0, 2.0);
    }
    float halfWidth = 0.5 * 256.0 / topScreenWidth;
    float halfTexel = 0.5 / texSize.x;
    float middle = 0.5 + 0.5 * (topScreenGuard.x - topScreenGuard.y) / topScreenWidth;
    return vec2(middle - halfWidth + halfTexel, middle + halfWidth - halfTexel);
}
// Range of v (xy) and u (zw) coordinates (texel centers) of the screen that contains p
vec4 screenBand(vec2 p) {
    vec2 band = p.y < frameLayout.z ? vec2(0.0, topScreenEnd) : vec2(bottomScreenStart, 1.0);
    float halfTexel = 0.5 / texSize.y;
    return vec4(band + vec2(halfTexel, -halfTexel), screenColumns(p));
}
vec3 sampleScreen(vec2 p, vec4 band) {
    return texture2D(tex, vec2(clamp(p.x, band.z, band.w), clamp(p.y, band.x, band.y))).bgr;
}
const float FXAA_SPAN_MAX = 8.0;
const float FXAA_REDUCE_MUL = 1.0 / 8.0;
const float FXAA_REDUCE_MIN = 1.0 / 128.0;
// Minimum local contrast to count as an edge, relative to the brightest sample and absolute (for dark areas)
const float FXAA_EDGE_THRESHOLD = 1.0 / 8.0;
const float FXAA_EDGE_THRESHOLD_MIN = 1.0 / 16.0;
const vec3 luma = vec3(0.299, 0.587, 0.114);
void main() {
    vec2 rcpFrame = 1.0 / texSize;
    vec4 band = screenBand(uv);
    vec3 rgbNW = sampleScreen(uv + vec2(-0.5, -0.5) * rcpFrame, band);
    vec3 rgbNE = sampleScreen(uv + vec2(0.5, -0.5) * rcpFrame, band);
    vec3 rgbSW = sampleScreen(uv + vec2(-0.5, 0.5) * rcpFrame, band);
    vec3 rgbSE = sampleScreen(uv + vec2(0.5, 0.5) * rcpFrame, band);
    vec3 rgbM = sampleScreen(uv, band);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM = dot(rgbM, luma);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    if (lumaMax - lumaMin < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD)) {
        gl_FragColor = vec4(rgbM.bgr, 1.0);
        return;
    }
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = (lumaNW + lumaSW) - (lumaNE + lumaSE);
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = min(vec2(FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX), dir * rcpDirMin)) * rcpFrame;
    vec3 rgbA = 0.5 * (
        sampleScreen(uv + dir * (1.0 / 3.0 - 0.5), band) +
        sampleScreen(uv + dir * (2.0 / 3.0 - 0.5), band));
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        sampleScreen(uv + dir * -0.5, band) +
        sampleScreen(uv + dir * 0.5, band));
    float lumaB = dot(rgbB, luma);
    vec3 color = rgbB;
    if (lumaB < lumaMin || lumaB > lumaMax) {
        color = rgbA;
    }
    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB
    gl_FragColor = vec4(color.bgr, 1.0);
}