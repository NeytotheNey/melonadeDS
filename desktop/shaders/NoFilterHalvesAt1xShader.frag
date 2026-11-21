#version 300 es
precision mediump float;
precision highp int;
uniform sampler2D tex;
uniform highp vec2 texSize;
uniform highp usampler2D coordXTex;
uniform highp usampler2D coordYTex;
// x: the screens at 1x (bit 0 the top one), y: the frame's internal scale, z: the scale they were drawn at
uniform ivec3 halvesAt1x;
uniform vec4 frameLayout;
int topHalfRows() {
    return int(frameLayout.w + 0.5) - 194;
}
bool isInScreenAt1x(ivec2 texel) {
    int scale = halvesAt1x.y;
    int topRows = topHalfRows();
    int screen = texel.y >= (topRows + 2) * scale ? 1 : 0;
    int rows = screen == 0 ? topRows : 192;
    return texel.y - screen * (topRows + 2) * scale < rows * scale && (halvesAt1x.x & (1 << screen)) != 0;
}
ivec2 sourceAt1x(ivec2 texel) {
    int scale = halvesAt1x.y;
    int up = scale / max(halvesAt1x.z, 1);
    int topRows = topHalfRows();
    int screen = texel.y >= (topRows + 2) * scale ? 1 : 0;
    int start = screen * (topRows + 2) * scale;
    return ivec2(min(texel.x / up, int(texSize.x) / up - 1), start + (texel.y - start) / up);
}
// mediump, as the default shader's
in vec2 uv;
in float alpha;
out vec4 fragColor;
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
    ivec2 texel = ivec2(int(texture(coordXTex, uv).r), int(texture(coordYTex, uv).r));
    vec4 color = isInScreenAt1x(texel) ? texelFetch(tex, sourceAt1x(texel), 0) : texture(tex, uv);
    fragColor = vec4(finishColor(color.bgr), alpha);
}