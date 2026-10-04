#version 330 core
layout(location=0) out vec4 result;
uniform sampler2D color, positionGuide, normalGuide, albedoGuide, materialGuide;
uniform int stepWidth;
uniform bool accumulatedInput, finalFiltering, protectVolume, stationaryFiltering;
uniform sampler2D averageNormal, averageAlbedo;
uniform float samples;
float luminance(vec3 c) { return dot(c,vec3(.2126,.7152,.0722)); }
vec4 value(ivec2 p)
{
    vec4 c=texelFetch(color,p,0);
    if(accumulatedInput) c.a=max(0.0,c.a-luminance(c.rgb)*luminance(c.rgb))/max(samples,1.0);
    return c;
}
vec4 normalAt(ivec2 p)
{
    vec4 guide=texelFetch(normalGuide,p,0);
    if(stationaryFiltering) {
        vec3 averaged=texelFetch(averageNormal,p,0).rgb*2.0-1.0;
        if(length(averaged)>1e-5) guide.xyz=normalize(averaged);
    }
    return guide;
}
vec4 albedoAt(ivec2 p)
{
    vec4 guide=texelFetch(albedoGuide,p,0);
    if(stationaryFiltering) guide.rgb=texelFetch(averageAlbedo,p,0).rgb;
    return guide;
}
void main()
{
    ivec2 p=ivec2(gl_FragCoord.xy), size=textureSize(color,0);
    vec4 center=value(p), position=texelFetch(positionGuide,p,0), n=normalAt(p);
    vec4 a=albedoAt(p), m=texelFetch(materialGuide,p,0);
    int kind=int(m.y);
    if(protectVolume && (int(texelFetch(averageAlbedo,p,0).a) & 16)!=0) { result=center; return; }
    if(finalFiltering || stationaryFiltering) {
        int classes=int(texelFetch(averageAlbedo,p,0).a)&63;
        // The last stochastic path must never classify the full spp average by itself.
        if((classes & 16)!=0 || (classes & (classes-1))!=0 ||
           (kind!=0 && length(texelFetch(averageNormal,p,0).xyz*2.0-1.0)<.9)) { result=center; return; }
    }
    // Never spread background/emission, volume samples, or sharp reflection across guides.
    if(kind==0 || kind==4 || kind==5 || m.w<.5 || (kind>=2 && (stepWidth>1 || m.x<.1))) { result=center; return; }
    float variance=0.0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
        variance+=value(clamp(p+ivec2(x,y),ivec2(0),size-1)).a/9.0;
    if(accumulatedInput && samples<4.0) {
        vec2 estimate=vec2(0); float count=0;
        for(int y=-3;y<=3;++y) for(int x=-3;x<=3;++x) {
            ivec2 q=p+ivec2(x,y);
            if(any(lessThan(q,ivec2(0))) || any(greaterThanEqual(q,size))) continue;
            if(albedoAt(q).w!=a.w || dot(normalAt(q).xyz,n.xyz)<.8) continue;
            float l=luminance(texelFetch(color,q,0).rgb); estimate+=vec2(l,l*l); count+=1;
        }
        estimate/=max(count,1); variance=max(variance,max(0.0,estimate.y-estimate.x*estimate.x)); center.a=variance;
    }
    float phi=4.0*sqrt(max(variance,0.0))+1e-5;
    // World-space depth tolerance scales with footprint; normals prevent joining perpendicular surfaces.
    float footprint=max(1e-4,position.w/float(size.y));
    const float kernel[3]=float[3](1.0,2.0/3.0,1.0/6.0);
    vec3 sum=center.rgb; float sumVariance=center.a, weights=1.0;
    for(int y=-2;y<=2;++y) for(int x=-2;x<=2;++x)
    {
        if(x==0 && y==0) continue;
        ivec2 q=p+ivec2(x,y)*stepWidth;
        if(any(lessThan(q,ivec2(0))) || any(greaterThanEqual(q,size))) continue;
        vec4 neighborPosition=texelFetch(positionGuide,q,0), neighborNormal=normalAt(q);
        vec4 neighborAlbedo=albedoAt(q), neighborMaterial=texelFetch(materialGuide,q,0);
        if(neighborAlbedo.w!=a.w || neighborMaterial.y!=m.y || neighborMaterial.w<.5) continue;
        if(protectVolume && (int(texelFetch(averageAlbedo,q,0).a) & 16)!=0) continue;
        if(finalFiltering || stationaryFiltering) {
            int classes=int(texelFetch(averageAlbedo,q,0).a)&63;
            if((classes & 16)!=0 || (classes & (classes-1))!=0 || length(texelFetch(averageNormal,q,0).xyz*2.0-1.0)<.9) continue;
        }
        vec4 c=value(q);
        float plane=abs(dot(neighborPosition.xyz-position.xyz,n.xyz));
        float w=kernel[abs(x)]*kernel[abs(y)]*pow(max(0.0,dot(n.xyz,neighborNormal.xyz)),64.0);
        w*=exp(-plane/(footprint*float(stepWidth)*max(length(vec2(x,y)),1.0)));
        w*=exp(-length(a.rgb-neighborAlbedo.rgb)*16.0-abs(m.x-neighborMaterial.x)*16.0);
        w*=exp(-abs(luminance(c.rgb)-luminance(center.rgb))/phi);
        sum+=w*c.rgb; sumVariance+=w*w*c.a; weights+=w;
    }
    result=vec4(sum/weights,sumVariance/(weights*weights));
}
