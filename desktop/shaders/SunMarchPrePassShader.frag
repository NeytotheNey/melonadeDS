#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 uv;
uniform highp sampler2D tex;
uniform vec2 texSize;
uniform float topScreenWidth;
uniform highp vec3 sunDirection;
uniform highp mat4 sunProjection;
uniform vec4 frameLayout;
uniform highp vec4 sun;
uniform highp mat4 sunInverse;
uniform highp vec4 sunViewport;
highp float depthValue(highp vec4 texel) {
    highp vec3 bytes = floor(texel.rgb * 255.0 + 0.5);
    return bytes.r * 65536.0 + bytes.g * 256.0 + bytes.b;
}
bool isValue3D(highp float value) {
    return value > 0.0 && value < 16777215.0;
}
highp vec3 encodeDepth(highp float value) {
    value = clamp(floor(value + 0.5), 0.0, 16777215.0);
    highp float high = floor(value / 65536.0);
    highp float middle = floor((value - high * 65536.0) / 256.0);
    highp float low = value - high * 65536.0 - middle * 256.0;
    return vec3(high, middle, low) / 255.0;
}
bool hasFlag(highp vec4 texel, float bit) {
    float flags = floor(texel.a * 255.0 + 0.5);
    return mod(floor(flags / bit), 2.0) == 1.0;
}
highp vec4 sunNdc(highp vec2 p, highp float value) {
    return vec4((p.x - sunViewport.x) * 2.0 / sunViewport.z - 1.0, 1.0 - (p.y - sunViewport.y) * 2.0 / sunViewport.w,
        (value - 8388096.0) / 8388608.0, 1.0);
}
highp float sunViewDistance(highp vec2 p, highp float value) {
    highp vec4 view = sunInverse * sunNdc(p, value);
    return -view.z / view.w;
}
highp vec2 sunScreen(highp vec4 clip) {
    highp vec2 ndc = clip.xy / clip.w;
    return vec2(sunViewport.x + (ndc.x + 1.0) * 0.5 * sunViewport.z, sunViewport.y + (1.0 - ndc.y) * 0.5 * sunViewport.w);
}
// How far apart two depths of one surface can be, in view-space units, at [dist] and [pixels] DS pixels apart: about what
// a surface 16 degrees off the line of sight changes by over a DS pixel of White's camera, plus a pixel's worth for the DS's
// rounding
highp float sunSurfaceTolerance(highp float dist, float pixels) {
    return 0.0125 * dist * (pixels + 1.0);
}
const int SUN_MAX_STEPS = 128;
const float SUN_EDGE_FADE = 32.0;
// Whether depth texel t shades a step dist units in front of the camera: it's 3D, not under another camera, and nearer than
// dist - bias
bool isOccluder(highp vec2 t, highp vec2 scale, highp float dist) {
    highp vec4 depth = texture2D(tex, (t + 0.5) / texSize);
    highp float value = depthValue(depth);
    if (!isValue3D(value) || hasFlag(depth, 8.0)) {
        return false;
    }
    return sunViewDistance((t + 0.5) / scale, value) < dist - sun.w;
}
// How far from p (DS pixels) along dir the march leaves the dsSize rect
highp float exitDistance(highp vec2 p, highp vec2 dir, highp vec2 dsSize) {
    highp vec2 room = vec2(dir.x > 0.0 ? dsSize.x - p.x : p.x, dir.y > 0.0 ? dsSize.y - p.y : p.y);
    highp vec2 along = abs(dir);
    highp float exit = 1.0e9;
    if (along.x > 1.0e-6) {
        exit = min(exit, room.x / along.x);
    }
    if (along.y > 1.0e-6) {
        exit = min(exit, room.y / along.y);
    }
    return exit;
}
void main() {
    // The top screen's half in DS pixels and the depth texels per DS pixel, both whole numbers
    highp vec2 dsSize = floor(vec2(topScreenWidth, frameLayout.x * frameLayout.w) + 0.5);
    highp vec2 scale = max(floor(texSize / dsSize + 0.5), 1.0);
    highp vec2 texel = min(floor((floor(uv * dsSize) + 0.5) * scale), texSize - 1.0);
    highp vec4 depth = texture2D(tex, (texel + 0.5) / texSize);
    highp float value = depthValue(depth);
    // A W-buffered pixel's depth isn't z/w (its scene has no camera, so this pass doesn't run then)
    if (!isValue3D(value) || hasFlag(depth, 4.0)) {
        gl_FragColor = vec4(0.0);
        return;
    }
    highp vec2 p = (texel + 0.5) / scale;
    highp vec4 view = sunInverse * sunNdc(p, value);
    highp vec3 origin = view.xyz / view.w;
    // The steps are evenly spaced in view space, so they are in clip space too
    highp vec3 stride = sunDirection * (sun.y / sun.z);
    highp vec4 clipOrigin = sunProjection * vec4(origin, 1.0);
    highp vec4 clipStep = sunProjection * vec4(stride, 0.0);
    float shadow = 0.0;
    for (int i = 1; i <= SUN_MAX_STEPS; i++) {
        float n = float(i);
        highp vec4 clip = clipOrigin + clipStep * n;
        highp float dist = -(origin.z + stride.z * n);
        // A line leaves the screen or passes the camera only once, so no later step can find a shadow either
        if (n > sun.z || dist <= 1.0 || clip.w <= 0.0) {
            break;
        }
        highp vec2 q = sunScreen(clip);
        if (q.x < 0.0 || q.y < 0.0 || q.x >= dsSize.x || q.y >= dsSize.y) {
            break;
        }
        if (isOccluder(min(floor(q * scale), texSize - 1.0), scale, dist)) {
            shadow = 1.0;
            break;
        }
    }
    // The fade, over SUN_EDGE_FADE or the march's own length on screen if that's shorter, so a march that stays on screen
    // doesn't fade
    highp vec4 clipFirst = clipOrigin + clipStep;
    highp vec2 first = clipFirst.w > 0.0 ? sunScreen(clipFirst) - p : vec2(0.0);
    highp vec4 clipEnd = clipOrigin + clipStep * sun.z;
    highp float onScreen = clipEnd.w > 0.0 ? length(sunScreen(clipEnd) - p) : 1.0e9;
    highp float fade = 1.0;
    if (length(first) > 1.0e-6) {
        highp float fadeLength = clamp(onScreen, 1.0, SUN_EDGE_FADE);
        fade = clamp(exitDistance(p, normalize(first), dsSize) / fadeLength, 0.0, 1.0);
    }
    gl_FragColor = vec4(encodeDepth(value), shadow * fade);
}