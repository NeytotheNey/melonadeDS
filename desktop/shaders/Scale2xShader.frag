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
#define topScreenEnd frameLayout.x
#define bottomScreenStart frameLayout.y
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
// Range of v (xy) and u (zw) coordinates (texel centers) of the screen that contains p
vec4 screenBand(vec2 p) {
    vec2 band = p.y < frameLayout.z ? vec2(0.0, topScreenEnd) : vec2(bottomScreenStart, 1.0);
    float halfTexel = 0.5 / texSize.y;
    return vec4(band + vec2(halfTexel, -halfTexel), screenColumns(p));
}
vec3 sampleScreen(vec2 p, vec4 band) {
    return texture2D(tex, vec2(clamp(p.x, band.z, band.w), clamp(p.y, band.x, band.y))).bgr;
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
    vec4 band = screenBand(uv);
    //   A
    // C P B
    //   D
    vec3 P = sampleScreen(uv, band);
    vec3 A = sampleScreen(uv - vec2(0.0, ps.y), band);
    vec3 B = sampleScreen(uv + vec2(ps.x, 0.0), band);
    vec3 C = sampleScreen(uv - vec2(ps.x, 0.0), band);
    vec3 D = sampleScreen(uv + vec2(0.0, ps.y), band);
    // Neighbors next to the quadrant this fragment is in, and the ones on the opposite sides
    vec2 fp = fract(uv * texSize);
    bool isLeft = fp.x < 0.5;
    bool isUp = fp.y < 0.5;
    vec3 h = isLeft ? C : B;
    vec3 hOpposite = isLeft ? B : C;
    vec3 v = isUp ? A : D;
    vec3 vOpposite = isUp ? D : A;
    vec3 color = P;
    if (h == v && h != vOpposite && v != hOpposite) {
        color = v;
    }
    gl_FragColor = vec4(finishColor(color), alpha);
}