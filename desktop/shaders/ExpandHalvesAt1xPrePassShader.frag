#version 300 es
precision highp float;
precision highp int;
uniform sampler2D tex;
uniform vec2 texSize;
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
in vec2 uv;
out vec4 fragColor;
void main()
{
    ivec2 texel = ivec2(floor(uv * texSize));
    fragColor = texelFetch(tex, isInScreenAt1x(texel) ? sourceAt1x(texel) : texel, 0);
}