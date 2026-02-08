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
    static RenderParams& instance();

    RENDER_PARAMS_PARAM(Denoise, denoise, bool, m_denoise, true)
    RENDER_PARAMS_PARAM(RenderLow, renderLow, bool, m_renderLow, false)
    RENDER_PARAMS_PARAM(UseTileRendering, useTileRendering, bool, m_useTileRendering, true)
    RENDER_PARAMS_PARAM(UseEnvironmentMap, useEnvironmentMap, bool, m_useEnvironmentMap, true)
    RENDER_PARAMS_PARAM(TileSize, tileSize, int, m_tileSize, 240)

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
