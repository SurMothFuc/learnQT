uniform vec3 diffuseEnvironment[9];

// Broad diffuse lighting depends on the surface normal, never the camera ray.
vec3 RasterDiffuseEnvironment(vec3 normal)
{
    vec3 n = normalize(EnvironmentDirection(normal, -1.0));
    vec3 light = diffuseEnvironment[0] * .2820947918
        + diffuseEnvironment[1] * (.4886025119 * n.x)
        + diffuseEnvironment[2] * (.4886025119 * n.y)
        + diffuseEnvironment[3] * (.4886025119 * n.z)
        + diffuseEnvironment[4] * (1.0925484306 * n.x * n.y)
        + diffuseEnvironment[5] * (1.0925484306 * n.y * n.z)
        + diffuseEnvironment[6] * (.3153915653 * (3.0 * n.y * n.y - 1.0))
        + diffuseEnvironment[7] * (1.0925484306 * n.x * n.z)
        + diffuseEnvironment[8] * (.5462742153 * (n.x * n.x - n.z * n.z));
    return max(light, vec3(0.0)) * environmentIntensity;
}
