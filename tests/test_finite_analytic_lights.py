#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def main():
    scene_h = read("include/Scene.h")
    scene_cpp = read("src/Scene.cpp")
    defines = read("shaders/include/defines.glsl")
    light_sampling = read("shaders/include/light_sampling.glsl")

    assert "EncodedLightSunDisk" in scene_h
    assert "EncodedLightPoint" not in scene_h
    assert "EncodedLightDirectional" not in scene_h

    assert "LIGHT_TYPE_SUN_DISK" in defines
    assert "LIGHT_TYPE_POINT" not in defines
    assert "LIGHT_TYPE_DIRECTIONAL" not in defines

    assert "EncodedLightSunDisk" in scene_cpp
    assert "EncodedLightSphere" in scene_cpp
    assert "EncodedLightPoint" not in scene_cpp
    assert "EncodedLightDirectional" not in scene_cpp
    assert "sunSolidAngle" in scene_cpp
    assert "sunIrradiance" in scene_cpp
    assert "sunIrradiance / sunSolidAngle" in scene_cpp
    assert "luminance(sunIrradiance)" in scene_cpp

    assert "SampleSunDiskLight" in light_sampling
    assert "SampleSphereLight" in light_sampling
    assert "SamplePointLight" not in light_sampling
    assert "SampleDirectionalLight" not in light_sampling
    assert "sample.delta = true" not in light_sampling


if __name__ == "__main__":
    main()
