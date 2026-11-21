#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex;
varying vec2 uv;
uniform vec4 frameLayout;
#define topScreenEnd frameLayout.x
uniform vec2 tiltShiftBoost;
const vec3 BOOST_LUMA = vec3(0.299, 0.587, 0.114);
vec3 finishTiltShift(vec3 color) {
    vec3 saturated = mix(vec3(dot(color, BOOST_LUMA)), color, tiltShiftBoost.x);
    return clamp((saturated - 0.5) * tiltShiftBoost.y + 0.5, 0.0, 1.0);
}
void main() {
    vec3 texel = texture2D(tex, uv).rgb;
    // The gap and the DS bottom screen stay as they are
    if (uv.y >= topScreenEnd) {
        gl_FragColor = vec4(texel, 1.0);
        return;
    }
    gl_FragColor = vec4(finishTiltShift(texel.bgr).bgr, 1.0);
}