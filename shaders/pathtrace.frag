#version 330 core
#define DENOISE_GUIDES
uniform bool antialiasing, realtimeGuides, guidesOnly;
uniform vec2 aaSample;
uniform uint sampleSequence;
uniform sampler2D previousNormal, previousAlbedo;
uniform samplerBuffer reprojectionTable;

#ifdef COMPUTE_PATH
layout(local_size_x=8, local_size_y=8) in;
uniform ivec2 traceTileOrigin;
uniform ivec2 traceTileSize;
layout(rgba32f, binding=0) uniform writeonly image2D traceColor;
layout(rgba32f, binding=1) uniform writeonly image2D traceNormal;
layout(rgba32f, binding=2) uniform writeonly image2D traceBase;
#define TRACE_PIXEL_COORD vec4(vec2(traceTileOrigin + ivec2(gl_GlobalInvocationID.xy)) + vec2(0.5), 0.0, 1.0)
layout(rgba32f, binding=3) uniform writeonly image2D traceSample;
layout(rgba32f, binding=4) uniform writeonly image2D tracePosition;
layout(rgba32f, binding=5) uniform writeonly image2D traceGuideNormal;
layout(rgba32f, binding=6) uniform writeonly image2D traceGuideAlbedo;
layout(rgba32f, binding=7) uniform writeonly image2D traceMaterial;
vec4 RenderColorResult, NormalResult, BaseColorResult;
#else
layout(location = 0) out vec4 RenderColorResult;
layout(location = 1) out vec4 NormalResult;
layout(location = 2) out vec4 BaseColorResult;
layout(location=3) out vec4 SampleResult;
layout(location=4) out vec4 PositionResult;
layout(location=5) out vec4 GuideNormalResult;
layout(location=6) out vec4 GuideAlbedoResult;
layout(location=7) out vec4 MaterialResult;
in vec3 pix;
#endif

#include "include/defines.glsl"
#include "include/structs.glsl"
#include "include/uniforms.glsl"
#include "include/camera_ray.glsl"
#include "include/utils.glsl"
#include "include/bvh_material.glsl"
#include "include/hdr_utils.glsl"
#include "include/bsdf.glsl"
#include "include/light_sampling.glsl"
#include "include/medium.glsl"
#include "include/pathtrace.glsl"

void main(void)
{     
#ifdef COMPUTE_PATH
    if (any(greaterThanEqual(ivec2(gl_GlobalInvocationID.xy), traceTileSize))) return;
#endif
    Ray ray;
    if(samplerSeed!=0u)seed^=SamplerHash(samplerSeed);
    ray.startPoint = eye;
   // ray.startPoint = vec3(0, 0, 4);

   
    vec2 normalizedCoords = TRACE_PIXEL_COORD.xy / vec2(width,height);
    vec2 pixel = TRACE_PIXEL_COORD.xy;
    if (antialiasing) {
        if(UnifiedSamplerEnabled()) pixel+=vec2(SampleDimension(0u),SampleDimension(1u))-.5;
        else {
        uint aaSeed = uint(pixel.x)*1973u + uint(pixel.y)*9277u + 0x9e3779b9u;
        aaSeed^=samplerSeed;
        vec2 rotation = vec2(wang_hash(aaSeed),wang_hash(aaSeed))*(1.0/4294967296.0);
        pixel += fract(aaSample+rotation)-.5;
        }
    }
    if (realtimeGuides) seed = (uint(TRACE_PIXEL_COORD.x)*1973u + uint(TRACE_PIXEL_COORD.y)*9277u + sampleSequence*26699u) | 1u;
    ray.direction = CameraRayDirection(pixel);

    // primary hit  
    OutputColor color = pathTracingImportanceSampling(ray, maxBounces);
    
    vec4 raw=vec4(color.render_color,Luminance(color.render_color)*Luminance(color.render_color));
    vec4 normal=vec4((color.normal_color+1.0)*.5,0);
    vec4 base=vec4(color.base_color,1);
    #ifdef TRACE_DIAGNOSTICS
    diagnosticStage=4;
#endif
    bool valid=ValidTraceSample(raw,normal,base);
    if((pathDiagnosticFlags & (DIAG_NONFINITE|DIAG_INVALID_RAY|DIAG_BVH_OVERFLOW|DIAG_MEDIUM_OVERFLOW|DIAG_BOUNDARY_LIMIT|DIAG_BOUNDARY_MISMATCH))!=0u) {
        if(valid)RaisePathDiagnostic(DIAG_REJECTED);
        valid=false;
    }
    float alpha=1.0/(float(frameCounter)+1.0);
    vec4 oldColor=texture(preRenderColor,normalizedCoords);
    vec4 oldNormal=texture(previousNormal,normalizedCoords), oldBase=texture(previousAlbedo,normalizedCoords);
    alpha=1.0/(oldNormal.a+1.0);
    RenderColorResult=valid ? mix(oldColor,raw,alpha) : oldColor;
    NormalResult=valid ? mix(oldNormal,normal,alpha) : oldNormal;
    BaseColorResult=valid ? mix(oldBase,base,alpha) : oldBase;
    // Normal alpha counts valid samples; albedo alpha records all accumulated path classes.
    if(valid) NormalResult.a=oldNormal.a+1.0;
    BaseColorResult.a=float(uint(oldBase.a) | (valid ? (1u << uint(color.guideMaterial.y)) : 0u) |
        (pathDiagnosticFlags<<8) | (color.oidnReliable?0u:64u) | (color.oidnConfidence?0u:128u));
#ifdef TRACE_PROFILE
    // A dedicated diagnostic variant reuses auxiliary RGB targets. Beauty and
    // its second moment are unchanged. Denoising is forbidden by the caller.
    NormalResult.rgb=mix(oldNormal.rgb,vec3(traversalNodeVisits,traversalTriangleTests,profileScatters),alpha);
    BaseColorResult.rgb=mix(oldBase.rgb,vec3(profileTextureFetches,profileLightAttempts,profileInvalidLights),alpha);
#endif
    if(guidesOnly) { RenderColorResult=oldColor; NormalResult=oldNormal; BaseColorResult=oldBase; }
    if(!valid) { raw=vec4(0); color.guideMaterial.w=0; }
#ifdef TRACE_DIAGNOSTICS
    raw=diagnosticCounts0;color.guidePosition=diagnosticCounts1;
    color.guideNormal=diagnosticOrigin;color.guideAlbedo=diagnosticDirection;color.guideMaterial=diagnosticThroughput;
#endif
#ifdef COMPUTE_PATH
    ivec2 outputPixel=traceTileOrigin+ivec2(gl_GlobalInvocationID.xy);
    imageStore(traceColor,outputPixel,RenderColorResult);
    imageStore(traceNormal,outputPixel,NormalResult);
    imageStore(traceBase,outputPixel,BaseColorResult);
    if(realtimeGuides) {
        imageStore(traceSample,outputPixel,raw); imageStore(tracePosition,outputPixel,color.guidePosition);
        imageStore(traceGuideNormal,outputPixel,color.guideNormal); imageStore(traceGuideAlbedo,outputPixel,color.guideAlbedo);
        imageStore(traceMaterial,outputPixel,color.guideMaterial);
    }
#else
    SampleResult=raw; PositionResult=color.guidePosition; GuideNormalResult=color.guideNormal;
    GuideAlbedoResult=color.guideAlbedo; MaterialResult=color.guideMaterial;
#endif
}
