// 片段着色器 (Pass1_Reprojection.frag)
#version 460 core
uniform sampler2D gColor;
uniform sampler2D gEmission;
uniform sampler2D gAlbedo;
uniform sampler2D gPrevIllumination;
uniform sampler2D gPrevMoments;
uniform sampler2D gPrevHistoryLength;
uniform sampler2D gLinearZAndNormal; // 深度和法线 (RG:深度, BA:法线)

in vec2 texCoord;
out vec4 OutIllumination;
out vec2 OutMoments;
out float OutHistoryLength;

const float eps = 1e-5;

float luminance(vec3 rgb) {
    return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec3 color = texture(gColor, texCoord).rgb;
    vec3 emission = texture(gEmission, texCoord).rgb;
    vec3 albedo = texture(gAlbedo, texCoord).rgb;
    
    // 计算当前帧光照
    vec3 illuminationCurrent = (color - emission) / max(albedo, vec3(eps));
    
    // 加载历史数据
    vec4 prevIllum = texture(gPrevIllumination, texCoord);
    vec2 prevMoments = texture(gPrevMoments, texCoord).rg;
    float prevHistory = texture(gPrevHistoryLength, texCoord).r;
    
    // 计算历史长度 (重投影逻辑需在此实现)
    bool isValid = /* 实现重投影有效性检查 */;
    float historyLength = isValid ? min(32.0, prevHistory + 1.0) : 1.0;
    
    // 计算混合因子
    float alpha = mix(gAlpha, 1.0/historyLength, isValid);
    float alphaMoments = mix(gMomentsAlpha, 1.0/historyLength, isValid);
    
    // 更新矩
    float lumi = luminance(illuminationCurrent);
    vec2 moments = mix(prevMoments, vec2(lumi, lumi*lumi), alphaMoments);
    
    // 计算方差
    float variance = max(0.0, moments.y - moments.x * moments.x);
    
    // 混合光照
    vec3 illumination = mix(prevIllum.rgb, illuminationCurrent, alpha);
    
    OutIllumination = vec4(illumination, variance);
    OutMoments = moments;
    OutHistoryLength = historyLength;
}

// Pass2_Filter.frag
#version 460 core
uniform sampler2D gIllumination;
uniform sampler2D gMoments;
uniform sampler2D gHistoryLength;
uniform sampler2D gLinearZAndNormal;

in vec2 texCoord;
out vec4 FilteredIllumination;
out vec2 FilteredMoments;
out float FilteredHistoryLength;

float computeWeight(float depthC, float depthP, float phiDepth,
                    vec3 normalC, vec3 normalP, float phiNormal,
                    float lumiC, float lumiP, float phiIllum) {
    float wNormal = pow(max(dot(normalC, normalP), 0.0), phiNormal);
    float wZ = (phiDepth == 0) ? 0.0 : abs(depthC - depthP) / phiDepth;
    float wLum = abs(lumiC - lumiP) / phiIllum;
    return exp(-max(wLum, 0.0) - max(wZ, 0.0)) * wNormal;
}

void main() {
    vec4 illumCenter = texture(gIllumination, texCoord);
    float history = texture(gHistoryLength, texCoord).r;
    
    if (history > 4.0) {
        FilteredIllumination = illumCenter;
        FilteredMoments = texture(gMoments, texCoord).rg;
        FilteredHistoryLength = history;
        return;
    }
    
    // 获取中心点数据
    vec4 zNormal = texture(gLinearZAndNormal, texCoord);
    float depthC = zNormal.r;
    vec3 normalC = oct_to_ndir_snorm(zNormal.ba); // 法线解码函数
    float lumiC = luminance(illumCenter.rgb);
    
    // 7x7 滤波
    vec4 sumIllum = vec4(0);
    vec2 sumMoments = vec2(0);
    float sumW = 0;
    
    for (int y = -3; y <= 3; y++) {
        for (int x = -3; x <= 3; x++) {
            vec2 uv = texCoord + vec2(x, y) * texelSize;
            vec4 illumP = texture(gIllumination, uv);
            vec4 zNormalP = texture(gLinearZAndNormal, uv);
            
            float depthP = zNormalP.r;
            vec3 normalP = oct_to_ndir_snorm(zNormalP.ba);
            float lumiP = luminance(illumP.rgb);
            
            float w = computeWeight(depthC, depthP, phiDepth,
                                   normalC, normalP, gPhiNormal,
                                   lumiC, lumiP, gPhiIllum);
            
            sumIllum += w * illumP;
            sumMoments += w * texture(gMoments, uv).rg;
            sumW += w;
        }
    }
    
    FilteredIllumination = sumIllum / sumW;
    FilteredMoments = sumMoments / sumW;
    FilteredHistoryLength = history;
}

// Pass3_ATrous.frag
#version 460 core
uniform sampler2D gIllumination;
uniform sampler2D gHistoryLength;
uniform sampler2D gLinearZAndNormal;
uniform int iteration; // 当前迭代次数 (0,1,2,...)

in vec2 texCoord;
out vec4 FinalIllumination;

float computeVarianceCenter(vec2 uv) {
    // 3x3 高斯模糊计算方差
    const float kernel[2][2] = {{1.0/4.0, 1.0/8.0}, {1.0/8.0, 1.0/16.0}};
    float sum = 0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float k = kernel[abs(x)][abs(y)];
            sum += texture(gIllumination, uv + vec2(x,y)*texelSize).a * k;
        }
    }
    return sum;
}

void main() {
    vec4 illumCenter = texture(gIllumination, texCoord);
    vec4 zNormal = texture(gLinearZAndNormal, texCoord);
    float depthC = zNormal.r;
    vec3 normalC = oct_to_ndir_snorm(zNormal.ba);
    float lumiC = luminance(illumCenter.rgb);
    
    // 计算方差指导的phi
    float var = computeVarianceCenter(texCoord);
    float phiL = gPhiColor * sqrt(max(0.0, var + 1e-10));
    
    // A-Trous 滤波 (5x5)
    const float kernel[3] = {1.0, 2.0/3.0, 1.0/6.0};
    int step = 1 << iteration; // 步长: 1,2,4,8...
    
    vec4 sum = illumCenter;
    float sumW = 1.0;
    
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            if (x == 0 && y == 0) continue;
            
            vec2 uv = texCoord + vec2(x,y) * texelSize * step;
            vec4 illumP = texture(gIllumination, uv);
            vec4 zNormalP = texture(gLinearZAndNormal, uv);
            
            float depthP = zNormalP.r;
            vec3 normalP = oct_to_ndir_snorm(zNormalP.ba);
            float lumiP = luminance(illumP.rgb);
            
            // 计算权重
            float k = kernel[abs(x)] * kernel[abs(y)];
            float w = computeWeight(
                depthC, depthP, phiDepth * length(vec2(x,y)),
                normalC, normalP, gPhiNormal,
                lumiC, lumiP, phiL
            ) * k;
            
            sum += w * illumP;
            sumW += w;
        }
    }
    
    FinalIllumination = sum / sumW;
}

// Pass4_Composite.frag
#version 460 core
uniform sampler2D gAlbedo;
uniform sampler2D gEmission;
uniform sampler2D gIllumination; // 滤波后结果

in vec2 texCoord;
out vec4 FragColor;

void main() {
    vec3 albedo = texture(gAlbedo, texCoord).rgb;
    vec3 emission = texture(gEmission, texCoord).rgb;
    vec3 illum = texture(gIllumination, texCoord).rgb;
    
    FragColor = vec4(albedo * illum + emission, 1.0);
}


float3 calcJBFWeight(int2 i,int2 j,float3 variance)
{
    float InvSigmaRT = 1.0f/500.0f;
    float SigmaN = 128.0f;
    float InvSigmaZ = 1.0f/60.0f;
    float3 eRT = length(SrcTexture[i].xyz - SrcTexture[j].xyz) * InvSigmaRT/ (sqrt(variance) + float3(0.001,0.001,0.001));
    float wN = pow(max(0, dot(g_normal[i*2].xyz*2.0f-1.0f, g_normal[j*2].xyz*2.0f-1.0f)), SigmaN);
    float eZ = length(g_z[i*2].x - g_z[j*2].x) * InvSigmaZ / (length(g_gz[i*2].x * (i - j)) + float3(0.001,0.001,0.001));
    return wN* exp(-eRT-eZ);
}