#version 330 core
#include "include/defines.glsl"
#include "include/uniforms.glsl"
#include "include/utils.glsl"
#include "include/hdr_utils.glsl"

out vec4 fragColor;

void main()
{
    vec3 background = vec3(0.055, 0.06, 0.07);
#ifdef USEENVIRONMENTMAP
    vec2 normalizedCoords = gl_FragCoord.xy / vec2(width, height);
    vec2 ndc = normalizedCoords * 2.0 - 1.0;
    vec3 direction = normalize((view * vec4(ndc.x * float(width) / float(height),
        ndc.y, -1.0 / tan(radians(cameraFov) * 0.5), 0.0)).xyz);
    background = max(hdrColor(direction), vec3(0.0));
#endif
    fragColor = vec4(background, 1.0);
}
