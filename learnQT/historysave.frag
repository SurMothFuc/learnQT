#version 330 core
out vec4 FragColor;
in vec3 pix;

uniform sampler2D RenderColor;

layout(location = 0) out vec4 RenderColorResult;

void main(void)
{
    RenderColorResult=texture2D( RenderColor, pix.xy*0.5+0.5);
}