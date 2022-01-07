#version 330 core
out vec4 FragColor;
in vec3 pix;

uniform sampler2D texPass0;

void main(void)
{
     FragColor =vec4(texture2D(texPass0, pix.xy*0.5+0.5).rgb,1.0);
     //FragColor =vec4(pix,1.0);
}