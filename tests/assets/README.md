# Import regression assets

`embedded_texture_triangle.fbx` is a purpose-built, CC0-1.0 regression fixture.
It contains one textured triangle and a PNG stored in the FBX `Video.Content`
field. It is used to verify the real Assimp FBX embedded-texture path without
depending on an external sidecar image.

`texture_transform_triangle.gltf` is a purpose-built, CC0-1.0 glTF fixture
with authored tangents, an embedded PNG, mirrored/clamped sampler modes, and a
`KHR_texture_transform` offset, scale, and rotation.
