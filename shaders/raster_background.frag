#version 330 core
#include "include/defines.glsl"
#include "include/uniforms.glsl"
#include "include/camera_ray.glsl"
#include "include/utils.glsl"
#include "include/hdr_utils.glsl"

out vec4 fragColor;

void main()
{
    vec3 background = vec3(0.055, 0.06, 0.07);
#ifdef USEENVIRONMENTMAP
    vec3 direction = CameraRayDirection(gl_FragCoord.xy);
    background = max(hdrColor(direction), vec3(0.0));
#endif
    fragColor = vec4(background, 1.0);
}
