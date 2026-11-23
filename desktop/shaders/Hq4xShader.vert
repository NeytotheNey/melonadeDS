attribute vec2 vPos;
attribute vec2 vUV;
attribute float vAlpha;
uniform vec2 texSize;
uniform float topScreenWidth;
varying vec4 uv[7];
varying float alpha;
varying vec2 columns;
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
void main()
{
    vec2 dg1 = 0.5 / texSize;
    vec2 dg2 = vec2(-dg1.x, dg1.y);
    vec2 sd1 = dg1 * 0.5;
    vec2 sd2 = dg2 * 0.5;
    vec2 ddx = vec2(dg1.x, 0.0);
    vec2 ddy = vec2(0.0, dg1.y);
    gl_Position = vec4(vPos, 0.0, 1.0);
    uv[0].xy = vUV;
    uv[1].xy = vUV - sd1;
    uv[2].xy = vUV - sd2;
    uv[3].xy = vUV + sd1;
    uv[4].xy = vUV + sd2;
    uv[5].xy = vUV - dg1;
    uv[6].xy = vUV + dg1;
    uv[5].zw = vUV - dg2;
    uv[6].zw = vUV + dg2;
    uv[1].zw = vUV - ddy;
    uv[2].zw = vUV + ddx;
    uv[3].zw = vUV + ddy;
    uv[4].zw = vUV - ddx;
    alpha = vAlpha;
    // The same for the whole quad, since a quad is one screen
    columns = screenColumns(vUV);
}