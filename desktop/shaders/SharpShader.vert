#version 300 es
in vec2 vUV;
in vec2 vPos;
in float vAlpha;
out vec2 uv;
out float alpha;
void main()
{
    gl_Position = vec4(vPos, 0.0, 1.0);
    uv = vUV;
    alpha = vAlpha;
}