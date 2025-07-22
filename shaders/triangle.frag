#version 330 core
out vec4 FragColor;
in vec3 pix;

//uniform sampler2D texPass0;

uniform sampler2D texPass1;//render

vec3 toneMapping(in vec3 c, float limit) {
    float luminance = 0.212671 * c.x + 0.715160 * c.y + 0.072169 * c.z;
    return c * 1.0 / (1.0 + luminance / limit);
}


void main(void)
{

   // vec3 color = texture2D(texPass0, pix.xy*0.5+0.5).rgb;//*100.0;
//    if( color.x>10.1)
//         color=vec3(10);
//            if( color.y>10.1)
//        color=vec3(10);
//            if( color.z>10.1)
//         color=vec3(10);
    vec3 color=texture2D(texPass1, pix.xy*0.5+0.5).rgb;

    color = toneMapping(color, 1.5);
    color = pow(color, vec3(1.0 / 2.2));
    FragColor =vec4(color,1.0);
     //FragColor =vec4(pix,1.0);
}