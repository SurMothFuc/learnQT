const int MAX_MEDIA = 8;
const int MAX_SHADOW_LAYERS = 128;

struct Medium {
    int type;
    float density;
    vec3 color;
    float g;
    float ior;
    int boundary;
};
struct MediumStack {
    Medium entries[MAX_MEDIA];
    int size;
};
Medium Vacuum() {
    Medium m; m.type = MEDIUM_NONE; m.density = 0.0; m.color = vec3(1.0); m.g = 0.0; m.ior=1.0; m.boundary=-1;
    return m;
}
Medium MaterialMedium(Material material) {
    Medium m;
    m.type = material.mediumtype;
    m.density = max(0.0, material.mediumDensity);
    m.color = max(material.mediumColor, vec3(0.0));
    m.g = clamp(material.mediumAnisotropy, -0.999, 0.999);
    m.ior=material.transmission>0.0?max(material.IOR,1e-6):1.0;m.boundary=-1;
    return m;
}
Medium CurrentMedium(MediumStack stack) {
#ifdef NO_PARTICIPATING_MEDIA
    return Vacuum();
#else
    return stack.size > 0 ? stack.entries[stack.size-1] : Vacuum();
#endif
}
int BoundaryIdentity(HitResult hit) {
#ifdef INSTANCED_SCENE
    return int(texelFetch(surfaceTable,hit.triangleIndex).y);
#else
    return hit.triangleIndex; // Fixtures set an explicit identity for multi-face boundaries.
#endif
}
MediumStack InitialMediumStack() {
    MediumStack stack;stack.size=useBoundaryMedia?clamp(initialMediumCount,0,MAX_MEDIA):0;
    for(int i=0;i<stack.size;++i) {
        vec4 p=initialMediumProperties[i];Medium m;
        m.type=int(p.x);m.density=p.y;m.g=p.z;m.ior=p.w;
        m.color=initialMediumColors[i].xyz;m.boundary=int(initialMediumIdentity[i].x);
        stack.entries[i]=m;
    }
    return stack;
}
float CurrentIOR(MediumStack stack) { return stack.size>0?stack.entries[stack.size-1].ior:1.0; }
bool SupportedBoundary(HitResult hit) {
#ifdef INSTANCED_SCENE
    return texelFetch(instanceTable,BoundaryIdentity(hit)*9+8).w>=1.5;
#else
    return true;
#endif
}
Medium ContactMedium(HitResult hit) {
    Medium result=Vacuum();
#ifdef INSTANCED_SCENE
    int low=0,high=mediumContactCount;
    while(low<high) {int mid=(low+high)/2;int surface=int(texelFetch(materialTable,mediumContactOffset+mid*3).x);
        if(surface<hit.triangleIndex)low=mid+1;else high=mid;
    }
    if(low<mediumContactCount) {
        vec4 key=texelFetch(materialTable,mediumContactOffset+low*3);
        if(int(key.x)==hit.triangleIndex) {
            vec4 p=texelFetch(materialTable,mediumContactOffset+low*3+1);
            result.type=int(p.x);result.density=p.y;result.g=p.z;result.ior=p.w;
            result.color=texelFetch(materialTable,mediumContactOffset+low*3+2).xyz;result.boundary=int(key.y);
        }
    }
#endif
    return result;
}
float BoundaryEta(MediumStack stack,HitResult hit) {
    if(!useBoundaryMedia || !SupportedBoundary(hit))return hit.isInside?hit.material.IOR:1.0/hit.material.IOR;
    float incident=CurrentIOR(stack);
    Medium contact=ContactMedium(hit);
    float transmitted=hit.isInside?(contact.boundary>=0?contact.ior:stack.size>1?stack.entries[stack.size-2].ior:1.0):max(hit.material.IOR,1e-6);
    return incident/transmitted;
}
// LIFO is supported for closed, oriented, properly nested instances. An exit
// must name the current boundary; mismatches terminate and are observable.
bool CrossMediumBoundary(inout MediumStack stack, HitResult hit, vec3 outgoing) {
    if(!useBoundaryMedia || !SupportedBoundary(hit)) {
#ifdef NO_PARTICIPATING_MEDIA
        return true;
#else
        if(hit.material.mediumtype==MEDIUM_NONE)return true;
#endif
    } else if(hit.material.mediumtype==MEDIUM_NONE && hit.material.transmission<=0.0)return true;
    bool intoObject=dot(outgoing,hit.geometricNormal)<0.0;
    if(useBoundaryMedia && SupportedBoundary(hit) && (hit.isInside!=intoObject)) {
        Medium contact=ContactMedium(hit);
        if(contact.boundary>=0) {
            int source=hit.isInside?BoundaryIdentity(hit):contact.boundary;
            if(stack.size==0 || stack.entries[stack.size-1].boundary!=source) {RaisePathDiagnostic(DIAG_BOUNDARY_MISMATCH);return false;}
            --stack.size;
            Medium destination=hit.isInside?contact:MaterialMedium(hit.material);
            destination.boundary=hit.isInside?contact.boundary:BoundaryIdentity(hit);
            stack.entries[stack.size++]=destination;return true;
        }
    }
    if(!hit.isInside && intoObject) {
        if(stack.size==MAX_MEDIA){RaisePathDiagnostic(DIAG_MEDIUM_OVERFLOW);return false;}
        Medium m=MaterialMedium(hit.material);m.boundary=BoundaryIdentity(hit);
        stack.entries[stack.size++]=m;
    } else if(hit.isInside && !intoObject) {
        if(useBoundaryMedia && SupportedBoundary(hit) && (stack.size==0 || stack.entries[stack.size-1].boundary!=BoundaryIdentity(hit))) {
            RaisePathDiagnostic(DIAG_BOUNDARY_MISMATCH);return false;
        }
        if(stack.size>0)--stack.size;
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
        allowNearBoundaryHit=useBoundaryMedia && media.size>0;
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
    RaisePathDiagnostic(DIAG_BOUNDARY_LIMIT);
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
