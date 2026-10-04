#pragma once

#include <QImage>
#include <QSize>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

inline QImage prepareMaterialTextureImage(const QImage &source,
                                          const QSize &size) {
  // Assimp uses lower-left UVs; QImage stores the top scanline first.
  QImage image = source.mirrored(false, true);
  if (image.size() != size) {
    image = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }
  // Qt's smooth scaler can return ARGB32_Premultiplied (BGRA bytes on
  // Windows). Convert last: GL_RGBA requires straight RGBA byte order.
  return image.convertToFormat(QImage::Format_RGBA8888);
}

// Stream rows rather than allocating a full float copy of a large source image.
// Decode before resizing; RGB and alpha are independent straight channels.
inline std::vector<float> prepareMaterialTexturePixels(const QImage &source,
                                                       const QSize &size,
                                                       bool decodeSrgb) {
  if (source.isNull() || size.width() <= 0 || size.height() <= 0)
    throw std::invalid_argument(
        "Material texture resampling requires nonempty images");
  QImage rgba = source.convertToFormat(QImage::Format_RGBA8888);
  std::array<float, 256> values;
  for (int i = 0; i < 256; ++i) {
    const float v = i / 255.f;
    values[i] =
        decodeSrgb
            ? (v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f))
            : v;
  }
  const int w = size.width(), h = size.height(), sw = rgba.width(),
            sh = rgba.height();
  std::vector<float> output(size_t(w) * h * 4, 0), row(size_t(w) * 4, 0);
  const double sx = double(sw) / w, sy = double(sh) / h;
  auto interval = [](int dst, double scale, int &begin, int &end, double &left,
                     double &right) {
    if (scale >= 1) {
      left = dst * scale;
      right = (dst + 1) * scale;
      begin = int(std::floor(left));
      end = int(std::ceil(right));
    } else {
      left = (dst + .5) * scale - .5;
      begin = int(std::floor(left));
      end = begin + 2;
      right = left + 1;
    }
  };
  auto weight = [](int src, double left, double right, double scale) {
    return scale >= 1 ? std::max(0.0, std::min(right, double(src + 1)) -
                                          std::max(left, double(src))) /
                            scale
                      : std::max(0.0, 1.0 - std::abs(double(src) - left));
  };
  for (int y = 0; y < h; ++y) {
    int y0, y1;
    double yl, yr;
    interval(y, sy, y0, y1, yl, yr);
    for (int iy = y0; iy < y1; ++iy) {
      const float wy = float(weight(iy, yl, yr, sy));
      if (wy == 0)
        continue;
      const auto bytes =
          rgba.constScanLine(sh - 1 - std::max(0, std::min(sh - 1, iy)));
      std::fill(row.begin(), row.end(), 0.f);
      for (int x = 0; x < w; ++x) {
        int x0, x1;
        double xl, xr;
        interval(x, sx, x0, x1, xl, xr);
        for (int ix = x0; ix < x1; ++ix) {
          const float wx = float(weight(ix, xl, xr, sx));
          const auto pixel = bytes + std::max(0, std::min(sw - 1, ix)) * 4;
          for (int c = 0; c < 4; ++c)
            row[x * 4 + c] +=
                wx * (c == 3 ? pixel[c] / 255.f : values[pixel[c]]);
        }
      }
      for (size_t i = 0; i < row.size(); ++i)
        output[(size_t(y) * w * 4) + i] += wy * row[i];
    }
  }
  return output;
}
