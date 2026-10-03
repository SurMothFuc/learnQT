// 材质贴图的 UV 变换、寻址与采样。路径追踪与光栅化交互预览共用，
// 只依赖材质贴图数组与其描述缓冲，不依赖任何场景访问函数。

vec4 MaterialTextureInfo(int textureIndex,int field) {
    return texelFetch(materialTextureInfo,textureIndex*max(3,materialTextureInfoStride)+field);
}
ivec2 MaterialView(int source,bool color) {
    if(materialTextureInfoStride>=5) {
        vec4 refs=MaterialTextureInfo(source,4);return ivec2(color?refs.zw:refs.xy);
    }
    vec4 filters=MaterialTextureInfo(source,2);
    return ivec2(0,color&&filters.w>.5?int(filters.z):source);
}
ivec2 MaterialPoolSize(int pool,int level) {
#if MATERIAL_TEXTURE_POOL_COUNT >= 2
    if(pool==1)return textureSize(materialTextures1,level).xy;
#endif
#if MATERIAL_TEXTURE_POOL_COUNT >= 3
    if(pool==2)return textureSize(materialTextures2,level).xy;
#endif
#if MATERIAL_TEXTURE_POOL_COUNT >= 4
    if(pool==3)return textureSize(materialTextures3,level).xy;
#endif
    return textureSize(materialTextures,level).xy;
}
vec4 MaterialPoolTexel(ivec2 ref,ivec2 p,int level) {
#ifdef TRACE_PROFILE
    ++profileTextureFetches;
#endif
#if MATERIAL_TEXTURE_POOL_COUNT >= 2
    if(ref.x==1)return texelFetch(materialTextures1,ivec3(p,ref.y),level);
#endif
#if MATERIAL_TEXTURE_POOL_COUNT >= 3
    if(ref.x==2)return texelFetch(materialTextures2,ivec3(p,ref.y),level);
#endif
#if MATERIAL_TEXTURE_POOL_COUNT >= 4
    if(ref.x==3)return texelFetch(materialTextures3,ivec3(p,ref.y),level);
#endif
    return texelFetch(materialTextures,ivec3(p,ref.y),level);
}
float SrgbChannelToLinear(float value)
{
    return value <= 0.04045
        ? value / 12.92
        : pow((value + 0.055) / 1.055, 2.4);
}

vec3 SrgbToLinear(vec3 value)
{
    return vec3(
        SrgbChannelToLinear(value.r),
        SrgbChannelToLinear(value.g),
        SrgbChannelToLinear(value.b));
}

float MirrorTextureCoordinate(float coordinate)
{
    float wrapped = mod(coordinate, 2.0);
    if (wrapped < 0.0) wrapped += 2.0;
    return wrapped <= 1.0 ? wrapped : 2.0 - wrapped;
}

vec2 TransformMaterialUV(int textureIndex, vec2 uv)
{
    if (textureIndex < 0 || textureIndex >= materialTextureCount) {
        return uv;
    }

    vec4 transform = MaterialTextureInfo(textureIndex,0);
    float rotationAngle = MaterialTextureInfo(textureIndex,1).x;
    float cosine = cos(rotationAngle);
    float sine = sin(rotationAngle);
    mat2 rotation = mat2(cosine, sine, -sine, cosine);
    vec2 transformed = uv * transform.xy;
    return rotation * (transformed - vec2(0.5)) + vec2(0.5) + transform.zw;
}

float WrapTextureCoordinate(float coordinate, int mode)
{
    if (mode == 1 || mode == 3) return clamp(coordinate, 0.0, 1.0);
    if (mode == 2) return MirrorTextureCoordinate(coordinate);
    return fract(coordinate);
}

// Explicit footprint filtering works in fragment and compute paths.
int WrappedTexel(int p,int size,int mode) {
    if(mode==0)return (p%size+size)%size;
    if(mode==2){int q=(p%(2*size)+2*size)%(2*size);return q<size?q:2*size-1-q;}
    return clamp(p,0,size-1);
}
vec4 MaterialTexel(ivec2 ref,ivec2 p,int level,ivec2 modes) {
    ivec2 size=MaterialPoolSize(ref.x,level);
    if((modes.x==3&&(p.x<0||p.x>=size.x)) || (modes.y==3&&(p.y<0||p.y>=size.y)))return vec4(0);
    p=ivec2(WrappedTexel(p.x,size.x,modes.x),WrappedTexel(p.y,size.y,modes.y));
    return MaterialPoolTexel(ref,p,level);
}
vec4 MaterialLevel(ivec2 ref,vec2 uv,int level,bool linearFilter,ivec2 modes) {
    vec2 p=uv*vec2(MaterialPoolSize(ref.x,level));
    if(!linearFilter)return MaterialTexel(ref,ivec2(floor(p)),level,modes);
    p-=.5;ivec2 lo=ivec2(floor(p));vec2 f=fract(p);
    return mix(mix(MaterialTexel(ref,lo,level,modes),MaterialTexel(ref,lo+ivec2(1,0),level,modes),f.x),
               mix(MaterialTexel(ref,lo+ivec2(0,1),level,modes),MaterialTexel(ref,lo+ivec2(1,1),level,modes),f.x),f.y);
}
vec4 SampleMaterialTextureFootprint(int textureIndex,vec2 uv,vec2 footprint,bool colorView) {
    if(textureIndex<0||textureIndex>=materialTextureCount)return vec4(1);
    vec4 filters=MaterialTextureInfo(textureIndex,2);
    ivec2 ref=MaterialView(textureIndex,colorView);
    if(ref.x<0||ref.y<0)return vec4(1);
    vec2 transformed=TransformMaterialUV(textureIndex,uv);
    vec4 sampling=MaterialTextureInfo(textureIndex,1);
    ivec2 modes=ivec2(sampling.yz);
    if((modes.x==3&&(transformed.x<0||transformed.x>1)) ||
       (modes.y==3&&(transformed.y<0||transformed.y>1)))return vec4(0);
    // Bound coordinates before float-to-int conversion; footprint remains
    // unwrapped below, so repeat seams do not create artificial minification.
    transformed=vec2(WrapTextureCoordinate(transformed.x,modes.x),
                     WrapTextureCoordinate(transformed.y,modes.y));
    vec2 scale=abs(MaterialTextureInfo(textureIndex,0).xy);
    float c=abs(cos(sampling.x)),s=abs(sin(sampling.x));
    vec2 extent=abs(footprint)*scale;
    extent=vec2(c*extent.x+s*extent.y,s*extent.x+c*extent.y);
    ivec2 size=MaterialPoolSize(ref.x,0);
    vec2 texels=extent*vec2(size);
    float rho=max(texels.x,texels.y);
    bool minifying=rho>1.0;
    int filterMode=minifying?int(filters.x):int(filters.y);
    int maxLevel=int(floor(log2(float(max(size.x,size.y)))));
    float lod=clamp(log2(max(rho,1.0)),0.0,float(maxLevel));
    vec4 value;
    if(!minifying||filterMode==9728||filterMode==9729)
        value=MaterialLevel(ref,transformed,0,filterMode!=9728,modes);
    else if(filterMode==9984||filterMode==9985)
        value=MaterialLevel(ref,transformed,int(floor(lod+.5)),filterMode==9985,modes);
    else {
        int lo=int(floor(lod)),hi=min(lo+1,maxLevel);
        bool linearFilter=filterMode!=9986;
        value=mix(MaterialLevel(ref,transformed,lo,linearFilter,modes),
                  MaterialLevel(ref,transformed,hi,linearFilter,modes),fract(lod));
    }
    if(colorView&&filters.w<.5)value.rgb=SrgbToLinear(value.rgb); // legacy numerical fixture layout
    return value;
}
vec4 SampleMaterialTexture(int textureIndex,vec2 uv) {
    return SampleMaterialTextureFootprint(textureIndex,uv,materialEvaluationFootprint,false);
}
float MaterialTextureRho(int textureIndex,vec2 footprint) {
    vec4 t=MaterialTextureInfo(textureIndex,0),s=MaterialTextureInfo(textureIndex,1);
    vec2 extent=abs(footprint*t.xy);float c=abs(cos(s.x)),v=abs(sin(s.x));
    extent=vec2(c*extent.x+v*extent.y,v*extent.x+c*extent.y);
    ivec2 ref=MaterialView(textureIndex,false);
    if(ref.x<0||ref.y<0)return 0.0;
    vec2 texels=extent*vec2(MaterialPoolSize(ref.x,0));
    return max(texels.x,texels.y);
}
bool HasMaskCoverage(int source) {
    return source>=0 && source<materialTextureCount && materialTextureInfoStride>=4 &&
           MaterialTextureInfo(source,3).z>.5 && MaterialView(source,false).y>=0;
}
float MaskCoverageTexel(ivec2 ref,ivec2 p,int level,ivec2 modes,vec4 recipe) {
    vec4 value=MaterialTexel(ref,p,level,modes);
    int channel=int(recipe.y);
    return level==0&&recipe.w<.5?float(value[channel]>=recipe.x):value[channel];
}
float MaskCoverageLevel(ivec2 ref,vec2 uv,int level,bool linearFilter,ivec2 modes,vec4 recipe) {
    vec2 p=uv*vec2(MaterialPoolSize(ref.x,level));
    if(!linearFilter)return MaskCoverageTexel(ref,ivec2(floor(p)),level,modes,recipe);
    p-=.5;ivec2 lo=ivec2(floor(p));vec2 f=fract(p);
    return mix(mix(MaskCoverageTexel(ref,lo,level,modes,recipe),MaskCoverageTexel(ref,lo+ivec2(1,0),level,modes,recipe),f.x),
        mix(MaskCoverageTexel(ref,lo+ivec2(0,1),level,modes,recipe),MaskCoverageTexel(ref,lo+ivec2(1,1),level,modes,recipe),f.x),f.y);
}
float SampleMaterialMaskCoverage(int source,vec2 uv,vec2 footprint) {
    vec4 sampling=MaterialTextureInfo(source,1),filters=MaterialTextureInfo(source,2),recipe=MaterialTextureInfo(source,3);
    if(recipe.x<=0.0)return 1.0;
    if(recipe.x>1.0)return 0.0;
    vec2 transformed=TransformMaterialUV(source,uv);ivec2 modes=ivec2(sampling.yz);
    if((modes.x==3&&(transformed.x<0||transformed.x>1)) || (modes.y==3&&(transformed.y<0||transformed.y>1)))return 0.0;
    transformed=vec2(WrapTextureCoordinate(transformed.x,modes.x),WrapTextureCoordinate(transformed.y,modes.y));
    float rho=MaterialTextureRho(source,footprint);
    ivec2 ref=MaterialView(source,false),size=MaterialPoolSize(ref.x,0);
    int mode=int(filters.x),maxLevel=int(floor(log2(float(max(size.x,size.y)))));
    float lod=clamp(log2(max(rho,1.0)),0.0,float(maxLevel));
    if(mode==9728 || mode==9729)return MaskCoverageLevel(ref,transformed,0,mode==9729,modes,recipe);
    if(mode==9984 || mode==9985)return MaskCoverageLevel(ref,transformed,int(floor(lod+.5)),mode==9985,modes,recipe);
    int lo=int(floor(lod)),hi=min(lo+1,maxLevel);
    return mix(MaskCoverageLevel(ref,transformed,lo,mode!=9986,modes,recipe),
               MaskCoverageLevel(ref,transformed,hi,mode!=9986,modes,recipe),fract(lod));
}
vec4 SampleMaterialColorTexture(int textureIndex,vec2 uv) {
    return SampleMaterialTextureFootprint(textureIndex,uv,materialEvaluationFootprint,true);
}
vec4 SampleMaterialColorTexturePoint(int textureIndex,vec2 uv) {
    return SampleMaterialTextureFootprint(textureIndex,uv,vec2(0),true);
}
vec2 RasterTextureFootprint(vec2 uv) {
#ifdef COMPUTE_PATH
    return vec2(0);
#else
    vec2 dx=abs(dFdx(uv)),dy=abs(dFdy(uv));return max(dx,dy);
#endif
}
vec4 SampleMaterialTextureFiltered(int textureIndex,vec2 uv) {
    return SampleMaterialTextureFootprint(textureIndex,uv,RasterTextureFootprint(uv),false);
}
vec4 SampleMaterialColorTextureFiltered(int textureIndex,vec2 uv) {
    return SampleMaterialTextureFootprint(textureIndex,uv,RasterTextureFootprint(uv),true);
}

float TextureChannel(vec4 value, int channel)
{
    if (channel == 1) return value.g;
    if (channel == 2) return value.b;
    if (channel == 3) return value.a;
    return value.r;
}
