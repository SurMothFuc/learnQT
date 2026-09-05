
float SchlickFresnel(float u) {
    float m = clamp(1-u, 0.0, 1.0);
    float m2 = m*m;
    return m2*m2*m; // pow(m,5)
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
    if (abs(eta - 1.0) < 1e-6) return 0.0;
    cosThetaI = clamp(abs(cosThetaI), 0.0, 1.0);
    float sinThetaTSq = eta * eta * (1.0 - cosThetaI * cosThetaI);

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

    float cosTheta = a >= 0.999999 ? sqrt(1.0-r2) : sqrt((1.0 - pow(a2, 1.0 - r2)) / (1.0 - a2));
    float sinTheta = clamp(sqrt(1.0 - (cosTheta * cosTheta)), 0.0, 1.0);
    float sinPhi = sin(phi);
    float cosPhi = cos(phi);

    return vec3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
}


vec3 DisneySample(float xi_1, float xi_2, float xi_3, vec3 V, vec3 N, in Material material,float eta, out bool delta)
{
    delta = false;
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
    float totalWt = diffPr + dielectricPr + metalPr + glassPr + clearCtPr;
    if (totalWt <= 0.0) return vec3(0.0);
    float invTotalWt = 1.0 / totalWt;
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
        delta = material.roughness <= 0.0;
        vec3 H = delta ? vec3(0,0,1) : SampleGGXVNDF(V, material.ax, material.ay, xi_1, xi_2);
        if (H.z < 0.0)
            H = -H;

        L = normalize(reflect(-V, H));
    }
    else if (xi_3 < cdf[3]) // Glass
    {
        delta = material.roughness <= 0.0 || abs(eta - 1.0) < 1e-6;
        vec3 H = delta ? vec3(0,0,1) : SampleGGXVNDF(V, material.ax, material.ay, xi_1, xi_2);
        float F = DielectricFresnel(abs(dot(V, H)), eta);

        if (H.z < 0.0)
            H = -H;

        // Rescale random number for reuse
        xi_3 = min((xi_3 - cdf[2]) / (cdf[3] - cdf[2]), 0.99999994);

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
        delta = material.clearcoatGloss <= 0.0;
        vec3 H = delta ? vec3(0,0,1) : SampleGTR1(material.clearcoatGloss,xi_1, xi_2);

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
    float totalWt = diffPr + dielectricPr + metalPr + glassPr + clearCtPr;
    if (totalWt <= 0.0) return vec3(0.0);
    float invTotalWt = 1.0 / totalWt;
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
    if (dielectricPr > 0.0 && reflect && material.roughness > 0.0)
    {
        // Normalize for interpolating based on Cspec0
        float F = (DielectricFresnel(VDotH, eta) - F0) / max(1.0 - F0, 1e-6);

        f += EvalMicrofacetReflection(material.ax,material.ay, V, L, H, mix(Cspec0, vec3(1.0), F), tmpPdf) * dielectricWt;
        pdf += tmpPdf * dielectricPr;
    }

    // Metallic Reflection
    if (metalPr > 0.0 && reflect && material.roughness > 0.0)
    {
        // Tinted to base color
        vec3 F = mix(material.baseColor, vec3(1.0), SchlickFresnel(VDotH));

        f += EvalMicrofacetReflection(material.ax,material.ay, V, L, H, F, tmpPdf) * metalWt;
        pdf += tmpPdf * metalPr;
    }

    // Glass/Specular BSDF
    if (glassPr > 0.0 && material.roughness > 0.0 && abs(eta - 1.0) >= 1e-6)
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
    if (clearCtPr > 0.0 && reflect && material.clearcoatGloss > 0.0)
    {
        f += EvalClearcoat(material.clearcoatGloss, V, L, H, tmpPdf) * 0.25 * material.clearcoat;
        pdf += tmpPdf * clearCtPr;
    }

    return f;
}
struct BsdfSample {
    vec3 direction;
    vec3 weight;
    float pdf; // Solid-angle density for continuous events only.
    bool delta;
};

bool HasNonDeltaLobes(Material m, float eta) {
    float dielectric = (1.0-m.metallic)*(1.0-m.transmission);
    return (dielectric > 0.0 && Luminance(m.baseColor) > 0.0) ||
        (m.roughness > 0.0 && (dielectric > 0.0 || m.metallic > 0.0 ||
            (m.transmission > 0.0 && abs(eta-1.0) >= 1e-6))) ||
        (m.clearcoat > 0.0 && m.clearcoatGloss > 0.0);
}

BsdfSample SampleDisneyBSDF(vec3 V, vec3 N, Material m, float eta, vec3 xi) {
    BsdfSample s;
    s.direction = DisneySample(xi.x, xi.y, xi.z, V, N, m, eta, s.delta);
    s.pdf = 0.0; s.weight = vec3(0.0);
    if (!s.delta) {
        vec3 f = DisneyEval(V, N, s.direction, m, eta, s.pdf);
        if (s.pdf > 0.0) s.weight = f * abs(dot(N, s.direction)) / s.pdf;
        return s;
    }
    // Delta events use probability masses, never a fake solid-angle PDF.
    // All lobes sharing the reflected direction must contribute to that mass.
    float cosV = abs(dot(N,V));
    float lum = Luminance(m.baseColor);
    vec3 tint = lum > 0.0 ? m.baseColor/lum : vec3(1.0);
    float F0 = sqr((1.0-eta)/(1.0+eta));
    vec3 Cspec0 = F0 * mix(vec3(1.0), tint, m.specularTint);
    float schlick = SchlickFresnel(cosV);
    float dielectric = (1.0-m.metallic)*(1.0-m.transmission);
    float glass = (1.0-m.metallic)*m.transmission;
    float pd = dielectric * lum;
    float pr = dielectric * Luminance(mix(Cspec0,vec3(1.0),schlick));
    float pm = m.metallic * Luminance(mix(m.baseColor,vec3(1.0),schlick));
    float pc = 0.25*m.clearcoat;
    float total = pd+pr+pm+glass+pc;
    float F = DielectricFresnel(cosV, eta);
    float mass = 0.0;
    vec3 value = vec3(0.0);
    if (dot(N,s.direction) > 0.0) {
        if (m.roughness <= 0.0) {
            mass += pr+pm;
            float tintF = clamp((F-F0)/max(1.0-F0,1e-6),0.0,1.0);
            value += dielectric*mix(Cspec0,vec3(1.0),tintF) +
                m.metallic*mix(m.baseColor,vec3(1.0),schlick);
        }
        if (m.roughness <= 0.0 || abs(eta-1.0)<1e-6) {
            mass += glass*F;
            value += vec3(glass*F);
        }
        if (m.clearcoatGloss <= 0.0) {
            mass += pc;
            value += vec3(pc*mix(0.04,1.0,schlick));
        }
    } else {
        mass = glass*(1.0-F);
        value = mass * sqrt(max(m.baseColor,vec3(0.0))) * eta*eta;
    }
    if (mass > 0.0 && total > 0.0) s.weight = value * total / mass;
    return s;
}
