// Shared pixel-center projection for path tracing and the raster HDR background.
vec3 CameraRayDirection(vec2 pixel)
{
    vec2 normalizedCoords = pixel / vec2(width, height);
    vec2 ndc = normalizedCoords * 2.0 - 1.0;
    return normalize((view * vec4(ndc.x * float(width) / float(height),
        ndc.y, -1.0 / tan(radians(cameraFov) * 0.5), 0.0)).xyz);
}
