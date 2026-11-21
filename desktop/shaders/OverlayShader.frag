#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex;
varying vec2 uv;
varying float alpha;
void main()
{
    gl_FragColor = texture2D(tex, uv).bgra * alpha;
}