float HdrTexelSolidAngle(int y, ivec2 size)
{
    return (TWO_PI / float(size.x)) * 2.0 *
        sin(PI * (float(y) + 0.5) / float(size.y)) * sin(0.5 * PI / float(size.y));
}

float hdrPdf(vec3 L, int unusedResolution)
{
    ivec2 size = textureSize(hdrCache, 0);
    vec2 uv = toSphericalCoord(normalize(L));
    ivec2 cell = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
    return texelFetch(hdrCache, cell, 0).b / HdrTexelSolidAngle(cell.y, size);
}

vec3 SampleHdr(float xi1, float xi2, out float pdf)
{
    ivec2 size = textureSize(hdrCache, 0);
    xi1 = clamp(xi1, 0.0, 0.99999994);
    xi2 = clamp(xi2, 0.0, 0.99999994);
    int low = 0, high = size.x - 1;
    while (low < high) {
        int middle = (low + high) / 2;
        if (xi1 < texelFetch(hdrCache, ivec2(middle, 0), 0).r) high = middle;
        else low = middle + 1;
    }
    int x = low;
    low = 0; high = size.y - 1;
    while (low < high) {
        int middle = (low + high) / 2;
        if (xi2 < texelFetch(hdrCache, ivec2(x, middle), 0).g) high = middle;
        else low = middle + 1;
    }
    int y = low;
    vec3 cdf = texelFetch(hdrCache, ivec2(x, y), 0).rgb;
    float x0 = x > 0 ? texelFetch(hdrCache, ivec2(x-1, 0), 0).r : 0.0;
    float y0 = y > 0 ? texelFetch(hdrCache, ivec2(x, y-1), 0).g : 0.0;
    float u = (xi1 - x0) / max(cdf.r - x0, 1e-30);
    float v = (xi2 - y0) / max(cdf.g - y0, 1e-30);
    // Residual variates sample the whole chosen texel in solid angle.
    float phi = TWO_PI * ((float(x) + u) / float(size.x) - 0.5);
    float cosTheta, sinTheta;
    if (y < size.y / 2) {
        float top = 2.0 * pow(sin(0.5 * PI * float(y) / float(size.y)), 2.0);
        float bottom = 2.0 * pow(sin(0.5 * PI * float(y+1) / float(size.y)), 2.0);
        float oneMinusCos = mix(top, bottom, v);
        cosTheta = 1.0 - oneMinusCos;
        sinTheta = sqrt(max(0.0, oneMinusCos * (2.0-oneMinusCos)));
    } else {
        float top = 2.0 * pow(sin(0.5 * PI * float(size.y-y) / float(size.y)), 2.0);
        float bottom = 2.0 * pow(sin(0.5 * PI * float(size.y-y-1) / float(size.y)), 2.0);
        float onePlusCos = mix(top, bottom, v);
        cosTheta = onePlusCos - 1.0;
        sinTheta = sqrt(max(0.0, onePlusCos * (2.0-onePlusCos)));
    }
    pdf = cdf.b / HdrTexelSolidAngle(y, size);
    return vec3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
}

vec3 hdrColor(vec3 L)
{
    return texture(hdrMap, toSphericalCoord(normalize(L))).rgb;
}
