#include "PreviewDenoiser.h"
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <stdexcept>

PreviewDenoiser::~PreviewDenoiser()
{
    cancel();
    if (future.valid())
        future.wait();
}
void PreviewDenoiser::start(Snapshot snapshot)
{
    if (busy())
        throw std::logic_error("Preview denoise already running");
    cancelled = false;
    future = std::async(std::launch::async, [this, snapshot = std::move(snapshot)]() mutable {
        return execute(std::move(snapshot));
    });
}
bool PreviewDenoiser::take(Result &result)
{
    if (!future.valid() || future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return false;
    result = future.get();
    bytes = 0;
    return true;
}
PreviewDenoiser::Result PreviewDenoiser::execute(Snapshot snapshot)
{
    Result result;
    result.size = snapshot.size;
    result.version = snapshot.version;
    result.samples = snapshot.samples;
    QElapsedTimer timer;
    timer.start();
    try
    {
        if (cancelled)
            return result;
        if (!device)
        {
            device = oidn::newDevice(oidn::DeviceType::CPU);
            // Leave CPU capacity for Qt, imports and the OpenGL submission thread.
            device.set("numThreads", std::max(1, std::min(4, QThread::idealThreadCount() / 2)));
            device.set("setAffinity", false);
            device.commit();
            albedoFilter = device.newFilter("RT");
            normalFilter = device.newFilter("RT");
            mainFilter = device.newFilter("RT");
            mainFilter.set("hdr", true);
            mainFilter.set("cleanAux", true);
        }
        for (auto &n : snapshot.normal)
        {
            n = std::max(-1.f, std::min(1.f, n * 2.f - 1.f));
            result.normalMinimum = std::min(result.normalMinimum, double(n));
            result.normalMaximum = std::max(result.normalMaximum, double(n));
        }
        result.color.resize(snapshot.color.size());
        bytes = quint64(snapshot.color.size()) * sizeof(float) * 4;
        const int w = snapshot.size.width(), h = snapshot.size.height();
        auto progress = [](void *user, double) { return !static_cast<std::atomic_bool *>(user)->load(); };
        albedoFilter.setImage("albedo", snapshot.albedo.data(), oidn::Format::Float3, w, h);
        albedoFilter.setImage("output", snapshot.albedo.data(), oidn::Format::Float3, w, h);
        normalFilter.setImage("normal", snapshot.normal.data(), oidn::Format::Float3, w, h);
        normalFilter.setImage("output", snapshot.normal.data(), oidn::Format::Float3, w, h);
        mainFilter.setImage("color", snapshot.color.data(), oidn::Format::Float3, w, h);
        mainFilter.setImage("normal", snapshot.normal.data(), oidn::Format::Float3, w, h);
        mainFilter.setImage("albedo", snapshot.albedo.data(), oidn::Format::Float3, w, h);
        mainFilter.setImage("output", result.color.data(), oidn::Format::Float3, w, h);
        for (auto filter : {albedoFilter, normalFilter, mainFilter})
        {
            filter.setProgressMonitorFunction(progress, &cancelled);
            filter.commit();
            if (cancelled)
                break;
            filter.execute();
        }
        const char *message = nullptr;
        const auto error = device.getError(message);
        if (cancelled || error == oidn::Error::Cancelled)
            result.color.clear();
        else if (error != oidn::Error::None)
            throw std::runtime_error(message ? message : "OIDN failed");
    }
    catch (const std::exception &e)
    {
        result.color.clear();
        result.error = QString::fromUtf8(e.what());
    }
    result.milliseconds = timer.nsecsElapsed() / 1e6;
    return result;
}
