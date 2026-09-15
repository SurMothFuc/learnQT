#pragma once
#include <QVector3D>
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

// Three-band spherical harmonics of irradiance / pi. Integrate every HDR texel
// with its solid angle, so small bright sources are not lost by point subsampling.
inline std::array<QVector3D, 9> rasterDiffuseEnvironment(const float *rgb, int width, int height)
{
    std::array<QVector3D, 9> result{};
    if (!rgb || width <= 0 || height <= 0) return result;
    constexpr double pi = 3.14159265358979323846;
    std::array<std::array<double, 3>, 9> sum{};
    std::vector<double> cosine(width), sine(width), cosineSine(width), cosineDifference(width);
    const double phiWidth = 2 * pi / width;
    for (int x = 0; x < width; ++x)
    {
        const double a = 2 * pi * (double(x) / width - .5), b = a + phiWidth;
        cosine[x] = std::sin(b) - std::sin(a);
        sine[x] = std::cos(a) - std::cos(b);
        cosineSine[x] = .5 * (std::sin(b)*std::sin(b) - std::sin(a)*std::sin(a));
        cosineDifference[x] = .5 * (std::sin(2*b) - std::sin(2*a));
    }
    for (int y = 0; y < height; ++y)
    {
        const double a = pi * y / height, b = pi * (y + 1) / height;
        const double ca = std::cos(a), cb = std::cos(b), sa = std::sin(a), sb = std::sin(b);
        const double sinIntegral = ca - cb;
        const double sinSquared = .5 * (b-a) - .25 * (std::sin(2*b)-std::sin(2*a));
        const double cosSin = .5 * (sb*sb-sa*sa);
        const double cosSquaredSin = (ca*ca*ca-cb*cb*cb)/3;
        const double sinCubed = sinIntegral-cosSquaredSin;
        const double sinSquaredCos = (sb*sb*sb-sa*sa*sa)/3;
        for (int x = 0; x < width; ++x)
        {
            // Integrate each basis over the texel footprint rather than evaluating
            // its center. This also handles polar rows and constant 1x1 HDRs.
            const double basis[] = {.2820947918 * sinIntegral * phiWidth,
                .4886025119 * sinSquared * cosine[x], .4886025119 * cosSin * phiWidth,
                .4886025119 * sinSquared * sine[x], 1.0925484306 * sinSquaredCos * cosine[x],
                1.0925484306 * sinSquaredCos * sine[x],
                .3153915653 * (3*cosSquaredSin-sinIntegral) * phiWidth,
                1.0925484306 * sinCubed * cosineSine[x],
                .5462742153 * sinCubed * cosineDifference[x]};
            const size_t pixel = (size_t(y) * width + x) * 3;
            for (int c = 0; c < 3; ++c)
            {
                const double value = std::isfinite(rgb[pixel+c]) ? std::max(0.f, rgb[pixel+c]) : 0.;
                for (int i = 0; i < 9; ++i) sum[i][c] += value * basis[i];
            }
        }
    }
    for (int i = 0; i < 9; ++i)
    {
        const double convolution = i == 0 ? 1. : (i < 4 ? 2. / 3. : .25);
        result[i] = QVector3D(float(sum[i][0] * convolution), float(sum[i][1] * convolution),
                              float(sum[i][2] * convolution));
    }
    return result;
}
