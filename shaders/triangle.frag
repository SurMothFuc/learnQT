#version 330 core
out vec4 FragColor;
in vec3 pix;
uniform sampler2D texPass1;
uniform float exposure;
uniform int tonemap;
uniform bool legacyGamma;
void main(){
    vec3 color=max(texture(texPass1,pix.xy*.5+.5).rgb,vec3(0))*exp2(exposure);
    if(tonemap==1) {
        for(int c=0;c<3;++c) {
            float value=color[c];
            if(value>1.0) {float inverse=1.0/value;color[c]=(2.51+.03*inverse)/(2.43+.59*inverse+.14*inverse*inverse);}
            else color[c]=(value*(2.51*value+.03))/(value*(2.43*value+.59)+.14);
        }
        color=clamp(color,0.0,1.0);
    }
    else if(tonemap==0)color/=1.0+dot(color,vec3(.212671,.715160,.072169))/1.5;
    else color=clamp(color,0.0,1.0);
    vec3 v=clamp(color,vec3(0),vec3(1));
    vec3 srgb=mix(1.055*pow(v,vec3(1.0/2.4))-.055,12.92*v,lessThanEqual(v,vec3(.0031308)));
    FragColor=vec4(legacyGamma?pow(v,vec3(1.0/2.2)):srgb,1);
}
