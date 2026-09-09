#version 330 core
out vec4 FragColor;
in vec3 pix;
uniform sampler2D texPass1;
uniform float exposure;
uniform int tonemap;
void main(){
    vec3 color=max(texture(texPass1,pix.xy*.5+.5).rgb,vec3(0))*exp2(exposure);
    if(tonemap==1)color=clamp((color*(2.51*color+.03))/(color*(2.43*color+.59)+.14),0.0,1.0);
    else if(tonemap==0)color/=1.0+dot(color,vec3(.212671,.715160,.072169))/1.5;
    else color=clamp(color,0.0,1.0);
    FragColor=vec4(pow(color,vec3(1.0/2.2)),1);
}
