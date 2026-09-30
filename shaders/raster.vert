#version 330 core

// 光栅化交互预览的顶点着色器。与路径追踪无关：
// 顶点属性按 mesh 展开的三角形上传，实例变换通过按实例步进的属性提供。
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUv0;
layout (location = 3) in mat4 aModel;
layout (location = 7) in float aMaterial;

out vec3 worldPosition;
out vec3 worldNormal;
out vec2 uv0;
flat out int materialIndex;

uniform mat4 view;
uniform mat4 projection;

void main()
{
    vec4 world = aModel * vec4(aPosition, 1.0);
    worldPosition = world.xyz;
    // 实例变换含非均匀缩放时，用模型矩阵的逆转置保持法线方向正确。
    mat3 normalMatrix = transpose(inverse(mat3(aModel)));
    worldNormal = normalize(normalMatrix * aNormal);
    uv0 = aUv0;
    materialIndex = int(aMaterial);
    vec4 clip = projection * (view * world);
    gl_Position = clip;
}
