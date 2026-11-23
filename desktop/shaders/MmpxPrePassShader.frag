#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex;
uniform vec2 texSize;
uniform float topScreenWidth;
varying vec2 uv;
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
float luma(vec3 col) {
    return dot(col, vec3(0.2126, 0.7152, 0.0722));
}
bool same(vec3 B, vec3 A0) {
    return all(equal(B, A0));
}
bool notsame(vec3 B, vec3 A0) {
    return any(notEqual(B, A0));
}
bool all_eq2(vec3 B, vec3 A0, vec3 A1) {
    return (same(B, A0) && same(B, A1));
}
bool all_eq3(vec3 B, vec3 A0, vec3 A1, vec3 A2) {
    return (same(B, A0) && same(B, A1) && same(B, A2));
}
bool all_eq4(vec3 B, vec3 A0, vec3 A1, vec3 A2, vec3 A3) {
    return (same(B, A0) && same(B, A1) && same(B, A2) && same(B, A3));
}
bool any_eq3(vec3 B, vec3 A0, vec3 A1, vec3 A2) {
    return (same(B, A0) || same(B, A1) || same(B, A2));
}
bool none_eq2(vec3 B, vec3 A0, vec3 A1) {
    return (notsame(B, A0) && notsame(B, A1));
}
bool none_eq4(vec3 B, vec3 A0, vec3 A1, vec3 A2, vec3 A3) {
    return (notsame(B, A0) && notsame(B, A1) && notsame(B, A2) && notsame(B, A3));
}
// The source texel x, y texels away from this one (y grows downwards), kept inside the screen being drawn
#define src(x, y) sampleScreen(uv + vec2(x, y) * ps, band)
void main() {
    vec2 ps = 1.0 / texSize;
    vec4 band = screenBand(uv);
    // Our current pixel
    vec3 E = src(0.0, 0.0);
    // Input: A-I central 3x3 grid
    vec3 A = src(-1.0, -1.0);
    vec3 B = src(0.0, -1.0);
    vec3 C = src(1.0, -1.0);
    vec3 D = src(-1.0, 0.0);
    vec3 F = src(1.0, 0.0);
    vec3 G = src(-1.0, 1.0);
    vec3 H = src(0.0, 1.0);
    vec3 I = src(1.0, 1.0);
    // Default to nearest magnification
    vec3 J = E;
    vec3 K = E;
    vec3 L = E;
    vec3 M = E;
    // Skip constant 3x3 centers and just use nearest-neighbor them
    if (same(E, A) && same(E, B) && same(E, C) && same(E, D) && same(E, F) && same(E, G) && same(E, H) && same(E, I)) {
        gl_FragColor = vec4(E.bgr, 1.0);
        return;
    }
    // Read additional values at the tips of the diamond pattern
    vec3 P = src(0.0, -2.0);
    vec3 Q = src(-2.0, 0.0);
    vec3 R = src(2.0, 0.0);
    vec3 S = src(0.0, 2.0);
    // Precompute luminances
    float Bl = luma(B);
    float Dl = luma(D);
    float El = luma(E);
    float Fl = luma(F);
    float Hl = luma(H);
    // Round some corners and fill in 1:1 slopes, but preserve sharp right angles. In each expression the 1st clause is from EPX, the 2nd
    // avoids double diagonal filling on 1:1 slopes (luminance breaks the tie for thick features), the 3rd preserves square corners and the
    // 4th keeps 1-pixel bumps on straight lines that are darker than their background (like the tail of a pixel art 4)
    if (((same(D, B) && notsame(D, H) && notsame(D, F))) && ((El >= Dl) || same(E, A)) && any_eq3(E, A, C, G) && ((El < Dl) || notsame(A, D) || notsame(E, P) || notsame(E, Q))) J = D;
    if (((same(B, F) && notsame(B, D) && notsame(B, H))) && ((El >= Bl) || same(E, C)) && any_eq3(E, A, C, I) && ((El < Bl) || notsame(C, B) || notsame(E, P) || notsame(E, R))) K = B;
    if (((same(H, D) && notsame(H, F) && notsame(H, B))) && ((El >= Hl) || same(E, G)) && any_eq3(E, A, G, I) && ((El < Hl) || notsame(G, H) || notsame(E, S) || notsame(E, Q))) L = H;
    if (((same(F, H) && notsame(F, B) && notsame(F, D))) && ((El >= Fl) || same(E, I)) && any_eq3(E, C, G, I) && ((El < Fl) || notsame(I, H) || notsame(E, R) || notsame(E, S))) M = F;
    // Clean up disconnected line intersections. The 1st clause recognizes being on the inside of a diagonal corner, the 2nd makes sure
    // that it is not a notch at the edge of a checkerboard dithering pattern
    if ((notsame(E, F) && all_eq4(E, C, I, D, Q) && all_eq2(F, B, H)) && notsame(F, src(3.0, 0.0))) K = M = F;
    if ((notsame(E, D) && all_eq4(E, A, G, F, R) && all_eq2(D, B, H)) && notsame(D, src(-3.0, 0.0))) J = L = D;
    if ((notsame(E, H) && all_eq4(E, G, I, B, P) && all_eq2(H, D, F)) && notsame(H, src(0.0, 3.0))) L = M = H;
    if ((notsame(E, B) && all_eq4(E, A, C, H, S) && all_eq2(B, D, F)) && notsame(B, src(0.0, -3.0))) J = K = B;
    // Remove tips of bright triangles on dark backgrounds
    if ((Bl < El) && all_eq4(E, G, H, I, S) && none_eq4(E, A, D, C, F)) J = K = B;
    if ((Hl < El) && all_eq4(E, A, B, C, P) && none_eq4(E, D, G, I, F)) L = M = H;
    if ((Fl < El) && all_eq4(E, A, D, G, Q) && none_eq4(E, B, C, I, H)) K = M = F;
    if ((Dl < El) && all_eq4(E, C, F, I, R) && none_eq4(E, B, A, G, H)) J = L = D;
    // Further neighborhood peeking to identify 2:1 and 1:2 slopes of constant color. These extend a color assigned by the rules above
    // instead of drawing from the source image
    if (notsame(H, B)) {
        // Above a 2:1 slope or -2:1 slope
        if (notsame(H, A) && notsame(H, E) && notsame(H, C)) {
            if (all_eq3(H, G, F, R) && none_eq2(H, D, src(2.0, -1.0))) L = M;
            if (all_eq3(H, I, D, Q) && none_eq2(H, F, src(-2.0, -1.0))) M = L;
        }
        // Below a 2:1 or -2:1 slope (the above patterns reflected vertically)
        if (notsame(B, I) && notsame(B, G) && notsame(B, E)) {
            if (all_eq3(B, A, F, R) && none_eq2(B, D, src(2.0, 1.0))) J = K;
            if (all_eq3(B, C, D, Q) && none_eq2(B, F, src(-2.0, 1.0))) K = J;
        }
    }
    if (notsame(F, D)) {
        // Right of a -1:2 or 1:2 slope (the left patterns below reflected horizontally)
        if (notsame(D, I) && notsame(D, E) && notsame(D, C)) {
            if (all_eq3(D, A, H, S) && none_eq2(D, B, src(1.0, 2.0))) J = L;
            if (all_eq3(D, G, B, P) && none_eq2(D, H, src(1.0, -2.0))) L = J;
        }
        // Left of a 1:2 or -1:2 slope (the 2:1 patterns above transposed)
        if (notsame(F, E) && notsame(F, A) && notsame(F, G)) {
            if (all_eq3(F, C, H, S) && none_eq2(F, B, src(-1.0, 2.0))) K = M;
            if (all_eq3(F, I, B, P) && none_eq2(F, H, src(-1.0, -2.0))) M = K;
        }
    }
    // Determine which of our 4 output pixels we need to use (v grows downwards, so a.y < 0.5 is the top half)
    vec2 a = fract(uv * texSize);
    vec3 color = (a.x < 0.5) ? (a.y < 0.5 ? J : L) : (a.y < 0.5 ? K : M);
    // Swap back to the frame texture's channel order, since sampleScreen() reads the BGR texels as RGB
    gl_FragColor = vec4(color.bgr, 1.0);
}