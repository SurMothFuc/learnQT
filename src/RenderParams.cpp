#include "RenderParams.h"

RenderParams& RenderParams::instance() {
    static RenderParams inst;
    return inst;
}

RenderParams::RenderParams(QObject* parent)
    : QObject(parent) {
}

#define RENDER_PARAMS_PARAM_LIST(X) \
    X(Denoise, denoise, bool, m_denoise, true) \
    X(RenderLow, renderLow, bool, m_renderLow, false) \
    X(UseTileRendering, useTileRendering, bool, m_useTileRendering, true) \
    X(UseEnvironmentMap, useEnvironmentMap, bool, m_useEnvironmentMap, true) \
    X(TileSize, tileSize, int, m_tileSize, 240)

#define DEFINE_PARAM_API(Camel, getter, type, member, defaultValue) \
    type RenderParams::getter() const { \
        m_reads.fetch_add(1, std::memory_order_relaxed); \
        return member.load(std::memory_order_relaxed); \
    } \
    void RenderParams::set##Camel(type v) { \
        type old = member.exchange(v, std::memory_order_relaxed); \
        m_writes.fetch_add(1, std::memory_order_relaxed); \
        if (old != v) { \
            emit getter##Changed(v); \
        } \
    }
RENDER_PARAMS_PARAM_LIST(DEFINE_PARAM_API)
#undef DEFINE_PARAM_API
#undef RENDER_PARAMS_PARAM_LIST

RenderParams::Stats RenderParams::stats() const {
    return {m_reads.load(), m_writes.load()};
}
