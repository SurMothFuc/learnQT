#include "RenderParams.h"

RenderParams& RenderParams::instance() {
    static RenderParams inst;
    return inst;
}

RenderParams::RenderParams(QObject* parent)
    : QObject(parent) {
}

RenderParams::Stats RenderParams::stats() const {
    return {m_reads.load(), m_writes.load()};
}

RenderParams::Snapshot RenderParams::snapshot() const {
    m_reads.fetch_add(1, std::memory_order_relaxed);
    return Snapshot{
        m_denoise.load(std::memory_order_relaxed),
        m_renderLow.load(std::memory_order_relaxed),
        m_useTileRendering.load(std::memory_order_relaxed),
        m_tileSize.load(std::memory_order_relaxed),
        m_useEnvironmentMap.load(std::memory_order_relaxed)
    };
}
