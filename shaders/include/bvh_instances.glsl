#ifndef BVH_STACK_CAPACITY
#define BVH_STACK_CAPACITY 64
#endif
#ifdef COMPUTE_PATH
int stack[BVH_STACK_CAPACITY];
float topDistances[BVH_STACK_CAPACITY];
int bs[BVH_STACK_CAPACITY];
float blasDistances[BVH_STACK_CAPACITY];
#endif
Material ShadowMaterial(int surface)
{
    Material m;
    m.tangent=vec4(0);
    m.emissive = vec3(0);
    m.baseColor = vec3(0);
    m.subsurface = 0;
    m.metallic = 0;
    m.specularTint = 0;
    m.roughness = 0;
    m.anisotropic = 0;
    m.sheen = 0;
    m.sheenTint = 0;
    m.clearcoat = 0;
    m.clearcoatGloss = 0;
    m.IOR = 0;
    m.transmission = 0;
    m.alphaMode = 0;
    m.opacity = 0;
    m.alphaCutoff = 0;
    m.mediumtype = 0;
    m.mediumDensity = 0;
    m.mediumColor = vec3(0);
    m.mediumAnisotropy = 0;
    m.ax = 0;
    m.ay = 0;
    m.normalTex = 0;
    m.normalScale = 0;
    m.normalMapFlipY = 0;
    int instance = int(texelFetch(surfaceTable, surface).y);
    int address = int(texelFetch(instanceTable, instance * 9 + 8).x) * 10;
    vec4 medium = texelFetch(materialTable, address + 2), params = texelFetch(materialTable, address + 4);
    vec4 surfaceParams=texelFetch(materialTable,address+3);
    m.alphaMode=int(surfaceParams.w);m.IOR=surfaceParams.y;m.transmission=surfaceParams.z;
    m.mediumColor = medium.xyz;
    m.mediumAnisotropy = clamp(medium.w, -.9, .9);
    m.mediumtype = int(params.x);
    m.mediumDensity = params.y;
    return m;
}
BVHNode TopNode(int index)
{
    BVHNode n;
    int i = index * 4;
    vec3 a = texelFetch(topNodes, i).xyz, b = texelFetch(topNodes, i + 1).xyz;
    n.left = int(a.x);
    n.right = int(a.y);
    n.n = int(b.x);
    n.index = int(b.y);
    n.AA = texelFetch(topNodes, i + 2).xyz;
    n.BB = texelFetch(topNodes, i + 3).xyz;
    return n;
}
float BoundsDistance(Ray r, vec3 reciprocal, vec3 lo, vec3 hi, float limit)
{
    bvec3 parallel = lessThan(abs(r.direction), vec3(1e-30));
    if (any(parallel))
    {
        if (any(bvec3(parallel.x && (r.startPoint.x < lo.x || r.startPoint.x > hi.x),
                      parallel.y && (r.startPoint.y < lo.y || r.startPoint.y > hi.y),
                      parallel.z && (r.startPoint.z < lo.z || r.startPoint.z > hi.z))))
            return -1.0;
    }
    vec3 a = (lo - r.startPoint) * mix(reciprocal, vec3(0), parallel);
    vec3 b = (hi - r.startPoint) * mix(reciprocal, vec3(0), parallel);
    vec3 nearV = mix(min(a, b), vec3(-1e30), parallel), farV = mix(max(a, b), vec3(1e30), parallel);
    float nearD = max(0.0, max(nearV.x, max(nearV.y, nearV.z)));
    float farD = min(limit, min(farV.x, min(farV.y, farV.z)));
    return farD >= nearD && farD > 0 ? nearD : -1.0;
}
bool BoundsHit(Ray r, vec3 reciprocal, vec3 lo, vec3 hi, float limit)
{
    return BoundsDistance(r, reciprocal, lo, hi, limit) >= 0;
}
HitResult hitBVH(Ray ray, bool shadowOnly,float distanceLimit,bool anyHit,int excludedSurface)
{
    BeginAlphaQuery();
    HitResult res;
    res.isHit = false;
    res.isInside = false;
    res.triangleIndex = -1;
    res.hitDistance = distanceLimit;
    // NaN rays can make the slab min/max operations accept every node, turning
    // a numerical error into an exhaustive traversal of a large scene.
    if (!FiniteRay(ray.startPoint,ray.direction))
        return res;
    if (nTopNodes <= 1)
        return res;
    vec3 worldReciprocal = 1.0 / ray.direction;
    #ifndef COMPUTE_PATH
    int stack[BVH_STACK_CAPACITY];
#endif
    #ifndef COMPUTE_PATH
    float topDistances[BVH_STACK_CAPACITY];
#endif
    int sp = 0;
    BVHNode topRoot = TopNode(1);
    float topEntry = BoundsDistance(ray, worldReciprocal, topRoot.AA, topRoot.BB, distanceLimit);
    if (topEntry < 0)
        return res;
    topDistances[sp] = topEntry;
    stack[sp++] = 1;
    vec3 bary = vec3(0);
    while (sp > 0)
    {
        int nodeIndex = stack[--sp];
#ifdef TRACE_TRAVERSAL_PROFILE
        ++traversalNodeVisits;
#endif
        if (topDistances[sp] > res.hitDistance)
            continue;
        BVHNode top = TopNode(nodeIndex);
        if (top.n == 0)
        {
            BVHNode left = TopNode(top.left), right = TopNode(top.right);
            float dl = BoundsDistance(ray, worldReciprocal, left.AA, left.BB, res.hitDistance);
            float dr = BoundsDistance(ray, worldReciprocal, right.AA, right.BB, res.hitDistance);
            if (sp < BVH_STACK_CAPACITY - 2)
            {
                if (dl >= 0 && dr >= 0)
                {
                    if (dl < dr)
                    {
                        {
                            topDistances[sp] = dr;
                            stack[sp++] = top.right;
                        }
                        {
                            topDistances[sp] = dl;
                            stack[sp++] = top.left;
                        }
                    }
                    else
                    {
                        {
                            topDistances[sp] = dl;
                            stack[sp++] = top.left;
                        }
                        {
                            topDistances[sp] = dr;
                            stack[sp++] = top.right;
                        }
                    }
                }
                else if (dl >= 0)
                {
                    topDistances[sp] = dl;
                    stack[sp++] = top.left;
                }
                else if (dr >= 0)
                {
                    topDistances[sp] = dr;
                    stack[sp++] = top.right;
                }
            }
            else RaisePathDiagnostic(DIAG_BVH_OVERFLOW);
            continue;
        }
        int instance = top.index;
        vec4 info = texelFetch(instanceTable, instance * 9 + 8);
        if (info.w < .5)
            continue;
        int instanceAlphaMode = int(texelFetch(materialTable, int(info.x) * 10 + 3).w);
        mat4 inverse = InstanceMatrix(instance, 4);
        Ray local;
        local.startPoint = (inverse * vec4(ray.startPoint, 1)).xyz;
        // Do not normalize: the affine transform preserves the original ray parameter.
        local.direction = (inverse * vec4(ray.direction, 0)).xyz;
        vec3 localReciprocal = 1.0 / local.direction;
        #ifndef COMPUTE_PATH
    int bs[BVH_STACK_CAPACITY];
#endif
        #ifndef COMPUTE_PATH
    float blasDistances[BVH_STACK_CAPACITY];
#endif
        int bp = 0;
        BVHNode blasRoot = getBVHNode(int(info.y));
        float blasEntry = BoundsDistance(local, localReciprocal, blasRoot.AA, blasRoot.BB, res.hitDistance);
        if (blasEntry < 0)
            continue;
        TriangleRay triangleRay = PrepareTriangleRay(local);
        blasDistances[bp] = blasEntry;
        bs[bp++] = int(info.y);
        while (bp > 0)
        {
            int blasIndex = bs[--bp];
#ifdef TRACE_TRAVERSAL_PROFILE
            ++traversalNodeVisits;
#endif
            if (blasDistances[bp] > res.hitDistance)
                continue;
            BVHNode n = getBVHNode(blasIndex);
            if (n.n == 0)
            {
                BVHNode left = getBVHNode(n.left), right = getBVHNode(n.right);
                float dl = BoundsDistance(local, localReciprocal, left.AA, left.BB, res.hitDistance);
                float dr = BoundsDistance(local, localReciprocal, right.AA, right.BB, res.hitDistance);
                if (bp < BVH_STACK_CAPACITY - 2)
                {
                    if (dl >= 0 && dr >= 0)
                    {
                        if (dl < dr)
                        {
                            {
                                blasDistances[bp] = dr;
                                bs[bp++] = n.right;
                            }
                            {
                                blasDistances[bp] = dl;
                                bs[bp++] = n.left;
                            }
                        }
                        else
                        {
                            {
                                blasDistances[bp] = dl;
                                bs[bp++] = n.left;
                            }
                            {
                                blasDistances[bp] = dr;
                                bs[bp++] = n.right;
                            }
                        }
                    }
                    else if (dl >= 0)
                    {
                        blasDistances[bp] = dl;
                        bs[bp++] = n.left;
                    }
                    else if (dr >= 0)
                    {
                        blasDistances[bp] = dr;
                        bs[bp++] = n.right;
                    }
                }
                else RaisePathDiagnostic(DIAG_BVH_OVERFLOW);
                continue;
            }
            for (int i = n.index; i < n.index + n.n; ++i)
            {
#ifdef TRACE_TRAVERSAL_PROFILE
                ++traversalTriangleTests;
#endif
                vec3 p1 = texelFetch(triangles, i * 11).xyz, p2 = texelFetch(triangles, i * 11 + 1).xyz,
                     p3 = texelFetch(triangles, i * 11 + 2).xyz;
                vec3 candidate;
                float d;
                if (!IntersectTriangle(triangleRay, p1, p2, p3, candidate, d) || d > res.hitDistance)
                    continue;
                vec3 face = cross(p2 - p1, p3 - p1);
                if (all(equal(face, vec3(0))) || any(isnan(face)) || any(isinf(face)))
                    continue;
                // Logical surface order is independent of BVH traversal order.
                int surface = i + int(info.z);
                if(surface==excludedSurface)continue;
                if (d == res.hitDistance && res.triangleIndex >= 0 && surface > res.triangleIndex)
                    continue;
                vec2 uv = vec2(0);
                if (instanceAlphaMode == ALPHA_MODE_MASK || instanceAlphaMode == ALPHA_MODE_BLEND)
                {
                    uv = InterpolateTriangleUV(surface, candidate);
                    if(instanceAlphaMode==ALPHA_MODE_MASK && materialTextureInfoStride>=4) {
                        mat3 normalMatrix=transpose(mat3(inverse));
                        vec3 worldFace=normalize(normalMatrix*(face/maxComponent(abs(face))));
                        SetTriangleFootprint(surface,ray,d,worldFace);
                    }
                    if (RejectAlphaIntersection(surface, uv))
                        continue;
                }
                res.isHit = true;
                res.triangleIndex = surface;
                res.hitDistance = d;
                res.uv = uv;
                bary = candidate;
                if(anyHit && shadowOnly) {
#ifdef TRACE_TRAVERSAL_PROFILE
                    ++traversalEarlyExits;
#endif
                    return res;
                }
            }
        }
    }
    // ID/depth picking needs alpha filtering and nearest traversal, but no
    // beauty material, normal-map or tangent reconstruction.
#ifdef PICKING_PASS
    return res;
#endif
    if (res.isHit)
    {
        res.uv = InterpolateTriangleUV(res.triangleIndex, bary);
        uvec2 selected = texelFetch(surfaceTable, res.triangleIndex).xy;
        int instance = int(selected.y), g = int(selected.x) * 11;
        mat4 world = InstanceMatrix(instance, 0);
        mat3 normals = transpose(mat3(InstanceMatrix(instance, 4)));
        vec3 localA = texelFetch(triangles, g).xyz;
        vec3 localAB = texelFetch(triangles, g + 1).xyz - localA;
        vec3 localAC = texelFetch(triangles, g + 2).xyz - localA;
        // Transform the local face normal, rather than subtracting separately
        // rounded world vertices. Tiny faces can collapse after a translation.
        vec3 localGeometry = cross(localAB, localAC);
        vec3 geometry = normals * (localGeometry / maxComponent(abs(localGeometry)));
        float geometryScale = maxComponent(abs(geometry));
        if (!(geometryScale > 0.0) || any(isnan(geometry)) || any(isinf(geometry)))
        {
            res.isHit = false;
            res.hitDistance = INF;
            return res;
        }
        res.geometricNormal = normalize(geometry / geometryScale);
        vec3 normal = bary.x * (normals * texelFetch(triangles, g + 3).xyz) +
                      bary.y * (normals * texelFetch(triangles, g + 4).xyz) +
                      bary.z * (normals * texelFetch(triangles, g + 5).xyz);
        normal = length(normal) < EPS ? res.geometricNormal : normalize(normal);
        if (dot(normal, res.geometricNormal) < 0)
            normal = -normal;
        res.isInside = dot(res.geometricNormal, ray.direction) > 0;
        vec3 facingGeometry = res.isInside ? -res.geometricNormal : res.geometricNormal;
        res.normal = ValidShadingNormal(res.isInside ? -normal : normal, facingGeometry, ray.direction);
        res.viewDir = ray.direction;
        // Reconstruct on the triangle, avoiding cancellation along long rays.
        // Keep hitDistance as the original ray parameter for traversal/light ordering.
        ReconstructSurfacePoint(res.triangleIndex,bary,res.hitPoint,res.positionError);
        if (shadowOnly)
            res.material = ShadowMaterial(res.triangleIndex);
        else
        {
            materialEvaluationUV = res.uv;
            SetTriangleFootprint(res.triangleIndex,ray,res.hitDistance,res.geometricNormal);
            res.material = getMaterial(res.triangleIndex);
            res.normal = ApplyNormalMap(res.triangleIndex, res.uv, bary, res.normal, res.material);
            res.normal = ValidShadingNormal(res.normal, facingGeometry, ray.direction);
            if(res.material.anisotropic>0.0)
                res.material.tangent=BsdfTangent(res.triangleIndex,bary,res.normal);
        }
    }
    return res;
}

HitResult hitBVH(Ray ray,bool shadowOnly)
{
    return hitBVH(ray,shadowOnly,INF,false,-1);
}
HitResult hitBVH(Ray ray)
{
    return hitBVH(ray,false,INF,false,-1);
}
