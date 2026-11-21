#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex;
uniform vec2 texSize;
uniform float topScreenWidth;
varying vec2 uv;
#ifndef VIGNETTE_ONLY
uniform sampler2D glowTex;
uniform vec3 bloom;
#endif
// The vignette's strength (x) and where it starts (y)
uniform vec2 vignette;
uniform float bottomScreenWide;
uniform vec4 frameLayout;
#define topScreenEnd frameLayout.x
// The DS top screen's columns (xy) and its SCREEN_HEIGHT rows inside the camera's margin (zw), as u and v coordinates
vec4 topScreenRect() {
    float halfWidth = bottomScreenWide > 0.5 && topScreenWidth > 256.0 ? 0.5 * 256.0 / topScreenWidth : 0.5;
    float margin = (topScreenEnd * frameLayout.w - 192.0) * 0.5;
    return vec4(0.5 - halfWidth, 0.5 + halfWidth, margin / frameLayout.w, (margin + 192.0) / frameLayout.w);
}
void main() {
    vec3 texel = texture2D(tex, uv).rgb;
    // The gap and the DS bottom screen stay as they are
    if (uv.y >= topScreenEnd) {
        gl_FragColor = vec4(texel, 1.0);
        return;
    }
    vec3 color = texel.bgr;
    vec4 rect = topScreenRect();
    // Both in DS pixels, so the darkening is round on the screen whatever its width
    vec2 dsPixels = vec2(topScreenWidth, frameLayout.w);
#ifndef VIGNETTE_ONLY
    // Kept on the glow's texel centers inside the top screen, so the black around it doesn't darken its edges
    vec2 glowHalfTexel = 0.5 / dsPixels;
    vec2 glowUv = clamp(uv, vec2(rect.x, 0.0) + glowHalfTexel, vec2(rect.y, topScreenEnd) - glowHalfTexel);
    color += bloom.x * texture2D(glowTex, glowUv).bgr;
#endif
    vec2 offset = (uv - 0.5 * (rect.xz + rect.yw)) * dsPixels;
    vec2 halfSize = 0.5 * (rect.yw - rect.xz) * dsPixels;
    float t = clamp((length(offset) / length(halfSize) - vignette.y) / max(1.0 - vignette.y, 0.001), 0.0, 1.0);
    color *= 1.0 - vignette.x * t * t * (3.0 - 2.0 * t);
    gl_FragColor = vec4(clamp(color, 0.0, 1.0).bgr, 1.0);
}