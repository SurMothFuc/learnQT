#pragma once
#include "MaterialMaskTextures.h"
#include <cstdint>

struct MaterialTexturePlacement
{
    int pool = -1, layer = -1;
};
struct MaterialTextureView
{
    int source = 0;
    bool color = false;
    MaterialTexturePlacement placement;
};
struct MaterialTexturePool
{
    QSize requested{1, 1}, size{1, 1};
    int layers = 0;
    bool floating = false;
};
struct MaterialTexturePlan
{
    std::vector<MaterialTexturePool> pools;
    std::vector<MaterialTextureView> views;
    std::vector<MaterialTexturePlacement> data, color;
    uint64_t bytes = 0;
    int missingViews = 0, unusedSources = 0;
    bool reduced = false;
};
inline uint64_t materialPoolBytes(const MaterialTexturePool &pool)
{
    uint64_t pixels = 0;
    for (int w = pool.size.width(), h = pool.size.height();; w = std::max(1, w / 2), h = std::max(1, h / 2))
    {
        pixels += uint64_t(w) * h;
        if (w == 1 && h == 1)
            break;
    }
    return pixels * std::max(1, pool.layers) * (pool.floating ? 8 : 4);
}
// Four native-size/format buckets where available. GL 3.3's 16-unit floor
// uses two format buckets after packing surface PDFs into the material buffer.
inline MaterialTexturePlan planMaterialTextures(const MaterialMaskTexturePlan &mask,
                                                const std::vector<Material> &materials, int capacity,
                                                int maxSize, int maxLayers, uint64_t budget)
{
    if (capacity < 2 || capacity > 4 || maxSize < 1 || maxLayers < 1 || budget < 8)
        throw std::invalid_argument("Invalid material texture budget/capabilities");
    MaterialTexturePlan plan;
    plan.pools.resize(capacity);
    const int n = int(mask.textures.size());
    plan.data.resize(n);
    plan.color.resize(n);
    std::vector<bool> color(n), data(n), normal(n);
    for (int i = 0; i < int(materials.size()); ++i)
    {
        const auto &m = materials[i];
        const auto refs = mask.materialSources[i];
        for (int s : {refs[0], m.emissiveTex})
            if (s >= 0 && s < n)
                color[s] = true;
        for (int s : {refs[1], m.normalTex, m.metallicTex, m.roughnessTex})
            if (s >= 0 && s < n)
                data[s] = true;
        if (m.normalTex >= 0 && m.normalTex < n)
            normal[m.normalTex] = true;
    }
    auto rounded = [&](int dimension) {
        int value = 1;
        while (value < dimension && value < maxSize)
            value *= 2;
        return std::min(value, maxSize);
    };
    auto place = [&](int source, bool decode, bool floating) {
        const auto &image = mask.textures[source].image;
        const bool smallImage = std::max(image.width(), image.height()) <= 512;
        const int poolIndex = capacity == 2   ? (floating ? 0 : 1)
                              : capacity == 3 ? (floating ? (smallImage ? 0 : 1) : 2)
                                              : (floating ? (smallImage ? 0 : 1) : (smallImage ? 2 : 3));
        auto &pool = plan.pools[poolIndex];
        pool.floating = floating;
        if (pool.layers >= maxLayers)
        {
            ++plan.missingViews;
            return MaterialTexturePlacement{};
        }
        pool.requested = QSize(std::max(pool.requested.width(), rounded(image.width())),
                               std::max(pool.requested.height(), rounded(image.height())));
        MaterialTexturePlacement ref{poolIndex, pool.layers++};
        plan.views.push_back({source, decode, ref});
        return ref;
    };
    for (int source = 0; source < n; ++source)
    {
        if (!color[source] && !data[source])
        {
            ++plan.unusedSources;
            continue;
        }
        if (data[source])
            plan.data[source] = place(source, false, normal[source] || mask.recipes[source].channel >= 0);
        if (color[source])
            plan.color[source] = place(source, true, true);
        else
            plan.color[source] = MaterialTexturePlacement{};
        if (!data[source])
            plan.data[source] = plan.color[source];
    }
    auto bytes = [&] {
        uint64_t result = 0;
        for (const auto &pool : plan.pools)
            result += materialPoolBytes(pool);
        return result;
    };
    for (auto &pool : plan.pools)
        pool.size = pool.requested;
    while (bytes() > budget)
    {
        int largest = -1;
        uint64_t maximum = 0;
        for (int i = 0; i < capacity; ++i)
        {
            const auto &pool = plan.pools[i];
            const auto amount = materialPoolBytes(pool);
            if ((pool.size.width() > 1 || pool.size.height() > 1) && amount > maximum)
            {
                largest = i;
                maximum = amount;
            }
        }
        if (largest < 0)
            throw std::runtime_error("Texture budget cannot fit minimum material views");
        auto &pool = plan.pools[largest];
        pool.size = QSize(std::max(1, pool.size.width() / 2), std::max(1, pool.size.height() / 2));
        plan.reduced = true;
    }
    plan.bytes = bytes();
    return plan;
}
