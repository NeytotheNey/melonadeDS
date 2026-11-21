precision mediump float;
varying vec2 uv;
varying float alpha;
void main()
{
    float dist = length(uv);
    gl_FragColor = vec4(0.0, 0.0, 0.0, alpha * (1.0 - smoothstep(0.0, 1.0, dist)));
}