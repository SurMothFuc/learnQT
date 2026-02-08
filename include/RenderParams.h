#pragma once

#include <QObject>

#include <atomic>

class RenderParams : public QObject {
    Q_OBJECT

public:
    static RenderParams& instance();

#define RENDER_PARAMS_PARAM_LIST(X) \
    X(Denoise, denoise, bool, m_denoise, true) \
    X(RenderLow, renderLow, bool, m_renderLow, false) \
    X(UseTileRendering, useTileRendering, bool, m_useTileRendering, true) \
    X(UseEnvironmentMap, useEnvironmentMap, bool, m_useEnvironmentMap, true) \
    X(TileSize, tileSize, int, m_tileSize, 240)

#define DECLARE_PARAM_API(Camel, getter, type, member, defaultValue) \
    type getter() const; \
    void set##Camel(type v);
    RENDER_PARAMS_PARAM_LIST(DECLARE_PARAM_API)
#undef DECLARE_PARAM_API

    struct Stats {
        unsigned long long reads;
        unsigned long long writes;
    };
    Stats stats() const;

signals:
#define DECLARE_CHANGED_SIGNAL(Camel, getter, type, member, defaultValue) \
    void getter##Changed(type value);
    RENDER_PARAMS_PARAM_LIST(DECLARE_CHANGED_SIGNAL)
#undef DECLARE_CHANGED_SIGNAL

private:
    explicit RenderParams(QObject* parent = nullptr);
    RenderParams(const RenderParams&) = delete;
    RenderParams& operator=(const RenderParams&) = delete;
    RenderParams(RenderParams&&) = delete;
    RenderParams& operator=(RenderParams&&) = delete;

#define DECLARE_PARAM_STORAGE(Camel, getter, type, member, defaultValue) \
    std::atomic<type> member{defaultValue};
    RENDER_PARAMS_PARAM_LIST(DECLARE_PARAM_STORAGE)
#undef DECLARE_PARAM_STORAGE
#undef RENDER_PARAMS_PARAM_LIST

    mutable std::atomic<unsigned long long> m_reads{0};
    mutable std::atomic<unsigned long long> m_writes{0};
};
