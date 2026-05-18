# MIS Light Sampling Design

## Context

The renderer currently samples paths through the Disney BSDF in `shaders/include/pathtrace.glsl`. Environment map helpers already exist in `shaders/include/hdr_utils.glsl`, and `src/common.cpp` builds an HDR sampling cache, but the miss branch still uses `mis_weight = 1.0`. Emissive geometry is only accumulated when a BSDF path happens to hit it, so small or distant lights are noisy.

This design adds next event estimation and multiple importance sampling across environment lighting, emissive triangles, and analytic lights.

## Goals

- Add one direct-light sample at each non-delta surface interaction.
- Support HDR environment direct sampling with a correct solid-angle PDF.
- Support arbitrary emissive triangle direct sampling.
- Add analytic light support for point, directional, and sphere lights.
- Use one light sampling contract so environment, triangle, and analytic lights can share MIS code.
- Keep the first implementation scoped to surface lighting. Volume MIS remains out of scope.

## Non-Goals

- No full glTF light import in this pass.
- No textured emission evaluation for emissive triangles in this pass.
- No transparent shadow-ray transmittance through alpha or participating media in this pass.
- No separate analytic rectangle light initially; rectangular area lights can be represented by emissive triangles.

## Architecture

CPU scene preparation will build light data after triangles and materials are available.

- `Scene` collects emissive triangles by testing material emission and triangle area.
- `Scene` stores a compact light list for analytic lights and emissive triangle references.
- `Renderer` uploads the light list to one or more GPU buffers alongside the existing triangle and BVH buffers.
- GLSL adds a light sampling module with `SampleOneLight()` and `LightPdf()` helpers.
- `pathTracingImportanceSampling()` calls direct lighting before sampling the BSDF continuation.

The light sampler returns all PDFs in solid angle measure at the shading point. This is the central invariant for MIS.

## Light Types

### Environment Light

The environment sampler uses the existing HDR texture and cache, but the cache must be corrected before MIS is enabled.

For lat-long environment maps, the discrete importance table should be proportional to:

```text
luminance(texel) * sin(colatitude)
```

Equivalently, with the shader's latitude angle convention, this is:

```text
luminance(texel) * max(cos(latitude), epsilon)
```

`hdrPdf(direction)` must return probability per steradian. Near the poles it clamps the Jacobian denominator to avoid infinities.

### Emissive Triangle Light

Each triangle with nonzero emission and nonzero area becomes a triangle light. Its selection weight is:

```text
area * luminance(emission)
```

Sampling chooses a triangle, samples barycentric coordinates uniformly over area, computes `L`, visibility distance, normal, emission, and converts area PDF to solid-angle PDF:

```text
pdf_solid_angle = pdf_area * distance^2 / abs(dot(lightNormal, -L))
```

Samples with backfacing lights or near-zero geometric terms are invalid.

### Analytic Lights

The first analytic light set is:

- `PointLight`: delta position light. NEE contribution uses weight 1 because it has no finite solid-angle density that BSDF sampling can hit.
- `DirectionalLight`: delta direction light. NEE contribution uses weight 1 for the same reason.
- `SphereLight`: finite area light. It should use solid-angle sampling when possible; if implementation risk is high, it may begin with surface-area sampling and the same area-to-solid-angle conversion as triangle lights.

Analytic lights are encoded in a separate light buffer with type, position/direction, color/intensity, radius, and optional reserved fields.

## Data Flow

1. Scene construction loads geometry and materials.
2. Scene finalization builds BVH and encoded triangles as it does today.
3. Scene finalization also builds:
   - emissive triangle indices and distribution weights,
   - analytic light records,
   - total light count and light-type counts.
4. Renderer uploads these buffers when scene data is uploaded.
5. Shader direct lighting samples one light from the combined distribution.
6. Shader BSDF continuation still samples one BSDF direction.
7. When a BSDF path hits the environment or a non-delta light, shader evaluates the matching light PDF and applies the BSDF-side MIS weight.

## MIS Rules

For direct light sampling:

```glsl
weight = PowerHeuristic(lightPdf, bsdfPdf);
contribution = throughput * Li * f * abs(dot(N, L)) * weight / lightPdf;
```

For BSDF-sampled environment or emissive-triangle hits:

```glsl
weight = PowerHeuristic(bsdfPdf, lightPdf);
contribution = throughput * emittedRadiance * weight;
```

Delta analytic lights use a direct-light weight of 1 and do not contribute to `LightPdf()` for BSDF-hit evaluation.

Specular or delta BSDF events should skip direct light sampling. Their next ray continues as today, and emitted light on the next hit is accumulated without MIS against direct light sampling.

## Visibility

Direct light samples use a shadow ray from the shading point to the sampled light.

- The ray origin uses the same hit-point offset convention as the path tracer or adds a small normal/direction epsilon.
- Environment and directional lights are visible when the shadow ray misses scene geometry.
- Point, sphere, and triangle lights are visible when no blocker is found before the sampled distance.
- Transparent and volumetric visibility are left for a later pass.

## Error Handling

- If no lights are available and the environment map is disabled, direct lighting returns an invalid sample.
- If the HDR map is black or the cache has zero total weight, the cache falls back to a uniform environment distribution.
- All light PDFs are clamped away from zero only for division safety; invalid samples are preferred when the true PDF is zero.
- Degenerate emissive triangles are ignored during CPU collection.
- Light buffer upload handles zero analytic lights and zero emissive triangles without changing shader control flow.

## Testing

Validation should cover:

- HDR-only scene: compare BSDF-only and MIS output for unbiased brightness with lower variance.
- Small emissive triangle scene: direct lighting should converge much faster than BSDF-only.
- Sphere/point/directional analytic light scenes: verify visibility, intensity falloff, and absence of double counting.
- Environment map disabled: shader should still render triangle and analytic lights.
- Black HDR map: no NaN or firefly spikes from PDF division.
- `hdrPdf()` sanity: sampled directions and evaluated PDFs use the same solid-angle measure.

## Implementation Order

1. Fix HDR cache weighting and `hdrPdf()` solid-angle conversion.
2. Add CPU light records and emissive triangle collection.
3. Upload light buffers and uniforms in `Renderer`.
4. Add GLSL light sampling and PDF helpers.
5. Integrate direct lighting and BSDF-side MIS in `pathtrace.glsl`.
6. Add analytic point, directional, and sphere light scene data.
7. Run focused render checks and build verification.
