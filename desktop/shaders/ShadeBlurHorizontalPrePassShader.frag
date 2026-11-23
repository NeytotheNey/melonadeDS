#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 uv;
uniform highp sampler2D tex;
uniform vec2 texSize;
uniform highp vec3 shade;
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
const vec2 BLUR_DIRECTION = vec2(1.0, 0.0);
const int SHADE_MAX_RADIUS = 16;
// Counts the texel at p by kernel times its A, as a difference from the first counted one (ref) so the sum keeps its precision
void addTap(vec2 p, float kernel, inout highp float ref, inout highp float sum, inout float weights) {
    // Clamping would read the edge texel again
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) {
        return;
    }
    highp vec4 texel = texture2D(tex, p);
    float weight = kernel * texel.a;
    if (weight <= 0.0) {
        return;
    }
    highp float value = depthValue(texel);
    if (weights == 0.0) {
        ref = value;
    }
    sum += weight * (value - ref);
    weights += weight;
}
void main() {
    vec2 tapStep = BLUR_DIRECTION / texSize;
    float sigma = max(0.5 * shade.y, 0.5);
    highp float ref = 0.0;
    highp float sum = 0.0;
    float weights = 0.0;
    float kernels = 1.0;
    addTap(uv, 1.0, ref, sum, weights);
    for (int i = 1; i <= SHADE_MAX_RADIUS; i++) {
        float x = float(i);
        if (x > shade.y) {
            break;
        }
        float kernel = exp(-0.5 * (x / sigma) * (x / sigma));
        addTap(uv - tapStep * x, kernel, ref, sum, weights);
        addTap(uv + tapStep * x, kernel, ref, sum, weights);
        kernels += 2.0 * kernel;
    }
    if (weights <= 0.0) {
        gl_FragColor = vec4(0.0);
        return;
    }
    gl_FragColor = vec4(encodeDepth(ref + sum / weights), weights / kernels);
}