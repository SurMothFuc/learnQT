struct EncodedLight {
    int type;
    int triangleIndex;
    float selectPdf;
    float radius;
    vec3 positionOrDirection;
    vec3 color;
    float cdf;
    float range;
};

struct LightSample {
    bool valid;
    bool delta;
    vec3 direction;
    float distance;
    vec3 radiance;
    float pdf;
    int triangleIndex;
};

LightSample InvalidLightSample()
{
    LightSample sample;
    sample.valid = false;
    sample.delta = false;
    sample.direction = vec3(0.0, 1.0, 0.0);
    sample.distance = INF;
    sample.radiance = vec3(0.0);
    sample.pdf = 0.0;
    sample.triangleIndex = -1;
    return sample;
}

EncodedLight GetEncodedLight(int index)
{
    int offset = index * SIZE_LIGHT;
    vec4 param0 = texelFetch(lights, offset + 0);
    vec4 param1 = texelFetch(lights, offset + 1);
    vec4 param2 = texelFetch(lights, offset + 2);
    vec4 param3 = texelFetch(lights, offset + 3);

    EncodedLight light;
    light.type = int(param0.x + 0.5);
    light.triangleIndex = int(param0.y + 0.5);
    light.selectPdf = param0.z;
    light.radius = param0.w;
    light.positionOrDirection = param1.xyz;
    light.color = param2.xyz;
    light.cdf = param3.x;
    light.range = param3.y;
    return light;
}

float EnvSelectPdf()
{
#ifdef USEENVIRONMENTMAP
    return nLights > 0 ? 0.5 : 1.0;
#else
    return 0.0;
#endif
}

float FiniteLightSelectPdf()
{
#ifdef USEENVIRONMENTMAP
    return nLights > 0 ? 0.5 : 0.0;
#else
    return nLights > 0 ? 1.0 : 0.0;
#endif
}

Triangle GetTriangleLightGeometry(int triangleIndex)
{
    int offset = triangleIndex * SIZE_TRIANGLE;
    Triangle triangle;
    triangle.p1 = texelFetch(triangles, offset + 0).xyz;
    triangle.p2 = texelFetch(triangles, offset + 1).xyz;
    triangle.p3 = texelFetch(triangles, offset + 2).xyz;
    triangle.n1 = texelFetch(triangles, offset + 3).xyz;
    triangle.n2 = texelFetch(triangles, offset + 4).xyz;
    triangle.n3 = texelFetch(triangles, offset + 5).xyz;
    GetTriangleUVs(triangleIndex, triangle.uv1, triangle.uv2, triangle.uv3);
    return triangle;
}

float TriangleArea(Triangle triangle)
{
    return 0.5 * length(cross(triangle.p2 - triangle.p1, triangle.p3 - triangle.p1));
}

vec3 TriangleFaceNormal(Triangle triangle)
{
    return normalize(cross(triangle.p2 - triangle.p1, triangle.p3 - triangle.p1));
}

vec3 SampleTriangleBarycentric(float xi1, float xi2)
{
    float su = sqrt(max(xi1, 0.0));
    float b0 = 1.0 - su;
    float b1 = xi2 * su;
    float b2 = 1.0 - b0 - b1;
    return vec3(b0, b1, b2);
}

EncodedLight SelectFiniteLight(float xi)
{
    int low = 0;
    int high = max(nLights - 1, 0);
    for (int iteration = 0; iteration < 32; ++iteration) {
        if (low >= high) {
            break;
        }
        int middle = low + (high - low) / 2;
        if (xi <= GetEncodedLight(middle).cdf) {
            high = middle;
        }
        else {
            low = middle + 1;
        }
    }
    return GetEncodedLight(low);
}

LightSample SampleEnvironmentLight(float xi1, float xi2, float envPdf)
{
    LightSample sample = InvalidLightSample();
    vec3 L = SampleHdr(xi1, xi2);
    float pdf = envPdf * hdrPdf(L, hdrResolution);
    if (pdf <= 0.0) {
        return sample;
    }

    sample.valid = true;
    sample.delta = false;
    sample.direction = L;
    sample.distance = INF;
    sample.radiance = hdrColor(L);
    sample.pdf = pdf;
    return sample;
}

LightSample SampleTriangleLight(EncodedLight light, vec3 origin, float xi1, float xi2, float finitePdf)
{
    LightSample sample = InvalidLightSample();
    Triangle triangle = GetTriangleLightGeometry(light.triangleIndex);
    float area = max(TriangleArea(triangle), EPS);
    vec3 bary = SampleTriangleBarycentric(xi1, xi2);
    vec3 lightPoint = bary.x * triangle.p1 + bary.y * triangle.p2 + bary.z * triangle.p3;
    vec3 toLight = lightPoint - origin;
    float dist2 = dot(toLight, toLight);
    if (dist2 <= EPS) {
        return sample;
    }

    float distance = sqrt(dist2);
    vec3 L = toLight / distance;
    vec3 lightNormal = TriangleFaceNormal(triangle);
    float cosLight = abs(dot(lightNormal, -L));
    if (cosLight <= EPS) {
        return sample;
    }

    float areaPdf = 1.0 / area;
    sample.valid = true;
    sample.delta = false;
    sample.direction = L;
    sample.distance = distance;
    vec2 lightUV = bary.x * triangle.uv1 + bary.y * triangle.uv2 + bary.z * triangle.uv3;
    materialEvaluationUV = lightUV;
    Material lightMaterial = getMaterial(light.triangleIndex);
    sample.radiance = lightMaterial.emissive;
    sample.pdf = finitePdf * light.selectPdf * areaPdf * dist2 / cosLight;
    sample.triangleIndex = light.triangleIndex;
    return sample;
}

LightSample SampleSunDiskLight(EncodedLight light, float xi1, float xi2, float finitePdf)
{
    LightSample sample = InvalidLightSample();
    float angularRadius = max(light.radius, EPS);
    float cosThetaMax = cos(angularRadius);
    float solidAngle = TWO_PI * (1.0 - cosThetaMax);
    if (solidAngle <= EPS) {
        return sample;
    }

    vec3 axis = normalize(-light.positionOrDirection);
    vec3 T, B;
    Onb(axis, T, B);

    float cosTheta = mix(cosThetaMax, 1.0, xi1);
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = TWO_PI * xi2;
    vec3 L = normalize(T * (cos(phi) * sinTheta) + B * (sin(phi) * sinTheta) + axis * cosTheta);

    sample.valid = true;
    sample.delta = false;
    sample.direction = L;
    sample.distance = INF;
    sample.radiance = light.color;
    sample.pdf = finitePdf * light.selectPdf / solidAngle;
    return sample;
}

LightSample SampleSphereLight(EncodedLight light, vec3 origin, float xi1, float xi2, float finitePdf)
{
    LightSample sample = InvalidLightSample();
    float z = 1.0 - 2.0 * xi1;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = TWO_PI * xi2;
    vec3 sphereNormal = vec3(r * cos(phi), z, r * sin(phi));
    vec3 lightPoint = light.positionOrDirection + sphereNormal * light.radius;
    vec3 toLight = lightPoint - origin;
    float dist2 = dot(toLight, toLight);
    if (dist2 <= EPS) {
        return sample;
    }

    float distance = sqrt(dist2);
    vec3 L = toLight / distance;
    float cosLight = abs(dot(sphereNormal, -L));
    if (cosLight <= EPS || light.radius <= EPS) {
        return sample;
    }

    float areaPdf = 1.0 / (4.0 * PI * light.radius * light.radius);
    sample.valid = true;
    sample.delta = false;
    sample.direction = L;
    sample.distance = distance;
    sample.radiance = light.color;
    sample.pdf = finitePdf * light.selectPdf * areaPdf * dist2 / cosLight;
    return sample;
}

LightSample SampleOneLight(vec3 origin, float xiSelect, float xi1, float xi2)
{
    float envPdf = EnvSelectPdf();
    if (envPdf > 0.0 && xiSelect < envPdf) {
        return SampleEnvironmentLight(xi1, xi2, envPdf);
    }

    float finitePdf = FiniteLightSelectPdf();
    if (finitePdf <= 0.0 || nLights <= 0) {
        return InvalidLightSample();
    }

    float remappedXi = envPdf < 1.0 ? clamp((xiSelect - envPdf) / (1.0 - envPdf), 0.0, 0.999999) : clamp(xiSelect, 0.0, 0.999999);
    EncodedLight light = SelectFiniteLight(remappedXi);

    if (light.type == LIGHT_TYPE_TRIANGLE) {
        return SampleTriangleLight(light, origin, xi1, xi2, finitePdf);
    }
    if (light.type == LIGHT_TYPE_SPHERE) {
        return SampleSphereLight(light, origin, xi1, xi2, finitePdf);
    }
    if (light.type == LIGHT_TYPE_SUN_DISK) {
        return SampleSunDiskLight(light, xi1, xi2, finitePdf);
    }

    return InvalidLightSample();
}

float TriangleLightPdf(vec3 origin, vec3 direction, int triangleIndex, float hitDistance, float selectPdf)
{
    if (hitDistance <= EPS) {
        return 0.0;
    }

    Triangle triangle = GetTriangleLightGeometry(triangleIndex);
    float area = max(TriangleArea(triangle), EPS);
    vec3 lightNormal = TriangleFaceNormal(triangle);
    float cosLight = abs(dot(lightNormal, -direction));
    if (cosLight <= EPS) {
        return 0.0;
    }

    float dist2 = hitDistance * hitDistance;
    return FiniteLightSelectPdf() * selectPdf * (1.0 / area) * dist2 / cosLight;
}

float LightPdf(vec3 origin, vec3 direction, int hitTriangleIndex, float hitDistance)
{
    float pdf = 0.0;

#ifdef USEENVIRONMENTMAP
    if (hitTriangleIndex < 0) {
        pdf += EnvSelectPdf() * hdrPdf(direction, hdrResolution);
    }
#endif

    if (hitTriangleIndex >= 0 && nLights > 0) {
        float selectPdf = GetTriangleLightSelectPdf(hitTriangleIndex);
        if (selectPdf > 0.0) {
            pdf += TriangleLightPdf(origin, direction, hitTriangleIndex, hitDistance, selectPdf);
        }
    }

    return pdf;
}
