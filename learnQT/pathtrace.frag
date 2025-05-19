#version 330 core
#define SIZE_TRIANGLE   12
#define SIZE_BVHNODE    4
#define INF 114514.0
#define PI 3.14159265358979323
#define TWO_PI 6.28318530717958648
#define INV_PI 0.31830988618379067
#define EPS 1e-6

out vec4 FragColor;
in vec3 pix;


//uniform mat4 model;
uniform mat4 view;
//uniform mat4 projection;
uniform int nTriangles;
uniform vec3 eye;
uniform int nNodes;
uniform int width;
uniform int height;
uniform uint frameCounter;
uniform int hdrResolution;
uniform bool useEnvironmentMap;

uniform float sobelNumber[24];
 
uniform samplerBuffer triangles;
uniform samplerBuffer nodes;

uniform sampler2D lastFrame;
uniform sampler2D hdrMap;
uniform sampler2D hdrCache;

// Triangle 数据格式
struct Triangle {
    vec3 p1, p2, p3;    // 顶点坐标
    vec3 n1, n2, n3;    // 顶点法线
};
// BVH 树节点
struct BVHNode {
    int left;           // 左子树
    int right;          // 右子树
    int n;              // 包含三角形数目
    int index;          // 三角形索引
    vec3 AA, BB;        // 碰撞盒
};

// 物体表面材质定义
struct Material {
    vec3 emissive;          // 作为光源时的发光颜色
    vec3 baseColor;
    float subsurface;
    float metallic;
    float specular;
    float specularTint;
    float roughness;
    float anisotropic;
    float sheen;
    float sheenTint;
    float clearcoat;
    float clearcoatGloss;
    float IOR;
    float transmission;
};
// 光线
struct Ray {
    vec3 startPoint;
    vec3 direction;
};

// 光线求交结果
struct HitResult {
    bool isHit;             // 是否命中
    bool isInside;          // 是否从内部命中
    float distance;         // 与交点的距离
    vec3 hitPoint;          // 光线命中点
    vec3 normal;            // 命中点法线
    vec3 viewDir;           // 击中该点的光线的方向
    Material material;      // 命中点的表面材质
};

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
   return float(wang_hash(seed)) * (1.0 / 4294967296.0);
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

// 获取第 i 下标的三角形
Triangle getTriangle(int i) {
    int offset = i * SIZE_TRIANGLE;
    Triangle t;

    // 顶点坐标
    t.p1 = texelFetch(triangles, offset + 0).xyz;
    t.p2 = texelFetch(triangles, offset + 1).xyz;
    t.p3 = texelFetch(triangles, offset + 2).xyz;
    // 法线
    t.n1 = texelFetch(triangles, offset + 3).xyz;
    t.n2 = texelFetch(triangles, offset + 4).xyz;
    t.n3 = texelFetch(triangles, offset + 5).xyz;

    return t;
}

// 获取第 i 下标的三角形的材质
Material getMaterial(int i) {
    Material m;

    int offset = i * SIZE_TRIANGLE;
    vec3 param1 = texelFetch(triangles, offset + 8).xyz;
    vec3 param2 = texelFetch(triangles, offset + 9).xyz;
    vec3 param3 = texelFetch(triangles, offset + 10).xyz;
    vec3 param4 = texelFetch(triangles, offset + 11).xyz;
    
    m.emissive = texelFetch(triangles, offset + 6).xyz;
    m.baseColor = texelFetch(triangles, offset + 7).xyz;
    m.subsurface = param1.x;
    m.metallic = param1.y;
    m.specular = param1.z;
    m.specularTint = param2.x;
    m.roughness = param2.y;
    m.anisotropic = param2.z;
    m.sheen = param3.x;
    m.sheenTint = param3.y;
    m.clearcoat = param3.z;
    m.clearcoatGloss = param4.x;
    m.IOR = param4.y;
    m.transmission = param4.z;

    return m;
}

// 光线和三角形求交 
HitResult hitTriangle(Triangle triangle, Ray ray) {
    HitResult res;
    res.distance = INF;
    res.isHit = false;
    res.isInside = false;

    vec3 p1 = triangle.p1;
    vec3 p2 = triangle.p2;
    vec3 p3 = triangle.p3;

    vec3 e0 = p2.xyz - p1.xyz;
    vec3 e1 = p3.xyz - p1.xyz;
    vec3 pv = cross(ray.direction, e1);
    float det = dot(e0, pv);

    vec3 tv = ray.startPoint - p1.xyz;
    vec3 qv = cross(tv, e0);

    vec4 uvt;
    uvt.x = dot(tv, pv);
    uvt.y = dot(ray.direction, qv);
    uvt.z = dot(e1, qv);
    uvt.xyz = uvt.xyz / det;
    uvt.w = 1.0 - uvt.x - uvt.y;
    
    if (all(greaterThanEqual(uvt, vec4(0.0))) && uvt.z < res.distance)
    {
        res.isHit = true;
        res.hitPoint = ray.startPoint + ray.direction * uvt.z;
        res.distance = uvt.z;
        res.viewDir = ray.direction;

        // 根据交点位置插值顶点法线 
        vec3 Nsmooth = uvt.w * triangle.n1 + uvt.x * triangle.n2 + uvt.y * triangle.n3;
        if (length(Nsmooth) < EPS) {
            Nsmooth = normalize(cross(p2-p1, p3-p1)); // 防止接近零向量导致溢出
        }else{
            Nsmooth = normalize(Nsmooth);
        }
        // 从三角形背后（模型内部）击中
        if (dot(Nsmooth, ray.direction) > 0.0f) {
            res.isInside = true;
            res.normal =-Nsmooth;
        }else{
            res.normal=Nsmooth;
        }

    }

    return res;
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


// 暴力遍历数组下标范围 [l, r] 求最近交点
HitResult hitArray(Ray ray, int l, int r) {
    HitResult res;
    res.isHit = false;
    res.distance = INF;
    for(int i=l; i<=r; i++) {
        Triangle triangle = getTriangle(i);
        HitResult r = hitTriangle(triangle, ray);
        if(r.isHit && r.distance<res.distance) {
            res = r;
            res.material = getMaterial(i);
        }
    }
    return res;
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
    res.distance = INF;
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

                if (abs(det) < 0.00001) 
                    continue;

                vec3 tv = ray.startPoint - p1.xyz;
                vec3 qv = cross(tv, e0);

                vec4 uvt;
                uvt.x = dot(tv, pv);
                uvt.y = dot(ray.direction, qv);
                uvt.z = dot(e1, qv);
                uvt.xyz = uvt.xyz / det;
                uvt.w = 1.0 - uvt.x - uvt.y;
                
                if(uvt.z<0.0005)
                    continue;

                if (all(greaterThanEqual(uvt, vec4(0.0))) && uvt.z < res.distance)
                {
                    res.isHit = true;
                    res.hitPoint = ray.startPoint + ray.direction * uvt.z;
                    res.distance = uvt.z;
                    res.viewDir = ray.direction;
                    bary = uvt.wxy;
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

            d1= (t1 >= t0) ? ((t0 > 0.0) ? ( t0<res.distance?(t0):0.0 ) : (t1)) : (-1);
        }
        if(node.right>0) {
            BVHNode rightNode = getBVHNode(node.right);

            vec3 f = ( rightNode.BB - ray.startPoint) * invdir;
            vec3 n = ( rightNode.AA - ray.startPoint) * invdir;

            vec3 tmax = max(f, n);
            vec3 tmin = min(f, n);

            float t1 = min(tmax.x, min(tmax.y, tmax.z));
            float t0 = max(tmin.x, max(tmin.y, tmin.z));

            d2= (t1 >= t0) ? ((t0 > 0.0) ? (t0<res.distance?(t0):0.0) : (t1)) : (-1);
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
        if (dot(Nsmooth, ray.direction) > 0.0f) {
            res.isInside = true;
            res.normal =-Nsmooth;
        }else{
            res.isInside=false;
            res.normal=Nsmooth;
        }

        res.material = getMaterial(triID);
    }





    return res;
}

// 将三维向量 v 转为 HDR map 的纹理坐标 uv
vec2 toSphericalCoord(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv /= vec2(2.0 * PI, PI);
    uv += 0.5;
    uv.y = 1.0 - uv.y;
    return uv;
}
// 输入光线方向 L 获取 HDR 在该位置的概率密度
// hdr 分辨率为 4096 x 2048 --> hdrResolution = 4096
float hdrPdf(vec3 L, int hdrResolution) {
    vec2 uv = toSphericalCoord(normalize(L));   // 方向向量转 uv 纹理坐标
  
    float pdf = texture2D(hdrCache, uv).b;      // 采样概率密度
    float theta = PI * (0.5-uv.y);            // theta 范围 [-pi/2 ~ pi/2]
    float sin_theta = max(abs(sin(theta)),1e-10);
    // 球坐标和图片积分域的转换系数
    float p_convert = float(hdrResolution * hdrResolution / 2) / (2.0 * PI * PI * sin_theta);  
    
    return pdf * p_convert;
    //return sin_theta;
}
// 采样预计算的 HDR cache
vec3 SampleHdr(float xi_1, float xi_2) {
    vec2 xy = texture2D(hdrCache, vec2(xi_1, xi_2)).rg; // x, y
    xy.y = 1.0 - xy.y; // flip y

    // 获取角度
    float phi = 2.0 * PI * (xy.x - 0.5);    // [-pi ~ pi]
    float theta = PI * (xy.y - 0.5);        // [-pi/2 ~ pi/2]   

    // 球坐标计算方向
    vec3 L = vec3(cos(theta)*cos(phi), sin(theta), cos(theta)*sin(phi));

    return L;
}
// 获取 HDR 环境颜色
vec3 hdrColor(vec3 L) {
    if(!useEnvironmentMap)
    {
        return vec3(0);
    }
    vec2 uv = toSphericalCoord(normalize(L));
    vec3 color = texture2D(hdrMap, uv).rgb;
    return color;
}

float misMixWeight(float a, float b) {
    float t = a * a;
    return t / (b*b + t);
}

float sqr(float x) { 
    return x*x; 
}

float SchlickFresnel(float u) {
    float m = clamp(1-u, 0.0, 1.0);
    float m2 = m*m;
    return m2*m2*m; // pow(m,5)
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
float GTR1(float NdotH, float a) {
    if (a >= 1.0) 
        return INV_PI;
    float a2 = a*a;
    float t = 1.0 + (a2-1.0)*NdotH*NdotH;
    return (a2-1.0) / (PI*log(a2)*t);
}
vec3 CosineSampleHemisphere(float r1, float r2)
{
    vec3 dir;
    float r = sqrt(r1);
    float phi = 2.0*PI * r2;
    dir.x = r * cos(phi);
    dir.y = r * sin(phi);
    dir.z = sqrt(max(0.0, 1.0 - dir.x * dir.x - dir.y * dir.y));
    return dir;
}
vec3 SampleGGXVNDF(vec3 V, float ax, float ay, float r1, float r2)
{
    vec3 Vh = normalize(vec3(ax * V.x, ay * V.y, V.z));

    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    vec3 T1 = lensq > 0 ? vec3(-Vh.y, Vh.x, 0) * inversesqrt(lensq) : vec3(1, 0, 0);
    vec3 T2 = cross(Vh, T1);

    float r = sqrt(r1);
    float phi = 2.0 * PI * r2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

    vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

    return normalize(vec3(ax * Nh.x, ay * Nh.y, max(0.0, Nh.z)));
}

//计算菲涅尔反射率
float DielectricFresnel(float cosThetaI, float eta)
{
    float sinThetaTSq = eta * eta * (1.0f - cosThetaI * cosThetaI);

    // Total internal reflection
    if (sinThetaTSq > 1.0)
        return 1.0;

    float cosThetaT = sqrt(max(1.0 - sinThetaTSq, 0.0));

    float rs = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);
    float rp = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);

    return 0.5f * (rs * rs + rp * rp);
}

vec3 SampleGTR1(float rgh, float r1, float r2)
{
    float a = max(0.001, rgh);
    float a2 = a * a;

    float phi = r1 * 2.0 * PI;

    float cosTheta = sqrt((1.0 - pow(a2, 1.0 - r2)) / (1.0 - a2));
    float sinTheta = clamp(sqrt(1.0 - (cosTheta * cosTheta)), 0.0, 1.0);
    float sinPhi = sin(phi);
    float cosPhi = cos(phi);

    return vec3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
}


vec3 DisneySample(float xi_1, float xi_2, float xi_3, vec3 V, vec3 N, in Material material,float eta)
{
    //float eta=dot(-V, N) < 0.0 ? (1.0 / material.IOR) :material.IOR;
    float aspect = sqrt(1.0 - material.anisotropic * 0.9);
    float ax = max(0.001, material.roughness / aspect);
    float ay = max(0.001, material.roughness * aspect);

    vec3 L;

    // TODO: Tangent and bitangent should be calculated from mesh (provided, the mesh has proper uvs)
    vec3 T, B;
    Onb(N, T, B);

    // Transform to shading space to simplify operations (NDotL = L.z; NDotV = V.z; NDotH = H.z)
    V = ToLocal(T, B, N, V);

    // Tint colors
    float lum = Luminance(material.baseColor);
    vec3 ctint = lum > 0.0 ? material.baseColor / lum : vec3(1.0);
    float F0 = (1.0 - eta) / (1.0 + eta);
    F0 *= F0;    
    vec3 Cspec0 = F0 * mix(vec3(1.0), ctint, material.specularTint);

    // Model weights
    float dielectricWt = (1.0 - material.metallic) * (1.0 - material.transmission);
    float metalWt =material.metallic;
    float glassWt = (1.0 - material.metallic) * material.transmission;

    // Lobe probabilities
    float schlickWt = SchlickFresnel(V.z);

    float diffPr = dielectricWt *lum;
    float dielectricPr = dielectricWt * Luminance(mix(Cspec0, vec3(1.0), schlickWt));
    float metalPr = metalWt * Luminance(mix(material.baseColor, vec3(1.0), schlickWt));
    float glassPr = glassWt;
    float clearCtPr = 0.25 * material.clearcoat;

    // Normalize probabilities
    float invTotalWt = 1.0 / (diffPr + dielectricPr + metalPr + glassPr + clearCtPr);
    diffPr *= invTotalWt;
    dielectricPr *= invTotalWt;
    metalPr *= invTotalWt;
    glassPr *= invTotalWt;
    clearCtPr *= invTotalWt;

    // CDF of the sampling probabilities
    float cdf[5];
    cdf[0] = diffPr;
    cdf[1] = cdf[0] + dielectricPr;
    cdf[2] = cdf[1] + metalPr;
    cdf[3] = cdf[2] + glassPr;
    cdf[4] = cdf[3] + clearCtPr;


    if (xi_3 < cdf[0]) // Diffuse
    {
        L = CosineSampleHemisphere(xi_1, xi_2);
    }
    else if (xi_3 < cdf[2]) // Dielectric + Metallic reflection
    {
        vec3 H = SampleGGXVNDF(V, ax, ay, xi_1, xi_2);
        if (H.z < 0.0)
            H = -H;

        L = normalize(reflect(-V, H));
    }
    else if (xi_3 < cdf[3]) // Glass
    {
        vec3 H = SampleGGXVNDF(V, ax, ay, xi_1, xi_2);
        float F = DielectricFresnel(abs(dot(V, H)), eta);

        if (H.z < 0.0)
            H = -H;

        // Rescale random number for reuse
        xi_3 = (xi_3 - cdf[2]) / (cdf[3] - cdf[2]);

        // Reflection
        if (xi_3 < F)
        {
            L = normalize(reflect(-V, H));
        }
        else // Transmission
        {
            L = normalize(refract(-V, H, eta));
        }
    }
    else // Clearcoat
    {
        vec3 H = SampleGTR1(material.clearcoatGloss,xi_1, xi_2);

        if (H.z < 0.0)
            H = -H;

        L = normalize(reflect(-V, H));
    }

    L = ToWorld(T, B, N, L);
    V = ToWorld(T, B, N, V);

    return L;
}

vec3 EvalDisneyDiffuse( Material material, vec3 Csheen, vec3 V, vec3 L, vec3 H, out float pdf)
{
    pdf = 0.0;
    if (L.z <= 0.0)
        return vec3(0.0);

    float LDotH = dot(L, H);

    float Rr = 2.0 * material.roughness * LDotH * LDotH;

    // Diffuse
    float FL = SchlickFresnel(L.z);
    float FV = SchlickFresnel(V.z);
    float Fretro = Rr * (FL + FV + FL * FV * (Rr - 1.0));
    float Fd = (1.0 - 0.5 * FL) * (1.0 - 0.5 * FV);

    // Fake subsurface
    float Fss90 = 0.5 * Rr;
    float Fss = mix(1.0, Fss90, FL) * mix(1.0, Fss90, FV);
    float ss = 1.25 * (Fss * (1.0 / (L.z + V.z) - 0.5) + 0.5);

    // Sheen
    float FH = SchlickFresnel(LDotH);
    vec3 Fsheen = FH * material.sheen * Csheen;

    pdf = L.z * INV_PI;
    return INV_PI * material.baseColor * mix(Fd + Fretro, ss, material.subsurface) + Fsheen;
}
float GTR2Aniso(float NDotH, float HDotX, float HDotY, float ax, float ay)
{
    float a = HDotX / ax;
    float b = HDotY / ay;
    float c = a * a + b * b + NDotH * NDotH;
    return 1.0 / (PI * ax * ay * c * c);
}
float SmithGAniso(float NDotV, float VDotX, float VDotY, float ax, float ay)
{
    float a = VDotX * ax;
    float b = VDotY * ay;
    float c = NDotV;
    return (2.0 * NDotV) / (NDotV + sqrt(a * a + b * b + c * c));
}

vec3 EvalMicrofacetReflection(float ax,float ay, vec3 V, vec3 L, vec3 H, vec3 F, out float pdf)
{
    pdf = 0.0;
    if (L.z <= 0.0)
        return vec3(0.0);

    float D = GTR2Aniso(H.z, H.x, H.y, ax, ay);
    float G1 = SmithGAniso(abs(V.z), V.x, V.y,  ax,  ay);
    float G2 = G1 * SmithGAniso(abs(L.z), L.x, L.y,  ax,  ay);

    pdf = G1 * D / (4.0 * V.z);
    return F * D * G2 / (4.0 * L.z * V.z);
}
vec3 EvalMicrofacetRefraction(vec3 baseColor, float ax,float ay, float eta, vec3 V, vec3 L, vec3 H, vec3 F, out float pdf)
{
    pdf = 0.0;
    if (L.z >= 0.0)
        return vec3(0.0);

    float LDotH = dot(L, H);
    float VDotH = dot(V, H);

    float D = GTR2Aniso(H.z, H.x, H.y, ax, ay);
    float G1 = SmithGAniso(abs(V.z), V.x, V.y, ax, ay);
    float G2 = G1 * SmithGAniso(abs(L.z), L.x, L.y, ax, ay);
    float denom = LDotH + VDotH * eta;
    denom *= denom;
    float eta2 = eta * eta;
    float jacobian = abs(LDotH) / denom;

    pdf = G1 * max(0.0, VDotH) * D * jacobian / V.z;
    return pow(baseColor, vec3(0.5)) * (1.0 - F) * D * G2 * abs(VDotH) * jacobian * eta2 / abs(L.z * V.z);
}
float SmithG(float NDotV, float alphaG)
{
    float a = alphaG * alphaG;
    float b = NDotV * NDotV;
    return (2.0 * NDotV) / (NDotV + sqrt(a + b - a * b));
}
vec3 EvalClearcoat(float clearcoatRoughness, vec3 V, vec3 L, vec3 H, out float pdf)
{
    pdf = 0.0;
    if (L.z <= 0.0)
        return vec3(0.0);

    float VDotH = dot(V, H);

    float F = mix(0.04, 1.0, SchlickFresnel(VDotH));
    float D = GTR1(H.z, clearcoatRoughness);
    float G = SmithG(L.z, 0.25) * SmithG(V.z, 0.25);
    float jacobian = 1.0 / (4.0 * VDotH);

    pdf = D * H.z * jacobian;
    return vec3(F) * D * G;
}
vec3 DisneyEval(vec3 V, vec3 N, vec3 L, in Material material,float eta,out float pdf)
{
    float aspect = sqrt(1.0 - material.anisotropic * 0.9);
    float ax = max(0.001, material.roughness / aspect);
    float ay = max(0.001, material.roughness * aspect);
    pdf = 0.0;
    vec3 f = vec3(0.0);

    // TODO: Tangent and bitangent should be calculated from mesh (provided, the mesh has proper uvs)
    vec3 T, B;
    Onb(N, T, B);

    // Transform to shading space to simplify operations (NDotL = L.z; NDotV = V.z; NDotH = H.z)
    V = ToLocal(T, B, N, V);
    L = ToLocal(T, B, N, L);

    vec3 H;
    if (L.z > 0.0)
        H = normalize(L + V);
    else
        H = normalize(L + V * eta);

    if (H.z < 0.0)
        H = -H;

    float lum = Luminance(material.baseColor);
    vec3 ctint = lum > 0.0 ? material.baseColor / lum : vec3(1.0);
    float F0 = (1.0 - eta) / (1.0 + eta);
    F0 *= F0;
    vec3  Cspec0 = F0 * mix(vec3(1.0), ctint, material.specularTint);
    vec3 Csheen = mix(vec3(1.0), ctint, material.sheenTint);

    // Model weights
    float dielectricWt = (1.0 -  material.metallic) * (1.0 -  material.transmission);
    float metalWt =  material.metallic;
    float glassWt = (1.0 -  material.metallic) *  material.transmission;

    // Lobe probabilities
    float schlickWt = SchlickFresnel(V.z);

    float diffPr = dielectricWt * lum;
    float dielectricPr = dielectricWt * Luminance(mix(Cspec0, vec3(1.0), schlickWt));
    float metalPr = metalWt * Luminance(mix(material.baseColor, vec3(1.0), schlickWt));
    float glassPr = glassWt;
    float clearCtPr = 0.25 * material.clearcoat;

    // Normalize probabilities
    float invTotalWt = 1.0 / (diffPr + dielectricPr + metalPr + glassPr + clearCtPr);
    diffPr *= invTotalWt;
    dielectricPr *= invTotalWt;
    metalPr *= invTotalWt;
    glassPr *= invTotalWt;
    clearCtPr *= invTotalWt;

    bool reflect = L.z * V.z > 0;

    float tmpPdf = 0.0;
    float VDotH = abs(dot(V, H));

    // Diffuse
    if (diffPr > 0.0 && reflect)
    {
        f += EvalDisneyDiffuse(material, Csheen, V, L, H, tmpPdf) * dielectricWt;
        pdf += tmpPdf * diffPr;
    }

    // Dielectric Reflection
    if (dielectricPr > 0.0 && reflect)
    {
        // Normalize for interpolating based on Cspec0
        float F = (DielectricFresnel(VDotH, 1.0 / material.IOR) - F0) / (1.0 - F0);

        f += EvalMicrofacetReflection(ax,ay, V, L, H, mix(Cspec0, vec3(1.0), F), tmpPdf) * dielectricWt;
        pdf += tmpPdf * dielectricPr;
    }

    // Metallic Reflection
    if (metalPr > 0.0 && reflect)
    {
        // Tinted to base color
        vec3 F = mix(material.baseColor, vec3(1.0), SchlickFresnel(VDotH));

        f += EvalMicrofacetReflection(ax,ay, V, L, H, F, tmpPdf) * metalWt;
        pdf += tmpPdf * metalPr;
    }

    // Glass/Specular BSDF
    if (glassPr > 0.0)
    {
        // Dielectric fresnel (achromatic)
        float F = DielectricFresnel(VDotH, eta);

        if (reflect)
        {
            f += EvalMicrofacetReflection(ax,ay ,V, L, H, vec3(F), tmpPdf) * glassWt;
            pdf += tmpPdf * glassPr * F;
        }
        else
        {
            f += EvalMicrofacetRefraction(material.baseColor,ax,ay,eta, V, L, H, vec3(F), tmpPdf) * glassWt;
            pdf += tmpPdf * glassPr * (1.0 - F);
        }
    }

    // Clearcoat
    if (clearCtPr > 0.0 && reflect)
    {
        f += EvalClearcoat(material.clearcoatGloss, V, L, H, tmpPdf) * 0.25 * material.clearcoat;
        pdf += tmpPdf * clearCtPr;
    }

    return f;
}


vec3 pathTracingImportanceSampling(HitResult hit, int maxBounce) {

    vec3 Lo = vec3(0);      // 最终的颜色
    vec3 history = vec3(1); // 递归积累的颜色


    for(int bounce=0; bounce<maxBounce; bounce++) {
        vec3 V = -hit.viewDir;
        vec3 N = hit.normal;   


        // HDR 环境贴图重要性采样    
        //Ray hdrTestRay;
        //hdrTestRay.startPoint = hit.hitPoint;
        //hdrTestRay.direction = SampleHdr(rand(), rand());

        /*
        // 进行一次求交测试 判断是否有遮挡
        if(dot(N, hdrTestRay.direction) > 0.0) { // 如果采样方向背向点 p 则放弃测试, 因为 N dot L < 0            
            HitResult hdrHit = hitBVH(hdrTestRay);
    
            // 天空光仅在没有遮挡的情况下积累亮度
            if(!hdrHit.isHit) {
                vec3 L = hdrTestRay.direction;
                vec3 color = hdrColor(L);
                float pdf_light =hdrPdf(L, hdrResolution);
                float pdf_brdf=0.0;
                vec3 f_r = DisneyEval(V, N, L, hit.material,pdf_brdf);
                float mis_weight = misMixWeight(pdf_light, pdf_brdf);
                Lo += mis_weight * history * color * f_r * dot(N, L) / pdf_light;
                //Lo=L*0.5+0.5;
            }
        }
        */
        
        // 获取 3 个随机数

        vec2 uv;
        uv.x=sobelNumber[bounce*2];
        uv.y=sobelNumber[bounce*2+1];
        uv = CranleyPattersonRotation(uv);
        float xi_1 = uv.x;
        float xi_2 = uv.y; 
        //float xi_1 = rand();
        //float xi_2 = rand();
        float xi_3 = rand();    // xi_3 是决定采样的随机数, 朴素 rand 就好

        // 采样 BRDF 得到一个方向 L
        //vec3 L = SampleBRDF(xi_1, xi_2, xi_3, V, N, hit.material); 
        float eta =hit.isInside ? hit.material.IOR:(1.0 / hit.material.IOR) ;
        vec3 L =  DisneySample(xi_1, xi_2, xi_3, V, N, hit.material,eta); 
        float NdotL =abs(dot(N, L));//有折射情况 取绝对值
       // if(NdotL <= 0.0) break;

        // 发射光线
        Ray randomRay;
        randomRay.startPoint = hit.hitPoint;
        randomRay.direction = L;
        HitResult newHit = hitBVH(randomRay);



        // 获取 L 方向上的 BRDF 值和概率密度
        float pdf_brdf=0.0;
        vec3 f_r = DisneyEval(V, N, L, hit.material,eta,pdf_brdf);
        if(pdf_brdf <= 0.0) break;

       // 未命中        
        if(!newHit.isHit) {
            vec3 color = hdrColor(L);
            //float pdf_light = hdrPdf(L, hdrResolution); 

            //float mis_weight = misMixWeight(pdf_brdf, pdf_light);   // f(a,b) = a^2 / (a^2 + b^2)
            float mis_weight = 1.0;   // f(a,b) = a^2 / (a^2 + b^2)
            Lo += mis_weight * history * color * f_r * NdotL / pdf_brdf;
            break;
        }
        
        // 命中光源积累颜色
        vec3 Le = newHit.material.emissive;
        Lo += history * Le * f_r * NdotL / pdf_brdf;             

        // 递归(步进)
        hit = newHit;
        history *= f_r * NdotL / pdf_brdf;   // 累积颜色

        // 加入俄罗斯轮盘赌 (关键位置)
        if (bounce >= 2) { // 前2次反弹不启用RR减少噪声
            float rrSurvivalProb = min(0.95, max(maxComponent(history), 0.05)); // 保持最小5%存活率
            if (rand() > rrSurvivalProb) break;     // 终止路径
            history /= rrSurvivalProb;              // 保持无偏
        }
    }
    
    return Lo;
}

void main(void)
{     
    Ray ray;
    ray.startPoint = eye;
   // ray.startPoint = vec3(0, 0, 4);

   
    vec2 AA = vec2((rand()-0.5)/float(width), (rand()-0.5)/float(height));
    //vec2 AA = vec2(0);
    vec4 dir = view*vec4(pix.x*float(width) /float(height)+AA.x,pix.y+AA.y, -2.0,0.0);//- ray.startPoint;
    ray.direction = normalize(dir.xyz);
    vec3 color=vec3(0);
    // primary hit
    HitResult firstHit = hitBVH(ray);
        if(!firstHit.isHit) {
            color = hdrColor(ray.direction);
    } else {
        vec3 Le = firstHit.material.emissive;
        vec3 Li = pathTracingImportanceSampling(firstHit,6);
        color = Le + Li;
    }  

    
    vec3 lastColor = texture2D(lastFrame, pix.xy*0.5+0.5).rgb;
    // lastColor*=100.0; 
    if(isnan(color.x)||isnan(color.x)||isnan(color.x))
        color=lastColor;
    else
        color = mix(lastColor, color, 1.0/float(frameCounter+1u)); 

    
     FragColor=vec4(color,1.0);
}