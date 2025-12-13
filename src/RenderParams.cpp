#include "RenderParams.h"
#include <QSettings>

RenderParams& RenderParams::instance() {
    static RenderParams inst;
    return inst;
}

RenderParams::RenderParams() {
}

bool RenderParams::denoise() const { 
    m_reads.fetch_add(1, std::memory_order_relaxed); 
    return m_denoise.load(std::memory_order_relaxed); 
}
void RenderParams::setDenoise(bool v) {
    bool old = m_denoise.exchange(v, std::memory_order_relaxed);
    m_writes.fetch_add(1, std::memory_order_relaxed);
    if (old != v) notify<bool>(m_onDenoise, mtx_denoise, v);
}

bool RenderParams::renderLow() const { m_reads.fetch_add(1, std::memory_order_relaxed); return m_renderLow.load(std::memory_order_relaxed); }
void RenderParams::setRenderLow(bool v) {
    bool old = m_renderLow.exchange(v, std::memory_order_relaxed);
    m_writes.fetch_add(1, std::memory_order_relaxed);
    if (old != v) notify<bool>(m_onRenderLow, mtx_renderLow, v);
}

bool RenderParams::useTileRendering() const { m_reads.fetch_add(1, std::memory_order_relaxed); return m_useTileRendering.load(std::memory_order_relaxed); }
void RenderParams::setUseTileRendering(bool v) {
    bool old = m_useTileRendering.exchange(v, std::memory_order_relaxed);
    m_writes.fetch_add(1, std::memory_order_relaxed);
    if (old != v) notify<bool>(m_onUseTileRendering, mtx_useTileRendering, v);
}

int RenderParams::tileSize() const { m_reads.fetch_add(1, std::memory_order_relaxed); return m_tileSize.load(std::memory_order_relaxed); }
void RenderParams::setTileSize(int v) {
    if (v < 16) v = 16;
    if (v > 2048) v = 2048;
    int old = m_tileSize.exchange(v, std::memory_order_relaxed);
    m_writes.fetch_add(1, std::memory_order_relaxed);
    if (old != v) notify<int>(m_onTileSize, mtx_tileSize, v);
}

bool RenderParams::useEnvironmentMap() const { m_reads.fetch_add(1, std::memory_order_relaxed); return m_useEnvironmentMap.load(std::memory_order_relaxed); }
void RenderParams::setUseEnvironmentMap(bool v) {
    bool old = m_useEnvironmentMap.exchange(v, std::memory_order_relaxed);
    m_writes.fetch_add(1, std::memory_order_relaxed);
    if (old != v) notify<bool>(m_onUseEnvironmentMap, mtx_useEnvironmentMap, v);
}

void RenderParams::onDenoiseChanged(const std::function<void(bool)>& cb) { std::lock_guard<std::mutex> lk(mtx_denoise); m_onDenoise.push_back(cb); }
void RenderParams::onRenderLowChanged(const std::function<void(bool)>& cb) { std::lock_guard<std::mutex> lk(mtx_renderLow); m_onRenderLow.push_back(cb); }
void RenderParams::onUseTileRenderingChanged(const std::function<void(bool)>& cb) { std::lock_guard<std::mutex> lk(mtx_useTileRendering); m_onUseTileRendering.push_back(cb); }
void RenderParams::onTileSizeChanged(const std::function<void(int)>& cb) { std::lock_guard<std::mutex> lk(mtx_tileSize); m_onTileSize.push_back(cb); }
void RenderParams::onUseEnvironmentMapChanged(const std::function<void(bool)>& cb) { std::lock_guard<std::mutex> lk(mtx_useEnvironmentMap); m_onUseEnvironmentMap.push_back(cb); }

template<typename T>
void RenderParams::notify(std::vector<std::function<void(T)>>& listeners, std::mutex& mtx, T value) {
    std::vector<std::function<void(T)>> cbs;
    {
        std::lock_guard<std::mutex> lk(mtx);
        cbs = listeners;
    }
    for (auto& f : cbs) f(value);
}


RenderParams::Stats RenderParams::stats() const { return { m_reads.load(), m_writes.load() }; }
