#pragma once

#include <QImage>
#include <QSize>

inline QImage prepareMaterialTextureImage(const QImage& source, const QSize& size)
{
    // Assimp uses lower-left UVs; QImage stores the top scanline first.
    QImage image = source.mirrored(false, true);
    if (image.size() != size) {
        image = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    // Qt's smooth scaler can return ARGB32_Premultiplied (BGRA bytes on
    // Windows). Convert last: GL_RGBA requires straight RGBA byte order.
    return image.convertToFormat(QImage::Format_RGBA8888);
}
