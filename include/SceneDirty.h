#pragma once

#include <cstdint>

enum class SceneDirtyFlag : uint32_t {
    None = 0u,
    Camera = 1u << 0,
    Material = 1u << 1,
    SceneBuffers = 1u << 2,
};

using SceneDirtyFlags = uint32_t;

constexpr SceneDirtyFlags toSceneDirtyFlags(SceneDirtyFlag flag)
{
    return static_cast<SceneDirtyFlags>(flag);
}

constexpr SceneDirtyFlags operator|(SceneDirtyFlag lhs, SceneDirtyFlag rhs)
{
    return toSceneDirtyFlags(lhs) | toSceneDirtyFlags(rhs);
}

constexpr SceneDirtyFlags operator|(SceneDirtyFlags lhs, SceneDirtyFlag rhs)
{
    return lhs | toSceneDirtyFlags(rhs);
}

constexpr SceneDirtyFlags operator|(SceneDirtyFlag lhs, SceneDirtyFlags rhs)
{
    return toSceneDirtyFlags(lhs) | rhs;
}

constexpr bool hasSceneDirtyFlag(SceneDirtyFlags flags, SceneDirtyFlag flag)
{
    return (flags & toSceneDirtyFlags(flag)) != 0;
}

constexpr SceneDirtyFlags kInitialSceneDirty =
    SceneDirtyFlag::Camera |
    SceneDirtyFlag::Material |
    SceneDirtyFlag::SceneBuffers;
