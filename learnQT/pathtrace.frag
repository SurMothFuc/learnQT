#version 330 core
#define SIZE_TRIANGLE   12
#define SIZE_BVHNODE    4
#define INF 114514.0
#define PI 3.14159265358979323
#define INV_PI 0.31830988618379067

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


const uint V[24*32] = uint[24*32](
    2147483648u, 1073741824u, 536870912u, 268435456u, 134217728u, 67108864u, 33554432u, 16777216u, 8388608u, 4194304u, 2097152u, 1048576u, 524288u, 262144u, 131072u, 65536u, 32768u, 16384u, 8192u, 4096u, 2048u, 1024u, 512u, 256u, 128u, 64u, 32u, 16u, 8u, 4u, 2u, 1u,
    2147483648u, 3221225472u, 2684354560u, 4026531840u, 2281701376u, 3422552064u, 2852126720u, 4278190080u, 2155872256u, 3233808384u, 2694840320u, 4042260480u, 2290614272u, 3435921408u, 2863267840u, 4294901760u, 2147516416u, 3221274624u, 2684395520u, 4026593280u, 2281736192u, 3422604288u, 2852170240u, 4278255360u, 2155905152u, 3233857728u, 2694881440u, 4042322160u, 2290649224u, 3435973836u, 2863311530u, 4294967295u,
    2147483648u, 3221225472u, 1610612736u, 2415919104u, 3892314112u, 1543503872u, 2382364672u, 3305111552u, 1753219072u, 2629828608u, 3999268864u, 1435500544u, 2154299392u, 3231449088u, 1626210304u, 2421489664u, 3900735488u, 1556135936u, 2388680704u, 3314585600u, 1751705600u, 2627492864u, 4008611328u, 1431684352u, 2147543168u, 3221249216u, 1610649184u, 2415969680u, 3892340840u, 1543543964u, 2382425838u, 3305133397u,
    2147483648u, 3221225472u, 536870912u, 1342177280u, 4160749568u, 1946157056u, 2717908992u, 2466250752u, 3632267264u, 624951296u, 1507852288u, 3872391168u, 2013790208u, 3020685312u, 2181169152u, 3271884800u, 546275328u, 1363623936u, 4226424832u, 1977167872u, 2693105664u, 2437829632u, 3689389568u, 635137280u, 1484783744u, 3846176960u, 2044723232u, 3067084880u, 2148008184u, 3222012020u, 537002146u, 1342505107u,
    2147483648u, 1073741824u, 536870912u, 2952790016u, 4160749568u, 3690987520u, 2046820352u, 2634022912u, 1518338048u, 801112064u, 2707423232u, 4038066176u, 3666345984u, 1875116032u, 2170683392u, 1085997056u, 579305472u, 3016343552u, 4217741312u, 3719483392u, 2013407232u, 2617981952u, 1510979072u, 755882752u, 2726789248u, 4090085440u, 3680870432u, 1840435376u, 2147625208u, 1074478300u, 537900666u, 2953698205u,
    2147483648u, 1073741824u, 1610612736u, 805306368u, 3355443200u, 603979776u, 1442840576u, 4211081216u, 3766484992u, 1883242496u, 2824863744u, 338690048u, 2663907328u, 3743678464u, 3067478016u, 2344288256u, 1207992320u, 1677737984u, 905994240u, 3405787136u, 679528448u, 1413489664u, 4267726336u, 4012964608u, 2118705280u, 2942595136u, 515287136u, 2676692016u, 3603439304u, 3139739428u, 2161563350u, 1086045115u,
    2147483648u, 3221225472u, 2684354560u, 3489660928u, 1476395008u, 2483027968u, 1040187392u, 3808428032u, 3196059648u, 599785472u, 505413632u, 4077912064u, 1182269440u, 1736704000u, 2017853440u, 2221342720u, 3329785856u, 2810494976u, 3628507136u, 1416089600u, 2658719744u, 864310272u, 3863387648u, 3076993792u, 553150080u, 272922560u, 4167467040u, 1148698640u, 1719673080u, 2009075780u, 2149644390u, 3222291575u,
    2147483648u, 1073741824u, 2684354560u, 1342177280u, 2281701376u, 603979776u, 301989888u, 754974720u, 1988100096u, 2654994432u, 136314880u, 1678770176u, 2988965888u, 2098462720u, 4272029696u, 3125346304u, 438599680u, 1226522624u, 3300237312u, 3816001536u, 4135585792u, 3728737280u, 2820672000u, 873465088u, 975702144u, 1494483520u, 3970040096u, 2538144464u, 1822721896u, 3613084132u, 3432358018u, 2271450689u,
    2147483648u, 1073741824u, 2684354560u, 1342177280u, 671088640u, 3556769792u, 1778384896u, 1895825408u, 947912704u, 1480589312u, 3927965696u, 823132160u, 2561146880u, 139722752u, 3257532416u, 3844407296u, 4071784448u, 2034778112u, 4205060096u, 3178434560u, 413665280u, 1213465600u, 1646922240u, 3039102208u, 3669131904u, 2907196736u, 2426676896u, 3430094608u, 539495304u, 269746564u, 2282357922u, 2218067473u,
    2147483648u, 1073741824u, 3758096384u, 2952790016u, 2550136832u, 2483027968u, 2315255808u, 1526726656u, 864026624u, 3653238784u, 1914699776u, 1058013184u, 3250061312u, 2800484352u, 1401290752u, 703922176u, 171606016u, 455786496u, 3549618176u, 1778348032u, 3929540608u, 2871788544u, 1269173760u, 4259646208u, 1610779008u, 4026976576u, 2016733344u, 605713840u, 305826616u, 3475687836u, 3113412898u, 2197780721u,
    2147483648u, 1073741824u, 2684354560u, 268435456u, 134217728u, 1811939328u, 2650800128u, 587202560u, 1468006400u, 2915041280u, 2141192192u, 2446327808u, 1233649664u, 3470000128u, 2282356736u, 739180544u, 1041072128u, 857194496u, 1605394432u, 3254300672u, 3784148992u, 3000484864u, 504392192u, 1663611136u, 4152723584u, 3183723200u, 2008703968u, 4260868912u, 3615493624u, 3988785180u, 3751805978u, 2177894957u,
    2147483648u, 1073741824u, 536870912u, 805306368u, 1476395008u, 2885681152u, 2516582400u, 721420288u, 3565158400u, 155189248u, 3802136576u, 1380974592u, 1311244288u, 3340500992u, 1654521856u, 308740096u, 1846771712u, 4147232768u, 983080960u, 3192164352u, 4164651008u, 3693986816u, 3993412096u, 3072561920u, 447221120u, 2388397760u, 2688420704u, 1882653104u, 2017167560u, 2620246612u, 3456542538u, 2267256725u,
    2147483648u, 3221225472u, 2684354560u, 1342177280u, 4160749568u, 2348810240u, 3791650816u, 855638016u, 260046848u, 557842432u, 2510290944u, 1584398336u, 3624402944u, 472121344u, 3122003968u, 4013359104u, 361136128u, 2658123776u, 2015059968u, 1278513152u, 1108248576u, 1661717504u, 4155337216u, 2910033152u, 2004879232u, 1832912064u, 3617588256u, 1030751792u, 797446008u, 2976123604u, 3451258746u, 2185692887u,
    2147483648u, 3221225472u, 1610612736u, 2415919104u, 939524096u, 3288334336u, 1107296256u, 2734686208u, 4051697664u, 2856321024u, 4242538496u, 2232418304u, 3758620672u, 1342963712u, 1476788224u, 1409875968u, 2047049728u, 1728856064u, 3011780608u, 155856896u, 225384448u, 794469376u, 484953600u, 3574878464u, 3087007872u, 67109056u, 570425440u, 855638160u, 3380609080u, 1849688260u, 3202351170u, 638582947u,
    2147483648u, 1073741824u, 536870912u, 4026531840u, 2818572288u, 1409286144u, 2583691264u, 2634022912u, 511705088u, 1556086784u, 2099249152u, 2366636032u, 612892672u, 1908670464u, 3953262592u, 1977548800u, 1805811712u, 902905856u, 1269014528u, 3318927360u, 3819005952u, 2447084544u, 2041508352u, 215957760u, 1730838656u, 1343569984u, 436314144u, 3708670192u, 1048832168u, 2898971732u, 3576517274u, 3642593693u,
    2147483648u, 3221225472u, 536870912u, 3489660928u, 3623878656u, 3288334336u, 1174405120u, 2231369728u, 2776629248u, 1992294400u, 2912944128u, 1789919232u, 765984768u, 2864447488u, 229244928u, 2058420224u, 3584524288u, 3200073728u, 2476990464u, 1001721856u, 908703744u, 1299348480u, 2609078784u, 667211520u, 3056187520u, 2373090496u, 3145949728u, 4156872656u, 1848227928u, 1232239620u, 4253246054u, 1925502805u,
    2147483648u, 1073741824u, 536870912u, 4026531840u, 939524096u, 335544320u, 4127195136u, 1728053248u, 2407530496u, 1346371584u, 2325741568u, 267386880u, 312999936u, 2884894720u, 4238999552u, 687538176u, 3173613568u, 196755456u, 1309073408u, 856436736u, 1501960192u, 3343725568u, 1026339328u, 1270008576u, 1845893248u, 3272422464u, 1636610592u, 3544370160u, 3408795832u, 750335060u, 3783701206u, 2471086999u,
    2147483648u, 3221225472u, 536870912u, 4026531840u, 1744830464u, 1677721600u, 905969664u, 1828716544u, 1098907648u, 3762290688u, 3537895424u, 2616197120u, 216530944u, 1392246784u, 1533673472u, 800260096u, 2685173760u, 805650432u, 1208475648u, 2484047872u, 1577187328u, 151950336u, 2005554688u, 2369874688u, 2473195648u, 2075301056u, 3724564000u, 3372379120u, 1468889320u, 2102121636u, 4218271254u, 532609949u,
    2147483648u, 1073741824u, 2684354560u, 1342177280u, 2550136832u, 4093640704u, 2919235584u, 3137339392u, 3883925504u, 2512388096u, 471859200u, 3492806656u, 3685220352u, 1442054144u, 4286709760u, 566296576u, 304316416u, 993673216u, 2754306048u, 875622400u, 1302763520u, 1257499648u, 772028928u, 4211744512u, 1199904896u, 3318328384u, 2217695904u, 607842128u, 1973649432u, 4009377972u, 403398670u, 3019960555u,
    2147483648u, 3221225472u, 3758096384u, 2952790016u, 3087007744u, 1006632960u, 3456106496u, 1090519040u, 562036736u, 1371537408u, 157286400u, 2238709760u, 4067950592u, 2392588288u, 1610743808u, 1879244800u, 1476624384u, 2348990464u, 1979899904u, 2097213440u, 4018354176u, 281084928u, 685803008u, 3568387840u, 4212663680u, 200152512u, 2457455072u, 4271716976u, 939524104u, 4227858444u, 771751950u, 4043309067u,
    2147483648u, 3221225472u, 3758096384u, 3489660928u, 1744830464u, 1006632960u, 2315255808u, 1358954496u, 2843738112u, 3720347648u, 1537212416u, 969932800u, 2516058112u, 1456734208u, 167903232u, 2432892928u, 1233354752u, 230899712u, 866230272u, 97579008u, 536487936u, 131417088u, 2743117312u, 1287681792u, 304279168u, 873703232u, 2791045088u, 1392880464u, 368574472u, 2530476044u, 3925999630u, 1090715661u,
    2147483648u, 1073741824u, 1610612736u, 3489660928u, 939524096u, 2348810240u, 2113929216u, 1895825408u, 3363831808u, 79691776u, 463470592u, 3144679424u, 1251475456u, 3283877888u, 2785148928u, 1828782080u, 4001464320u, 700661760u, 2501959680u, 1118973952u, 3887724544u, 219005952u, 1069097472u, 286069504u, 431746688u, 1007463872u, 2537179744u, 3318710000u, 974651400u, 192675844u, 2745303046u, 2003894285u,
    2147483648u, 3221225472u, 2684354560u, 2415919104u, 134217728u, 1677721600u, 1778384896u, 2298478592u, 2776629248u, 3409969152u, 404750336u, 2911895552u, 2944925696u, 1928593408u, 629276672u, 188940288u, 3089268736u, 1032994816u, 2810716160u, 385191936u, 1334028288u, 2185307136u, 497030656u, 4140920064u, 3215474816u, 3144099392u, 3758691872u, 4038389712u, 941785096u, 4254220300u, 126361610u, 2264240137u,
    2147483648u, 3221225472u, 536870912u, 3489660928u, 1207959552u, 2348810240u, 3590324224u, 956301312u, 3581935616u, 843055104u, 2996830208u, 1913651200u, 1406664704u, 2194407424u, 3414294528u, 1195573248u, 2434826240u, 2840805376u, 2096701440u, 1318989824u, 4244199424u, 2392843264u, 3707360768u, 1587316992u, 2499373696u, 3533682752u, 1123661664u, 3952910128u, 2541256712u, 3655286796u, 635117570u, 2882351117u
);
// 格林码 
uint grayCode(uint i) {
	return i ^ (i>>1);
}

// 生成第 d 维度的第 i 个 sobol 数
float sobol(uint d, uint i) {
    uint result = uint(0);
    uint offset = d * uint(32);
    for(uint j = uint(0); i!=uint(0); i >>= 1, j++) 
        if((i & uint(1))!=uint(0))
            result ^= V[j+offset];

    return float(result) * (1.0f/float(0xFFFFFFFFU));
}
vec2 sobolVec2(uint i, uint b) {
    float u = sobol(b*uint(2), grayCode(i));
    float v = sobol(b*uint(2)+uint(1), grayCode(i));
    return vec2(u, v);
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
// ----------------------------------------------------------------------------- //

// 半球均匀采样
vec3 SampleHemisphere(float xi_1, float xi_2) {
    float z = xi_1;
    float r = max(0, sqrt(1.0 - z*z));
    float phi = 2.0 * PI * xi_2;
    return vec3(r * cos(phi), r * sin(phi), z);
}
// 将向量 v 投影到 N 的法向半球
vec3 toNormalHemisphere(vec3 v, vec3 N) {
    vec3 helper = vec3(1, 0, 0);
    if(abs(N.x)>0.999) helper = vec3(0, 0, 1);
    vec3 tangent = normalize(cross(N, helper));
    vec3 bitangent = normalize(cross(N, tangent));
    return v.x * tangent + v.y * bitangent + v.z * N;
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

    vec3 S = ray.startPoint;    // 射线起点
    vec3 d = ray.direction;     // 射线方向
    vec3 N = normalize(cross(p2-p1, p3-p1));    // 法向量

    // 从三角形背后（模型内部）击中
    if (dot(N, d) > 0.0f) {
        N = -N;   
        res.isInside = true;
    }

    // 如果视线和三角形平行
    if (abs(dot(N, d)) < 0.00001f) return res;

    // 距离
    float t = (dot(N, p1) - dot(S, N)) / dot(d, N);
    if (t < 0.0005f) return res;    // 如果三角形在光线背面

    // 交点计算
    vec3 P = S + d * t;

    // 判断交点是否在三角形中
    vec3 c1 = cross(p2 - p1, P - p1);
    vec3 c2 = cross(p3 - p2, P - p2);
    vec3 c3 = cross(p1 - p3, P - p3);
    bool r1 = (dot(c1, N) > 0 && dot(c2, N) > 0 && dot(c3, N) > 0);
    bool r2 = (dot(c1, N) < 0 && dot(c2, N) < 0 && dot(c3, N) < 0);

    // 命中，封装返回结果
    if (r1 || r2) {
        res.isHit = true;
        res.hitPoint = P;
        res.distance = t;
        res.normal = N;
        res.viewDir = d;
        // 根据交点位置插值顶点法线
        //float alpha = (-(P.x-p2.x)*(p3.y-p2.y) + (P.y-p2.y)*(p3.x-p2.x)) / (-(p1.x-p2.x-0.00005)*(p3.y-p2.y+0.00005) + (p1.y-p2.y+0.00005)*(p3.x-p2.x+0.00005));
       // float beta  = (-(P.x-p3.x)*(p1.y-p3.y) + (P.y-p3.y)*(p1.x-p3.x)) / (-(p2.x-p3.x-0.00005)*(p1.y-p3.y+0.00005) + (p2.y-p3.y+0.00005)*(p1.x-p3.x+0.00005));
       
       // float alpha = (-(P.x-p2.x)*(p3.y-p2.y) + (P.y-p2.y)*(p3.x-p2.x)) / (-(p1.x-p2.x)*(p3.y-p2.y) + (p1.y-p2.y)*(p3.x-p2.x)+1e-7);
       // float beta  = (-(P.x-p3.x)*(p1.y-p3.y) + (P.y-p3.y)*(p1.x-p3.x)) / (-(p2.x-p3.x)*(p1.y-p3.y) + (p2.y-p3.y)*(p1.x-p3.x)+1e-7);
        
        //float gama  = 1.0 - alpha - beta;

        // 选择最大投影轴 (防止退化三角形导致的分母接近零)
        vec3 absNormal = abs(N);
        int maxAxis = (absNormal.x > absNormal.y) ? 
                     ((absNormal.x > absNormal.z) ? 0 : 2) :
                     ((absNormal.y > absNormal.z) ? 1 : 2);

        // 投影顶点到选定平面
        vec2 p1_proj, p2_proj, p3_proj, P_proj;
        if (maxAxis == 0) { // 投影到 YZ 平面
            p1_proj = p1.yz; p2_proj = p2.yz; p3_proj = p3.yz; P_proj = P.yz;
        } else if (maxAxis == 1) { // 投影到 XZ 平面
            p1_proj = p1.xz; p2_proj = p2.xz; p3_proj = p3.xz; P_proj = P.xz;
        } else { // 投影到 XY 平面
            p1_proj = p1.xy; p2_proj = p2.xy; p3_proj = p3.xy; P_proj = P.xy;
        }
        
        float alpha = abs(-( P_proj.x-p2_proj.x)*(p3_proj.y-p2_proj.y) + ( P_proj.y-p2_proj.y)*(p3_proj.x-p2_proj.x)) 
        / (abs(-(p1_proj.x-p2_proj.x)*(p3_proj.y-p2_proj.y) + (p1_proj.y-p2_proj.y)*(p3_proj.x-p2_proj.x))+1e-7);
        float beta  = abs(-( P_proj.x-p3_proj.x)*(p1_proj.y-p3_proj.y) + ( P_proj.y-p3_proj.y)*(p1_proj.x-p3_proj.x)) 
        / (abs(-(p2_proj.x-p3_proj.x)*(p1_proj.y-p3_proj.y) + (p2_proj.y-p3_proj.y)*(p1_proj.x-p3_proj.x))+1e-7);
        
        float gamma  = 1.0 - alpha - beta;
        vec3 Nsmooth = alpha * triangle.n1 + beta * triangle.n2 + gamma * triangle.n3;

        const float EPS = 1e-6;
        if (length(Nsmooth) < EPS) {
            Nsmooth = N; // 防止接近零向量导致溢出
        }

        Nsmooth = normalize(Nsmooth);     
        //Nsmooth = N;     
        res.normal = (res.isInside) ? (-Nsmooth) : (Nsmooth);
        //res.normal = Nsmooth;
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

    // 栈
    int stack[256];
    int sp = 0;

    stack[sp++] = 1;
    while(sp>0) {
        int top = stack[--sp];
        BVHNode node = getBVHNode(top);
        
        // 是叶子节点，遍历三角形，求最近交点
        if(node.n>0) {
            int L = node.index;
            int R = node.index + node.n - 1;
            HitResult r = hitArray(ray, L, R);
            if(r.isHit && r.distance<res.distance) res = r;
            continue;
        }
        
        // 和左右盒子 AABB 求交
        float d1 = INF; // 左盒子距离
        float d2 = INF; // 右盒子距离
        if(node.left>0) {
            BVHNode leftNode = getBVHNode(node.left);
            d1 = hitAABB(ray, leftNode.AA, leftNode.BB);
        }
        if(node.right>0) {
            BVHNode rightNode = getBVHNode(node.right);
            d2 = hitAABB(ray, rightNode.AA, rightNode.BB);
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

    return res;
}

// 将三维向量 v 转为 HDR map 的纹理坐标 uv
vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv /= vec2(2.0 * PI, PI);
    uv += 0.5;
    uv.y = 1.0 - uv.y;
    return uv;
}
float misMixWeight(float a, float b) {
    float t = a * a;
    return t / (b*b + t);
}
// 余弦加权的法向半球采样
vec3 SampleCosineHemisphere(float xi_1, float xi_2, vec3 N) {
    // 均匀采样 xy 圆盘然后投影到 z 半球
    float r = sqrt(xi_1);
    float theta = xi_2 * 2.0 * PI;
    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(max(0.0,1.0 - x*x - y*y));

    // 从 z 半球投影到法向半球
    vec3 L = toNormalHemisphere(vec3(x, y, z), N);
    return L;
}


void getTangent(vec3 N, inout vec3 tangent, inout vec3 bitangent) {
    /*
    vec3 helper = vec3(0, 0, 1);
    if(abs(N.z)>0.999) helper = vec3(0, -1, 0);
    tangent = normalize(cross(N, helper));
    bitangent = normalize(cross(N, tangent));
    */
    vec3 helper = vec3(1, 0, 0);
    if(abs(N.x)>0.999) helper = vec3(0, 0, 1);
    bitangent = normalize(cross(N, helper));
    tangent = normalize(cross(N, bitangent));
}
float sqr(float x) { 
    return x*x; 
}


float SchlickFresnel(float u) {
    float m = clamp(1-u, 0.0, 1.0);
    float m2 = m*m;
    return m2*m2*m; // pow(m,5)
}

float GTR1(float NdotH, float a) {
    if (a >= 1) return 1/PI;
    float a2 = a*a;
    float t = 1 + (a2-1)*NdotH*NdotH;
    return (a2-1) / (PI*log(a2)*t);
}

float GTR2(float NdotH, float a) {
    float a2 = a*a;
    float t = 1 + (a2-1)*NdotH*NdotH;
    return a2 / (PI * t*t);
}
float smithG_GGX(float NdotV, float alphaG) {
    float a = alphaG*alphaG;
    float b = NdotV*NdotV;
    return 1 / (NdotV + sqrt(a + b - a*b));
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
    vec2 uv = toSphericalCoord(normalize(L));
    vec3 color = texture2D(hdrMap, uv).rgb;
    return color;
}

// GTR2 重要性采样
vec3 SampleGTR2(float xi_1, float xi_2, vec3 V, vec3 N, float alpha) {
    
    float phi_h = 2.0 * PI * xi_1;
    float sin_phi_h = sin(phi_h);
    float cos_phi_h = cos(phi_h);

    float cos_theta_h = sqrt((1.0-xi_2)/(1.0+(alpha*alpha-1.0)*xi_2));
    float sin_theta_h = sqrt(max(0.0, 1.0 - cos_theta_h * cos_theta_h));

    // 采样 "微平面" 的法向量 作为镜面反射的半角向量 h 
    vec3 H = vec3(sin_theta_h*cos_phi_h, sin_theta_h*sin_phi_h, cos_theta_h);
    H = toNormalHemisphere(H, N);   // 投影到真正的法向半球

    // 根据 "微法线" 计算反射光方向
    vec3 L = reflect(-V, H);

    return L;
}
// GTR1 重要性采样
vec3 SampleGTR1(float xi_1, float xi_2, vec3 V, vec3 N, float alpha) {
    
    float phi_h = 2.0 * PI * xi_1;
    float sin_phi_h = sin(phi_h);
    float cos_phi_h = cos(phi_h);

    float cos_theta_h = sqrt((1.0-pow(alpha*alpha, 1.0-xi_2))/(1.0-alpha*alpha));
    float sin_theta_h = sqrt(max(0.0, 1.0 - cos_theta_h * cos_theta_h));

    // 采样 "微平面" 的法向量 作为镜面反射的半角向量 h 
    vec3 H = vec3(sin_theta_h*cos_phi_h, sin_theta_h*sin_phi_h, cos_theta_h);
    H = toNormalHemisphere(H, N);   // 投影到真正的法向半球

    // 根据 "微法线" 计算反射光方向
    vec3 L = reflect(-V, H);

    return L;
}
vec3 Cal_BRDF(vec3 V, vec3 N, vec3 L, in Material material,out float pdf){
    float NdotL = dot(N, L);
    float NdotV = dot(N, V);
    if(NdotL < 0 || NdotV < 0){
       pdf=0.0;
       return vec3(0);
    }

    vec3 H = normalize(L + V);
    float NdotH = dot(N, H);
    float LdotH = dot(L, H);

    // 各种颜色
    vec3 Cdlin = material.baseColor;
    float Cdlum = 0.212671 * Cdlin.r + 0.715160 * Cdlin.g  + 0.072169 * Cdlin.b;
    vec3 Ctint = (Cdlum > 0) ? (Cdlin/Cdlum) : (vec3(1));   
    vec3 Cspec = material.specular * mix(vec3(1), Ctint, material.specularTint);
    vec3 Cspec0 = mix(0.08*Cspec, Cdlin, material.metallic); // 0° 镜面反射颜色
    vec3 Csheen = mix(vec3(1), Ctint, material.sheenTint);   // 织物颜色

    // 漫反射
    float Fd90 = 0.5 + 2.0 * LdotH * LdotH * material.roughness;
    float FL = SchlickFresnel(NdotL);
    float FV = SchlickFresnel(NdotV);
    float Fd = mix(1.0, Fd90, FL) * mix(1.0, Fd90, FV);

    // 次表面散射
    float Fss90 = LdotH * LdotH * material.roughness;
    float Fss = mix(1.0, Fss90, FL) * mix(1.0, Fss90, FV);
    float ss = 1.25 * (Fss * (1.0 / (NdotL + NdotV) - 0.5) + 0.5);

    // 镜面反射 -- 各向同性
    float alpha = max(0.001, sqr(material.roughness));
    float Ds = GTR2(NdotH, alpha);
    float FH = SchlickFresnel(LdotH);
    vec3 Fs = mix(Cspec0, vec3(1), FH);
    float Gs = smithG_GGX(NdotL, material.roughness);
    Gs *= smithG_GGX(NdotV, material.roughness);

    // 清漆
    float Dr = GTR1(NdotH, mix(0.1, 0.001, material.clearcoatGloss));
    float Fr = mix(0.04, 1.0, FH);
    float Gr = smithG_GGX(NdotL, 0.25) * smithG_GGX(NdotV, 0.25);

    // sheen
    vec3 Fsheen = FH * material.sheen * Csheen;

    /// 计算pdf
    // 分别计算三种 BRDF 的概率密度
    float pdf_diffuse = NdotL / PI;
    float pdf_specular = Ds * NdotH / (4.0 * LdotH);
    float pdf_clearcoat = Dr * NdotH / (4.0 * LdotH);

    // 辐射度统计
    float r_diffuse = (1.0 - material.metallic);
    float r_specular = 1.0;
    float r_clearcoat = 0.25 * material.clearcoat;
    float r_sum = r_diffuse + r_specular + r_clearcoat;
    // 根据辐射度计算选择某种采样方式的概率
    float p_diffuse = r_diffuse / r_sum;
    float p_specular = r_specular / r_sum;
    float p_clearcoat = r_clearcoat / r_sum;
    // 根据概率混合 pdf
    pdf = p_diffuse   * pdf_diffuse 
          + p_specular  * pdf_specular
          + p_clearcoat * pdf_clearcoat;
    pdf = max(1e-10, pdf);

    ///计算brdf
    vec3 diffuse = (1.0/PI) * mix(Fd, ss, material.subsurface) * Cdlin + Fsheen;
    vec3 specular = Gs * Fs * Ds;
    vec3 clearcoat = vec3(0.25 * Gr * Fr * Dr * material.clearcoat);
    return diffuse * (1.0 - material.metallic) + specular + clearcoat;
}

float Luminance(vec3 c)
{
    return 0.212671 * c.x + 0.715160 * c.y + 0.072169 * c.z;
}



// 按照辐射度分布分别采样三种 BRDF
vec3 SampleBRDF(float xi_1, float xi_2, float xi_3, vec3 V, vec3 N, in Material material) {
    float alpha_GTR1 = mix(0.1, 0.001, material.clearcoatGloss);
    float alpha_GTR2 = max(0.001, sqr(material.roughness));
    
    // 辐射度统计
    float r_diffuse = (1.0 - material.metallic);
    float r_specular = 1.0;
    float r_clearcoat = 0.25 * material.clearcoat;
    float r_sum = r_diffuse + r_specular + r_clearcoat;

    // 根据辐射度计算概率
    float p_diffuse = r_diffuse / r_sum;
    float p_specular = r_specular / r_sum;
    float p_clearcoat = r_clearcoat / r_sum;

    // 按照概率采样
    float rd = xi_3;

    // 漫反射
    if(rd <= p_diffuse) {
        return SampleCosineHemisphere(xi_1, xi_2, N);
    } 
    // 镜面反射
    else if(p_diffuse < rd && rd <= p_diffuse + p_specular) {    
        return SampleGTR2(xi_1, xi_2, V, N, alpha_GTR2);
    } 
    // 清漆
    else if(p_diffuse + p_specular < rd) {
        return SampleGTR1(xi_1, xi_2, V, N, alpha_GTR1);
    }
    return vec3(0, 1, 0);
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


vec3 DisneySample(float xi_1, float xi_2, float xi_3, vec3 V, vec3 N, in Material material)
{
    float eta=dot(-V, N) < 0.0 ? (1.0 / material.IOR) :material.IOR;
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
vec3 DisneyEval(vec3 V, vec3 N, vec3 L, in Material material,out float pdf)
{
    float eta=dot(-V, N) < 0.0 ? (1.0 / material.IOR) :material.IOR;
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

    bool inMedium=false;//判断是否在介质内
    for(int bounce=0; bounce<maxBounce; bounce++) {
        vec3 V = -hit.viewDir;
        vec3 N = hit.normal;   


        // HDR 环境贴图重要性采样    
        if(!inMedium){
            Ray hdrTestRay;
            hdrTestRay.startPoint = hit.hitPoint;
            hdrTestRay.direction = SampleHdr(rand(), rand());

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
        }
        
        
        // 获取 3 个随机数
        vec2 uv = sobolVec2(frameCounter+1u, uint(bounce));
        uv = CranleyPattersonRotation(uv);
        float xi_1 = uv.x;
        float xi_2 = uv.y; 
//        float xi_1 = rand();
//        float xi_2 = rand();
        float xi_3 = rand();    // xi_3 是决定采样的随机数, 朴素 rand 就好

        // 采样 BRDF 得到一个方向 L
        //vec3 L = SampleBRDF(xi_1, xi_2, xi_3, V, N, hit.material); 
        vec3 L =  DisneySample(xi_1, xi_2, xi_3, V, N, hit.material); 
        float NdotL =abs(dot(N, L));//有折射情况 取绝对值
       // if(NdotL <= 0.0) break;

        // 发射光线
        Ray randomRay;
        randomRay.startPoint = hit.hitPoint;
        randomRay.direction = L;
        HitResult newHit = hitBVH(randomRay);



        // 获取 L 方向上的 BRDF 值和概率密度
        float pdf_brdf=0.0;
        vec3 f_r = DisneyEval(V, inMedium?-N:N, L, hit.material,pdf_brdf);        
        if(dot(N, L)<0.0){
            //射入或者出射的情况
            inMedium=!inMedium;
        }
        if(pdf_brdf <= 0.0) break;

       // 未命中        
        if(!newHit.isHit) {
            vec3 color = hdrColor(L);
            float pdf_light = hdrPdf(L, hdrResolution); 

            float mis_weight = misMixWeight(pdf_brdf, pdf_light);   // f(a,b) = a^2 / (a^2 + b^2)
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
            color = vec3(0);
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

    

    //color/=100.0;
    //color=(1-1.0/(frameCounter+1))*lastColor+1.0/(frameCounter+1)*color;
    
    //else
       // color = vec3(0.0,0.0,0.0);


//    vec3 color; 
//    vec3 lastColor = texture2D(lastFrame, pix.xy*0.5+0.5).rgb;
//    color = lastColor+ color;
//    if(abs(pix.x-(rand()*2-1))<0.01&&abs(pix.y-(rand()*2-1))<0.01){
//        color=vec3(1,0,0);
//    }
    
    /*vec3 lastColor = texture2D(lastFrame, pix.xy*0.5+0.5).rgb;
    color = mix(lastColor, color, 1.0/float(frameCounter+1));
    vec2 point=vec2(pix.x,pix.y);
    if(abs(float(1.0*(frameCounter%(width*height)/width)/height*2-1)-point.x)<0.005 &&abs(float(1.0*(frameCounter%width)/width*2-1)-point.y)<0.005)
        color=vec3(1.0,0.0,0.0);*/

     FragColor=vec4(color,1.0);
//     FragColor=vec4(vec3(texture2D(hdrMap,pix.xy*0.5+0.5)),1.0);
//     for(int i=0;i<10000;i++){
//        vec2 xy = texture2D(hdrCache, sobolVec2(uint(i),0u)).rg; // x, y
//        if(abs((pix.xy*0.5+0.5).x-xy.x)<0.001 && abs((pix.xy*0.5+0.5).y-xy.y)<0.001){
//            FragColor=vec4(1.0,0.0,0.0,1.0);
//        }
//     }
     
    // gl_FragData[0] = vec4(color, 1.0);
}