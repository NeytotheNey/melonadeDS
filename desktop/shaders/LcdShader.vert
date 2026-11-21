attribute vec2 vPos;
attribute vec2 vUV;
attribute float vAlpha;
varying vec2 uv;
varying float alpha;
varying vec2 omega;
uniform float topScreenWidth;
uniform vec4 frameLayout;
void main() {
    gl_Position = vec4(vPos, 0.0, 1.0);
    uv = vUV;
    alpha = vAlpha;
    // The frame's rows, the constant without the WideMelon camera's margin as before it
    if (frameLayout.w == 386.0) {
        omega = 3.141592654 * 2.0 * vec2(topScreenWidth, 386);
    } else {
        omega = 3.141592654 * 2.0 * vec2(topScreenWidth, frameLayout.w);
    }
}