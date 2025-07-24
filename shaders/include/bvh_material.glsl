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