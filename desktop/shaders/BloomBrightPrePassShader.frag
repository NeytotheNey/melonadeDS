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
vec2 screenColumns(vec2 p) {
    if ((p.y < frameLayout.z) == (bottomScreenWide < 0.5) || topScreenWidth <= 256.0) {
        return vec2(-1.0, 2.0);
    }
    float halfWidth = 0.5 * 256.0 / topScreenWidth;
    float halfTexel = 0.5 / texSize.x;
    return vec2(0.5 - halfWidth + halfTexel, 0.5 + halfWidth - halfTexel);
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
void main() {
    vec4 band = screenBand(uv);
    // The gap, the DS bottom screen, and the sides beside the top screen when only the bottom one is wide
    if (uv.y >= topScreenEnd || (band.z >= 0.0 && (uv.x < band.z || uv.x > band.w))) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    // A quarter of this pass's texel, which is one DS pixel
    vec2 tap = 0.25 / vec2(topScreenWidth, frameLayout.w);
    vec3 color = 0.25 * (
        sampleScreen(uv - tap, band) + sampleScreen(uv + tap, band) +
        sampleScreen(uv + vec2(tap.x, -tap.y), band) + sampleScreen(uv + vec2(-tap.x, tap.y), band));
    float luma = dot(color, BLOOM_LUMA);
    float knee = clamp(luma - bloom.y + BLOOM_KNEE, 0.0, 2.0 * BLOOM_KNEE);
    knee = knee * knee / (4.0 * BLOOM_KNEE);
    float weight = max(knee, luma - bloom.y) / max(luma, 0.0001);
    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB
    gl_FragColor = vec4((color * weight).bgr, 1.0);
}