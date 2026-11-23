#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex;
uniform vec2 texSize;
uniform float topScreenWidth;
varying vec2 uv;
uniform vec3 bloom;
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
const vec3 BLOOM_LUMA = vec3(0.299, 0.587, 0.114);
// Half the soft knee's width, as a luma: the glow eases in from threshold - BLOOM_KNEE to threshold + BLOOM_KNEE
const float BLOOM_KNEE = 0.1;
#ifdef LIGHTS
uniform highp sampler2D depthTex;
// The lights' threshold, as a brightest channel (x), and strength (y)
uniform vec2 bloomLights;
highp float decodeDepth(highp vec4 texel) {
    highp vec3 bytes = floor(texel.rgb * 255.0 + 0.5);
    highp float value = bytes.r * 65536.0 + bytes.g * 256.0 + bytes.b;
    if (value >= 16777215.0) {
        return 1.0;
    }
    // The largest float below 1
    return min(value / 16777215.0, 0.99999994);
}
// sampleScreen(p, band) for the luma average (black where the depth isn't 3D with ONLY_3D), or black for a light, whose own
// glow by its brightest channel goes into lights instead
vec3 sampleTap(vec2 p, vec4 band, inout vec3 lights) {
    vec2 q = vec2(clamp(p.x, band.z, band.w), clamp(p.y, band.x, band.y));
    highp vec4 depthTexel = texture2D(depthTex, vec2(q.x, clamp(q.y / topScreenEnd, 0.0, 1.0)));
    highp float depth = decodeDepth(depthTexel);
    bool is3D = depth > 0.0 && depth < 1.0;
    float flags = floor(depthTexel.a * 255.0 + 0.5);
    if (is3D && mod(floor(flags / 32.0), 2.0) == 1.0) {
        vec3 light = texture2D(tex, q).bgr;
        float peak = max(light.r, max(light.g, light.b));
        float knee = clamp(peak - bloomLights.x + BLOOM_KNEE, 0.0, 2.0 * BLOOM_KNEE);
        knee = knee * knee / (4.0 * BLOOM_KNEE);
        lights += light * (max(knee, peak - bloomLights.x) / max(peak, 0.0001));
        return vec3(0.0);
    }
#ifdef ONLY_3D
    return is3D ? texture2D(tex, q).bgr : vec3(0.0);
#else
    return sampleScreen(p, band);
#endif
}
#elif defined(ONLY_3D)
uniform highp sampler2D depthTex;
highp float decodeDepth(highp vec4 texel) {
    highp vec3 bytes = floor(texel.rgb * 255.0 + 0.5);
    highp float value = bytes.r * 65536.0 + bytes.g * 256.0 + bytes.b;
    if (value >= 16777215.0) {
        return 1.0;
    }
    // The largest float below 1
    return min(value / 16777215.0, 0.99999994);
}
// sampleScreen(p, band) where the frame's depth at that texel is 3D, black for 2D over the 3D and for what's behind it
vec3 sampleTap(vec2 p, vec4 band) {
    vec2 q = vec2(clamp(p.x, band.z, band.w), clamp(p.y, band.x, band.y));
    highp float depth = decodeDepth(texture2D(depthTex, vec2(q.x, clamp(q.y / topScreenEnd, 0.0, 1.0))));
    return depth > 0.0 && depth < 1.0 ? texture2D(tex, q).bgr : vec3(0.0);
}
#else
vec3 sampleTap(vec2 p, vec4 band) {
    return sampleScreen(p, band);
}
#endif
void main() {
    vec4 band = screenBand(uv);
    // The gap, the DS bottom screen, and the sides beside the top screen when only the bottom one is wide
    if (uv.y >= topScreenEnd || (band.z >= 0.0 && (uv.x < band.z || uv.x > band.w))) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    // A quarter of this pass's texel, which is one DS pixel
    vec2 tap = 0.25 / vec2(topScreenWidth, frameLayout.w);
#ifdef LIGHTS
    vec3 lights = vec3(0.0);
    vec3 color = 0.25 * (
        sampleTap(uv - tap, band, lights) + sampleTap(uv + tap, band, lights) +
        sampleTap(uv + vec2(tap.x, -tap.y), band, lights) + sampleTap(uv + vec2(-tap.x, tap.y), band, lights));
#else
    vec3 color = 0.25 * (
        sampleTap(uv - tap, band) + sampleTap(uv + tap, band) +
        sampleTap(uv + vec2(tap.x, -tap.y), band) + sampleTap(uv + vec2(-tap.x, tap.y), band));
#endif
    float luma = dot(color, BLOOM_LUMA);
    float knee = clamp(luma - bloom.y + BLOOM_KNEE, 0.0, 2.0 * BLOOM_KNEE);
    knee = knee * knee / (4.0 * BLOOM_KNEE);
    float weight = max(knee, luma - bloom.y) / max(luma, 0.0001);
    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB
#ifdef LIGHTS
    gl_FragColor = vec4((color * weight + 0.25 * bloomLights.y * lights).bgr, 1.0);
#else
    gl_FragColor = vec4((color * weight).bgr, 1.0);
#endif
}