#pragma once
#include "SceneDocument.h"
#include <QColor>
#include <cmath>
#include <algorithm>

inline QString materialDisplayName(const SceneDocument &document, const QString &id)
{
    for (auto v : document.root["materials"].toArray()) {
        auto m = v.toObject();
        if (m["id"].toString() == id && !m["name"].toString().isEmpty()) return m["name"].toString();
    }
    for (auto v : document.root["objects"].toArray()) {
        auto o = v.toObject();
        if (o["material"].toString() == id) return o["name"].toString(QObject::tr("未命名")) + QObject::tr(" · 材质");
    }
    return QObject::tr("未命名材质");
}

// UI relevance follows the current shader, not the names of the controls.
inline bool materialFieldApplicable(const QJsonObject &m, QString field)
{
    if (field == "opacity" || field == "textures.opacity")
        return m["alphaMode"].toInt() == Mask || m["alphaMode"].toInt() == Blend;
    if (field == "alphaCutoff") return m["alphaMode"].toInt() == Mask;
    if (field == "mediumColor" || field == "mediumDensity") return m["mediumtype"].toInt() != None;
    if (field == "mediumAnisotropy") return m["mediumtype"].toInt() == Scatter;
    if (field == "normalScale" || field == "normalMapFlipY")
        return !m["textures"].toObject()["normal"].toString().isEmpty();
    if (field == "clearcoatGloss") return m["clearcoat"].toDouble() > 0;
    if (field == "sheenTint") return m["sheen"].toDouble() > 0;
    return true;
}
inline float materialSrgb(float x)
{
    x = qBound(0.f, x, 1.f);
    return x <= .0031308f ? 12.92f*x : 1.055f*std::pow(x, 1.f/2.4f)-.055f;
}
inline float materialLinear(float x)
{
    return x <= .04045f ? x/12.92f : std::pow((x+.055f)/1.055f, 2.4f);
}
inline QColor materialDisplayColor(QVector3D c)
{
    return QColor::fromRgbF(materialSrgb(c.x()), materialSrgb(c.y()), materialSrgb(c.z()));
}
inline QVector3D materialLinearColor(QColor c)
{
    return {materialLinear(float(c.redF())), materialLinear(float(c.greenF())), materialLinear(float(c.blueF()))};
}
inline QVector3D emissionHue(QVector3D c)
{
    float peak = std::max({c.x(), c.y(), c.z()});
    return peak > 0 ? c/peak : QVector3D(1,1,1);
}
