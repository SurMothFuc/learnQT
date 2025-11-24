#pragma once
#include <atomic>
#include <functional>
#include <vector>
#include <mutex>

enum class RenderParamID {
    Denoise,
    RenderLow,
    UseTileRendering,
    TileSize,
    UseEnvironmentMap
};

class RenderParams {
public:
    static RenderParams& instance();

    bool denoise() const;
    void setDenoise(bool v);

    bool renderLow() const;
    void setRenderLow(bool v);

    bool useTileRendering() const;
    void setUseTileRendering(bool v);

    int tileSize() const;
    void setTileSize(int v);

    bool useEnvironmentMap() const;
    void setUseEnvironmentMap(bool v);

    void onDenoiseChanged(const std::function<void(bool)>& cb);
    void onRenderLowChanged(const std::function<void(bool)>& cb);
    void onUseTileRenderingChanged(const std::function<void(bool)>& cb);
    void onTileSizeChanged(const std::function<void(int)>& cb);
    void onUseEnvironmentMapChanged(const std::function<void(bool)>& cb);

    void load();
    void save() const;

    struct Stats { unsigned long long reads; unsigned long long writes; };
    Stats stats() const;

private:
    RenderParams();
    RenderParams(const RenderParams&) = delete;
    RenderParams& operator=(const RenderParams&) = delete;

    template<typename T>
    void notify(std::vector<std::function<void(T)>>& listeners, std::mutex& mtx, T value);

private:
    std::atomic<bool> m_denoise{false};
    std::atomic<bool> m_renderLow{false};
    std::atomic<bool> m_useTileRendering{true};
    std::atomic<int>  m_tileSize{240};
    std::atomic<bool> m_useEnvironmentMap{true};

    std::vector<std::function<void(bool)>> m_onDenoise;
    std::vector<std::function<void(bool)>> m_onRenderLow;
    std::vector<std::function<void(bool)>> m_onUseTileRendering;
    std::vector<std::function<void(int)>>  m_onTileSize;
    std::vector<std::function<void(bool)>> m_onUseEnvironmentMap;

    std::mutex mtx_denoise;
    std::mutex mtx_renderLow;
    std::mutex mtx_useTileRendering;
    std::mutex mtx_tileSize;
    std::mutex mtx_useEnvironmentMap;

    mutable std::atomic<unsigned long long> m_reads{0};
    mutable std::atomic<unsigned long long> m_writes{0};
};
