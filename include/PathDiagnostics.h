#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QtGlobal>
#include <cmath>
#include <stdexcept>
#include <vector>

// Shader stores cumulative event bits above the six denoiser path-class bits.
// Counts below are affected pixels, not a claim about number of traversal
// events.
inline QJsonObject summarizePathDiagnostics(const std::vector<float> &normal,
                                            const std::vector<float> &albedo,
                                            int width, unsigned samples,
                                            quint64 version) {
  if (width <= 0 || normal.size() != albedo.size() ||
      normal.size() % (size_t(width) * 4) != 0)
    throw std::invalid_argument("Mismatched diagnostic image dimensions");
  quint64 valid = 0, affected[6] = {};
  int first = -1;
  unsigned firstFlags = 0;
  const size_t pixels = normal.size() / 4;
  for (size_t p = 0; p < pixels; ++p) {
    const float count = normal[p * 4 + 3], encoded = albedo[p * 4 + 3];
    if (std::isfinite(count) && count >= 0)
      valid += quint64(count);
    const unsigned flags =
        std::isfinite(encoded) && encoded >= 0 ? unsigned(encoded) >> 8 : 1u;
    for (int bit = 0; bit < 6; ++bit)
      if (flags & (1u << bit))
        ++affected[bit];
    if (flags && first < 0) {
      first = int(p);
      firstFlags = flags;
    }
  }
  const quint64 expected = quint64(pixels) * samples;
  QJsonObject result{
      {"width", width},
      {"height", width > 0 ? int(pixels) / width : 0},
      {"samples", int(samples)},
      {"version", double(version)},
      {"validSamples", double(valid)},
      {"rejectedSamples", double(expected >= valid ? expected - valid : 0)},
      {"countMeaning", "affected pixels accumulated since reset; "
                       "rejectedSamples counts sample events"}};
  const char *names[] = {"nonFinite",   "rejected",       "invalidRay",
                         "bvhOverflow", "mediumOverflow", "boundaryLimit"};
  for (int i = 0; i < 6; ++i)
    result[names[i]] = double(affected[i]);
  result["firstAffectedPixel"] =
      first < 0 ? QJsonArray() : QJsonArray{first % width, first / width};
  result["firstFlags"] = int(firstFlags);
  return result;
}
