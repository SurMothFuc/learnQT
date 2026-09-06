#version 330 core
layout(location=0)out uint objectId;
layout(location=1)out float objectDepth;
in vec3 pix;
#include "include/defines.glsl"
#include "include/structs.glsl"
#include "include/uniforms.glsl"
#include "include/utils.glsl"
#include "include/bvh_material.glsl"
void main(){
    vec2 uv=gl_FragCoord.xy/vec2(width,height);
    Ray ray;ray.startPoint=eye;ray.direction=normalize((view*vec4((uv.x*2-1)*float(width)/float(height),uv.y*2-1,-1.0/tan(radians(cameraFov)*.5),0)).xyz);
    HitResult h=hitBVH(ray);objectId=h.isHit?texelFetch(surfaceTable,h.triangleIndex).y+1u:0u;objectDepth=h.isHit?h.hitDistance:0.0;
}
