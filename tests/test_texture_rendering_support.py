#!/usr/bin/env python3
import json
import base64
import re
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "resources/models/texture_test/Lantern/Lantern.glb"
FBX_FIXTURE = ROOT / "tests/assets/embedded_texture_triangle.fbx"


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def read_glb_json(path):
    data = path.read_bytes()
    magic, version, declared_length = struct.unpack_from("<4sII", data)
    assert magic == b"glTF"
    assert version == 2
    assert declared_length == len(data)

    json_length, json_type = struct.unpack_from("<II", data, 12)
    assert json_type == 0x4E4F534A
    return json.loads(data[20:20 + json_length].decode("utf-8"))


def test_official_glb_exercises_the_required_texture_slots():
    document = read_glb_json(MODEL)
    assert document["asset"]["version"] == "2.0"
    assert len(document["images"]) >= 4

    material = document["materials"][0]
    pbr = material["pbrMetallicRoughness"]
    assert "baseColorTexture" in pbr
    assert "metallicRoughnessTexture" in pbr
    assert "normalTexture" in material
    assert "emissiveTexture" in material


def test_cpu_import_and_gpu_sampling_contract_stays_connected():
    mesh = read("src/Mesh.cpp")
    scene = read("src/Scene.cpp")
    defines = read("shaders/include/defines.glsl")
    uniforms = read("shaders/include/uniforms.glsl")
    material_shader = read("shaders/include/bvh_material.glsl")
    light_shader = read("shaders/include/light_sampling.glsl")
    renderer = read("src/renderer.cpp")
    main_window = read("src/learnQT.cpp")
    cmake = read("CMakeLists.txt")

    assert "decodeEmbeddedTexture" in mesh
    assert "aiTextureType_GLTF_METALLIC_ROUGHNESS" in mesh
    assert "result.metallicChannel =" in mesh
    assert "result.roughnessChannel =" in mesh
    assert "aiProcess_CalcTangentSpace" in mesh
    assert "HasTangentsAndBitangents" in mesh
    assert "AI_MATKEY_UVTRANSFORM" in mesh
    assert "AI_MATKEY_GLTF_MAPPINGFILTER_MIN" in mesh
    assert "AI_MATKEY_GLTF_MAPPINGFILTER_MAG" in mesh
    assert "mapModes" in mesh
    assert "buildBedroomScene" not in scene
    bedroom = read("resources/scenes/bedroom.scene.json")
    assert "panel-wood-3.jpg" in bedroom
    assert "wallpaper-1.jpg" in bedroom
    assert "wood4.jpg" in bedroom
    assert "Teapot.png" in bedroom
    assert "Lantern.glb" in read("resources/scenes/lantern.scene.json")
    assert "prepareScene" in scene
    assert "loadModelScene" in scene
    assert "LoadModelButton" in main_window

    assert "#define SIZE_TRIANGLE   20" in defines
    assert "uniform sampler2DArray materialTextures;" in uniforms
    assert "uniform samplerBuffer materialTextureInfo;" in uniforms
    assert "SrgbToLinear" in material_shader
    assert "ApplyNormalMap" in material_shader
    assert "importedTangent" in material_shader
    assert "TransformMaterialUV" in material_shader
    assert "WrapTextureCoordinate" in material_shader
    assert "ALPHA_MODE_MASK" in material_shader
    assert "ALPHA_MODE_BLEND" in material_shader
    assert "GetTriangleLightSelectPdf" in material_shader
    assert "int middle = low + (high - low) / 2;" in light_shader
    assert "materialTextureInfoBuffer" in renderer
    assert ".mirrored(false, true)" in renderer
    assert "gpu_render_regression" in cmake
    assert "--regression-lantern" in cmake
    assert "import_fbx_embedded_texture" in cmake


def test_fbx_fixture_contains_real_embedded_png_payload():
    source = FBX_FIXTURE.read_text(encoding="utf-8")
    match = re.search(r'Content:\s*,\s*"([A-Za-z0-9+/=]+)"', source)
    assert match is not None
    payload = base64.b64decode(match.group(1), validate=True)
    assert payload.startswith(b"\x89PNG\r\n\x1a\n")
    assert 'C: "OO",5000,4000' in source


if __name__ == "__main__":
    test_official_glb_exercises_the_required_texture_slots()
    test_cpu_import_and_gpu_sampling_contract_stays_connected()
    test_fbx_fixture_contains_real_embedded_png_payload()
