precision mediump float;
uniform sampler2D tex;
varying vec2 uv;
void main()
{
    vec4 color = texture2D(tex, uv);
    gl_FragColor = vec4(color.rgb, 1);
}