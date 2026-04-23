//uniform mat4 model;
//uniform mat4 projection;
uniform mat4 view;
uniform int nTriangles;
uniform vec3 eye;
uniform int nNodes;
uniform int width;
uniform int height;
uniform uint frameCounter;
uniform int hdrResolution;
uniform int maxBounces;

uniform float sobelNumber[MAX_BOUNCES_LIMIT * 2];

uniform samplerBuffer triangles;
uniform samplerBuffer nodes;

uniform sampler2D hdrMap;
uniform sampler2D hdrCache;

uniform sampler2D preRenderColor;

