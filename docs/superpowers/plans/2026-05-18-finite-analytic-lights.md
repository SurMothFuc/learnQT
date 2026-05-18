# Finite Analytic Lights Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace default delta point/directional analytic lights with finite analytic sphere and sun disk lights.

**Architecture:** CPU light encoding will emit `Sphere` and `SunDisk` records only. GLSL light sampling will remove delta point/directional sampling paths, sample the sun disk over a finite solid angle, and keep all direct-light samples in solid-angle measure for MIS.

**Tech Stack:** C++14, Qt `QVector4D`, GLSL 330, OpenGL texture-buffer light records.

---

## File Structure

- Modify `include/Scene.h`: replace point/directional encoded light types with sun disk.
- Modify `src/Scene.cpp`: emit analytic sphere and sun disk lights from `addAnalyticLights()`.
- Modify `shaders/include/defines.glsl`: expose `LIGHT_TYPE_SUN_DISK`.
- Modify `shaders/include/light_sampling.glsl`: remove delta light samplers and add finite sun disk sampling.
- Create `tests/test_finite_analytic_lights.py`: static regression test for no delta analytic light paths.

## Task 1: Regression Test

- [ ] Add `tests/test_finite_analytic_lights.py` to assert the source tree no longer contains default delta light sampling paths and does contain sun disk sampling.
- [ ] Run `python3 tests/test_finite_analytic_lights.py` and verify it fails before implementation because `sample.delta = true` still exists and no sun disk type exists.

## Task 2: CPU Light Encoding

- [ ] Replace `EncodedLightPoint` and `EncodedLightDirectional` with `EncodedLightSunDisk` in `include/Scene.h`.
- [ ] Update `Scene::addAnalyticLights()` so it appends an `EncodedLightSunDisk` record and an `EncodedLightSphere` record, with matching weights.
- [ ] Store sun angular radius in `param0.w`, sun incoming direction in `param1.xyz`, and radiance in `param2.xyz`.

## Task 3: GLSL Sampling

- [ ] Replace point/directional light type defines with `LIGHT_TYPE_SUN_DISK`.
- [ ] Remove `SamplePointLight()` and `SampleDirectionalLight()`.
- [ ] Add `SampleSunDiskLight()` that samples a cone around the encoded sun direction, sets `delta=false`, and returns `finitePdf * selectPdf / solidAngle`.
- [ ] Route `LIGHT_TYPE_SUN_DISK` through `SampleOneLight()`.

## Task 4: Verification

- [ ] Run `python3 tests/test_finite_analytic_lights.py`.
- [ ] Run `cmake --build build --config Debug`.
- [ ] Review `git diff --stat`.
