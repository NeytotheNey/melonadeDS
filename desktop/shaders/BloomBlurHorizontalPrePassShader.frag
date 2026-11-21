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
const vec2 BLUR_DIRECTION = vec2(1.0, 0.0);
// A Gaussian whose sigma is 2 samples at offsets of 0 to 4 samples, adding up to 1
const float WEIGHT_0 = 0.204162;
const float WEIGHT_1 = 0.180174;
const float WEIGHT_2 = 0.123832;
const float WEIGHT_3 = 0.066282;
const float WEIGHT_4 = 0.027631;
void main() {
    // Only the DS top screen has a glow
    if (uv.y >= topScreenEnd) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    // This pass's texels are DS pixels
    vec4 band = screenBand(uv);
    vec2 spacing = BLUR_DIRECTION * bloom.z / 4.0 / texSize;
    vec3 color = WEIGHT_0 * sampleScreen(uv, band);
    color += WEIGHT_1 * (sampleScreen(uv - spacing, band) + sampleScreen(uv + spacing, band));
    color += WEIGHT_2 * (sampleScreen(uv - spacing * 2.0, band) + sampleScreen(uv + spacing * 2.0, band));
    color += WEIGHT_3 * (sampleScreen(uv - spacing * 3.0, band) + sampleScreen(uv + spacing * 3.0, band));
    color += WEIGHT_4 * (sampleScreen(uv - spacing * 4.0, band) + sampleScreen(uv + spacing * 4.0, band));
    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB
    gl_FragColor = vec4(color.bgr, 1.0);
}