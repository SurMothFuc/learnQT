#pragma once
#include <QString>
#include <QJsonObject>

enum class DenoiseMode { None = 0, Realtime = 1, OIDN = 2 };
inline QString denoiseModeName(DenoiseMode mode)
{
    return mode == DenoiseMode::Realtime ? QStringLiteral("realtime") :
           mode == DenoiseMode::OIDN ? QStringLiteral("oidn") : QStringLiteral("none");
}
inline DenoiseMode readDenoiseMode(const QJsonObject &o)
{
    if (o.contains("denoiseMode"))
    {
        const auto value = o["denoiseMode"].toString();
        return value == "realtime" ? DenoiseMode::Realtime :
               value == "oidn" ? DenoiseMode::OIDN : DenoiseMode::None;
    }
    return o["denoise"].toBool(true) ? DenoiseMode::OIDN : DenoiseMode::None;
}
inline bool validDenoiseSettings(const QJsonObject &o)
{
    return (!o.contains("antialiasing") || o["antialiasing"].isBool()) &&
           (!o.contains("denoise") || o["denoise"].isBool()) &&
           (!o.contains("denoiseMode") || (o["denoiseMode"].isString() &&
            (o["denoiseMode"] == "none" || o["denoiseMode"] == "realtime" || o["denoiseMode"] == "oidn")));
}
