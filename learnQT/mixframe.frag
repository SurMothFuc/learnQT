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

void main(void)
{
    
    float SigmaN = 128.0f;
    vec3 totalColorWeight = vec3(0);
    vec3 totalColor = vec3(0);

    float px=(pix.x*0.5+0.5);
    float py=(pix.y*0.5+0.5);
    vec3 p_normal=texture2D(texPass1,vec2(px,py)).rgb*2.0-1.0;
    px=px*width;
    py=py*height;

    
    for (int i = 0; i < 25; i++)
    {
        float npx=(px+offsetScale*g_offsets[i*2])/width;
        float npy=(py+offsetScale*g_offsets[i*2+1])/height;
        npx=max(0.0,min(1.0,npx));
        npy=max(0.0,min(1.0,npy));
        vec3 q_normal=texture2D(texPass1,vec2(npx,npy)).rgb*2.0-1.0;
        float wn=pow(max(0.0,dot(p_normal,q_normal)),SigmaN);

        totalColor += wn*g_h[i]*texture2D(texPass0,vec2(npx,npy)).rgb;
        totalColorWeight +=  wn*g_h[i];
    }
    totalColor /= totalColorWeight;

    FilteredColor=vec4(totalColor,1.0); 
    
    // FilteredColor=vec4(texture2D(texPass1, pix.xy*0.5+0.5).rgb,1.0);         
     //FragColor =vec4(pix,1.0);
     //gl_FragData[0] = vec4(texture2D(texPass0, pix.xy*0.5+0.5).rgb, 1.0);
}