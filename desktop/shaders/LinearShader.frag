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
#ifdef DEBAND
#define topScreenEnd frameLayout.x
#define bottomScreenStart frameLayout.y
// Range of v (xy) and u (zw) coordinates (texel centers) of the screen that contains p
vec4 screenBand(vec2 p) {
    vec2 band = p.y < frameLayout.z ? vec2(0.0, topScreenEnd) : vec2(bottomScreenStart, 1.0);
    float halfTexel = 0.5 / texSize.y;
    return vec4(band + vec2(halfTexel, -halfTexel), screenColumns(p));
}
vec3 sampleScreen(vec2 p, vec4 band) {
    return texture2D(tex, vec2(clamp(p.x, band.z, band.w), clamp(p.y, band.x, band.y))).bgr;
}
// A bit over two 6 bit levels (one 5 bit level), per channel
const float DEBAND_THRESHOLD = 2.5 / 63.0;
const float DEBAND_RANGE = 3.0;
const float DEBAND_GRAIN = 1.0 / 255.0;
// Hash without sine by Dave Hoskins (MIT), since sin() of big values is imprecise on mobile GPUs
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
// Whether all 4 samples around p (offset and perpendicular, both ways) are close to center, with their average in average
bool debandRing(vec2 p, vec3 center, vec2 offset, vec2 perpendicular, vec4 band, out vec3 average) {
    vec3 s1 = sampleScreen(p + offset, band);
    vec3 s2 = sampleScreen(p - offset, band);
    vec3 s3 = sampleScreen(p + perpendicular, band);
    vec3 s4 = sampleScreen(p - perpendicular, band);
    vec3 d = max(max(abs(s1 - center), abs(s2 - center)), max(abs(s3 - center), abs(s4 - center)));
    average = (s1 + s2 + s3 + s4) * 0.25;
    return max(d.r, max(d.g, d.b)) < DEBAND_THRESHOLD;
}
// center (RGB, as sampleScreen() reads it) debanded with the rings around p, clamped to 0-1
vec3 deband(vec2 p, vec3 center, vec4 band) {
    // Texels per DS pixel; the frame texture is frameLayout.w DS pixels high at any internal resolution (the constant without a
    // margin, as before it)
    float scale = frameLayout.w == 386.0 ? texSize.y / 386.0 : texSize.y / frameLayout.w;
    float angle = 6.2831853 * hash12(gl_FragCoord.xy);
    float radius = DEBAND_RANGE * scale * (0.5 + 0.5 * hash12(gl_FragCoord.yx + 17.0));
    vec2 offset = vec2(cos(angle), sin(angle)) * radius / texSize;
    // Rotated in texels, since the frame texture isn't square; scaling both by 2 or 4 later is exact
    vec2 texels = offset * texSize;
    vec2 perpendicular = vec2(-texels.y, texels.x) / texSize;
    vec3 result = center;
    vec3 average;
    // Farthest ring first, since it blends the most: the first that passes wins, so flat areas never sample the nearer ones
    if (debandRing(p, center, offset * 4.0, perpendicular * 4.0, band, average)) {
        result = average;
    } else if (debandRing(p, center, offset * 2.0, perpendicular * 2.0, band, average)) {
        result = average;
    } else if (debandRing(p, center, offset, perpendicular, band, average)) {
        result = average;
    }
    // Grain only where the color was changed, so flat areas and untouched detail stay clean
    if (result != center) {
        result += (hash12(gl_FragCoord.xy + 101.0) - 0.5) * DEBAND_GRAIN;
    }
    return clamp(result, 0.0, 1.0);
}
#endif
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
void main()
{
    vec4 color = sampleColumns(uv, screenColumns(uv));
    vec3 rgb = color.bgr;
#ifdef DEBAND
    rgb = deband(uv, rgb, screenBand(uv));
#endif
    gl_FragColor = vec4(finishColor(rgb), alpha);
}