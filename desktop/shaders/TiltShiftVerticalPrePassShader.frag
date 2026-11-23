#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex;
uniform vec2 texSize;
uniform float topScreenWidth;
varying vec2 uv;
// Sharp band's edges as fractions of the screen's height from its top (xy), and the blur's radius in DS pixels at the screen's top and bottom edges (zw)
uniform vec4 tiltShift;
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
const vec2 BLUR_DIRECTION = vec2(0.0, 1.0);
// Below this many texels the blur can't be seen, so the row skips it
const float MIN_BLUR_RADIUS = 0.1;
// A Gaussian whose sigma is the radius / 2.5 at offsets of 0 to 6 sixths of the radius, adding up to 1
const float WEIGHT_0 = 0.167292;
const float WEIGHT_1 = 0.153382;
const float WEIGHT_2 = 0.118217;
const float WEIGHT_3 = 0.076592;
const float WEIGHT_4 = 0.041715;
const float WEIGHT_5 = 0.019098;
const float WEIGHT_6 = 0.007350;
uniform vec2 tiltShiftBoost;
const vec3 BOOST_LUMA = vec3(0.299, 0.587, 0.114);
vec3 finishTiltShift(vec3 color) {
    vec3 saturated = mix(vec3(dot(color, BOOST_LUMA)), color, tiltShiftBoost.x);
    return clamp((saturated - 0.5) * tiltShiftBoost.y + 0.5, 0.0, 1.0);
}
float blurRadius(float y) {
    // y is from 0 to just under 1, so neither division can be by 0
    if (y < tiltShift.x) {
        return tiltShift.z * smoothstep(0.0, 1.0, (tiltShift.x - y) / tiltShift.x);
    }
    if (y > tiltShift.y) {
        return tiltShift.w * smoothstep(0.0, 1.0, (y - tiltShift.y) / (1.0 - tiltShift.y));
    }
    return 0.0;
}
void main() {
    // The gap and the DS bottom screen stay as they are
    if (uv.y >= topScreenEnd) {
        gl_FragColor = vec4(texture2D(tex, uv).rgb, 1.0);
        return;
    }
    // Texels per DS pixel; the frame texture is frameLayout.w DS pixels high at any internal resolution. The band is placed on the
    // top screen's SCREEN_HEIGHT rows, inside the WideMelon camera's margin (none: the old math exactly)
    float radius;
    if (frameLayout.w == 386.0) {
        radius = blurRadius(uv.y / topScreenEnd) * texSize.y / 386.0;
    } else {
        float margin = (frameLayout.w - 386.0) * 0.5 + 0.5 * (topScreenGuard.z - topScreenGuard.w);
        float screenY = clamp((uv.y * frameLayout.w - margin) / 192.0, 0.0, 0.999);
        radius = blurRadius(screenY) * texSize.y / frameLayout.w;
    }
    if (radius < MIN_BLUR_RADIUS) {
        gl_FragColor = vec4(finishTiltShift(texture2D(tex, uv).bgr).bgr, 1.0);
        return;
    }
    vec4 band = screenBand(uv);
    vec2 spacing = BLUR_DIRECTION * radius / 6.0 / texSize;
    vec3 color = WEIGHT_0 * sampleScreen(uv, band);
    color += WEIGHT_1 * (sampleScreen(uv - spacing, band) + sampleScreen(uv + spacing, band));
    color += WEIGHT_2 * (sampleScreen(uv - spacing * 2.0, band) + sampleScreen(uv + spacing * 2.0, band));
    color += WEIGHT_3 * (sampleScreen(uv - spacing * 3.0, band) + sampleScreen(uv + spacing * 3.0, band));
    color += WEIGHT_4 * (sampleScreen(uv - spacing * 4.0, band) + sampleScreen(uv + spacing * 4.0, band));
    color += WEIGHT_5 * (sampleScreen(uv - spacing * 5.0, band) + sampleScreen(uv + spacing * 5.0, band));
    color += WEIGHT_6 * (sampleScreen(uv - spacing * 6.0, band) + sampleScreen(uv + spacing * 6.0, band));
    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB
    gl_FragColor = vec4(finishTiltShift(color).bgr, 1.0);
}