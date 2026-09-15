#pragma once

#include <QObject>

#include <atomic>

#define RENDER_PARAMS_PARAM(Camel, getter, type, member, defaultValue) \
public: \
    type getter() const { \
        m_reads.fetch_add(1, std::memory_order_relaxed); \
        return member.load(std::memory_order_relaxed); \
    } \
    void set##Camel(type v) { \
        const type old = member.exchange(v, std::memory_order_relaxed); \
        m_writes.fetch_add(1, std::memory_order_relaxed); \
        if (old != v) { \
            emit getter##Changed(v); \
        } \
    } \
    Q_SIGNAL void getter##Changed(type value); \
private: \
    std::atomic<type> member{defaultValue};

class RenderParams : public QObject {
    Q_OBJECT

public:
    // 交互期间（相机/对象拖动）的预览回退策略。
    enum InteractionMode
    {
        InteractionKeepPathtrace = 0, // 什么都不改：不改渲染模式、不改分辨率
        InteractionRaster = 1,        // 切到光栅化交互预览
        InteractionLowResolution = 2  // 只切 renderLow 的低分辨率路径追踪
    };

    struct Snapshot {
        bool denoise = true;
        bool renderLow = false;
        int interactionMode = InteractionRaster;
        bool useTileRendering = true;
        int tileSize = 240;
        bool useEnvironmentMap = true;
        int maxBounces = 4;
        int maxRenderFrames = 0;
        // 锁定后始终使用光栅化交互预览，不再回到路径追踪。
        bool rasterLocked = false;
        // 停手多久后从交互回退切回路径追踪。
        int interactionIdleMs = 250;

        bool operator==(const Snapshot& other) const {
            return denoise == other.denoise &&
                   renderLow == other.renderLow &&
                   interactionMode == other.interactionMode &&
                   useTileRendering == other.useTileRendering &&
                   tileSize == other.tileSize &&
                   useEnvironmentMap == other.useEnvironmentMap &&
                   maxBounces == other.maxBounces &&
                   maxRenderFrames == other.maxRenderFrames &&
                   rasterLocked == other.rasterLocked &&
                   interactionIdleMs == other.interactionIdleMs;
        }

        bool operator!=(const Snapshot& other) const {
            return !(*this == other);
        }
    };

    static RenderParams& instance();

    RENDER_PARAMS_PARAM(Denoise, denoise, bool, m_denoise, true)
    RENDER_PARAMS_PARAM(RenderLow, renderLow, bool, m_renderLow, false)
    RENDER_PARAMS_PARAM(InteractionMode, interactionMode, int, m_interactionMode, InteractionRaster)
    RENDER_PARAMS_PARAM(UseTileRendering, useTileRendering, bool, m_useTileRendering, true)
    RENDER_PARAMS_PARAM(UseEnvironmentMap, useEnvironmentMap, bool, m_useEnvironmentMap, true)
    RENDER_PARAMS_PARAM(TileSize, tileSize, int, m_tileSize, 240)
    RENDER_PARAMS_PARAM(MaxBounces, maxBounces, int, m_maxBounces, 4)
    RENDER_PARAMS_PARAM(MaxRenderFrames, maxRenderFrames, int, m_maxRenderFrames, 0)
    RENDER_PARAMS_PARAM(RasterLocked, rasterLocked, bool, m_rasterLocked, false)
    RENDER_PARAMS_PARAM(InteractionIdleMs, interactionIdleMs, int, m_interactionIdleMs, 250)

public:
    Snapshot snapshot() const;
    void applySnapshot(const Snapshot& s) {
        setDenoise(s.denoise); setRenderLow(s.renderLow); setInteractionMode(s.interactionMode);
        setUseTileRendering(s.useTileRendering);
        setTileSize(s.tileSize); setUseEnvironmentMap(s.useEnvironmentMap);
        setMaxBounces(s.maxBounces); setMaxRenderFrames(s.maxRenderFrames);
        setRasterLocked(s.rasterLocked); setInteractionIdleMs(s.interactionIdleMs);
    }

public:
    struct Stats {
        unsigned long long reads;
        unsigned long long writes;
    };
    Stats stats() const;

private:
    explicit RenderParams(QObject* parent = nullptr);
    RenderParams(const RenderParams&) = delete;
    RenderParams& operator=(const RenderParams&) = delete;
    RenderParams(RenderParams&&) = delete;
    RenderParams& operator=(RenderParams&&) = delete;

    mutable std::atomic<unsigned long long> m_reads{0};
    mutable std::atomic<unsigned long long> m_writes{0};
};

#undef RENDER_PARAMS_PARAM
