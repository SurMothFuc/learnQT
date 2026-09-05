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
    return stack.size > 0 ? stack.entries[stack.size-1] : Vacuum();
}
// Closed, consistently wound, properly nested boundaries use a bounded LIFO stack.
// An overflowing stack terminates the path instead of reading undefined memory.
bool CrossMediumBoundary(inout MediumStack stack, HitResult hit, vec3 outgoing) {
    if (hit.material.mediumtype == MEDIUM_NONE) return true;
    bool intoObject = dot(outgoing, hit.geometricNormal) < 0.0;
    if (!hit.isInside && intoObject) {
        if (stack.size == MAX_MEDIA) return false;
        stack.entries[stack.size++] = MaterialMedium(hit.material);
    } else if (hit.isInside && !intoObject && stack.size > 0) {
        --stack.size;
    }
    return true;
}
vec3 MediumTransmittance(Medium medium, float distance) {
    vec3 sigma = vec3(0.0);
    if (medium.type == MEDIUM_ABSORB)
        sigma = (1.0 - clamp(medium.color, 0.0, 1.0)) * medium.density;
    else if (medium.type == MEDIUM_SCATTER)
        sigma = vec3(medium.density);
    return exp(-sigma * max(0.0, distance));
}
vec3 ShadowTransmittance(vec3 origin, vec3 normal, vec3 direction,
                         float maxDistance, MediumStack media, int targetLight, int targetTriangle) {
    Ray ray;
    ray.startPoint = length(normal) > 0.0 ? OffsetRayOrigin(origin, normal, direction) : origin;
    ray.direction = direction;
    vec3 tr = vec3(1.0);
    for (int layer=0; layer<MAX_SHADOW_LAYERS; ++layer) {
        float remaining = maxDistance < INF ? maxDistance-dot(ray.startPoint-origin,direction) : INF;
        float tolerance = 4.0 * RayEpsilon(ray.startPoint);
        if (remaining <= tolerance) return tr;
        HitResult hit = hitBVH(ray);
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
        ray.startPoint = OffsetRayOrigin(hit.hitPoint, hit.geometricNormal, direction);
    }
    return vec3(0.0);
}

vec3 ShadowTransmittance(vec3 origin, vec3 normal, vec3 direction,
                         float maxDistance, MediumStack media) {
    return ShadowTransmittance(origin, normal, direction, maxDistance, media, -1, -1);
}
