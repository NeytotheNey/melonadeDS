#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex;
varying float alpha;
varying vec2 uv;
varying vec2 omega;
const float base_brightness = 0.95;
const vec2 sine_comp = vec2(0.05, 0.15);
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
void main ()
{
    vec4 color = vec4(texture2D(tex, uv).bgr, alpha);
    vec4 scanline = clamp(color * (base_brightness + dot(sine_comp * sin(uv * omega), vec2(1.0))), 0.0, 1.0);
    gl_FragColor = vec4(finishColor(scanline.rgb), scanline.a);
}