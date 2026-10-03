int samplingBounce=0;
vec3 EstimateDirectLighting(HitResult hit, vec3 history, float eta, MediumStack media)
{
#ifdef TRACE_PROFILE
    ++profileLightAttempts;
#endif
    LightSample lightSample = SampleOneLight(hit.hitPoint, SampleBounce(samplingBounce,0), SampleBounce(samplingBounce,1), SampleBounce(samplingBounce,2));
    if (!lightSample.valid || lightSample.pdf <= 0.0 || maxComponent(lightSample.radiance) <= 0.0) {
#ifdef TRACE_PROFILE
        ++profileInvalidLights;
#endif
        return vec3(0.0);
    }
    float bsdfPdf;
    vec3 f = EvaluateSurfaceBSDF(hit,lightSample.direction,eta,bsdfPdf);
    if (bsdfPdf <= 0.0 || maxComponent(f) <= 0.0) {
#ifdef TRACE_PROFILE
        ++profileInvalidLights;
#endif
        return vec3(0.0);
    }
    if (!CrossMediumBoundary(media, hit, lightSample.direction)) return vec3(0.0);
    vec3 tr = ShadowLightTransmittance(hit.hitPoint,hit.positionError,hit.geometricNormal,lightSample,media);
    return history * tr * lightSample.radiance * f * abs(dot(hit.normal, lightSample.direction)) *
        misMixWeight(lightSample.pdf, bsdfPdf) / lightSample.pdf;
}

vec3 EstimateVolumeLighting(vec3 point, vec3 incoming, vec3 history, MediumStack media)
{
#ifdef TRACE_PROFILE
    ++profileLightAttempts;
#endif
    LightSample lightSample = SampleOneLight(point, SampleBounce(samplingBounce,0), SampleBounce(samplingBounce,1), SampleBounce(samplingBounce,2));
    if (!lightSample.valid || lightSample.pdf <= 0.0 || maxComponent(lightSample.radiance) <= 0.0) {
#ifdef TRACE_PROFILE
        ++profileInvalidLights;
#endif
        return vec3(0.0);
    }
    Medium medium = CurrentMedium(media);
    float phasePdf = PhaseHG(dot(-incoming, lightSample.direction), medium.g);
    vec3 tr = ShadowLightTransmittance(point,vec3(0),vec3(0),lightSample,media);
    return history * tr * lightSample.radiance * phasePdf * misMixWeight(lightSample.pdf, phasePdf) / lightSample.pdf;
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
#ifdef DENOISE_GUIDES
    result.guidePosition = vec4(ray.direction,-1.0);
    result.guideNormal = vec4(0);
    result.guideAlbedo = vec4(0);
    result.guideMaterial = vec4(1,0,-1,1);
    bool fragilePath = false, volumePath = false;
#endif
    vec3 throughput = vec3(1.0);
    float etaScale=1.0;
    vec3 previousPoint = ray.startPoint;
    float previousPdf = 0.0;
    bool previousDelta = true;
    bool recordedFeatures = false;
    MediumStack media; media.size = 0;
    rayConeWidth=0.0;
    rayConeSpread=cameraFov>0.0 ? 2.0*tan(radians(cameraFov)*.5)/float(max(height,1)) : 0.0;
    int depth = 0;
    // Transparent boundaries do not consume scattering depth, but remain bounded.
    int step;
    for (step=0; step<MAX_BOUNCES_LIMIT+MAX_SHADOW_LAYERS; ++step) {
        samplingBounce=depth;
#ifdef DENOISE_GUIDES
        unstableAlpha = false;
#endif
        HitResult hit = hitBVH(ray);
#ifdef DENOISE_GUIDES
        fragilePath = fragilePath || unstableAlpha;
#endif
        float sphereDistance;
        int sphereIndex = IntersectAnalyticLights(ray.startPoint, ray.direction, sphereDistance);
        float segment = min(hit.hitDistance, sphereDistance);
#ifdef NO_PARTICIPATING_MEDIA
        if (false)
#else
        if (step == 0 && hit.isHit && hit.isInside && hit.material.mediumtype != MEDIUM_NONE)
#endif
            media.entries[media.size++] = MaterialMedium(hit.material);
        Medium medium = CurrentMedium(media);
#ifdef DENOISE_GUIDES
        volumePath = volumePath || (medium.type != MEDIUM_NONE && medium.density > 0.0);
#endif
        bool scattered = false;
        if (medium.type == MEDIUM_SCATTER && medium.density > 0.0) {
            float flightSample=step==depth?SampleBounce(depth,6):SampleEvent(0xf11e0000u+uint(depth),uint(step));
            float freeFlight = -log(max(1.0-flightSample, 1e-30)) / medium.density;
            if (freeFlight < segment) {
                if (depth >= maxBounce) break;
                vec3 point = ray.startPoint + freeFlight * ray.direction;
                rayConeWidth+=freeFlight*rayConeSpread;
                rayConeSpread=min(1.0,rayConeSpread+.25);
                throughput *= clamp(medium.color, 0.0, 1.0);
                if (!recordedFeatures) {
                    result.normal_color = -ray.direction;
                    result.base_color = clamp(medium.color, 0.0, 1.0);
                    recordedFeatures = true;
#ifdef DENOISE_GUIDES
                    result.guidePosition = vec4(point,distance(point,eye));
                    result.guideNormal = vec4(-ray.direction,0);
                    result.guideAlbedo = vec4(result.base_color,-1);
                    result.guideMaterial = vec4(0,4,-1,1);
#endif
                }
                result.render_color += EstimateVolumeLighting(point, ray.direction, throughput, media);
                vec3 direction = normalize(SampleHG(-ray.direction, medium.g, SampleBounce(depth,7), SampleBounce(depth,8)));
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
#ifdef DENOISE_GUIDES
                if (!recordedFeatures) {
                    result.guidePosition=vec4(point,distance(point,eye));
                    result.guideNormal=vec4(normalize(point-light.positionOrDirection),0);
                    result.guideAlbedo=vec4(0,0,0,float(-sphereIndex-2));
                    result.guideMaterial=vec4(0,5,-1,1);
                }
#endif
                if (dot(ray.direction, point-light.positionOrDirection) < 0.0) {
                    float p = SphereLightPdf(light, previousPoint, ray.direction);
                    result.render_color += throughput * light.color *
                        EmitterMisWeight(previousDelta, previousPdf, p);
                }
                break;
            }
            if (!hit.isHit) {
#ifdef DENOISE_GUIDES
                if(!recordedFeatures && fragilePath) result.guideMaterial.y=4.0;
#endif
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
                rayConeWidth+=hit.hitDistance*rayConeSpread;
#ifdef DENOISE_GUIDES
                fragilePath = true;
#endif
                if (!CrossMediumBoundary(media, hit, ray.direction)) break;
                ray.startPoint = OffsetRayOrigin(hit.hitPoint, hit.positionError, hit.geometricNormal, ray.direction);
                continue;
            }
            if (!recordedFeatures) {
                result.normal_color = hit.normal;
                result.base_color = hit.material.baseColor;
                recordedFeatures = true;
#ifdef DENOISE_GUIDES
                result.guidePosition=vec4(hit.hitPoint,distance(hit.hitPoint,eye));
                result.guideNormal=vec4(hit.normal,hit.material.roughness);
                int kind=1;
                if(hit.material.metallic>.5 || hit.material.roughness<.2) kind=2;
                if(fragilePath || hit.material.transmission>0.0 || hit.material.alphaMode==ALPHA_MODE_BLEND) kind=3;
                if(maxComponent(hit.material.emissive)>0.0) kind=5;
                int instance=int(texelFetch(surfaceTable,hit.triangleIndex).y);
                float identity=realtimeGuides ? texelFetch(reprojectionTable,instance*5+4).x : 0.0;
                result.guideAlbedo=vec4(hit.material.baseColor,identity);
                result.guideMaterial=vec4(hit.material.roughness,float(kind),float(instance),1);
#endif
            }
            if (depth >= maxBounce) break;
            float eta = hit.isInside ? hit.material.IOR : 1.0 / hit.material.IOR;
            if (HasNonDeltaLobes(hit.material, eta))
                result.render_color += EstimateDirectLighting(hit, throughput, eta, media);
            vec2 uv = UnifiedSamplerEnabled()?vec2(SampleBounce(depth,3),SampleBounce(depth,4)):
                CranleyPattersonRotation(vec2(sobelNumber[depth*2], sobelNumber[depth*2+1]));
            BsdfSample bsdfSample = SampleSurfaceBSDF(hit,eta,vec3(uv,SampleBounce(depth,5)));
            if(maxComponent(bsdfSample.weight)<=0.0)break;
            rayConeWidth+=hit.hitDistance*rayConeSpread;
            bool transmitted=dot(ray.direction,hit.geometricNormal)*dot(bsdfSample.direction,hit.geometricNormal)>0.0;
            // eta here is nIncident/nTransmitted, the inverse of pbrt's sample
            // eta. Undo its temporary radiance eta^2 only for the RR decision.
            if(transmitted && useEtaScaleRR)etaScale/=eta*eta;
            if(transmitted)rayConeSpread*=abs(eta);
            if(!bsdfSample.delta)
                rayConeSpread=min(1.0,rayConeSpread+.25*hit.material.roughness*hit.material.roughness);
            throughput *= bsdfSample.weight;
            if (!CrossMediumBoundary(media, hit, bsdfSample.direction)) break;
            previousPoint = hit.hitPoint;
            previousPdf = bsdfSample.pdf;
            previousDelta = bsdfSample.delta;
            ray.direction = bsdfSample.direction;
            ray.startPoint = OffsetRayOrigin(hit.hitPoint, hit.positionError, hit.geometricNormal, ray.direction);
        }
        if (any(isnan(throughput)) || any(isinf(throughput))) {
            pathDiagnosticFlags|=DIAG_NONFINITE;break;
        }
        if (maxComponent(throughput) <= 0.0) break;
        ++depth;
#ifdef TRACE_PROFILE
        ++profileScatters;
#endif
        if (depth >= (useEtaScaleRR?rrMinDepth:3)) {
            float survival = useEtaScaleRR?clamp(maxComponent(throughput)*etaScale,0.05,1.0):
                clamp(maxComponent(throughput), 0.05, 0.95);
            if (SampleBounce(depth-1,9) >= survival) break;
            throughput /= survival;
        }
    }
    if(step==MAX_BOUNCES_LIMIT+MAX_SHADOW_LAYERS) pathDiagnosticFlags|=DIAG_BOUNDARY_LIMIT;
#ifdef DENOISE_GUIDES
    if(volumePath) result.guideMaterial.y=4.0;
#endif
    return result;
}
