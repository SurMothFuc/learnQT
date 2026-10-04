//uniform mat4 model;
//uniform mat4 projection;
uniform mat4 view;
uniform int nTriangles;
uniform vec3 eye;
uniform int nNodes;
uniform int nLights;
uniform int nAnalyticLights;
uniform int width;
uniform int height;
uniform uint frameCounter;
uniform int hdrResolution;
uniform int maxBounces;

uniform float sobelNumber[MAX_BOUNCES_LIMIT * 2];
uniform bool useUnifiedSampler;
uniform uint samplerSeed,samplerIndex;
uniform uvec4 samplerSobol[30]; // Existing 120-dimensional direction table.
uniform bool useEtaScaleRR;
uniform int rrMinDepth;
uniform bool usePowerLightGroups;
uniform float environmentSelectProbability;

uniform samplerBuffer triangles;
uniform samplerBuffer nodes;
uniform samplerBuffer lights;

uniform sampler2D hdrMap;
uniform sampler2D hdrCache;
uniform sampler2DArray materialTextures;
#ifndef MATERIAL_TEXTURE_POOL_COUNT
#define MATERIAL_TEXTURE_POOL_COUNT 1
#endif
#if MATERIAL_TEXTURE_POOL_COUNT >= 2
uniform sampler2DArray materialTextures1;
#endif
#if MATERIAL_TEXTURE_POOL_COUNT >= 3
uniform sampler2DArray materialTextures2;
#endif
#if MATERIAL_TEXTURE_POOL_COUNT >= 4
uniform sampler2DArray materialTextures3;
#endif
uniform samplerBuffer materialTextureInfo;
uniform int materialTextureCount;
uniform int materialTextureInfoStride; // 0 preserves legacy three-vec4 numerical fixtures.
uniform bool shadowAnyHit,shadowBinaryScene;

uniform sampler2D preRenderColor;

uniform float cameraFov;

uniform bool useBoundaryMedia;
uniform int initialMediumCount;
uniform vec4 initialMediumProperties[8],initialMediumColors[8],initialMediumIdentity[8];

uniform bool correctClearcoat;

uniform bool legacyOidnGuides;

uniform int mediumContactOffset,mediumContactCount;
