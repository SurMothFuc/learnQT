// 材质贴图的 UV 变换、寻址与采样。路径追踪与光栅化交互预览共用，
// 只依赖材质贴图数组与其描述缓冲，不依赖任何场景访问函数。

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

    vec4 transform = texelFetch(materialTextureInfo, textureIndex * 3);
    float rotationAngle = texelFetch(materialTextureInfo, textureIndex * 3 + 1).x;
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

vec4 SampleMaterialTexture(int textureIndex, vec2 uv)
{
    if (textureIndex < 0 || textureIndex >= materialTextureCount) {
        return vec4(1.0);
    }

    vec2 transformed = TransformMaterialUV(textureIndex, uv);
    vec4 sampling = texelFetch(materialTextureInfo, textureIndex * 3 + 1);
    int wrapS = int(sampling.y);
    int wrapT = int(sampling.z);
    if ((wrapS == 3 && (transformed.x < 0.0 || transformed.x > 1.0)) ||
        (wrapT == 3 && (transformed.y < 0.0 || transformed.y > 1.0))) {
        return vec4(0.0);
    }
    transformed.x = WrapTextureCoordinate(transformed.x, wrapS);
    transformed.y = WrapTextureCoordinate(transformed.y, wrapT);
    int magFilter = int(texelFetch(materialTextureInfo, textureIndex * 3 + 2).y);
    if (magFilter == 9728) {
        ivec2 dimensions = textureSize(materialTextures, 0).xy;
        ivec2 pixel = clamp(ivec2(floor(transformed * vec2(dimensions))), ivec2(0), dimensions - ivec2(1));
        return texelFetch(materialTextures, ivec3(pixel, textureIndex), 0);
    }
    // Ray hits do not have useful screen-space derivatives; use an explicit base LOD.
    return textureLod(materialTextures, vec3(transformed, float(textureIndex)), 0.0);
}

// 光栅化时片元有真实屏幕导数，可以用完整的 mip 链；采样语义与上面的基础 LOD 一致。
vec4 SampleMaterialTextureFiltered(int textureIndex, vec2 uv)
{
    if (textureIndex < 0 || textureIndex >= materialTextureCount) {
        return vec4(1.0);
    }

    vec2 transformed = TransformMaterialUV(textureIndex, uv);
    vec4 sampling = texelFetch(materialTextureInfo, textureIndex * 3 + 1);
    int wrapS = int(sampling.y);
    int wrapT = int(sampling.z);
    if ((wrapS == 3 && (transformed.x < 0.0 || transformed.x > 1.0)) ||
        (wrapT == 3 && (transformed.y < 0.0 || transformed.y > 1.0))) {
        return vec4(0.0);
    }
    transformed.x = WrapTextureCoordinate(transformed.x, wrapS);
    transformed.y = WrapTextureCoordinate(transformed.y, wrapT);
    int magFilter = int(texelFetch(materialTextureInfo, textureIndex * 3 + 2).y);
    if (magFilter == 9728) {
        ivec2 dimensions = textureSize(materialTextures, 0).xy;
        ivec2 pixel = clamp(ivec2(floor(transformed * vec2(dimensions))), ivec2(0), dimensions - ivec2(1));
        return texelFetch(materialTextures, ivec3(pixel, textureIndex), 0);
    }
    return texture(materialTextures, vec3(transformed, float(textureIndex)));
}

float TextureChannel(vec4 value, int channel)
{
    if (channel == 1) return value.g;
    if (channel == 2) return value.b;
    if (channel == 3) return value.a;
    return value.r;
}
