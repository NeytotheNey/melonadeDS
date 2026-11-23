#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 uv;
uniform highp sampler2D tex;
uniform vec2 texSize;
uniform float sunSoftness;
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
const vec2 BLUR_DIRECTION = vec2(0.0, 1.0);
const int SUN_MAX_SOFTNESS = 8;
// Counts the texel x taps away by kernel, if it's 3D and within the tolerance of dist
void addTap(float x, float kernel, highp float dist, inout float sum, inout float weights) {
    highp vec2 p = floor(uv * texSize) + 0.5 + BLUR_DIRECTION * x;
    // Clamping would read the edge texel again
    if (p.x < 0.0 || p.y < 0.0 || p.x > texSize.x || p.y > texSize.y) {
        return;
    }
    highp vec4 texel = texture2D(tex, p / texSize);
    highp float value = depthValue(texel);
    if (!isValue3D(value)) {
        return;
    }
    highp float apart = abs(sunViewDistance(p, value) - dist);
    float weight = kernel * clamp(1.0 - apart / sunSurfaceTolerance(dist, abs(x)), 0.0, 1.0);
    sum += weight * texel.a;
    weights += weight;
}
void main() {
    highp vec4 center = texture2D(tex, uv);
    highp float value = depthValue(center);
    if (!isValue3D(value)) {
        gl_FragColor = center;
        return;
    }
    highp float dist = sunViewDistance(floor(uv * texSize) + 0.5, value);
    float sigma = max(0.5 * sunSoftness, 0.5);
    float sum = center.a;
    float weights = 1.0;
    for (int i = 1; i <= SUN_MAX_SOFTNESS; i++) {
        float x = float(i);
        if (x > sunSoftness) {
            break;
        }
        float kernel = exp(-0.5 * (x / sigma) * (x / sigma));
        addTap(-x, kernel, dist, sum, weights);
        addTap(x, kernel, dist, sum, weights);
    }
    gl_FragColor = vec4(center.rgb, sum / weights);
}