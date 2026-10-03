const int MAX_MEDIA = 8;
const int MAX_SHADOW_LAYERS = 128;

struct Medium {
    int type;
    float density;
    vec3 color;
    float g;
};
struct MediumStack {
    Medium entries[MAX_MEDIA];
    int size;
};
Medium Vacuum() {
    Medium m; m.type = MEDIUM_NONE; m.density = 0.0; m.color = vec3(1.0); m.g = 0.0;
    return m;
}
Medium MaterialMedium(Material material) {
    Medium m;
    m.type = material.mediumtype;
    m.density = max(0.0, material.mediumDensity);
    m.color = max(material.mediumColor, vec3(0.0));
    m.g = clamp(material.mediumAnisotropy, -0.999, 0.999);
    return m;
}
Medium CurrentMedium(MediumStack stack) {
#ifdef NO_PARTICIPATING_MEDIA
    return Vacuum();
#else
    return stack.size > 0 ? stack.entries[stack.size-1] : Vacuum();
#endif
}
// Closed, consistently wound, properly nested boundaries use a bounded LIFO stack.
// An overflowing stack terminates the path instead of reading undefined memory.
bool CrossMediumBoundary(inout MediumStack stack, HitResult hit, vec3 outgoing) {
#ifdef NO_PARTICIPATING_MEDIA
    return true;
#else
    if (hit.material.mediumtype == MEDIUM_NONE) return true;
    bool intoObject = dot(outgoing, hit.geometricNormal) < 0.0;
    if (!hit.isInside && intoObject) {
        if (stack.size == MAX_MEDIA) {
            pathDiagnosticFlags|=DIAG_MEDIUM_OVERFLOW;return false;
        }
        stack.entries[stack.size++] = MaterialMedium(hit.material);
    } else if (hit.isInside && !intoObject && stack.size > 0) {
        --stack.size;
    }
    return true;
#endif
}
vec3 MediumTransmittance(Medium medium, float distance) {
    vec3 sigma = vec3(0.0);
    if (medium.type == MEDIUM_ABSORB)
        sigma = (1.0 - clamp(medium.color, 0.0, 1.0)) * medium.density;
    else if (medium.type == MEDIUM_SCATTER)
        sigma = vec3(medium.density);
    return exp(-sigma * max(0.0, distance));
}
vec3 ShadowTransmittance(vec3 origin, vec3 originError, vec3 normal, vec3 direction,
                         float maxDistance, MediumStack media, int targetLight, int targetTriangle) {
    Ray ray;
    ray.startPoint = length(normal) > 0.0 ? OffsetRayOrigin(origin, originError, normal, direction) : origin;
    ray.direction = direction;
    vec3 tr = vec3(1.0);
    float travelled=dot(ray.startPoint-origin,direction);
#ifdef INSTANCED_SCENE
    // Boundary/medium queries retain ordered segments. Mask/Blend still pass
    // through the same alpha test before an occluder can terminate this query.
    if(shadowAnyHit && shadowBinaryScene && media.size==0) {
        float limit=maxDistance<INF?max(0.0,maxDistance-travelled):INF;
        float sphereDistance;int sphere=IntersectAnalyticLights(ray.startPoint,direction,sphereDistance);
        if(sphere>=0 && sphere!=targetLight && sphereDistance<limit)return vec3(0);
        HitResult blocker=hitBVH(ray,true,limit,true,targetTriangle);
        return blocker.isHit?vec3(0):vec3(1);
    }
#endif
    for (int layer=0; layer<MAX_SHADOW_LAYERS; ++layer) {
        float remaining = maxDistance < INF ? maxDistance-travelled : INF;
        float tolerance = maxDistance < INF ? FloatGamma(5.0)*(abs(maxDistance)+abs(travelled)) : 0.0;
        if (remaining <= tolerance) return tr;
#ifdef INSTANCED_SCENE
        HitResult hit = hitBVH(ray, true);
#else
        HitResult hit = hitBVH(ray);
#endif
        float sphereDistance;
        int sphere = IntersectAnalyticLights(ray.startPoint, direction, sphereDistance);
        float segment = min(remaining, min(hit.hitDistance, sphereDistance));
        tr *= MediumTransmittance(CurrentMedium(media), segment);
        if (maxComponent(tr) <= 0.0) return vec3(0.0);
        if (sphere >= 0 && sphere == targetLight && sphereDistance <= hit.hitDistance) return tr;
        if (hit.isHit && hit.triangleIndex == targetTriangle && hit.hitDistance <= sphereDistance) return tr;
        if (segment >= remaining-tolerance) return tr;
        if (sphere >= 0 && sphereDistance <= hit.hitDistance) return vec3(0.0);
        if (!hit.isHit) return tr;
        if (hit.material.alphaMode != ALPHA_MODE_TRANSPARENT) return vec3(0.0);
        if (!CrossMediumBoundary(media, hit, direction)) return vec3(0.0);
        vec3 nextOrigin=OffsetRayOrigin(hit.hitPoint,hit.positionError,hit.geometricNormal,direction);
        travelled+=hit.hitDistance+dot(nextOrigin-hit.hitPoint,direction);
        ray.startPoint=nextOrigin;
    }
    pathDiagnosticFlags|=DIAG_BOUNDARY_LIMIT;
    return vec3(0.0);
}
vec3 ShadowTransmittance(vec3 origin,vec3 normal,vec3 direction,float maxDistance,
                         MediumStack media,int targetLight,int targetTriangle) {
    return ShadowTransmittance(origin,FloatGamma(3.0)*abs(origin),normal,direction,maxDistance,media,targetLight,targetTriangle);
}
vec3 ShadowLightTransmittance(vec3 origin,vec3 error,vec3 normal,LightSample light,MediumStack media) {
    if(light.distance>=INF)
        return ShadowTransmittance(origin,error,normal,light.direction,INF,media,light.lightIndex,light.triangleIndex);
    vec3 from=length(normal)>0.0?OffsetRayOrigin(origin,error,normal,light.direction):origin;
    vec3 to=OffsetRayOrigin(light.point,light.positionError,light.normal,from-light.point);
    vec3 connection=to-from;float distance=length(connection);
    if(distance<=0.0)return vec3(1);
    return ShadowTransmittance(from,vec3(0),vec3(0),connection/distance,distance,
        media,light.lightIndex,light.triangleIndex);
}

vec3 ShadowTransmittance(vec3 origin, vec3 normal, vec3 direction,
                         float maxDistance, MediumStack media) {
    return ShadowTransmittance(origin, normal, direction, maxDistance, media, -1, -1);
}
