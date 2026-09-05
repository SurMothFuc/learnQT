vec3 EstimateDirectLighting(HitResult hit, vec3 history, float eta, MediumStack media)
{
    LightSample sample = SampleOneLight(hit.hitPoint, rand(), rand(), rand());
    if (!sample.valid || sample.pdf <= 0.0 || maxComponent(sample.radiance) <= 0.0) return vec3(0.0);
    float bsdfPdf;
    vec3 f = DisneyEval(-hit.viewDir, hit.normal, sample.direction, hit.material, eta, bsdfPdf);
    if (bsdfPdf <= 0.0 || maxComponent(f) <= 0.0) return vec3(0.0);
    if (!CrossMediumBoundary(media, hit, sample.direction)) return vec3(0.0);
    vec3 tr = ShadowTransmittance(hit.hitPoint, hit.geometricNormal, sample.direction, sample.distance, media, sample.lightIndex, sample.triangleIndex);
    return history * tr * sample.radiance * f * abs(dot(hit.normal, sample.direction)) *
        misMixWeight(sample.pdf, bsdfPdf) / sample.pdf;
}

vec3 EstimateVolumeLighting(vec3 point, vec3 incoming, vec3 history, MediumStack media)
{
    LightSample sample = SampleOneLight(point, rand(), rand(), rand());
    if (!sample.valid || sample.pdf <= 0.0 || maxComponent(sample.radiance) <= 0.0) return vec3(0.0);
    Medium medium = CurrentMedium(media);
    float phasePdf = PhaseHG(dot(-incoming, sample.direction), medium.g);
    vec3 tr = ShadowTransmittance(point, vec3(0.0), sample.direction, sample.distance, media, sample.lightIndex, sample.triangleIndex);
    return history * tr * sample.radiance * phasePdf * misMixWeight(sample.pdf, phasePdf) / sample.pdf;
}

float EmitterMisWeight(bool previousDelta, float previousPdf, float lightPdf) {
    return previousDelta ? 1.0 : misMixWeight(previousPdf, lightPdf);
}

vec3 InfiniteEmission(vec3 direction, vec3 origin, bool previousDelta, float previousPdf)
{
    vec3 radiance = vec3(0.0);
#ifdef USEENVIRONMENTMAP
    float p = EnvSelectPdf() * hdrPdf(direction, hdrResolution);
    radiance += hdrColor(direction) * EmitterMisWeight(previousDelta, previousPdf, p);
#endif
    // Each overlapping infinite emitter is a separate integrand and light choice.
    for (int i=nLights-nAnalyticLights; i<nLights; ++i) {
        EncodedLight light = GetEncodedLight(i);
        if (light.type != LIGHT_TYPE_SUN_DISK) continue;
        float p = SunLightPdf(light, direction);
        // A positive emitter may have zero selection mass after CDF quantization;
        // it remains visible to the BSDF technique with weight one.
        if (SunContainsDirection(light, direction))
            radiance += light.color * EmitterMisWeight(previousDelta, previousPdf, p);
    }
    return radiance;
}

OutputColor pathTracingImportanceSampling(Ray ray, int maxBounce)
{
    OutputColor result;
    result.render_color = vec3(0.0);
    result.normal_color = vec3(0.0);
    result.base_color = vec3(0.0);
    vec3 throughput = vec3(1.0);
    vec3 previousPoint = ray.startPoint;
    float previousPdf = 0.0;
    bool previousDelta = true;
    bool recordedFeatures = false;
    MediumStack media; media.size = 0;
    int depth = 0;
    // Transparent boundaries do not consume scattering depth, but remain bounded.
    for (int step=0; step<MAX_BOUNCES_LIMIT+MAX_SHADOW_LAYERS; ++step) {
        HitResult hit = hitBVH(ray);
        float sphereDistance;
        int sphereIndex = IntersectAnalyticLights(ray.startPoint, ray.direction, sphereDistance);
        float segment = min(hit.hitDistance, sphereDistance);
        if (step == 0 && hit.isHit && hit.isInside && hit.material.mediumtype != MEDIUM_NONE)
            media.entries[media.size++] = MaterialMedium(hit.material);
        Medium medium = CurrentMedium(media);
        bool scattered = false;
        if (medium.type == MEDIUM_SCATTER && medium.density > 0.0) {
            float freeFlight = -log(max(1.0-rand(), 1e-30)) / medium.density;
            if (freeFlight < segment) {
                if (depth >= maxBounce) break;
                vec3 point = ray.startPoint + freeFlight * ray.direction;
                throughput *= clamp(medium.color, 0.0, 1.0);
                if (!recordedFeatures) {
                    result.normal_color = -ray.direction;
                    result.base_color = clamp(medium.color, 0.0, 1.0);
                    recordedFeatures = true;
                }
                result.render_color += EstimateVolumeLighting(point, ray.direction, throughput, media);
                vec3 direction = normalize(SampleHG(-ray.direction, medium.g, rand(), rand()));
                previousPdf = PhaseHG(dot(-ray.direction, direction), medium.g);
                previousDelta = false;
                previousPoint = point;
                ray.startPoint = point;
                ray.direction = direction;
                scattered = true;
            }
            // Survival/no-event probability already includes exp(-sigma_t*d).
        } else if (medium.type == MEDIUM_ABSORB) {
            throughput *= MediumTransmittance(medium, segment);
        } else if (medium.type == MEDIUM_EMISSIVE) {
            result.render_color += throughput * medium.color * medium.density * segment;
        }

        if (!scattered) {
            if (sphereIndex >= 0 && sphereDistance <= hit.hitDistance) {
                EncodedLight light = GetEncodedLight(sphereIndex);
                vec3 point = ray.startPoint + sphereDistance * ray.direction;
                if (dot(ray.direction, point-light.positionOrDirection) < 0.0) {
                    float p = SphereLightPdf(light, previousPoint, ray.direction);
                    result.render_color += throughput * light.color *
                        EmitterMisWeight(previousDelta, previousPdf, p);
                }
                break;
            }
            if (!hit.isHit) {
                result.render_color += throughput * InfiniteEmission(ray.direction, previousPoint, previousDelta, previousPdf);
                break;
            }
            // Attenuation/free flight is resolved before emission at the endpoint.
            if (maxComponent(hit.material.emissive) > 0.0) {
                float p = LightPdf(previousPoint, ray.direction, hit.triangleIndex,
                                   distance(previousPoint, hit.hitPoint));
                result.render_color += throughput * hit.material.emissive *
                    EmitterMisWeight(previousDelta, previousPdf, p);
            }
            if (hit.material.alphaMode == ALPHA_MODE_TRANSPARENT) {
                if (!CrossMediumBoundary(media, hit, ray.direction)) break;
                ray.startPoint = OffsetRayOrigin(hit.hitPoint, hit.geometricNormal, ray.direction);
                continue;
            }
            if (!recordedFeatures) {
                result.normal_color = hit.normal;
                result.base_color = hit.material.baseColor;
                recordedFeatures = true;
            }
            if (depth >= maxBounce) break;
            float eta = hit.isInside ? hit.material.IOR : 1.0 / hit.material.IOR;
            if (HasNonDeltaLobes(hit.material, eta))
                result.render_color += EstimateDirectLighting(hit, throughput, eta, media);
            vec2 uv = CranleyPattersonRotation(vec2(sobelNumber[depth*2], sobelNumber[depth*2+1]));
            BsdfSample sample = SampleDisneyBSDF(-ray.direction, hit.normal, hit.material, eta, vec3(uv,rand()));
            throughput *= sample.weight;
            if (!CrossMediumBoundary(media, hit, sample.direction)) break;
            previousPoint = hit.hitPoint;
            previousPdf = sample.pdf;
            previousDelta = sample.delta;
            ray.direction = sample.direction;
            ray.startPoint = OffsetRayOrigin(hit.hitPoint, hit.geometricNormal, ray.direction);
        }
        if (any(isnan(throughput)) || any(isinf(throughput)) || maxComponent(throughput) <= 0.0) break;
        ++depth;
        if (depth >= 3) {
            float survival = clamp(maxComponent(throughput), 0.05, 0.95);
            if (rand() >= survival) break;
            throughput /= survival;
        }
    }
    return result;
}
