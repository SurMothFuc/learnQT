#version 330 core
layout(location = 0) out vec4 FilteredColor;

in vec3 pix;

uniform sampler2D texPass0;
uniform sampler2D texPass1;

uniform int width;
uniform int height;
uniform int offsetScale;

int g_offsets[25*2]=int[25*2](-2,-2,-1,-2,0,-2,1,-2,2,-2,
                              -2,-1,-1,-1,0,-1,1,-1,2,-1,
                              -2, 0,-1, 0,0, 0,1, 0,2, 0,
                              -2, 1,-1, 1,0, 1,1, 1,2, 1,
                              -2, 2,-1, 2,0, 2,1, 2,2, 2);//x,y


int g_h[25]=int[25]( 1, 4, 6, 4, 1, 
                     4,16,24,16, 4,
                     6,24,36,24, 6,  
                     4,16,24,16, 4,  
                     1, 4, 6, 4, 1);

float computeVarianceCenter(vec2 uv) {
    // 3x3 高斯模糊计算方差
    const float kernel[4] =float[4](1.0/4.0, 1.0/8.0, 1.0/8.0, 1.0/16.0);
    float sum = 0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float k = kernel[abs(x)*2+abs(y)];
            vec2 n_uv=max(vec2(0.0,0.0),min(vec2(1.0,1.0),( uv + vec2(x,y))/vec2(width,height)));
            sum += texture2D(texPass0,n_uv).w * k;
        }
    }
    return sum;
}
float computeWeight(float depthC, float depthP, float phiDepth,
                    vec3 normalC, vec3 normalP, float phiNormal,
                    float lumiC, float lumiP, float phiIllum) {
    float InvSigmaRT = 1.0f/500.0f;
    float wNormal =1.0;// pow(max(dot(normalC, normalP), 0.0), phiNormal);
    float wZ = 0;//(phiDepth == 0) ? 0.0 : abs(depthC - depthP) / phiDepth;
    float wLum = abs(lumiC - lumiP);//*InvSigmaRT / phiIllum;
    return exp(-max(wLum, 0.0));
    return exp(-max(wLum, 0.0) - max(wZ, 0.0)) * wNormal;
}
float luminance(vec3 c)
{
    return 0.212671 * c.x + 0.715160 * c.y + 0.072169 * c.z;
}
void main(void)
{
    
    float gPhiColor=1.0f;
    float phiDepth = 60f;
    float gPhiNormal = 128.0f;

    vec3 totalColorWeight = vec3(0);
    vec3 totalColor = vec3(0);

    float px=(pix.x*0.5+0.5);
    float py=(pix.y*0.5+0.5);
    vec2 texCoord=vec2(px,py);
    vec4 n_p=texture2D(texPass1,texCoord);
    vec3 normalC =n_p.rgb*2.0-1.0;
    float depthC = n_p.w;
    
    vec4 lumicP=texture2D(texPass0,texCoord);
    float var = computeVarianceCenter(texCoord);
    float phiL = gPhiColor * sqrt(max(0.0, var + 1e-10));
    float lumiC = luminance(lumicP.rgb);

    px=px*width;
    py=py*height;

    
    for (int i = 0; i < 25; i++)
    {
        float npx=(px+offsetScale*g_offsets[i*2])/width;
        float npy=(py+offsetScale*g_offsets[i*2+1])/height;
        npx=max(0.0,min(1.0,npx));
        npy=max(0.0,min(1.0,npy));
        vec4 n_p=texture2D(texPass1,vec2(npx,npy));

        float depthP = n_p.w;
        vec3 normalP = n_p.rgb*2.0-1.0;
        vec3 illumP=texture2D(texPass0,vec2(npx,npy)).rgb;
        float lumiP = luminance(illumP);
        float len=1;//length(vec2(abs((i/5)-2),abs((i%5)-2)));

        float k=g_h[i];
        float w = computeWeight(
                depthC, depthP, len*phiDepth,
                normalC, normalP, gPhiNormal,
                lumiC, lumiP, phiL
            ) * k;

        totalColor += w*illumP;
        totalColorWeight += w;
    }
    totalColor /= totalColorWeight;

    FilteredColor=vec4(totalColor,lumicP.w); 
    
    // FilteredColor=vec4(texture2D(texPass1, pix.xy*0.5+0.5).rgb,1.0);         
     //FragColor =vec4(pix,1.0);
     //gl_FragData[0] = vec4(texture2D(texPass0, pix.xy*0.5+0.5).rgb, 1.0);
}