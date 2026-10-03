#pragma once
#include "OpenImageDenoise/oidn.hpp"
#include <QSize>
#include <QString>
#include <atomic>
#include <future>
#include <vector>

// One independent CPU job at a time. No OpenGL or scene access is allowed in the worker.
class PreviewDenoiser
{
  public:
    struct Snapshot
    {
        QSize size;
        quint64 version = 0;
        unsigned samples = 0;
        std::vector<float> normal, albedo, color;
        bool useAuxiliary=true,confidence=true;
        std::vector<float> secondMoment,sampleCounts;
        std::vector<unsigned char> confidenceEligible;
    };
    struct Result
    {
        QSize size;
        quint64 version = 0;
        unsigned samples = 0;
        double milliseconds = 0, normalMinimum = 1, normalMaximum = -1;
        std::vector<float> color;
        QString error;
        quint64 protectedPixels=0;bool usedAuxiliary=true;
    };
    ~PreviewDenoiser();
    bool busy() const
    {
        return future.valid();
    }
    void cancel()
    {
        cancelled = true;
    }
    void start(Snapshot snapshot);
    bool take(Result &result);
    quint64 allocatedBytes() const
    {
        return bytes.load();
    }

  private:
    Result execute(Snapshot snapshot);
    std::atomic_bool cancelled{false};
    std::atomic<quint64> bytes{0};
    std::future<Result> future;
    oidn::DeviceRef device;
    oidn::FilterRef albedoFilter, normalFilter, mainFilter;
};
