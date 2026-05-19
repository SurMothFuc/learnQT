# MIS Light Sampling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add next event estimation and MIS for environment maps, emissive triangles, and point/directional/sphere analytic lights.

**Architecture:** CPU scene finalization builds compact light records and uploads them through `Renderer` as a texture buffer. GLSL samples one light at each non-transparent surface bounce, evaluates BSDF and visibility, and applies power-heuristic MIS against the existing BSDF continuation path.

**Tech Stack:** C++14, Qt `QVector4D`, OpenGL 3.3 texture buffers, GLSL 330 path tracing shader.

---

## File Structure

- Modify `include/Scene.h`: add `Light_encoded`, analytic light helpers, and light storage fields.
- Modify `src/Scene.cpp`: collect emissive triangles, add default analytic light records, and rebuild light data when materials change.
- Modify `include/renderer.h`: add light buffer GL handles and upload method declaration.
- Modify `src/renderer.cpp`: upload/bind the light buffer, set light uniforms, and release GL resources.
- Modify `include/common.h` and `src/common.cpp`: fix HDR cache generation to use lat-long solid-angle weights and zero-weight fallback.
- Modify `shaders/include/defines.glsl`: define light types and light record size.
- Modify `shaders/include/uniforms.glsl`: expose light buffer and counts.
- Create `shaders/include/light_sampling.glsl`: implement `SampleOneLight()` and light PDF helpers.
- Modify `shaders/include/hdr_utils.glsl`: return environment PDF per steradian.
- Modify `shaders/include/bvh_material.glsl`: include hit triangle id for BSDF-side emissive MIS.
- Modify `shaders/pathtrace.frag`: include the new light sampling module.
- Modify `shaders/include/pathtrace.glsl`: add direct-light sampling and BSDF-side MIS.

## Task 1: Baseline And HDR Cache Safety

**Files:**
- Modify: `include/common.h`
- Modify: `src/common.cpp`
- Modify: `shaders/include/hdr_utils.glsl`

- [ ] **Step 1: Run baseline build**

Run: `cmake --build build --config Debug`

Expected: build either passes or reports pre-existing toolchain issues before code changes.

- [ ] **Step 2: Fix HDR cache weighting**

Update `calculateHdrCache()` so texel weights use:

```cpp
const float v = (static_cast<float>(i) + 0.5f) / static_cast<float>(height);
const float latitude = PI * (0.5f - v);
const float jacobian = std::max(std::cos(latitude), 1e-6f);
const float lum = std::max(0.0f, 0.212671f * R + 0.715160f * G + 0.072169f * B);
pdf[i][j] = lum * jacobian;
```

If the total is zero, fill `pdf` uniformly with `1.0f / (width * height)`.

- [ ] **Step 3: Fix environment PDF conversion**

In `hdrPdf()`, compute:

```glsl
float sinTheta = max(sqrt(max(0.0, 1.0 - L.y * L.y)), 1e-6);
float texelCount = float(hdrResolution * hdrResolution / 2);
return texelPdf * texelCount / (TWO_PI * PI * sinTheta);
```

- [ ] **Step 4: Build after HDR changes**

Run: `cmake --build build --config Debug`

Expected: compilation succeeds, or GLSL compile failures are reported by runtime shader creation in later manual render checks.

## Task 2: CPU Light Records

**Files:**
- Modify: `include/Scene.h`
- Modify: `src/Scene.cpp`

- [ ] **Step 1: Add encoded light storage**

Add:

```cpp
struct Light_encoded {
    QVector4D param0;
    QVector4D param1;
    QVector4D param2;
    QVector4D param3;
};
```

`param0` stores `(type, triangleIndex, pdfSelect, radius)`, `param1` stores position or direction, `param2` stores color, and `param3` stores reserved data.

- [ ] **Step 2: Add scene fields and helpers**

Add `std::vector<Light_encoded> lights_encoded;`, `float lightPowerSum`, and private helper declarations:

```cpp
void buildLightData();
void addAnalyticLights();
```

- [ ] **Step 3: Collect emissive triangles**

After `DataEncode()`, iterate sorted `triangles`, compute area and luminance, and append triangle lights with selection PDF `weight / totalWeight`.

- [ ] **Step 4: Add default analytic lights**

For the benchmark scene, add compact point, directional, and sphere light records so shader code paths are exercised without importing a new scene format.

- [ ] **Step 5: Refresh light data on material edits**

At the end of `Scene::updateMaterial()`, call `buildLightData()` so emissive-triangle light weights match edited materials.

## Task 3: Renderer Light Buffer Upload

**Files:**
- Modify: `include/renderer.h`
- Modify: `src/renderer.cpp`

- [ ] **Step 1: Add GL handles**

Add `GLuint tboLights = 0;` and `GLuint lightsTextureBuffer = 0;`.

- [ ] **Step 2: Upload light records**

Implement `uploadLightBuffer(bool recreateResources)` using `GL_RGBA32F` texture buffer storage over `Scene::getInstance().lights_encoded`.

- [ ] **Step 3: Bind light buffer during render**

Bind the light buffer to texture unit 5 in both `renderTile()` and `renderFullImage()`:

```cpp
pathtrace_program->setUniformValue("lights", 5);
glActiveTexture(GL_TEXTURE5);
glBindTexture(GL_TEXTURE_BUFFER, lightsTextureBuffer);
```

- [ ] **Step 4: Set light uniforms**

In `syncSceneBuffers()` and `syncMaterialBuffer()`, upload the light buffer and set `nLights`.

## Task 4: GLSL Light Sampling

**Files:**
- Modify: `shaders/include/defines.glsl`
- Modify: `shaders/include/uniforms.glsl`
- Create: `shaders/include/light_sampling.glsl`
- Modify: `shaders/include/bvh_material.glsl`

- [ ] **Step 1: Add GLSL light definitions**

Add light type defines for environment, triangle, point, directional, and sphere lights.

- [ ] **Step 2: Add uniforms**

Add:

```glsl
uniform int nLights;
uniform samplerBuffer lights;
```

- [ ] **Step 3: Preserve hit triangle id**

Add `int triangleIndex;` to `HitResult`, initialize it to `-1`, and assign it when `hitBVH()` finds the closest triangle.

- [ ] **Step 4: Implement light sampling helpers**

Create helpers for fetching encoded lights, sampling triangle/point/directional/sphere lights, evaluating environment PDF, and estimating the combined light PDF for a BSDF-sampled direction.

## Task 5: Integrate NEE And MIS

**Files:**
- Modify: `shaders/pathtrace.frag`
- Modify: `shaders/include/pathtrace.glsl`

- [ ] **Step 1: Include light sampling**

Include `light_sampling.glsl` after `hdr_utils.glsl` and before `pathtrace.glsl`.

- [ ] **Step 2: Add direct lighting at surface hits**

Before BSDF continuation, sample one direct light, shadow test it, evaluate `DisneyEval()`, and accumulate:

```glsl
o_c.render_color += history * sample.radiance * f * abs(dot(N, L)) * misWeight / sample.pdf;
```

- [ ] **Step 3: Add BSDF-side MIS for miss and emissive hits**

For environment misses and emissive triangle hits reached from a BSDF sample, use `LightPdf()` and `misMixWeight(pdf_brdf, pdf_light)`.

- [ ] **Step 4: Avoid double counting**

When direct lighting is active, keep emissive accumulation only for camera hits, delta/specular chains, or BSDF-side MIS weighted hits.

## Task 6: Verification

**Files:**
- Read: `docs/superpowers/specs/2026-05-18-mis-light-sampling-design.md`

- [ ] **Step 1: Build**

Run: `cmake --build build --config Debug`

Expected: exit code 0.

- [ ] **Step 2: Check tracked changes**

Run: `git diff --stat`

Expected: only MIS implementation files, the plan, and pre-existing user changes are present.

- [ ] **Step 3: Review requirements**

Confirm each design requirement maps to code:

```text
HDR direct sampling: common.cpp, hdr_utils.glsl, light_sampling.glsl
Emissive triangles: Scene.cpp, light_sampling.glsl
Analytic lights: Scene.cpp, light_sampling.glsl
Renderer upload: renderer.h, renderer.cpp
NEE and MIS: pathtrace.glsl
```
