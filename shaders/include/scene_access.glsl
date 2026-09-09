// Logical surface IDs address (instance, local triangle), preserving unique emitter IDs.
#ifdef INSTANCED_SCENE
uniform samplerBuffer instanceTable;
uniform samplerBuffer materialTable;
uniform samplerBuffer topNodes;
uniform usamplerBuffer surfaceTable;
uniform samplerBuffer surfacePdfTable;
uniform int nTopNodes;
uniform bool picking;
mat4 InstanceMatrix(int instance,int start) {
    int i=instance*9+start;
    return mat4(texelFetch(instanceTable,i),texelFetch(instanceTable,i+1),texelFetch(instanceTable,i+2),texelFetch(instanceTable,i+3));
}
vec4 FetchTriangleVector(int address) {
    int surface=address/20,field=address%20;
    uvec2 ref=texelFetch(surfaceTable,surface).xy; int geometry=int(ref.x),instance=int(ref.y);
    int material=int(texelFetch(instanceTable,instance*9+8).x)*10;
    if(field<3) return vec4((InstanceMatrix(instance,0)*vec4(texelFetch(triangles,geometry*11+field).xyz,1)).xyz,0);
    if(field<6) return vec4(transpose(mat3(InstanceMatrix(instance,4)))*texelFetch(triangles,geometry*11+field).xyz,0);
    if(field<12) return texelFetch(materialTable,material+field-6);
    if(field==12) return texelFetch(triangles,geometry*11+6);
    if(field==13) return vec4(texelFetch(triangles,geometry*11+7).xy,texelFetch(materialTable,material+6).zw);
    if(field==14) return texelFetch(materialTable,material+7);
    if(field==15) return texelFetch(materialTable,material+8);
    if(field==16) return vec4(texelFetch(materialTable,material+9).xy,texelFetch(surfacePdfTable,surface).r,0);
    vec4 tangent=texelFetch(triangles,geometry*11+field-9);mat3 world=mat3(InstanceMatrix(instance,0));
    return vec4(world*tangent.xyz,tangent.w*sign(determinant(world)));
}
#else
vec4 FetchTriangleVector(int address) { return texelFetch(triangles,address); }
#endif
