#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 uv;
uniform sampler2D tex;
uniform float topScreenWidth;
uniform highp sampler2D shadeDepthTex;
uniform highp sampler2D shadeBlurTex;
uniform highp vec3 shade;
uniform vec3 shadeColor;
uniform vec4 frameLayout;
#define topScreenEnd frameLayout.x
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
#ifdef LIGHTS
uniform vec2 bloomLights;
bool isLitLight(highp vec4 depthTexel, vec3 color) {
    float flags = floor(depthTexel.a * 255.0 + 0.5);
    return mod(floor(flags / 32.0), 2.0) == 1.0 && max(color.r, max(color.g, color.b)) >= bloomLights.x;
}
#endif
// Counts the blurred depth's texel p (clamped onto its target of dsSize texels) by bilinear times its A, as a difference from
// the pixel's own depth
void addTap(vec2 p, vec2 dsSize, float bilinear, highp float depth, inout highp float sum, inout float weights) {
    highp vec4 texel = texture2D(shadeBlurTex, (clamp(p, vec2(0.0), dsSize - 1.0) + 0.5) / dsSize);
    float weight = bilinear * texel.a;
    if (weight <= 0.0) {
        return;
    }
    sum += weight * (depthValue(texel) - depth);
    weights += weight;
}
void main() {
    vec4 texel = texture2D(tex, uv);
    // The gap and the DS bottom screen stay as they are
    if (uv.y >= topScreenEnd) {
        gl_FragColor = texel;
        return;
    }
    vec2 depthUv = vec2(uv.x, clamp(uv.y / topScreenEnd, 0.0, 1.0));
    highp vec4 depthTexel = texture2D(shadeDepthTex, depthUv);
    highp float depth = depthValue(depthTexel);
    if (!isValue3D(depth)) {
        gl_FragColor = texel;
        return;
    }
#ifdef LIGHTS
    // The frame texture's texels are BGR, which doesn't change the brightest channel
    if (isLitLight(depthTexel, texel.rgb)) {
        gl_FragColor = texel;
        return;
    }
#endif
    vec2 dsSize = floor(vec2(topScreenWidth, topScreenEnd * frameLayout.w) + 0.5);
    vec2 p = depthUv * dsSize - 0.5;
    vec2 base = floor(p);
    vec2 f = p - base;
    highp float sum = 0.0;
    float weights = 0.0;
    addTap(base, dsSize, (1.0 - f.x) * (1.0 - f.y), depth, sum, weights);
    addTap(base + vec2(1.0, 0.0), dsSize, f.x * (1.0 - f.y), depth, sum, weights);
    addTap(base + vec2(0.0, 1.0), dsSize, (1.0 - f.x) * f.y, depth, sum, weights);
    addTap(base + vec2(1.0, 1.0), dsSize, f.x * f.y, depth, sum, weights);
    if (weights <= 0.0) {
        gl_FragColor = texel;
        return;
    }
    // How much farther this pixel is than the blurred depth around it, in the depth's 24-bit steps
    highp float farther = -sum / weights;
    // weights is the blur's coverage, as the bilinear weights add up to 1. Next to 2D or the screen's edge only the 3D on one
    // side counts, which reads as farther wherever the depth slopes, so the shading fades out from full coverage to half
    float coverage = clamp(2.0 * weights - 1.0, 0.0, 1.0);
    float darkening = shade.x * coverage * clamp(farther / max(shade.z * 16777215.0, 1.0), 0.0, 1.0);
    // The frame texture's texels are BGR
    vec3 color = texel.bgr;
    gl_FragColor = vec4(mix(color, color * shadeColor, darkening).bgr, texel.a);
}