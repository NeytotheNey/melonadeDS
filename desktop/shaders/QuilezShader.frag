#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex;
uniform vec2 texSize;
uniform float topScreenWidth;
varying float alpha;
varying vec2 uv;
uniform float bottomScreenWide;
uniform vec4 frameLayout;
uniform vec4 topScreenGuard;
vec2 screenColumns(vec2 p) {
    if ((p.y < frameLayout.z) == (bottomScreenWide < 0.5) || topScreenWidth <= 256.0) {
        return vec2(-1.0, 2.0);
    }
    float halfWidth = 0.5 * 256.0 / topScreenWidth;
    float halfTexel = 0.5 / texSize.x;
    float middle = 0.5 + 0.5 * (topScreenGuard.x - topScreenGuard.y) / topScreenWidth;
    return vec2(middle - halfWidth + halfTexel, middle + halfWidth - halfTexel);
}
vec4 sampleColumns(vec2 p, vec2 columns) {
    if (columns.x < 0.0) {
        return texture2D(tex, p);
    }
    return texture2D(tex, vec2(clamp(p.x, columns.x, columns.y), p.y));
}
vec4 getTexel(vec2 p) {
    p = p * texSize + vec2(0.5);
    vec2 i = floor(p);
    vec2 f = p - i;
    f = f * f * f * (f * (f * 6.0 - vec2(15.0)) + vec2(10.0));
    p = i + f;
    p = (p - vec2(0.5)) / texSize;
    return sampleColumns(p, screenColumns(uv));
}
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
    gl_FragColor = vec4(finishColor(getTexel(uv).bgr), alpha);
}