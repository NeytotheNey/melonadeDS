attribute vec2 vUV;
attribute vec2 vPos;
varying vec2 uv;
void main()
{
    gl_Position = vec4(vPos, 0.0, 1.0);
    uv = vUV;
}