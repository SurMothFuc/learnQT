#pragma once
#include <QJsonObject>
#include <QMetaType>
#include <QSize>
#include <QString>
#include "DenoiseMode.h"
#include <cmath>
struct RenderJobSettings
{
    QSize size{1920, 1080};
    int samples = 256, tileSize = 128, bounces = 8;
    bool denoise = true;
    bool antialiasing = false;
    DenoiseMode denoiseMode = DenoiseMode::OIDN;
    unsigned sampleSeed=0;
    int rrMinDepth=3;
    bool samplerSettingsValid=true;
    DenoiseMode effectiveDenoiseMode() const { return denoise ? denoiseMode : DenoiseMode::None; }
    static RenderJobSettings fromJson(const QJsonObject &o)
    {
        RenderJobSettings s;
        s.size = {o["width"].toInt(1920), o["height"].toInt(1080)};
        s.samples = o["samples"].toInt(256);
        s.tileSize = o["tileSize"].toInt(128);
        s.bounces = o["bounces"].toInt(8);
        s.denoise = o["denoise"].toBool(true);
        s.denoiseMode = readDenoiseMode(o);
        s.denoise = s.denoiseMode != DenoiseMode::None;
        s.antialiasing = o["antialiasing"].toBool(false);
        if(o.contains("sampleSeed")) {
            const double seed=o["sampleSeed"].toDouble(-1);
            s.samplerSettingsValid=o["sampleSeed"].isDouble() && std::isfinite(seed) && seed>=0 &&
                seed<=4294967295.0 && std::floor(seed)==seed;
            if(s.samplerSettingsValid)s.sampleSeed=unsigned(seed);
        }
        s.rrMinDepth=o["rrMinDepth"].toInt(3);
        if(o.contains("rrMinDepth")) {
            const double rr=o["rrMinDepth"].toDouble(-1);
            s.samplerSettingsValid=s.samplerSettingsValid && o["rrMinDepth"].isDouble() &&
                std::isfinite(rr) && rr>=0 && rr<=64 && std::floor(rr)==rr;
        }
        return s;
    }
    bool valid() const
    {
        return samplerSettingsValid && size.width() >= 16 && size.height() >= 16 && size.width() <= 16384 && size.height() <= 16384 &&
               qint64(size.width()) * size.height() <= 67108864 && samples > 0 && samples <= 1000000 &&
               tileSize >= 16 && tileSize <= 1024 && bounces > 0 && bounces <= 64 && rrMinDepth>=0 && rrMinDepth<=64;
    }
};
enum class RenderJobState
{
    Idle,
    Preparing,
    Rendering,
    Paused,
    Denoising,
    Completed,
    Stopped,
    Failed
};
inline QString renderJobText(RenderJobState s)
{
    switch (s)
    {
    case RenderJobState::Preparing:
        return QStringLiteral("准备");
    case RenderJobState::Rendering:
        return QStringLiteral("渲染中");
    case RenderJobState::Paused:
        return QStringLiteral("已暂停");
    case RenderJobState::Denoising:
        return QStringLiteral("降噪中");
    case RenderJobState::Completed:
        return QStringLiteral("完成");
    case RenderJobState::Stopped:
        return QStringLiteral("已停止");
    case RenderJobState::Failed:
        return QStringLiteral("失败");
    default:
        return QStringLiteral("编辑预览");
    }
}
struct RenderStats
{
    double seconds = 0, fps = 0, tileFps = 0, gpuMs = 0, gpuHistoryMs = 0, gpuCompositeMs = 0, oidnMs = 0,
           uploadMs = 0, blasMs = 0, tlasMs = 0, jobSeconds = 0;
    quint64 allocatedBytes = 0, geometryUploadBytes = 0;
    quint64 version = 0, accumulationVersion = 0, denoisedVersion = 0;
    double normalMinimum = 0, normalMaximum = 0;
    QSize auxiliarySize;
    int blasBuilds = 0, samples = 0, target = 0;
    bool tiled = false;
    bool previewDenoising = false;
    int denoisedSamples = 0, previewDenoiseSamples = 0;
    int batchTiles = 0, batchLimit = 1;
    quint64 compositeCount = 0;
    QSize size;
    RenderJobState state = RenderJobState::Idle;
    // 交互预览当前是否由光栅化提供，以及最近一帧光栅化耗时。
    bool rasterActive = false;
    bool computePathtrace = false;
    QString pathtraceBackend;
    double rasterMs = 0, rasterFps = 0;
    // GPU 拾取 pass 的最近耗时、本窗口重绘次数与累计 GPU 耗时，用于定位相机交互期间的额外整屏开销。
    double pickMs = 0;
    double pickWindowMs = 0;
    int pickPasses = 0;
    // 单次拾取重绘的最长耗时：确认补绘不是零成本，也不是每帧都在跑。
    double pickMaxMs = 0;
    double realtimeDenoiseMs = 0, historyAcceptance = 0;
    quint64 denoiseRounds = 0, denoiseBytes = 0, publishedFrames = 0, completedRounds = 0;
    double completedFps = 0, publishedFps = 0;
    int rasterSamples = 1;
    QString denoiseMode, denoiseError;
    // 最近一个统计窗口（约 200 ms）的渲染循环分解，单位为毫秒。
    int frames = 0, ticks = 0;
    double windowMs = 0, boundaryWaitMs = 0, loopMs = 0, cadenceSleepMs = 0, tailMs = 0,
           rasterSubmitMs = 0, pickSubmitMs = 0, presentMs = 0;
};
Q_DECLARE_METATYPE(RenderStats)
Q_DECLARE_METATYPE(RenderJobState)
