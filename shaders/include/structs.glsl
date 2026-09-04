struct OutputColor{
    vec3 render_color;
    vec3 normal_color;
    vec3 base_color;
};

// Triangle 数据格式
struct Triangle {
    vec3 p1, p2, p3;    // 顶点坐标
    vec3 n1, n2, n3;    // 顶点法线
    vec2 uv1, uv2, uv3; // UV0
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
    float opacity;
    float alphaCutoff;
    int mediumtype;
    float mediumDensity;
    vec3 mediumColor;
    float mediumAnisotropy;

    float ax;
    float ay;

    int normalTex;
    float normalScale;
    float normalMapFlipY;
};
// 光线
struct Ray {
    vec3 startPoint;
    vec3 direction;
};

struct HitResult {
    bool isHit;
    bool isInside;
    int triangleIndex;
    float hitDistance;
    vec3 hitPoint;
    vec3 normal;
    vec2 uv;
    vec3 viewDir;
    Material material;
};
