#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex;
varying vec2 uv;
varying float alpha;
varying vec2 omega;
/* configuration (higher values mean brighter image but reduced effect depth) */
const float brighten_scanlines = 16.0;
const float brighten_lcd = 4.0;
const vec3 offsets = 3.141592654 * vec3(1.0/2.0,1.0/2.0 - 2.0/3.0,1.0/2.0-4.0/3.0);
vec3 finishColor(vec3 rgb) {
#ifdef COLOR_CORRECTION
    vec3 c = pow(clamp(rgb, 0.0, 1.0), vec3(1.91)) * 0.89;
    // One column per input channel: new R = 0.87 R + 0.255 G - 0.125 B, new G = 0.1 R + 0.645 G + 0.255 B, new B = 0.1 R + 0.17 G + 0.73 B
    c = mat3(0.87, 0.1, 0.1, 0.255, 0.645, 0.17, -0.125, 0.255, 0.73) * c;
    return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 1.91));
#else
    return rgb;
#endif
}
void main() {
    vec2 angle = uv * omega;
    float yfactor = (brighten_scanlines + sin(angle.y)) / (brighten_scanlines + 1.0);
    vec3 xfactors = (brighten_lcd + sin(angle.x + offsets)) / (brighten_lcd + 1.0);
    gl_FragColor.rgb = finishColor(yfactor * xfactors * texture2D(tex, uv).bgr);
    gl_FragColor.a = alpha;
}