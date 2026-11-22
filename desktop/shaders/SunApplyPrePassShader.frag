#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 uv;
uniform sampler2D tex;
uniform float topScreenWidth;
uniform highp sampler2D sunDepthTex;
uniform highp sampler2D sunMaskTex;
uniform vec3 sunColor;
uniform vec4 frameLayout;
#define topScreenEnd frameLayout.x
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
#ifdef LIGHTS
uniform vec2 bloomLights;
bool isLitLight(highp vec4 depthTexel, vec3 color) {
    float flags = floor(depthTexel.a * 255.0 + 0.5);
    return mod(floor(flags / 32.0), 2.0) == 1.0 && max(color.r, max(color.g, color.b)) >= bloomLights.x;
}
#endif
// Counts the shadow texel t (clamped onto its target of dsSize texels) by bilinear times how near its depth is to dist, and
// keeps the nearest one's shadow in closest (x the shadow, y how far apart)
void addTap(highp vec2 t, highp vec2 p, highp vec2 dsSize, float bilinear, highp float dist, inout float sum,
        inout float weights, inout highp vec2 closest) {
    t = clamp(t, vec2(0.0), dsSize - 1.0);
    highp vec4 texel = texture2D(sunMaskTex, (t + 0.5) / dsSize);
    highp float value = depthValue(texel);
    if (!isValue3D(value)) {
        return;
    }
    highp float apart = abs(sunViewDistance(t + 0.5, value) - dist);
    float weight = bilinear * clamp(1.0 - apart / sunSurfaceTolerance(dist, length(t + 0.5 - p)), 0.0, 1.0);
    sum += weight * texel.a;
    weights += weight;
    if (apart < closest.y) {
        closest = vec2(texel.a, apart);
    }
}
void main() {
    vec4 texel = texture2D(tex, uv);
    // The gap and the DS bottom screen stay as they are
    if (uv.y >= topScreenEnd) {
        gl_FragColor = texel;
        return;
    }
    vec2 depthUv = vec2(uv.x, clamp(uv.y / topScreenEnd, 0.0, 1.0));
    highp vec4 depth = texture2D(sunDepthTex, depthUv);
    highp float value = depthValue(depth);
    if (!isValue3D(value)) {
        gl_FragColor = texel;
        return;
    }
#ifdef LIGHTS
    // The frame texture's texels are BGR, which doesn't change the brightest channel
    if (isLitLight(depth, texel.rgb)) {
        gl_FragColor = texel;
        return;
    }
#endif
    float shadow = 1.0;
    if (!hasFlag(depth, 16.0)) {
        highp vec2 dsSize = floor(vec2(topScreenWidth, topScreenEnd * frameLayout.w) + 0.5);
        highp vec2 p = depthUv * dsSize;
        highp float dist = sunViewDistance(p, value);
        highp vec2 base = floor(p - 0.5);
        vec2 f = p - 0.5 - base;
        float sum = 0.0;
        float weights = 0.0;
        highp vec2 closest = vec2(0.0, 1.0e9);
        addTap(base, p, dsSize, (1.0 - f.x) * (1.0 - f.y), dist, sum, weights, closest);
        addTap(base + vec2(1.0, 0.0), p, dsSize, f.x * (1.0 - f.y), dist, sum, weights, closest);
        addTap(base + vec2(0.0, 1.0), p, dsSize, (1.0 - f.x) * f.y, dist, sum, weights, closest);
        addTap(base + vec2(1.0, 1.0), p, dsSize, f.x * f.y, dist, sum, weights, closest);
        shadow = weights > 0.001 ? sum / weights : closest.x;
    }
    // The frame texture's texels are BGR
    vec3 color = texel.bgr;
    gl_FragColor = vec4(mix(color, color * sunColor, sun.x * shadow).bgr, texel.a);
}