float AxisSelect(vec3 v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }
// Ray-aligned projection gives shared vertices identical coordinates on both
// triangles. Prepare the projection once per ray/instance, not per triangle.
struct TriangleRay {
    vec3 origin;
    ivec3 axes;
    vec3 shear;
};

TriangleRay PrepareTriangleRay(Ray ray) {
    vec3 ad = abs(ray.direction);
    int z = ad.x > ad.y ? (ad.x > ad.z ? 0 : 2) : (ad.y > ad.z ? 1 : 2);
    int x = (z + 1) % 3, y = (x + 1) % 3;
    if (ray.direction[z] < 0.0) { int swapAxis = x; x = y; y = swapAxis; }
    TriangleRay result;
    result.origin = ray.startPoint;
    result.axes = ivec3(x, y, z);
    result.shear = ray.direction[z] == 0.0 ? vec3(0.0) :
        vec3(ray.direction[x] / ray.direction[z], ray.direction[y] / ray.direction[z],
             1.0 / ray.direction[z]);
    return result;
}

float TriangleEdge(vec2 a, vec2 b) {
    // Canonical operand order makes a reversed shared edge exactly opposite,
    // including on GLSL 3.3 drivers that contract multiply/subtract operations.
    bool forward = a.x < b.x || (a.x == b.x && a.y < b.y);
    vec2 lo = forward ? a : b, hi = forward ? b : a;
    float edge = lo.x * hi.y - lo.y * hi.x;
    return forward ? edge : -edge;
}

bool IntersectTriangle(TriangleRay ray, vec3 a, vec3 b, vec3 c,
                       out vec3 bary, out float distance) {
    vec3 pa = (a - ray.origin), pb = (b - ray.origin), pc = (c - ray.origin);
    int x = ray.axes.x, y = ray.axes.y, z = ray.axes.z;
    vec2 aa = vec2(AxisSelect(pa, x) - ray.shear.x * AxisSelect(pa, z), AxisSelect(pa, y) - ray.shear.y * AxisSelect(pa, z));
    vec2 bb = vec2(AxisSelect(pb, x) - ray.shear.x * AxisSelect(pb, z), AxisSelect(pb, y) - ray.shear.y * AxisSelect(pb, z));
    vec2 cc = vec2(AxisSelect(pc, x) - ray.shear.x * AxisSelect(pc, z), AxisSelect(pc, y) - ray.shear.y * AxisSelect(pc, z));
    vec3 edges = vec3(TriangleEdge(bb, cc), TriangleEdge(cc, aa), TriangleEdge(aa, bb));
    if (min(edges.x, min(edges.y, edges.z)) < 0.0 &&
        max(edges.x, max(edges.y, edges.z)) > 0.0) return false;
    float determinant = edges.x + edges.y + edges.z;
    if (determinant == 0.0) return false;
    vec3 projectedZ = vec3(AxisSelect(pa, z), AxisSelect(pb, z), AxisSelect(pc, z));
    vec3 depth = projectedZ * ray.shear.z;
    distance = dot(edges, depth) / determinant;
    bary = edges / determinant;
    // Do not re-test bary.y+bary.z <= 1: rounding could reject an accepted edge.
    // Bound the rounding in this ray-aligned projection (pbrt's triangle t bound).
    // A point exactly on a plane can otherwise produce a small positive t after
    // cancellation for an oblique secondary ray. This scales with the operands,
    // rather than imposing a scene-wide near-distance epsilon.
    float maxZ=maxComponent(abs(depth));
    float maxX=max(abs(aa.x),max(abs(bb.x),abs(cc.x)));
    float maxY=max(abs(aa.y),max(abs(bb.y),abs(cc.y)));
    float deltaZ=FloatGamma(3.0)*maxZ;
    // Instance rays deliberately retain their non-unit local direction so t
    // stays in world units. Shear-coordinate error uses unscaled vertex z;
    // using depth here would mix local coordinates with world ray distance.
    float maxProjectedZ=maxComponent(abs(projectedZ));
    float deltaX=FloatGamma(5.0)*(maxX+maxProjectedZ);
    float deltaY=FloatGamma(5.0)*(maxY+maxProjectedZ);
    float deltaE=2.0*(FloatGamma(2.0)*maxX*maxY+deltaY*maxX+deltaX*maxY);
    float maxE=maxComponent(abs(edges));
    float deltaT=3.0*(FloatGamma(3.0)*maxE*maxZ+deltaE*maxZ+deltaZ*maxE)/abs(determinant);
    // A robustly offset ray inside a named closed boundary may immediately
    // exit an adjacent face at a corner. Discarding that positive hit loses
    // the boundary identity. The conservative t bound remains the default.
    return distance > (allowNearBoundaryHit?0.0:deltaT);
}
