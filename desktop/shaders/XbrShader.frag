#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex;
uniform vec2 texSize;
uniform float topScreenWidth;
varying vec2 uv;
varying float alpha;
uniform float bottomScreenWide;
uniform vec4 frameLayout;
vec2 screenColumns(vec2 p) {
    if ((p.y < frameLayout.z) == (bottomScreenWide < 0.5) || topScreenWidth <= 256.0) {
        return vec2(-1.0, 2.0);
    }
    float halfWidth = 0.5 * 256.0 / topScreenWidth;
    float halfTexel = 0.5 / texSize.x;
    return vec2(0.5 - halfWidth + halfTexel, 0.5 + halfWidth - halfTexel);
}
vec4 sampleColumns(vec2 p, vec2 columns) {
    if (columns.x < 0.0) {
        return texture2D(tex, p);
    }
    return texture2D(tex, vec2(clamp(p.x, columns.x, columns.y), p.y));
}
const vec3 dtt = vec3(65536.0, 255.0, 1.0);
float reduce(vec3 color) {
    return dot(color, dtt);
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
    vec2 ps = 1.0 / texSize;
    vec2 up = vec2(0.0, -ps.y);
    vec2 left = vec2(-ps.x, 0.0);
    vec2 fp = fract(uv * texSize);
    vec2 columns = screenColumns(uv);
    vec2 g1 = up * (step(0.5, fp.x) + step(0.5, fp.y) - 1.0) +
            left * (step(0.5, fp.x) - step(0.5, fp.y));
    vec2 g2 = up * (step(0.5, fp.y) - step(0.5, fp.x)) +
            left * (step(0.5, fp.x) + step(0.5, fp.y) - 1.0);
    vec3 B = sampleColumns(uv + g1     , columns).bgr;
    vec3 C = sampleColumns(uv + g1 - g2, columns).bgr;
    vec3 D = sampleColumns(uv      + g2, columns).bgr;
    vec3 E = sampleColumns(uv          , columns).bgr;
    vec3 F = sampleColumns(uv      - g2, columns).bgr;
    vec3 G = sampleColumns(uv - g1 + g2, columns).bgr;
    vec3 H = sampleColumns(uv - g1     , columns).bgr;
    vec3 I = sampleColumns(uv - g1 - g2, columns).bgr;
    float b = reduce(B);
    float c = reduce(C);
    float d = reduce(D);
    float e = reduce(E);
    float f = reduce(F);
    float g = reduce(G);
    float h = reduce(H);
    float i = reduce(I);
    vec3 color = E;
    if (h==f && h!=e && ( e==g && (h==i || e==d) || e==c && (h==i || e==b) ))
    {
        color = mix(E, F, 0.5);
    }
    gl_FragColor = vec4(finishColor(color), alpha);
}