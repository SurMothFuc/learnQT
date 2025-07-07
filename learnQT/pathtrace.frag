#version 330 core
#define SIZE_TRIANGLE   12
#define SIZE_BVHNODE    4
#define INF 114514.0
#define PI 3.14159265358979323
#define TWO_PI 6.28318530717958648
#define INV_PI 0.31830988618379067
#define INV_4_PI 0.07957747154594766
#define EPS 1e-6

#define ALPHA_MODE_OPAQUE 0
#define ALPHA_MODE_TRANSPARENT 1

#define MEDIUM_NONE 0
#define MEDIUM_ABSORB 1
#define MEDIUM_SCATTER 2
#define MEDIUM_EMISSIVE 3


// 定义多个输出目标
//layout(location = 0) out vec4 FragColor;
layout(location = 0) out vec4 RenderColorResult;
layout(location = 1) out vec4 NormalResult;
layout(location = 2) out vec4 BaseColorResult;

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

uniform sampler2D hdrMap;
uniform sampler2D hdrCache;

uniform sampler2D preRenderColor;

struct OutputColor{
    vec3 render_color;
    vec3 normal_color;
    vec3 base_color;
};

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
    //float specular;
    float specularTint;
    float roughness;
    float anisotropic;
    float sheen;
    float sheenTint;
    float clearcoat;
    float clearcoatGloss;
    float IOR;
    float transmission;
    int alphaMode;
    int mediumtype;
    float mediumDensity;
    vec3 mediumColor;
    float mediumAnisotropy;

    float ax;
    float ay;
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
    float hitDistance;         // 与交点的距离
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

// 获取第 i 下标的三角形的材质
Material getMaterial(int i) {
    Material m;

    int offset = i * SIZE_TRIANGLE;
    vec4 param1 = texelFetch(triangles, offset + 6);
    vec4 param2 = texelFetch(triangles, offset + 7);
    vec4 param3 = texelFetch(triangles, offset + 8);
    vec4 param4 = texelFetch(triangles, offset + 9);
    vec4 param5 = texelFetch(triangles, offset + 10);
    vec4 param6 = texelFetch(triangles, offset + 11);
    
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

    m.mediumtype=int(param5.x);
    m.mediumDensity=param5.y;
    m.subsurface=param5.z;
    m.metallic=param5.w;

    m.specularTint=param6.x;
    m.roughness=max(param6.y,0.001);
    m.anisotropic=param6.z;
    m.sheen=param6.w;

    float aspect = sqrt(1.0 - m.anisotropic * 0.9);
    m.ax = max(0.001, m.roughness / aspect);
    m.ay = max(0.001, m.roughness * aspect);
    return m;
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
    res.hitDistance = INF;
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

                //if (abs(det) < 0.00001) 
                   // continue;

                vec3 tv = ray.startPoint - p1.xyz;
                vec3 qv = cross(tv, e0);

                vec4 uvt;
                uvt.x = dot(tv, pv);
                uvt.y = dot(ray.direction, qv);
                uvt.z = dot(e1, qv);
                uvt.xyz = uvt.xyz / det;
                uvt.w = 1.0 - uvt.x - uvt.y;
                
                if(uvt.z<0.00005)
                    continue;

                if (all(greaterThanEqual(uvt, vec4(0.0))) && uvt.z < res.hitDistance)
                {
                    res.isHit = true;
                    res.hitPoint = ray.startPoint + ray.direction * uvt.z;
                    res.hitDistance = uvt.z;
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
        vec3 H = SampleGGXVNDF(V, material.ax, material.ay, xi_1, xi_2);
        if (H.z < 0.0)
            H = -H;

        L = normalize(reflect(-V, H));
    }
    else if (xi_3 < cdf[3]) // Glass
    {
        vec3 H = SampleGGXVNDF(V, material.ax, material.ay, xi_1, xi_2);
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
vec3 SampleHG(vec3 V, float g, float r1, float r2)
{
    float cosTheta;

    if (abs(g) < 0.001)
        cosTheta = 1 - 2 * r2;
    else 
    {
        float sqrTerm = (1 - g * g) / (1 + g - 2 * g * r2);
        cosTheta = -(1 + g * g - sqrTerm * sqrTerm) / (2 * g);
    }

    float phi = r1 * TWO_PI;
    float sinTheta = clamp(sqrt(1.0 - (cosTheta * cosTheta)), 0.0, 1.0);
    float sinPhi = sin(phi);
    float cosPhi = cos(phi);

    vec3 v1, v2;
    Onb(V, v1, v2);

    return sinTheta * cosPhi * v1 + sinTheta * sinPhi * v2 + cosTheta * V;
}

float PhaseHG(float cosTheta, float g)
{
    float denom = 1 + g * g + 2 * g * cosTheta;
    return INV_4_PI * (1 - g * g) / (denom * sqrt(denom));
}
vec3 DisneyEval(vec3 V, vec3 N, vec3 L, in Material material,float eta,out float pdf)
{
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

        f += EvalMicrofacetReflection(material.ax,material.ay, V, L, H, mix(Cspec0, vec3(1.0), F), tmpPdf) * dielectricWt;
        pdf += tmpPdf * dielectricPr;
    }

    // Metallic Reflection
    if (metalPr > 0.0 && reflect)
    {
        // Tinted to base color
        vec3 F = mix(material.baseColor, vec3(1.0), SchlickFresnel(VDotH));

        f += EvalMicrofacetReflection(material.ax,material.ay, V, L, H, F, tmpPdf) * metalWt;
        pdf += tmpPdf * metalPr;
    }

    // Glass/Specular BSDF
    if (glassPr > 0.0)
    {
        // Dielectric fresnel (achromatic)
        float F = DielectricFresnel(VDotH, eta);

        if (reflect)
        {
            f += EvalMicrofacetReflection(material.ax,material.ay ,V, L, H, vec3(F), tmpPdf) * glassWt;
            pdf += tmpPdf * glassPr * F;
        }
        else
        {
            f += EvalMicrofacetRefraction(material.baseColor,material.ax,material.ay,eta, V, L, H, vec3(F), tmpPdf) * glassWt;
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

//{
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
//}

OutputColor pathTracingImportanceSampling(Ray r, int maxBounce) {
    OutputColor o_c;
    //vec3 Lo = vec3(0);      // 最终的颜色
    vec3 history = vec3(1); // 递归积累的颜色
    o_c.render_color=vec3(0);

    o_c.normal_color=vec3(0);//对于环境光贴图的位置设为0
    o_c.base_color=vec3(0);

    float pdf_brdf;
    float NdotL;
    vec3 f_r;

    bool inMedium = false;
    bool mediumSampled = false;
    bool surfaceScatter = false;

    bool log_normal=false;
    for(int bounce=0;; bounce++) {
        
         HitResult newHit = hitBVH(r);

         // 未命中   
         
        if(!newHit.isHit) {
            vec3 color = hdrColor(r.direction);
            //float pdf_light = hdrPdf(L, hdrResolution); 
            //float mis_weight = misMixWeight(pdf_brdf, pdf_light);   // f(a,b) = a^2 / (a^2 + b^2)
            float mis_weight = 1.0;   // f(a,b) = a^2 / (a^2 + b^2)
            //Lo += mis_weight * history * color;

            o_c.render_color+=mis_weight * history * color;
           
            break;
        }
        
        // 命中光源积累颜色  
       // Lo +=  history *newHit.material.emissive;

        o_c.render_color+=history *newHit.material.emissive;

       
        mediumSampled = false;
        surfaceScatter = false;

        if(inMedium)
        {        
            if(newHit.material.mediumtype== MEDIUM_ABSORB)
            {
                history *= exp(-(1.0 -newHit.material.mediumColor) * newHit.hitDistance * newHit.material.mediumDensity);
            }
            else if(newHit.material.mediumtype == MEDIUM_EMISSIVE)
            {
                vec3 light_st=newHit.material.mediumColor * newHit.hitDistance * newHit.material.mediumDensity * history;
                
                o_c.render_color+=light_st;
                
            
            }
            else
            {
                // Sample a distance in the medium
                float scatterDist = min(-log(rand()) / newHit.material.mediumDensity, newHit.hitDistance);
                mediumSampled = scatterDist < newHit.hitDistance;

                if (mediumSampled)
                {       
                    if(bounce == maxBounce)//将maxBounce放置在这里是为了maxBounce为1时正确处理透明效果
                        break;
                    history *= newHit.material.mediumColor;

                    // Move ray origin to scattering position
                    r.startPoint += r.direction * scatterDist;
                    //state.fhp = r.origin;

                    // Transmittance Evaluation
                    

                    // Pick a new direction based on the phase function
                    vec3 scatterDir = SampleHG(-r.direction, newHit.material.mediumAnisotropy, rand(), rand());

                    //这里计算一个虚拟法向
//                    if(bounce==0){     
//                        o_c.normal_color=normalize(normalize(scatterDir)-newHit.viewDir);
//                        o_c.normal_color=-newHit.viewDir;                        
//                        o_c.base_color=newHit.material.mediumColor;
//                        o_c.depth_point=newHit.hitPoint;
//                    }
                    //scatterSample.pdf = PhaseHG(dot(-r.direction, scatterDir), state.medium.anisotropy);//在体积散射多重重要性采样里使用
                    r.direction = scatterDir;
                }
            }

        }

        if (!mediumSampled)
        {
            if(newHit.material.alphaMode==ALPHA_MODE_TRANSPARENT){
                //如果是透明的直接穿透 并沿用之前的光线方向
                if(!log_normal){     
                    //o_c.normal_color=normalize(normalize(scatterDir)-newHit.viewDir);
                    o_c.normal_color=-newHit.viewDir;         
                    o_c.base_color=newHit.material.mediumColor;
                    log_normal=true;
                   // o_c.depth_point=newHit.hitPoint;
                }
                bounce--;
            }else{
            
                if(!log_normal){        
                    o_c.normal_color=newHit.normal;
                    o_c.base_color=newHit.material.baseColor;
                    log_normal=true;
                    //o_c.depth_point=newHit.hitPoint;
                 }
                if(bounce == maxBounce)//将maxBounce放置在这里是为了maxBounce为1时正确处理透明效果
                    break;
                surfaceScatter = true;
                vec2 uv;
                uv.x=sobelNumber[bounce*2];
                uv.y=sobelNumber[bounce*2+1];
                uv = CranleyPattersonRotation(uv);
                float xi_1 = uv.x;
                float xi_2 = uv.y; 
                float xi_3 = rand();   
                float eta =newHit.isInside ? newHit.material.IOR:(1.0 / newHit.material.IOR) ;
                vec3 V = -newHit.viewDir;
                vec3 N = newHit.normal;   
                // 采样 BRDF 得到一个方向 L
                vec3 L =  DisneySample(xi_1, xi_2, xi_3, V, N, newHit.material,eta); 
                NdotL =abs(dot(N, L));
                f_r = DisneyEval(V, N, L, newHit.material,eta,pdf_brdf);
                if(pdf_brdf <= 0.0) break;
                history *= f_r * NdotL / pdf_brdf;  // 累积颜色
                r.direction = L;
            }        
            r.startPoint = newHit.hitPoint;        

            //这里至少要单独存储 medium ，之后要引入体积栈
            if(!newHit.isInside &&
            dot(r.direction,newHit.normal)<0 &&
            newHit.material.mediumtype!=MEDIUM_NONE){
                inMedium = true;
            }
            else if(newHit.material.mediumtype!=MEDIUM_NONE){
                inMedium = false;
            }
        }


        // 加入俄罗斯轮盘赌 (关键位置)
        if (bounce >= 3) { // 前3次反弹不启用RR减少噪声
            float rrSurvivalProb = min(0.95, max(maxComponent(history), 0.05)); // 保持最小5%存活率
            if (rand() > rrSurvivalProb) break;     // 终止路径
            history /= rrSurvivalProb;              // 保持无偏
        }
    }
   // return Lo;
   return o_c;
}

void main(void)
{     
    Ray ray;
    ray.startPoint = eye;
   // ray.startPoint = vec3(0, 0, 4);

   
   // vec2 AA = vec2((rand()-0.5)/float(width), (rand()-0.5)/float(height));
    //vec2 AA = vec2(0);
    vec4 dir = view*vec4(pix.x*float(width) /float(height),pix.y, -2.0,0.0);//- ray.startPoint;    
    //vec4 dir = view*vec4(pix.x*float(width) /float(height)+AA.x,pix.y+AA.y, -2.0,0.0);//- ray.startPoint;
    ray.direction = normalize(dir.xyz);
    // primary hit  

    OutputColor color = pathTracingImportanceSampling(ray,6);

    

    // lastColor*=100.0; 
    //if(isnan(color.x)||isnan(color.x)||isnan(color.x))
       // color=lastColor;
   // else
       // color = mix(lastColor, color, 1.0/float(frameCounter+1u)); 

    
    //FragColor=vec4(color,1.0);
    
    RenderColorResult=vec4(color.render_color,1.0);
    NormalResult=vec4((color.normal_color+1.0)/2.0,0.0);
    BaseColorResult=vec4(color.base_color,1.0);
    

    // 计算混合因子    
    float alpha =1.0/(frameCounter+1.0);//该项控制累计帧数


    vec4 prevIllum= texture2D(preRenderColor, pix.xy*0.5+0.5);

    // 混合光照
    RenderColorResult = mix(prevIllum, RenderColorResult, alpha);


}