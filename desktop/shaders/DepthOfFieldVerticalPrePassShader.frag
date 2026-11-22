#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex;
uniform vec2 texSize;
uniform float topScreenWidth;
varying vec2 uv;
uniform vec3 depthOfField;
uniform vec3 depthFocus;
uniform highp sampler2D depthTex;
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
const vec2 BLUR_DIRECTION = vec2(0.0, 1.0);
// Below this many texels the blur can't be seen, so the texel skips it
const float MIN_BLUR_RADIUS = 0.1;
// How far around the focus point to look for 3D, in DS pixels
const float FOCUS_SEARCH = 4.0;
// The depth at p (u and v of the frame), from 0 to 1
highp float frameDepth(vec2 p) {
    highp vec2 rg = texture2D(depthTex, vec2(p.x, clamp(p.y / topScreenEnd, 0.0, 1.0))).rg;
    return (floor(rg.r * 255.0 + 0.5) * 256.0 + floor(rg.g * 255.0 + 0.5)) / 65535.0;
}
highp float depthAt(vec2 p) {
    highp vec2 rg = texture2D(depthTex, p).rg;
    return (floor(rg.r * 255.0 + 0.5) * 256.0 + floor(rg.g * 255.0 + 0.5)) / 65535.0;
}
bool is3D(highp float depth) {
    return depth > 0.0 && depth < 1.0;
}
// The focus depth, negative for none
highp float focusDepth() {
    if (depthFocus.x >= 0.0) {
        return depthFocus.x;
    }
    vec2 searchStep = FOCUS_SEARCH / vec2(topScreenWidth, topScreenEnd * frameLayout.w);
    highp float depth = depthAt(depthFocus.yz);
    if (is3D(depth)) {
        return depth;
    }
    depth = depthAt(depthFocus.yz + vec2(0.0, searchStep.y));
    if (is3D(depth)) {
        return depth;
    }
    depth = depthAt(depthFocus.yz - vec2(0.0, searchStep.y));
    if (is3D(depth)) {
        return depth;
    }
    depth = depthAt(depthFocus.yz + vec2(searchStep.x, 0.0));
    if (is3D(depth)) {
        return depth;
    }
    depth = depthAt(depthFocus.yz - vec2(searchStep.x, 0.0));
    return is3D(depth) ? depth : -1.0;
}
// The blur's radius in DS pixels at a depth, 0 in focus and for 2D
float blurRadius(highp float depth, highp float focus) {
    if (depth <= 0.0 || focus < 0.0) {
        return 0.0;
    }
    highp float offset = abs(depth - focus);
    return depthOfField.x * smoothstep(depthOfField.y, depthOfField.y + max(depthOfField.z, 0.0001), offset);
}
void main() {
    // The gap and the DS bottom screen stay as they are
    if (uv.y >= topScreenEnd) {
        gl_FragColor = vec4(texture2D(tex, uv).rgb, 1.0);
        return;
    }
    // Texels per DS pixel; the frame texture is frameLayout.w DS pixels high at any internal resolution
    float scale = texSize.y / frameLayout.w;
    highp float focus = focusDepth();
    float radius = blurRadius(frameDepth(uv), focus) * scale;
    if (radius < MIN_BLUR_RADIUS) {
        gl_FragColor = vec4(texture2D(tex, uv).rgb, 1.0);
        return;
    }
    vec4 band = screenBand(uv);
    vec2 spacing = BLUR_DIRECTION * radius / 6.0 / texSize;
    vec3 color = sampleScreen(uv, band);
    float total = 1.0;
    for (int i = 1; i <= 6; i++) {
        // A Gaussian whose sigma is the radius / 2.5, as the tilt-shift's
        float x = float(i) * 2.5 / 6.0;
        float weight = exp(-0.5 * x * x);
        float tapDistance = float(i) * radius / 6.0;
        vec2 before = uv - spacing * float(i);
        vec2 after = uv + spacing * float(i);
        float beforeWeight = weight * clamp(blurRadius(frameDepth(before), focus) * scale / tapDistance, 0.0, 1.0);
        float afterWeight = weight * clamp(blurRadius(frameDepth(after), focus) * scale / tapDistance, 0.0, 1.0);
        color += beforeWeight * sampleScreen(before, band) + afterWeight * sampleScreen(after, band);
        total += beforeWeight + afterWeight;
    }
    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB
    gl_FragColor = vec4((color / total).bgr, 1.0);
}