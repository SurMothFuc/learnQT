#version 330 core
layout(location=0) out vec4 colorResult;
layout(location=1) out vec4 momentsResult;
uniform sampler2D currentColor, positionGuide, normalGuide, albedoGuide, materialGuide;
uniform sampler2D previousPosition, previousNormal, previousAlbedo, previousMaterial;
uniform sampler2D previousColor, previousMoments, accumulatedColor, accumulatedAlbedo, accumulatedNormal;
uniform bool hasAccumulation;
uniform samplerBuffer transforms;
uniform mat4 previousView;
uniform float previousFov;
uniform bool historyValid, moving;
uniform bool rejectStaleMotion;
float luminance(vec3 c) { return dot(c, vec3(.2126,.7152,.0722)); }
void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy), size = textureSize(currentColor,0);
    vec3 current = texelFetch(currentColor,pixel,0).rgb;
    vec4 position = texelFetch(positionGuide,pixel,0), normal = texelFetch(normalGuide,pixel,0);
    vec4 albedo = texelFetch(albedoGuide,pixel,0), material = texelFetch(materialGuide,pixel,0);
    int kind = int(material.y);
    if(material.w<.5 && hasAccumulation)
        current=texelFetch(accumulatedColor,pixel,0).rgb;
    bool volume = kind==4;
    if(hasAccumulation) {
        volume = volume || ((int(texelFetch(accumulatedAlbedo,pixel,0).a) & 16)!=0);
        if(volume) current=texelFetch(accumulatedColor,pixel,0).rgb;
    }
    bool valid = historyValid && material.w > .5 && !volume && (!moving || kind < 2);
    vec3 oldPosition = position.xyz, oldNormal = normal.xyz;
    if (kind != 0 && material.z >= 0.0)
    {
        int index = int(material.z)*5;
        mat4 delta = mat4(texelFetch(transforms,index),texelFetch(transforms,index+1),
                          texelFetch(transforms,index+2),texelFetch(transforms,index+3));
        valid = valid && texelFetch(transforms,index+4).y > .5;
        oldPosition = (delta*vec4(position.xyz,1)).xyz;
        oldNormal = normalize(transpose(inverse(mat3(delta)))*normal.xyz);
    }
    vec3 viewPosition = (previousView*vec4(oldPosition,kind == 0 ? 0.0 : 1.0)).xyz;
    float scale = tan(radians(previousFov)*.5);
    vec2 projected = vec2(viewPosition.x/(float(size.x)/float(size.y)),viewPosition.y)/(-viewPosition.z*scale);
    vec2 oldPixel = (projected*.5+.5)*vec2(size)-.5;
    valid = valid && viewPosition.z < 0.0 && all(greaterThanEqual(oldPixel,vec2(-.5))) && all(lessThan(oldPixel,vec2(size)-.5));
    vec3 historyColor = vec3(0); vec3 historyMoments = vec3(0); float weight = 0.0;
    if (valid)
    {
        ivec2 base = ivec2(floor(oldPixel)); vec2 fraction = fract(oldPixel);
        float footprint = max(1e-4, position.w * 2.0 * scale / float(size.y));
        for (int y=0;y<2;++y) for (int x=0;x<2;++x)
        {
            ivec2 q = base+ivec2(x,y);
            if (any(lessThan(q,ivec2(0))) || any(greaterThanEqual(q,size))) continue;
            vec4 p = texelFetch(previousPosition,q,0), n = texelFetch(previousNormal,q,0);
            vec4 a = texelFetch(previousAlbedo,q,0), m = texelFetch(previousMaterial,q,0);
            bool match = a.w == albedo.w && m.y == material.y && m.w > .5 && texelFetch(previousMoments,q,0).z>0.0;
            if (kind == 0) match = match && length(p.xyz-oldPosition) < 4.0/float(size.y);
            else match = match && length(p.xyz-oldPosition) < footprint*2.0 && dot(n.xyz,oldNormal) > .8;
            match = match && length(a.rgb-albedo.rgb) < .15 && abs(m.x-material.x) < .1;
            if (!match) continue;
            float w = (x==0 ? 1.0-fraction.x : fraction.x)*(y==0 ? 1.0-fraction.y : fraction.y);
            historyColor += w*texelFetch(previousColor,q,0).rgb;
            historyMoments += w*texelFetch(previousMoments,q,0).xyz; weight += w;
        }
    }
    bool accepted = weight > .01;
    float lengthHistory = 1.0;
    float l = luminance(current);
    vec2 moments = vec2(l,l*l); vec3 result = current;
    if (accepted)
    {
        historyColor /= weight; historyMoments /= weight;
        // Wide variance clipping removes stale lighting without clipping every bright MC sample.
        vec3 mean=vec3(0), square=vec3(0); float count=0;
        for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
        {
            ivec2 q=clamp(pixel+ivec2(x,y),ivec2(0),size-1);
            if(texelFetch(albedoGuide,q,0).w != albedo.w) continue;
            vec3 c=texelFetch(currentColor,q,0).rgb; mean+=c; square+=c*c; count+=1;
        }
        mean/=max(count,1); vec3 deviation=sqrt(max(square/max(count,1)-mean*mean,vec3(0)));
        // Static lighting needs no clipping: clipping stationary MC noise introduces bias.
        if(moving) {
            vec3 low=mean-3.0*deviation,high=mean+3.0*deviation;
            bool stale=any(lessThan(historyColor,low)) || any(greaterThan(historyColor,high));
            if(rejectStaleMotion && stale && count>=5.0 && historyMoments.z>=4.0) {
                // A clipped stale sample still has influence for many frames.
                // Reject confident radiance disocclusions during motion instead.
                accepted=false;historyColor=current;historyMoments=vec3(moments,0.0);
            }else historyColor=clamp(historyColor,low,high);
        }
        lengthHistory=min(historyMoments.z+1.0,kind>=2 ? 4.0 : 32.0);
        float alpha=1.0/lengthHistory;
        result=mix(historyColor,current,alpha); moments=mix(historyMoments.xy,moments,alpha);
    }
    // Reprojected motion history bridges settling, but must not impose a noise floor
    // on a stationary image. Use the matching full accumulation once it has at
    // least as much information as the short history (also after local rejection).
    if(hasAccumulation && !moving && !volume) {
        float count=texelFetch(accumulatedNormal,pixel,0).a;
        if(count>=lengthHistory) {
            vec4 accumulated=texelFetch(accumulatedColor,pixel,0);
            result=accumulated.rgb;
            float averageLuminance=luminance(result);
            moments=vec2(averageLuminance,accumulated.a);
            lengthHistory=count;
        }
    }
    float variance=max(0.0,moments.y-moments.x*moments.x)/lengthHistory;
    if(lengthHistory<4.0 && kind!=0 && kind!=4 && kind!=5) {
        vec2 localMoments=vec2(0); float count=0;
        for(int y=-3;y<=3;++y) for(int x=-3;x<=3;++x) {
            ivec2 q=pixel+ivec2(x,y);
            if(any(lessThan(q,ivec2(0))) || any(greaterThanEqual(q,size))) continue;
            if(texelFetch(albedoGuide,q,0).w!=albedo.w || dot(texelFetch(normalGuide,q,0).xyz,normal.xyz)<.8) continue;
            float v=luminance(texelFetch(currentColor,q,0).rgb); localMoments+=vec2(v,v*v); count+=1;
        }
        localMoments/=max(count,1); variance=max(variance,max(0.0,localMoments.y-localMoments.x*localMoments.x)/lengthHistory);
    }
    colorResult=vec4(result,variance);
    momentsResult=vec4(moments,volume ? -1.0 : lengthHistory,accepted ? 1.0 : 0.0);
}
