#version 330 core
out vec4 FragColor;
in vec3 pix;

uniform sampler2D DirectLight;
uniform sampler2D IndirectLight;
uniform sampler2D Movement;

layout(location = 0) out vec4 DirectLightResult;
layout(location = 1) out vec4 IndirectLightResult;
layout(location = 2) out vec4 MovementResult;

void main(void)
{
    DirectLightResult=texture2D( DirectLight, pix.xy*0.5+0.5);
    IndirectLightResult=texture2D( IndirectLight, pix.xy*0.5+0.5);
    MovementResult=texture2D( Movement, pix.xy*0.5+0.5);
}