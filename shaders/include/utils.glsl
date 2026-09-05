// 返回 vec3 中最大的分量（r/g/b 中的最大值）
float maxComponent(vec3 v) {
    return max(max(v.r, v.g), v.b);
}

uint seed = uint(
    uint((pix.x * 0.5 + 0.5) * width)  * uint(1973) + 
    uint((pix.y * 0.5 + 0.5) * height) * uint(9277) + 
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

vec2 CranleyPattersonRotation(vec2 p) {
    uint pseed = uint(
        uint((pix.x * 0.5 + 0.5) * width)  * uint(1973) + 
        uint((pix.y * 0.5 + 0.5) * height) * uint(9277) + 
        uint(114514/1919) * uint(26699)) | uint(1);
    
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
float RayEpsilon(vec3 p) { return max(1e-5, 2e-6 * maxComponent(abs(p))); }
vec3 OffsetRayOrigin(vec3 p, vec3 normal, vec3 direction) {
    return p + normal * (dot(normal, direction) >= 0.0 ? RayEpsilon(p) : -RayEpsilon(p));
}
