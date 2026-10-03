#ifndef TRACE_PIXEL_COORD
#define TRACE_PIXEL_COORD gl_FragCoord
#endif
// Per-sample flags. Stored in the albedo auxiliary alpha, separate from path classes.
const uint DIAG_NONFINITE=1u, DIAG_REJECTED=2u, DIAG_INVALID_RAY=4u;
const uint DIAG_BVH_OVERFLOW=8u, DIAG_MEDIUM_OVERFLOW=16u, DIAG_BOUNDARY_LIMIT=32u;
const uint DIAG_BOUNDARY_MISMATCH=64u;
uint pathDiagnosticFlags=0u;
#ifdef TRACE_DIAGNOSTICS
vec4 diagnosticCounts0=vec4(0),diagnosticCounts1=vec4(0);
vec4 diagnosticOrigin=vec4(0),diagnosticDirection=vec4(0),diagnosticThroughput=vec4(0);
vec3 diagnosticRayOrigin=vec3(0),diagnosticRayDirection=vec3(0),diagnosticWeight=vec3(1);
int diagnosticDepth=0,diagnosticMediumCount=0,diagnosticSurface=-1,diagnosticStage=0;
float diagnosticEta=1.0,diagnosticEtaScale=1.0;
#endif
void RaisePathDiagnostic(uint flags) {
    pathDiagnosticFlags|=flags;
#ifdef TRACE_DIAGNOSTICS
    for(int c=0;c<7;++c)if((flags&(1u<<uint(c)))!=0u) {
        if(c<4)diagnosticCounts0[c]+=1.0;else diagnosticCounts1[c-4]+=1.0;
    }
    if(diagnosticCounts1.w==0.0) {
        uint meta=flags|(uint(diagnosticMediumCount)<<8)|(uint(diagnosticDepth)<<12)|(uint(diagnosticStage)<<19);
        diagnosticCounts1.w=float(meta);
        diagnosticOrigin=vec4(diagnosticRayOrigin,float(diagnosticSurface));
        diagnosticDirection=vec4(diagnosticRayDirection,diagnosticEta);
        diagnosticThroughput=vec4(diagnosticWeight,diagnosticEtaScale);
    }
#endif
}
#ifdef TRACE_TRAVERSAL_PROFILE
uint traversalNodeVisits=0u,traversalTriangleTests=0u,traversalEarlyExits=0u;
#endif
#ifdef TRACE_PROFILE
uint profileScatters=0u,profileTextureFetches=0u,profileLightAttempts=0u,profileInvalidLights=0u;
#endif
bool ValidTraceSample(vec4 beauty,vec4 normal,vec4 albedo) {
    bool valid=!any(isnan(beauty))&&!any(isinf(beauty)) &&
        !any(isnan(normal))&&!any(isinf(normal))&&!any(isnan(albedo))&&!any(isinf(albedo));
    if(!valid)RaisePathDiagnostic(DIAG_NONFINITE|DIAG_REJECTED);
    return valid;
}
float rayConeWidth=0.0,rayConeSpread=0.0;
bool allowNearBoundaryHit=false;
vec2 materialEvaluationFootprint=vec2(0);
bool FiniteRay(vec3 origin,vec3 direction) {
    bool finite=!any(isnan(origin)) && !any(isinf(origin)) &&
        !any(isnan(direction)) && !any(isinf(direction));
    if(!finite) RaisePathDiagnostic(DIAG_NONFINITE);
    bool valid=finite && any(notEqual(direction,vec3(0)));
    if(!valid) RaisePathDiagnostic(DIAG_INVALID_RAY);
    return valid;
}
// 返回 vec3 中最大的分量（r/g/b 中的最大值）
float maxComponent(vec3 v) {
    return max(max(v.r, v.g), v.b);
}

uint seed = uint(
    uint(TRACE_PIXEL_COORD.x) * uint(1973) +
    uint(TRACE_PIXEL_COORD.y) * uint(9277) +
    uint(frameCounter) * uint(26699)) | uint(1);
uint wang_hash(inout uint seed) {
    seed = (seed ^ uint(61)) ^ (seed >> uint(16));
    seed *= uint(9);
    seed = seed ^ (seed >> uint(4));
    seed *= uint(0x27d4eb2d);
    seed = seed ^ (seed >> uint(15));
    return seed;
} 
float rand() {
   return min(float(wang_hash(seed)) * (1.0 / 4294967296.0), 0.99999994);
}
#include "sampler.glsl"

vec2 CranleyPattersonRotation(vec2 p) {
    uint pseed = uint(
        uint(TRACE_PIXEL_COORD.x) * uint(1973) +
        uint(TRACE_PIXEL_COORD.y) * uint(9277) +
        uint(114514/1919) * uint(26699)) | uint(1);
    pseed^=samplerSeed;
    
    float u = float(wang_hash(pseed)) / 4294967296.0;
    float v = float(wang_hash(pseed)) / 4294967296.0;

    p.x += u;
    if(p.x>1) p.x -= 1;
    if(p.x<0) p.x += 1;

    p.y += v;
    if(p.y>1) p.y -= 1;
    if(p.y<0) p.y += 1;

    return p;
}

// 将三维向量 v 转为 HDR map 的纹理坐标 uv
vec2 toSphericalCoord(vec3 v) {
    // atan preserves polar latitude when the y component rounds to +/-1.
    return vec2(atan(v.z, v.x) / TWO_PI + 0.5, atan(length(v.xz), v.y) / PI);
}
float misMixWeight(float a, float b) {
    float scale = max(a, b);
    if (scale <= 0.0) return 1.0;
    a /= scale; b /= scale;
    return a*a / (a*a + b*b);
}

float sqr(float x) { 
    return x*x; 
}
float Luminance(vec3 c)
{
    return 0.212671 * c.x + 0.715160 * c.y + 0.072169 * c.z;
}

void Onb(in vec3 N, inout vec3 T, inout vec3 B)
{
    vec3 up = abs(N.z) < 0.9999999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}
vec3 ToLocal(vec3 X, vec3 Y, vec3 Z, vec3 V)
{
    return vec3(dot(V, X), dot(V, Y), dot(V, Z));
}
vec3 ToWorld(vec3 X, vec3 Y, vec3 Z, vec3 V)
{
    return V.x * X + V.y * Y + V.z * Z;
}
float FloatGamma(float operations) {
    float e=operations*5.960464477539063e-8;
    return e/(1.0-e);
}
float NextFloatAway(float value, float direction) {
    if(direction==0.0 || isinf(value))return value;
    // Avoid denormals: many GPU arithmetic units flush them to zero.
    if(value==0.0)return direction>0.0?1.1754943508222875e-38:-1.1754943508222875e-38;
    uint bits=floatBitsToUint(value);
    bits=(value>0.0)==(direction>0.0)?bits+1u:bits-1u;
    return uintBitsToFloat(bits);
}
// Kept for source-compatible analytic/test callers; surface rays use positionError.
float RayEpsilon(vec3 p) { return FloatGamma(3.0)*maxComponent(abs(p)); }
// Keep valid smooth/mapped normals. BSDF sampling assumes the incident direction
// lies above the shading surface; flipping an invalid normal could change sides.
vec3 ValidShadingNormal(vec3 shading, vec3 facingGeometry, vec3 incoming) {
    return dot(shading, facingGeometry) > 0.0 && dot(shading, -incoming) > 0.0
        ? shading : facingGeometry;
}
vec3 OffsetRayOrigin(vec3 p, vec3 normal, vec3 direction) {
    vec3 error=FloatGamma(3.0)*abs(p);
    float d=dot(abs(normal),error);
    vec3 offset=normal*(dot(normal,direction)>=0.0?d:-d);
    vec3 result=p+offset;
    return vec3(NextFloatAway(result.x,offset.x),NextFloatAway(result.y,offset.y),NextFloatAway(result.z,offset.z));
}
vec3 OffsetRayOrigin(vec3 p, vec3 error, vec3 normal, vec3 direction) {
    float d=dot(abs(normal),error);
    vec3 offset=normal*(dot(normal,direction)>=0.0?d:-d);
    vec3 result=p+offset;
    return vec3(NextFloatAway(result.x,offset.x),NextFloatAway(result.y,offset.y),NextFloatAway(result.z,offset.z));
}
