#pragma once
#include "MaterialTextureImage.h"
#include "Mesh.h"
#include <array>
#include <map>
#include <tuple>

struct MaterialMaskRecipe
{
    int channel = -1;
    float cutoff = 0;
};
struct MaterialMaskTexturePlan
{
    std::vector<TextureAsset> textures;
    std::vector<MaterialMaskRecipe> recipes;
    std::vector<std::array<int, 2>> materialSources; // baseColor, opacity
    int compositePointFallbacks = 0;
};
// Derived GPU views never alter the scene document or shared source image.
inline MaterialMaskTexturePlan planMaterialMaskTextures(const std::vector<TextureAsset> &textures,
                                                        const std::vector<Material> &materials)
{
    MaterialMaskTexturePlan plan;
    plan.textures = textures;
    plan.recipes.resize(textures.size());
    std::map<std::tuple<int, int, float>, int> derived;
    std::map<std::pair<int, int>, float> constants;
    auto valid = [&](int i) { return i >= 0 && i < int(textures.size()); };
    auto constant = [&](int source, int channel) {
        auto key = std::make_pair(source, channel);
        auto found = constants.find(key);
        if (found != constants.end())
            return found->second;
        const QImage rgba = textures[source].image.convertToFormat(QImage::Format_RGBA8888);
        int value = -1;
        bool same = true;
        for (int y = 0; y < rgba.height() && same; ++y)
            for (int x = 0; x < rgba.width(); ++x)
            {
                const int v = rgba.constScanLine(y)[x * 4 + channel];
                if (value < 0)
                    value = v;
                else if (v != value)
                {
                    same = false;
                    break;
                }
            }
        const float result = same ? value / 255.f : -1.f;
        constants[key] = result;
        return result;
    };
    for (const auto &m : materials)
    {
        std::array<int, 2> indices = {m.baseColorTex, m.opacityTex};
        if (m.alphaMode == Mask && m.emissive.lengthSquared() == 0)
        {
            const bool base = valid(m.baseColorTex), opacity = valid(m.opacityTex);
            int source = -1, channel = -1, slot = -1;
            float multiplier = m.opacity;
            if (base && !opacity)
            {
                source = m.baseColorTex;
                channel = 3;
                slot = 0;
            }
            else if (opacity && !base)
            {
                source = m.opacityTex;
                channel = 0;
                slot = 1;
            }
            else if (base && opacity)
            {
                const float a = constant(m.baseColorTex, 3), b = constant(m.opacityTex, 0);
                if (a >= 0)
                {
                    source = m.opacityTex;
                    channel = 0;
                    slot = 1;
                    multiplier *= a;
                }
                else if (b >= 0)
                {
                    source = m.baseColorTex;
                    channel = 3;
                    slot = 0;
                    multiplier *= b;
                }
                else
                    ++plan.compositePointFallbacks;
            }
            if (source >= 0)
            {
                const float cutoff = multiplier > 0 ? std::min(2.f, m.alphaCutoff / multiplier)
                                                    : (m.alphaCutoff > 0 ? 2.f : 0.f);
                const auto key = std::make_tuple(source, channel, cutoff);
                auto found = derived.find(key);
                int index;
                if (found != derived.end())
                    index = found->second;
                else
                {
                    index = int(plan.textures.size());
                    derived[key] = index;
                    plan.textures.push_back(textures[source]);
                    plan.recipes.push_back({channel, cutoff});
                }
                indices[slot] = index;
            }
        }
        plan.materialSources.push_back(indices);
    }
    return plan;
}
struct MaterialTextureMip
{
    QSize size;
    std::vector<float> pixels;
};
// Level zero retains authored values. Higher levels carry average binary
// coverage for this exact cutoff, rather than thresholding averaged alpha.
inline std::vector<MaterialTextureMip> prepareMaskTextureMips(const QImage &source, QSize size, bool color,
                                                              MaterialMaskRecipe recipe)
{
    std::vector<MaterialTextureMip> levels;
    levels.push_back({size, prepareMaterialTexturePixels(source, size, color)});
    auto rgba = source.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < rgba.height(); ++y)
        for (int x = 0; x < rgba.width(); ++x)
        {
            auto pixel = rgba.scanLine(y) + x * 4;
            pixel[recipe.channel] = pixel[recipe.channel] / 255.f >= recipe.cutoff ? 255 : 0;
        }
    auto coverage = prepareMaterialTexturePixels(rgba, size, false);
    if (size.width() < source.width() || size.height() < source.height())
        for (size_t i = recipe.channel; i < coverage.size(); i += 4)
            levels.front().pixels[i] = coverage[i];
    auto unscaled = levels.front().pixels;
    // The first average must start with thresholded source samples, not with
    // a threshold applied after resizing the source.
    for (size_t i = recipe.channel; i < unscaled.size(); i += 4)
        unscaled[i] = coverage[i];
    while (size.width() > 1 || size.height() > 1)
    {
        const QSize next(std::max(1, size.width() / 2), std::max(1, size.height() / 2));
        MaterialTextureMip mip{next, std::vector<float>(size_t(next.width()) * next.height() * 4)};
        for (int y = 0; y < next.height(); ++y)
            for (int x = 0; x < next.width(); ++x)
                for (int c = 0; c < 4; ++c)
                {
                    double sum = 0;
                    // Exact area average also handles odd source dimensions.
                    const double lx = double(x) * size.width() / next.width(),
                                 rx = double(x + 1) * size.width() / next.width();
                    const double ly = double(y) * size.height() / next.height(),
                                 ry = double(y + 1) * size.height() / next.height();
                    for (int sy = int(ly); sy < int(std::ceil(ry)); ++sy)
                        for (int sx = int(lx); sx < int(std::ceil(rx)); ++sx)
                        {
                            const double w =
                                std::max(0., std::min(rx, double(sx + 1)) - std::max(lx, double(sx))) *
                                std::max(0., std::min(ry, double(sy + 1)) - std::max(ly, double(sy))) /
                                ((rx - lx) * (ry - ly));
                            sum += w * unscaled[(size_t(sy) * size.width() + sx) * 4 + c];
                        }
                    mip.pixels[(size_t(y) * next.width() + x) * 4 + c] = float(sum);
                }
        size = next;
        unscaled = mip.pixels;
        levels.push_back(std::move(mip));
    }
    return levels;
}
