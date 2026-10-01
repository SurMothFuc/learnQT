#version 330 core
layout(location=0) out vec4 RenderColorResult;
layout(location=1) out vec4 NormalResult;
layout(location=2) out vec4 BaseResult;
in vec3 pix;
uniform sampler2D RenderColor, NormalColor, BaseColor;
void main() {
 vec2 uv=pix.xy*.5+.5;
 RenderColorResult=texture(RenderColor,uv);
 NormalResult=texture(NormalColor,uv);
 BaseResult=texture(BaseColor,uv);
}
