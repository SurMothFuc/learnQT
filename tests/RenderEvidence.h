#pragma once
#include <QImage>
#include <QPainter>
#include <cmath>
#include <vector>

inline QImage evidenceImage(const std::vector<float> &pixels, int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const size_t i = (size_t(y) * width + x) * 4;
            auto encode = [&](int c) {
                const float v = std::max(0.f, std::min(1.f, pixels[i + c]));
                return qRound(255 * (v <= .0031308f ? 12.92f * v : 1.055f * std::pow(v, 1 / 2.4f) - .055f));
            };
            image.setPixel(x, height - 1 - y, qRgb(encode(0), encode(1), encode(2)));
        }
    return image;
}
inline bool saveEvidence(const QString &directory, const QString &name, const QImage &before,
                         const QImage &after, const QString &description)
{
    if (before.size() != after.size())
        return false;
    const int w = before.width(), h = before.height();
    QImage panel(w * 2, h + 110, QImage::Format_RGB32);
    panel.fill(QColor("#151b24"));
    QPainter painter(&panel);
    painter.setPen(Qt::white);
    painter.setFont(QFont("Segoe UI", 10));
    painter.drawText(QRect(12, 8, w - 24, 35), Qt::AlignVCenter, "Before / baseline");
    painter.drawText(QRect(w + 12, 8, w - 24, 35), Qt::AlignVCenter, "After / current");
    painter.drawImage(0, 48, before);
    painter.drawImage(w, 48, after);
    painter.drawText(QRect(12, h + 53, w * 2 - 24, 52), Qt::AlignVCenter | Qt::TextWordWrap, description);
    painter.end();
    return before.save(directory + "/" + name + "-before.png") &&
           after.save(directory + "/" + name + "-after.png") &&
           panel.save(directory + "/" + name + "-comparison.png");
}
