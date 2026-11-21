#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex;
varying vec4 uv[7];
varying float alpha;
varying vec2 columns;
vec4 sampleColumns(vec2 p, vec2 columns) {
    if (columns.x < 0.0) {
        return texture2D(tex, p);
    }
    return texture2D(tex, vec2(clamp(p.x, columns.x, columns.y), p.y));
}
const float mx = 1.00;      // start smoothing wt.
const float k = -1.10;      // wt. decrease factor
const float max_w = 0.75;   // max filter weigth
const float min_w = 0.03;   // min filter weigth
const float lum_add = 0.33; // effects smoothing
vec3 finishColor(vec3 rgb) {
#ifdef COLOR_CORRECTION
    vec3 c = pow(clamp(rgb, 0.0, 1.0), vec3(1.91)) * 0.89;
    // One column per input channel: new R = 0.87 R + 0.255 G - 0.125 B, new G = 0.1 R + 0.645 G + 0.255 B, new B = 0.1 R + 0.17 G + 0.73 B
    c = mat3(0.87, 0.1, 0.1, 0.255, 0.645, 0.17, -0.125, 0.255, 0.73) * c;
    return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 1.91));
#else
    return rgb;
#endif
}
void main()
{
    vec3 c  = sampleColumns(uv[0].xy, columns).bgr;
    vec3 i1 = sampleColumns(uv[1].xy, columns).bgr;
    vec3 i2 = sampleColumns(uv[2].xy, columns).bgr;
    vec3 i3 = sampleColumns(uv[3].xy, columns).bgr;
    vec3 i4 = sampleColumns(uv[4].xy, columns).bgr;
    vec3 o1 = sampleColumns(uv[5].xy, columns).bgr;
    vec3 o3 = sampleColumns(uv[6].xy, columns).bgr;
    vec3 o2 = sampleColumns(uv[5].zw, columns).bgr;
    vec3 o4 = sampleColumns(uv[6].zw, columns).bgr;
    vec3 s1 = sampleColumns(uv[1].zw, columns).bgr;
    vec3 s2 = sampleColumns(uv[2].zw, columns).bgr;
    vec3 s3 = sampleColumns(uv[3].zw, columns).bgr;
    vec3 s4 = sampleColumns(uv[4].zw, columns).bgr;
    vec3 dt = vec3(1.0,1.0,1.0);
    float ko1=dot(abs(o1-c),dt);
    float ko2=dot(abs(o2-c),dt);
    float ko3=dot(abs(o3-c),dt);
    float ko4=dot(abs(o4-c),dt);
    float k1=min(dot(abs(i1-i3),dt),max(ko1,ko3));
    float k2=min(dot(abs(i2-i4),dt),max(ko2,ko4));
    float w1 = k2; if(ko3<ko1) w1*=ko3/ko1;
    float w2 = k1; if(ko4<ko2) w2*=ko4/ko2;
    float w3 = k2; if(ko1<ko3) w3*=ko1/ko3;
    float w4 = k1; if(ko2<ko4) w4*=ko2/ko4;
    c=(w1*o1+w2*o2+w3*o3+w4*o4+0.001*c)/(w1+w2+w3+w4+0.001);
    w1 = k*dot(abs(i1-c)+abs(i3-c),dt)/(0.125*dot(i1+i3,dt)+lum_add);
    w2 = k*dot(abs(i2-c)+abs(i4-c),dt)/(0.125*dot(i2+i4,dt)+lum_add);
    w3 = k*dot(abs(s1-c)+abs(s3-c),dt)/(0.125*dot(s1+s3,dt)+lum_add);
    w4 = k*dot(abs(s2-c)+abs(s4-c),dt)/(0.125*dot(s2+s4,dt)+lum_add);
    w1 = clamp(w1+mx,min_w,max_w); 
    w2 = clamp(w2+mx,min_w,max_w);
    w3 = clamp(w3+mx,min_w,max_w); 
    w4 = clamp(w4+mx,min_w,max_w);
    gl_FragColor = vec4(finishColor((w1*(i1+i3)+w2*(i2+i4)+w3*(s1+s3)+w4*(s2+s4)+c)/(2.0*(w1+w2+w3+w4)+1.0)), alpha);
}