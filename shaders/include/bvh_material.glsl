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

float TextureChannel(vec4 value, int channel)
{
    if (channel == 1) return value.g;
    if (channel == 2) return value.b;
    if (channel == 3) return value.a;
    return value.r;
}

void GetTriangleUVs(int triangleIndex, out vec2 uv1, out vec2 uv2, out vec2 uv3)
{
    int offset = triangleIndex * SIZE_TRIANGLE;
    vec4 uv12 = texelFetch(triangles, offset + 12);
    vec4 uv3Tex0 = texelFetch(triangles, offset + 13);
    uv1 = uv12.xy;
    uv2 = uv12.zw;
    uv3 = uv3Tex0.xy;
}

vec2 InterpolateTriangleUV(int triangleIndex, vec3 bary)
{
    vec2 uv1, uv2, uv3;
    GetTriangleUVs(triangleIndex, uv1, uv2, uv3);
    return bary.x * uv1 + bary.y * uv2 + bary.z * uv3;
}

float GetTriangleLightSelectPdf(int triangleIndex)
{
    return texelFetch(triangles, triangleIndex * SIZE_TRIANGLE + 16).z;
}

float GetMaterialOpacity(int triangleIndex, vec2 uv)
{
    int offset = triangleIndex * SIZE_TRIANGLE;
    vec4 uv3Tex0 = texelFetch(triangles, offset + 13);
    vec4 tex1 = texelFetch(triangles, offset + 14);
    vec4 textureParam0 = texelFetch(triangles, offset + 15);
    int baseColorTex = int(uv3Tex0.z);
    int opacityTex = int(tex1.w);

    float opacity = textureParam0.x;
    if (baseColorTex >= 0) {
        opacity *= SampleMaterialTexture(baseColorTex, uv).a;
    }
    if (opacityTex >= 0) {
        opacity *= SampleMaterialTexture(opacityTex, uv).r;
    }
    return clamp(opacity, 0.0, 1.0);
}

bool RejectAlphaIntersection(int triangleIndex, vec2 uv)
{
    int offset = triangleIndex * SIZE_TRIANGLE;
    vec4 param4 = texelFetch(triangles, offset + 9);
    int alphaMode = int(param4.w);
    if (alphaMode != ALPHA_MODE_MASK && alphaMode != ALPHA_MODE_BLEND) {
        return false;
    }

    float opacity = GetMaterialOpacity(triangleIndex, uv);
    if (alphaMode == ALPHA_MODE_MASK) {
        float alphaCutoff = texelFetch(triangles, offset + 15).y;
        return opacity < alphaCutoff;
    }
    return rand() >= opacity;
}

// Keep the original one-argument material loader shape for compatibility with
// NVIDIA's GLSL 330 compiler. Callers set this immediately before evaluation.
vec2 materialEvaluationUV;
Material getMaterial(int i) {
    Material m;

    int offset = i * SIZE_TRIANGLE;
    vec4 param1 = texelFetch(triangles, offset + 6);
    vec4 param2 = texelFetch(triangles, offset + 7);
    vec4 param3 = texelFetch(triangles, offset + 8);
    vec4 param4 = texelFetch(triangles, offset + 9);
    vec4 param5 = texelFetch(triangles, offset + 10);
    vec4 param6 = texelFetch(triangles, offset + 11);
    vec4 uv3Tex0 = texelFetch(triangles, offset + 13);
    vec4 tex1 = texelFetch(triangles, offset + 14);
    vec4 textureParam0 = texelFetch(triangles, offset + 15);
    vec4 textureParam1 = texelFetch(triangles, offset + 16);
    
    m.emissive = param1.xyz;
    m.sheenTint= param1.w;

    m.baseColor = param2.xyz;
    m.clearcoat = param2.w;

    m.mediumColor=param3.xyz;
    m.mediumAnisotropy=clamp(param3.w,-0.9, 0.9);

    m.clearcoatGloss=mix(0.1, 0.001,param4.x);
    m.IOR=param4.y;
    m.transmission=param4.z;
    m.alphaMode=int(param4.w);
    m.opacity=textureParam0.x;
    m.alphaCutoff=textureParam0.y;

    m.mediumtype=int(param5.x);
    m.mediumDensity=param5.y;
    m.subsurface=param5.z;
    m.metallic=param5.w;

    m.specularTint=param6.x;
    m.roughness=max(param6.y,0.0);
    m.anisotropic=param6.z;
    m.sheen=param6.w;

    int baseColorTex=int(uv3Tex0.z);
    m.normalTex=int(uv3Tex0.w);
    int metallicTex=int(tex1.x);
    int roughnessTex=int(tex1.y);
    int emissiveTex=int(tex1.z);
    m.normalScale=textureParam0.z;
    m.normalMapFlipY=textureParam0.w;
    int metallicChannel=int(textureParam1.x);
    int roughnessChannel=int(textureParam1.y);

    vec4 baseColorSample = SampleMaterialTexture(baseColorTex, materialEvaluationUV);
    if (baseColorTex >= 0) {
        m.baseColor *= SrgbToLinear(baseColorSample.rgb);
    }
    if (metallicTex >= 0) {
        m.metallic *= TextureChannel(
            SampleMaterialTexture(metallicTex, materialEvaluationUV),
            metallicChannel);
    }
    if (roughnessTex >= 0) {
        m.roughness *= TextureChannel(
            SampleMaterialTexture(roughnessTex, materialEvaluationUV),
            roughnessChannel);
    }
    if (emissiveTex >= 0) {
        m.emissive *= SrgbToLinear(SampleMaterialTexture(emissiveTex, materialEvaluationUV).rgb);
    }
    m.opacity = GetMaterialOpacity(i, materialEvaluationUV);
    m.metallic = clamp(m.metallic, 0.0, 1.0);
    m.roughness = clamp(m.roughness, 0.0, 1.0);

    float aspect = sqrt(1.0 - m.anisotropic * 0.9);
    m.ax = max(0.001, m.roughness / aspect);
    m.ay = max(0.001, m.roughness * aspect);
    return m;
}

vec3 ApplyNormalMap(int triangleIndex, vec2 uv, vec3 bary, vec3 surfaceNormal, Material material)
{
    if (material.normalTex < 0 || material.normalTex >= materialTextureCount) {
        return surfaceNormal;
    }

    int offset = triangleIndex * SIZE_TRIANGLE;
    vec3 p1 = texelFetch(triangles, offset + 0).xyz;
    vec3 p2 = texelFetch(triangles, offset + 1).xyz;
    vec3 p3 = texelFetch(triangles, offset + 2).xyz;
    vec2 uv1, uv2, uv3;
    GetTriangleUVs(triangleIndex, uv1, uv2, uv3);
    uv1 = TransformMaterialUV(material.normalTex, uv1);
    uv2 = TransformMaterialUV(material.normalTex, uv2);
    uv3 = TransformMaterialUV(material.normalTex, uv3);

    vec3 dp1 = p2 - p1;
    vec3 dp2 = p3 - p1;
    vec2 duv1 = uv2 - uv1;
    vec2 duv2 = uv3 - uv1;
    float determinant = duv1.x * duv2.y - duv1.y * duv2.x;

    vec4 tangent1 = texelFetch(triangles, offset + 17);
    vec4 tangent2 = texelFetch(triangles, offset + 18);
    vec4 tangent3 = texelFetch(triangles, offset + 19);
    vec4 importedTangent = bary.x * tangent1 + bary.y * tangent2 + bary.z * tangent3;

    vec3 tangent;
    vec3 bitangent;
    if (length(importedTangent.xyz) > EPS) {
        tangent = normalize(importedTangent.xyz - surfaceNormal * dot(surfaceNormal, importedTangent.xyz));
        float handedness = importedTangent.w < 0.0 ? -1.0 : 1.0;
        bitangent = normalize(cross(surfaceNormal, tangent)) * handedness;
    }
    else if (abs(determinant) <= EPS) {
        Onb(surfaceNormal, tangent, bitangent);
    }
    else {
        tangent = normalize((dp1 * duv2.y - dp2 * duv1.y) / determinant);
        tangent = normalize(tangent - surfaceNormal * dot(surfaceNormal, tangent));
        float handedness = determinant < 0.0 ? -1.0 : 1.0;
        bitangent = normalize(cross(surfaceNormal, tangent)) * handedness;
    }

    vec3 tangentNormal = SampleMaterialTexture(material.normalTex, uv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalScale;
    if (material.normalMapFlipY > 0.5) {
        tangentNormal.y = -tangentNormal.y;
    }
    tangentNormal = normalize(tangentNormal);
    vec3 mappedNormal = normalize(
        tangent * tangentNormal.x
        + bitangent * tangentNormal.y
        + surfaceNormal * tangentNormal.z);
    return dot(mappedNormal, surfaceNormal) < 0.0 ? -mappedNormal : mappedNormal;
}
// 获取第 i 下标的 BVHNode 对象
BVHNode getBVHNode(int i) {
    BVHNode node;

    // 左右子树
    int offset = i * SIZE_BVHNODE;
    ivec3 childs = ivec3(texelFetch(nodes, offset + 0).xyz);
    ivec3 leafInfo = ivec3(texelFetch(nodes, offset + 1).xyz);
    node.left = int(childs.x);
    node.right = int(childs.y);
    node.n = int(leafInfo.x);
    node.index = int(leafInfo.y);

    // 包围盒
    node.AA = texelFetch(nodes, offset + 2).xyz;
    node.BB = texelFetch(nodes, offset + 3).xyz;

    return node;
}

// 和 aabb 盒子求交，没有交点则返回 -1
float hitAABB(Ray r, vec3 AA, vec3 BB) {
    vec3 invdir = 1.0 / r.direction;

    vec3 f = (BB - r.startPoint) * invdir;
    vec3 n = (AA - r.startPoint) * invdir;

    vec3 tmax = max(f, n);
    vec3 tmin = min(f, n);

    float t1 = min(tmax.x, min(tmax.y, tmax.z));
    float t0 = max(tmin.x, max(tmin.y, tmin.z));

    return (t1 >= t0) ? ((t0 > 0.0) ? (t0) : (t1)) : (-1);
}
 
 // 遍历 BVH 求交
HitResult hitBVH(Ray ray) {
    HitResult res;
    res.isHit = false;
    res.triangleIndex = -1;
    res.hitDistance = INF;
    if (nTriangles <= 0 || nNodes <= 1) return res;
    vec3 bary;
    int triID = -1;
    vec3 vert1;
    vec3 vert2;
    vec3 vert3;

    // 栈
    int stack[64];
    int sp = 0;

    stack[sp++] = 1;
    while(sp>0) {
        int top = stack[--sp];
        BVHNode node = getBVHNode(top);
        
        // 是叶子节点，遍历三角形，求最近交点
        if(node.n>0) {
            int L = node.index;
            int R = node.index + node.n - 1;
            for(int i=L; i<=R; i++) {
                int offset = i * SIZE_TRIANGLE;
                
                // 顶点坐标
                vec3 p1 = texelFetch(triangles, offset + 0).xyz;
                vec3 p2 = texelFetch(triangles, offset + 1).xyz;
                vec3 p3 = texelFetch(triangles, offset + 2).xyz;

                vec3 e0 = p2.xyz - p1.xyz;
                vec3 e1 = p3.xyz - p1.xyz;
                vec3 pv = cross(ray.direction, e1);
                float det = dot(e0, pv);

                if (abs(det) < 1e-12) continue;

                vec3 tv = ray.startPoint - p1.xyz;
                vec3 qv = cross(tv, e0);

                vec4 uvt;
                uvt.x = dot(tv, pv);
                uvt.y = dot(ray.direction, qv);
                uvt.z = dot(e1, qv);
                uvt.xyz = uvt.xyz / det;
                uvt.w = 1.0 - uvt.x - uvt.y;
                
                if(uvt.z<=0.0)
                    continue;

                if (all(greaterThanEqual(uvt, vec4(0.0))) && uvt.z < res.hitDistance)
                {
                    vec3 candidateBary = uvt.wxy;
                    vec2 candidateUV = InterpolateTriangleUV(i, candidateBary);
                    if (RejectAlphaIntersection(i, candidateUV)) {
                        continue;
                    }
                    res.isHit = true;
                    res.hitPoint = ray.startPoint + ray.direction * uvt.z;
                    res.hitDistance = uvt.z;
                    res.viewDir = ray.direction;
                    bary = candidateBary;
                    res.uv = candidateUV;
                    triID=i;
                    vert1=p1,vert2=p2,vert3=p3;

                    
                }
            }
            continue;
        }
        
        // 和左右盒子 AABB 求交
        float d1 = INF; // 左盒子距离
        float d2 = INF; // 右盒子距离
        vec3 invdir = 1.0 / ray.direction;
        if(node.left>0) {
            BVHNode leftNode = getBVHNode(node.left);

            vec3 f = (leftNode.BB - ray.startPoint) * invdir;
            vec3 n = (leftNode.AA - ray.startPoint) * invdir;

            vec3 tmax = max(f, n);
            vec3 tmin = min(f, n);

            float t1 = min(tmax.x, min(tmax.y, tmax.z));
            float t0 = max(tmin.x, max(tmin.y, tmin.z));

            d1= (t1 >= t0) ? ((t0 > 0.0) ? ( t0<res.hitDistance?(t0):0.0 ) : (t1)) : (-1);
        }
        if(node.right>0) {
            BVHNode rightNode = getBVHNode(node.right);

            vec3 f = ( rightNode.BB - ray.startPoint) * invdir;
            vec3 n = ( rightNode.AA - ray.startPoint) * invdir;

            vec3 tmax = max(f, n);
            vec3 tmin = min(f, n);

            float t1 = min(tmax.x, min(tmax.y, tmax.z));
            float t0 = max(tmin.x, max(tmin.y, tmin.z));

            d2= (t1 >= t0) ? ((t0 > 0.0) ? (t0<res.hitDistance?(t0):0.0) : (t1)) : (-1);
        }

        // 在最近的盒子中搜索
        if(d1>0 && d2>0) {
            if(d1<d2) { // d1<d2, 左边先
                stack[sp++] = node.right;
                stack[sp++] = node.left;
            } else {    // d2<d1, 右边先
                stack[sp++] = node.left;
                stack[sp++] = node.right;
            }
        } else if(d1>0) {   // 仅命中左边
            stack[sp++] = node.left;
        } else if(d2>0) {   // 仅命中右边
            stack[sp++] = node.right;
        }
    }
    if(res.isHit){
        // 根据交点位置插值顶点法线 
        
        int offset = triID * SIZE_TRIANGLE;
        // 法线
        vec3 n1 = texelFetch(triangles, offset + 3).xyz;
        vec3 n2 = texelFetch(triangles, offset + 4).xyz;
        vec3 n3 = texelFetch(triangles, offset + 5).xyz;

        vec3 Nsmooth =bary.x * n1 +bary.y * n2 + bary.z * n3;
        if (length(Nsmooth) < EPS) {
            Nsmooth = normalize(cross(vert2-vert1, vert3-vert1)); // 防止接近零向量导致溢出
        }else{
            Nsmooth = normalize(Nsmooth);
        }
        // 从三角形背后（模型内部）击中
        res.geometricNormal = normalize(cross(vert2-vert1, vert3-vert1));
        if (dot(Nsmooth, res.geometricNormal) < 0.0) Nsmooth = -Nsmooth;
        if (dot(res.geometricNormal, ray.direction) > 0.0) {
            res.isInside = true;
            res.normal =-Nsmooth;
        }else{
            res.isInside=false;
            res.normal=Nsmooth;
        }

        materialEvaluationUV = res.uv;
        res.material = getMaterial(triID);
        res.normal = ApplyNormalMap(triID, res.uv, bary, res.normal, res.material);
        res.triangleIndex = triID;
    }
    return res;
}
