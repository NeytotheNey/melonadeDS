attribute vec2 vUV;
attribute vec2 vPos;
attribute float vAlpha;
varying vec2 uv;
varying float alpha;
void main()
{
    gl_Position = vec4(vPos, 0.0, 1.0);
    uv = vUV;
    alpha = vAlpha;
}