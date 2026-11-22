#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 uv;
uniform highp sampler2D tex;
uniform vec2 texSize;
uniform float topScreenWidth;
uniform vec4 frameLayout;
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
// Counts the depth texel at p (in DS pixels) if it's 3D, with scale depth texels per DS pixel, as a difference from the first 3D
// one (ref) so the sum stays exact
void addTap(vec2 p, vec2 scale, inout highp float ref, inout highp float sum, inout float count) {
    highp vec2 texel = min(floor(p * scale), texSize - 1.0) + 0.5;
    highp float value = depthValue(texture2D(tex, texel / texSize));
    if (!isValue3D(value)) {
        return;
    }
    if (count == 0.0) {
        ref = value;
    }
    sum += value - ref;
    count += 1.0;
}
void main() {
    // The top screen's half in DS pixels and the depth texels per DS pixel, both whole numbers, so the taps land inside texels
    vec2 dsSize = floor(vec2(topScreenWidth, frameLayout.x * frameLayout.w) + 0.5);
    vec2 scale = max(floor(texSize / dsSize + 0.5), 1.0);
    vec2 dsPixel = floor(uv * dsSize);
    highp float ref = 0.0;
    highp float sum = 0.0;
    float count = 0.0;
    addTap(dsPixel + vec2(0.25, 0.25), scale, ref, sum, count);
    addTap(dsPixel + vec2(0.75, 0.25), scale, ref, sum, count);
    addTap(dsPixel + vec2(0.25, 0.75), scale, ref, sum, count);
    addTap(dsPixel + vec2(0.75, 0.75), scale, ref, sum, count);
    if (count == 0.0) {
        gl_FragColor = vec4(0.0);
        return;
    }
    gl_FragColor = vec4(encodeDepth(ref + sum / count), count / 4.0);
}