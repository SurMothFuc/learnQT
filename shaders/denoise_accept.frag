#version 330 core
uniform sampler2D moments;
layout(location=0) out vec4 result;
void main()
{
    if(texelFetch(moments,ivec2(gl_FragCoord.xy),0).w<.5) discard;
    result=vec4(0);
}
