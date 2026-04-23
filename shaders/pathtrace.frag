#version 330 core

// 定义多个输出目标
//layout(location = 0) out vec4 FragColor;
layout(location = 0) out vec4 RenderColorResult;
layout(location = 1) out vec4 NormalResult;
layout(location = 2) out vec4 BaseColorResult;

in vec3 pix;

#include "include/defines.glsl"
#include "include/structs.glsl"
#include "include/uniforms.glsl"
#include "include/utils.glsl"
#include "include/bvh_material.glsl"
#include "include/hdr_utils.glsl"
#include "include/bsdf.glsl"
#include "include/pathtrace.glsl"

void main(void)
{     
    Ray ray;
    ray.startPoint = eye;
   // ray.startPoint = vec3(0, 0, 4);

   
   // vec2 AA = vec2((rand()-0.5)/float(width), (rand()-0.5)/float(height));
    //vec2 AA = vec2(0);
    // 计算当前像素在整个窗口中的归一化坐标 (0.0-1.0范围)
    vec2 normalizedCoords = vec2(
        (gl_FragCoord.x) / float(width),
        (gl_FragCoord.y) / float(height)
    );
    
    // 使用归一化坐标计算光线方向，这样就与视口无关
    vec4 dir = view*vec4((normalizedCoords.x*2.0-1.0)*float(width)/float(height), 
                         (normalizedCoords.y*2.0-1.0), 
                         -2.0, 0.0);
    ray.direction = normalize(dir.xyz);
    
    // primary hit  
    OutputColor color = pathTracingImportanceSampling(ray,1);
    
    // 输出结果
    RenderColorResult=vec4(color.render_color,1.0);
    NormalResult=vec4((color.normal_color+1.0)/2.0,0.0);
    BaseColorResult=vec4(color.base_color,1.0);
    
    // 计算混合因子    
    float alpha =1.0/(frameCounter+1.0);//该项控制累计帧数
    
    // 使用相同的归一化坐标获取上一帧的结果
    vec4 prevIllum= texture2D(preRenderColor, normalizedCoords);
    
    float hasNaN = float(any(isnan(RenderColorResult.xyz)));
    float finalAlpha = mix(alpha, 0.0, hasNaN);
    RenderColorResult = mix(prevIllum, RenderColorResult, finalAlpha);
}