#include "scene_access.glsl"
#include "triangle_intersection.glsl"
#include "material_texture_sampling.glsl"
void GetTriangleUVs(int triangleIndex, out vec2 uv1, out vec2 uv2, out vec2 uv3)
{
    int offset = triangleIndex * SIZE_TRIANGLE;
#ifdef INSTANCED_SCENE
    int geometry = int(texelFetch(surfaceTable, triangleIndex).x) * 11;
    vec4 uv12 = texelFetch(triangles, geometry + 6);
    vec4 uv3Tex0 = texelFetch(triangles, geometry + 7);
#else
    vec4 uv12 = FetchTriangleVector(offset + 12);
    vec4 uv3Tex0 = FetchTriangleVector(offset + 13);
#endif
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

// Reconstruct locally, then transform once. The error bounds follow pbrt's
// gamma(7) barycentric interpolation and gamma(3) affine point transform.
void ReconstructSurfacePoint(int surface,vec3 bary,out vec3 point,out vec3 error)
{
#ifdef INSTANCED_SCENE
    uvec2 ref=texelFetch(surfaceTable,surface).xy;int base=int(ref.x)*11;
    vec3 a=texelFetch(triangles,base).xyz,b=texelFetch(triangles,base+1).xyz,
         c=texelFetch(triangles,base+2).xyz;
    mat4 world=InstanceMatrix(int(ref.y),0);
#else
    int base=surface*SIZE_TRIANGLE;
    vec3 a=FetchTriangleVector(base).xyz,b=FetchTriangleVector(base+1).xyz,
         c=FetchTriangleVector(base+2).xyz;
#endif
    vec3 local=bary.x*a+bary.y*b+bary.z*c;
    error=FloatGamma(7.0)*(abs(bary.x*a)+abs(bary.y*b)+abs(bary.z*c));
#ifdef INSTANCED_SCENE
    mat3 absoluteWorld=mat3(abs(world[0].xyz),abs(world[1].xyz),abs(world[2].xyz));
    error=(1.0+FloatGamma(3.0))*(absoluteWorld*error)+
        FloatGamma(3.0)*(absoluteWorld*abs(local)+abs(world[3].xyz));
    point=(world*vec4(local,1)).xyz;
#else
    point=local;
#endif
}

float GetTriangleLightSelectPdf(int triangleIndex)
{
    return FetchTriangleVector(triangleIndex * SIZE_TRIANGLE + 16).z;
}

float GetMaterialOpacity(int triangleIndex, vec2 uv)
{
    int offset = triangleIndex * SIZE_TRIANGLE;
    vec4 uv3Tex0 = FetchTriangleVector(offset + 13);
    vec4 tex1 = FetchTriangleVector(offset + 14);
    vec4 textureParam0 = FetchTriangleVector(offset + 15);
    int baseColorTex = int(uv3Tex0.z);
    int opacityTex = int(tex1.w);

    float opacity = textureParam0.x;
    if (baseColorTex >= 0) {
        opacity *= SampleMaterialTextureFootprint(baseColorTex,uv,vec2(0),false).a;
    }
    if (opacityTex >= 0) {
        opacity *= SampleMaterialTextureFootprint(opacityTex,uv,vec2(0),false).r;
    }
    return clamp(opacity, 0.0, 1.0);
}

#ifdef DENOISE_GUIDES
bool unstableAlpha = false;
#endif
bool RejectAlphaIntersection(int triangleIndex, vec2 uv)
{
    int offset = triangleIndex * SIZE_TRIANGLE;
    vec4 param4 = FetchTriangleVector(offset + 9);
    int alphaMode = int(param4.w);
    if (alphaMode != ALPHA_MODE_MASK && alphaMode != ALPHA_MODE_BLEND) {
        return false;
    }

    float opacity = GetMaterialOpacity(triangleIndex, uv);
    if (alphaMode == ALPHA_MODE_MASK) {
        int base=int(FetchTriangleVector(offset+13).z),opacitySource=int(FetchTriangleVector(offset+14).w);
        int coverageSource=HasMaskCoverage(base)?base:HasMaskCoverage(opacitySource)?opacitySource:-1;
#ifdef PICKING_PASS
        bool coverageAllowed=false;
#elif defined(INSTANCED_SCENE)
        bool coverageAllowed=!picking;
#else
        bool coverageAllowed=true;
#endif
        if(coverageAllowed && coverageSource>=0 && (MaterialTextureRho(coverageSource,materialEvaluationFootprint)>1.0 ||
            MaterialTextureInfo(coverageSource,3).w>.5)) {
            float coverage=clamp(SampleMaterialMaskCoverage(coverageSource,uv,materialEvaluationFootprint),0.0,1.0);
#ifdef DENOISE_GUIDES
            unstableAlpha=unstableAlpha || (coverage>0.0 && coverage<1.0);
#endif
            return SampleAlpha(triangleIndex)>=coverage;
        }
        float alphaCutoff = FetchTriangleVector(offset + 15).y;
        return opacity < alphaCutoff;
    }
#ifdef DENOISE_GUIDES
    unstableAlpha = true;
#endif
    #ifdef PICKING_PASS
    return opacity < 0.5;
    #elif defined(INSTANCED_SCENE)
    if(picking) return opacity < 0.5;
    #endif
    return SampleAlpha(triangleIndex) >= opacity;
}

// Keep the original one-argument material loader shape for compatibility with
// NVIDIA's GLSL 330 compiler. Callers set this immediately before evaluation.
vec2 materialEvaluationUV;
Material getMaterial(int i) {
    Material m;
    m.tangent=vec4(0);
#ifdef INSTANCED_SCENE
    int materialOffset = int(texelFetch(instanceTable, int(texelFetch(surfaceTable, i).y) * 9 + 8).x) * 10;
    vec4 param1 = texelFetch(materialTable, materialOffset + 0);
    vec4 param2 = texelFetch(materialTable, materialOffset + 1);
    vec4 param3 = texelFetch(materialTable, materialOffset + 2);
    vec4 param4 = texelFetch(materialTable, materialOffset + 3);
    vec4 param5 = texelFetch(materialTable, materialOffset + 4);
    vec4 param6 = texelFetch(materialTable, materialOffset + 5);
    vec4 uv3Tex0 = texelFetch(materialTable, materialOffset + 6);
    vec4 tex1 = texelFetch(materialTable, materialOffset + 7);
    vec4 textureParam0 = texelFetch(materialTable, materialOffset + 8);
    vec4 textureParam1 = texelFetch(materialTable, materialOffset + 9);
#else
    int offset = i * SIZE_TRIANGLE;
    vec4 param1 = FetchTriangleVector(offset + 6);
    vec4 param2 = FetchTriangleVector(offset + 7);
    vec4 param3 = FetchTriangleVector(offset + 8);
    vec4 param4 = FetchTriangleVector(offset + 9);
    vec4 param5 = FetchTriangleVector(offset + 10);
    vec4 param6 = FetchTriangleVector(offset + 11);
    vec4 uv3Tex0 = FetchTriangleVector(offset + 13);
    vec4 tex1 = FetchTriangleVector(offset + 14);
    vec4 textureParam0 = FetchTriangleVector(offset + 15);
    vec4 textureParam1 = FetchTriangleVector(offset + 16);

#endif
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

    vec4 baseColorSample = SampleMaterialColorTexture(baseColorTex, materialEvaluationUV);
    if (baseColorTex >= 0) {
        m.baseColor *= baseColorSample.rgb;
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
        // Keep point-emission semantics identical for NEE and BSDF-hit MIS.
        m.emissive *= SampleMaterialColorTexturePoint(emissiveTex, materialEvaluationUV).rgb;
    }
    m.opacity = GetMaterialOpacity(i, materialEvaluationUV);
    m.metallic = clamp(m.metallic, 0.0, 1.0);
    m.roughness = clamp(m.roughness, 0.0, 1.0);

    float aspect = sqrt(1.0 - m.anisotropic * 0.9);
    m.ax = max(0.001, m.roughness / aspect);
    m.ay = max(0.001, m.roughness * aspect);
    return m;
}

void SetTriangleFootprint(int surface,Ray ray,float distance,vec3 geometryNormal)
{
    materialEvaluationFootprint=vec2(0);
    float diameter=rayConeWidth+distance*rayConeSpread;
    if(diameter<=0.0)return;
    vec2 a,b,c;GetTriangleUVs(surface,a,b,c);
    vec3 e1,e2;
#ifdef INSTANCED_SCENE
    uvec2 ref=texelFetch(surfaceTable,surface).xy;int base=int(ref.x)*11;
    mat3 world=mat3(InstanceMatrix(int(ref.y),0));
    vec3 p=texelFetch(triangles,base).xyz;
    e1=world*(texelFetch(triangles,base+1).xyz-p);
    e2=world*(texelFetch(triangles,base+2).xyz-p);
#else
    int base=surface*SIZE_TRIANGLE;
    e1=FetchTriangleVector(base+1).xyz-FetchTriangleVector(base).xyz;
    e2=FetchTriangleVector(base+2).xyz-FetchTriangleVector(base).xyz;
#endif
    vec2 u=b-a,v=c-a;float determinant=u.x*v.y-u.y*v.x;
    if(abs(determinant)<1e-12)return;
    vec3 dpdu=(e1*v.y-e2*u.y)/determinant,dpdv=(e2*u.x-e1*v.x)/determinant;
    float area=length(cross(dpdu,dpdv));
    if(area<=1e-20)return;
    float grazing=max(abs(dot(geometryNormal,ray.direction)),1e-4);
    materialEvaluationFootprint=diameter/grazing*vec2(length(dpdv),length(dpdu))/area;
    if(any(isnan(materialEvaluationFootprint))||any(isinf(materialEvaluationFootprint))) {
        pathDiagnosticFlags|=DIAG_NONFINITE;materialEvaluationFootprint=vec2(0);
    }
}
vec4 BsdfTangent(int triangleIndex,vec3 bary,vec3 normal)
{
    int offset=triangleIndex*SIZE_TRIANGLE;
    vec4 t=bary.x*FetchTriangleVector(offset+17)+bary.y*FetchTriangleVector(offset+18)+
        bary.z*FetchTriangleVector(offset+19);
    vec3 projected=t.xyz-normal*dot(normal,t.xyz);
    if(dot(projected,projected)>1e-12 && !any(isnan(projected)) && !any(isinf(projected)))
        return vec4(normalize(projected),t.w<0.0?-1.0:1.0);
    vec2 a,b,c;GetTriangleUVs(triangleIndex,a,b,c);
    vec3 e1=FetchTriangleVector(offset+1).xyz-FetchTriangleVector(offset).xyz;
    vec3 e2=FetchTriangleVector(offset+2).xyz-FetchTriangleVector(offset).xyz;
    vec2 u=b-a,v=c-a;float d=u.x*v.y-u.y*v.x;
    if(abs(d)>1e-12) {
        projected=(e1*v.y-e2*u.y)/d;
        projected-=normal*dot(normal,projected);
        if(dot(projected,projected)>1e-12)
            return vec4(normalize(projected),d<0.0?-1.0:1.0);
    }
    return vec4(0);
}
vec3 ApplyNormalMap(int triangleIndex, vec2 uv, vec3 bary, vec3 surfaceNormal, inout Material material)
{
    if (material.normalTex < 0 || material.normalTex >= materialTextureCount) {
        return surfaceNormal;
    }

    int offset = triangleIndex * SIZE_TRIANGLE;
    vec3 p1 = FetchTriangleVector(offset + 0).xyz;
    vec3 p2 = FetchTriangleVector(offset + 1).xyz;
    vec3 p3 = FetchTriangleVector(offset + 2).xyz;
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

    vec4 tangent1 = FetchTriangleVector(offset + 17);
    vec4 tangent2 = FetchTriangleVector(offset + 18);
    vec4 tangent3 = FetchTriangleVector(offset + 19);
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
    if(MaterialTextureRho(material.normalTex,materialEvaluationFootprint)>1.0 ||
       (materialTextureInfoStride>=4 && MaterialTextureInfo(material.normalTex,3).w>.5)) {
        float retainedLength=min(1.0,length(tangentNormal));
        float variance=max(0.0,1.0-retainedLength)/max(retainedLength,.1);
        material.roughness=sqrt(min(1.0,material.roughness*material.roughness+variance));
        float aspect=sqrt(1.0-material.anisotropic*.9);
        material.ax=max(.001,material.roughness/aspect);material.ay=max(.001,material.roughness*aspect);
    }
    tangentNormal.xy *= material.normalScale;
    if (material.normalMapFlipY > 0.5) {
        tangentNormal.y = -tangentNormal.y;
    }
    if(dot(tangentNormal,tangentNormal)<1e-12 || any(isnan(tangentNormal)) || any(isinf(tangentNormal)))
        return surfaceNormal;
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
#ifdef INSTANCED_SCENE
#include "bvh_instances.glsl"
#else
HitResult hitBVH(Ray ray) {
    BeginAlphaQuery();
    HitResult res;
    res.isHit = false;
    res.triangleIndex = -1;
    res.hitDistance = INF;
    if (!FiniteRay(ray.startPoint,ray.direction)) return res;
    if (nTriangles <= 0 || nNodes <= 1) return res;
    TriangleRay triangleRay = PrepareTriangleRay(ray);
    vec3 bary;
    int triID = -1;
    vec3 vert1;
    vec3 vert2;
    vec3 vert3;

    // 栈
    #ifndef BVH_STACK_CAPACITY
    #define BVH_STACK_CAPACITY 64
    #endif
    int stack[BVH_STACK_CAPACITY];
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
                vec3 p1 = FetchTriangleVector(offset + 0).xyz;
                vec3 p2 = FetchTriangleVector(offset + 1).xyz;
                vec3 p3 = FetchTriangleVector(offset + 2).xyz;

                vec3 candidateBary;
                float hitDistance;
                if (IntersectTriangle(triangleRay, p1, p2, p3, candidateBary, hitDistance) &&
                    hitDistance < res.hitDistance)
                {
                    vec2 candidateUV = InterpolateTriangleUV(i, candidateBary);
                    if(materialTextureInfoStride>=4 && int(FetchTriangleVector(i*SIZE_TRIANGLE+9).w)==ALPHA_MODE_MASK)
                        SetTriangleFootprint(i,ray,hitDistance,normalize(cross(p2-p1,p3-p1)));
                    if (RejectAlphaIntersection(i, candidateUV)) {
                        continue;
                    }
                    res.isHit = true;
                    res.hitPoint = p1 + candidateBary.y * (p2 - p1) + candidateBary.z * (p3 - p1);
                    res.hitDistance = hitDistance;
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
        int required=int(d1>0)+int(d2>0);
        if(sp+required>BVH_STACK_CAPACITY) {
            pathDiagnosticFlags|=DIAG_BVH_OVERFLOW;continue;
        }
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
        ReconstructSurfacePoint(triID,bary,res.hitPoint,res.positionError);
        // 法线
        vec3 n1 = FetchTriangleVector(offset + 3).xyz;
        vec3 n2 = FetchTriangleVector(offset + 4).xyz;
        vec3 n3 = FetchTriangleVector(offset + 5).xyz;

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

        vec3 facingGeometry = res.isInside ? -res.geometricNormal : res.geometricNormal;
        res.normal = ValidShadingNormal(res.normal, facingGeometry, ray.direction);
        materialEvaluationUV = res.uv;
        SetTriangleFootprint(triID,ray,res.hitDistance,res.geometricNormal);
        res.material = getMaterial(triID);
        res.normal = ApplyNormalMap(triID, res.uv, bary, res.normal, res.material);
        res.normal = ValidShadingNormal(res.normal, facingGeometry, ray.direction);
        res.triangleIndex = triID;
        if(res.material.anisotropic>0.0)res.material.tangent=BsdfTangent(triID,bary,res.normal);
    }
    return res;
}

#endif
