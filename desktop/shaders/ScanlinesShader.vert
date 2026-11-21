attribute vec2 vPos;
attribute vec2 vUV;
attribute float vAlpha;
varying vec2 uv;
varying float alpha;
varying vec2 omega;
uniform float topScreenWidth;
uniform vec4 frameLayout;
void main()
{
    // In main(), since ES 1.00 globals can only start from constants. The frame's rows, the constant without the WideMelon
    // camera's margin as before it
    float frameRows = frameLayout.w == 386.0 ? 386.0 : frameLayout.w;
    vec2 inputSize = vec2(topScreenWidth, frameRows);
    vec2 outputSize = vec2(topScreenWidth, frameRows);
    gl_Position = vec4(vPos, 0.0, 1.0);
    uv = vUV;
    vec2 textureSize = vec2(topScreenWidth, frameRows);
    alpha = vAlpha;
    omega = vec2(3.1415 * outputSize.x * textureSize.x / inputSize.x, 2.0 * 3.1415 * textureSize.y);
}