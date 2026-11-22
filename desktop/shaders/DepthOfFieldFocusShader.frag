#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 uv;
uniform highp sampler2D tex;
uniform float topScreenWidth;
uniform vec4 frameLayout;
uniform vec3 depthOfField;
uniform vec4 depthFocus;
uniform highp sampler2D depthTex;
// How far around the focus point the fallback looks, in DS pixels: the near ring first, and the far one only if the near one
// finds no 3D
const float FOCUS_NEAR = 3.0;
const float FOCUS_FAR = 6.0;
// The flat 3D search: rows every FLAT_STEP DS pixels from the point up FLAT_ROWS - 1 steps, in three columns FLAT_STEP apart,
// each against the depth FLAT_STEP lower. Two depths count as the same within a few of the DS's 24-bit steps, where White's
// floor changes by hundreds over 3 pixels
const float FLAT_STEP = 3.0;
const int FLAT_ROWS = 7;
const highp float FLAT_TOLERANCE = 2.5 / 16777215.0;
const float HOLD_LIMIT = 255.0;
highp float decodeDepth(highp vec4 texel) {
    highp vec3 bytes = floor(texel.rgb * 255.0 + 0.5);
    highp float value = bytes.r * 65536.0 + bytes.g * 256.0 + bytes.b;
    if (value >= 16777215.0) {
        return 1.0;
    }
    // The largest float below 1
    return min(value / 16777215.0, 0.99999994);
}
highp float decodeFocus(highp vec4 texel) {
    highp float depth = decodeDepth(texel);
    return depth > 0.0 ? depth : -1.0;
}
highp float depthAt(vec2 p) {
    return decodeDepth(texture2D(depthTex, p));
}
bool is3D(highp float depth) {
    return depth > 0.0 && depth < 1.0;
}
// One DS pixel as u and v of depthTex, which is the top screen's half of the frame
vec2 dsPixel() {
    return 1.0 / vec2(topScreenWidth, frameLayout.x * frameLayout.w);
}
// The nearer of nearest and the depth at p if that's 3D, so a negative nearest means none yet
highp float nearer3D(highp float nearest, vec2 p) {
    highp float depth = depthAt(p);
    if (!is3D(depth)) {
        return nearest;
    }
    return nearest < 0.0 ? depth : min(nearest, depth);
}
// The nearest of nearest and the 3D at the four points ringPixels DS pixels from the focus point, negative for none
highp float nearestOnRing(highp float nearest, float ringPixels) {
    vec2 ringStep = ringPixels * dsPixel();
    nearest = nearer3D(nearest, depthFocus.yz + vec2(ringStep.x, 0.0));
    nearest = nearer3D(nearest, depthFocus.yz - vec2(ringStep.x, 0.0));
    nearest = nearer3D(nearest, depthFocus.yz + vec2(0.0, ringStep.y));
    return nearer3D(nearest, depthFocus.yz - vec2(0.0, ringStep.y));
}
// The nearest flat 3D at and above the focus point (see above), negative for none
highp float nearestFlat() {
    vec2 flatStep = FLAT_STEP * dsPixel();
    highp float nearest = -1.0;
    for (int row = 0; row < FLAT_ROWS; row++) {
        for (int column = -1; column <= 1; column++) {
            vec2 p = depthFocus.yz + vec2(float(column) * flatStep.x, -float(row) * flatStep.y);
            // Above the depth texture's top, clamping would read its top row for both and call it flat
            if (p.y < 0.0) {
                continue;
            }
            highp float depth = depthAt(p);
            highp float below = depthAt(p + vec2(0.0, flatStep.y));
            if (is3D(depth) && is3D(below) && abs(depth - below) <= FLAT_TOLERANCE) {
                nearest = nearest < 0.0 ? depth : min(nearest, depth);
            }
        }
    }
    return nearest;
}
// The focus as stored: the depth's 24 bits in RGB, high byte in R, all 0 for none (a real depth keeps at least 1), and the hold's
// age in A
vec4 encodeFocus(highp float depth, float age) {
    if (depth < 0.0) {
        return vec4(0.0, 0.0, 0.0, age / 255.0);
    }
    highp float value = clamp(floor(depth * 16777215.0 + 0.5), 1.0, 16777215.0);
    highp float high = floor(value / 65536.0);
    highp float middle = floor((value - high * 65536.0) / 256.0);
    highp float low = value - high * 65536.0 - middle * 256.0;
    return vec4(vec3(high, middle, low) / 255.0, age / 255.0);
}
void main() {
    if (depthFocus.x >= 0.0) {
        gl_FragColor = encodeFocus(depthFocus.x, HOLD_LIMIT);
        return;
    }
    highp float flatDepth = nearestFlat();
    if (flatDepth >= 0.0) {
        gl_FragColor = encodeFocus(flatDepth, 0.0);
        return;
    }
    highp float nearest = nearestOnRing(nearer3D(-1.0, depthFocus.yz), FOCUS_NEAR);
    if (nearest < 0.0) {
        nearest = nearestOnRing(-1.0, FOCUS_FAR);
    }
    highp vec4 last = texture2D(tex, vec2(0.5));
    highp float lastFocus = decodeFocus(last);
    float age = floor(last.a * 255.0 + 0.5);
    if (nearest < 0.0 || lastFocus < 0.0 || age >= HOLD_LIMIT) {
        gl_FragColor = encodeFocus(nearest, HOLD_LIMIT);
        return;
    }
    // Still within a hold's time: something nearer at the point than the sharp range keeps the last focus, and anything else is taken
    // as it is, but the hold's time keeps running
    age = min(age + depthFocus.w, HOLD_LIMIT);
    gl_FragColor = encodeFocus(nearest < lastFocus - depthOfField.y ? lastFocus : nearest, age);
}