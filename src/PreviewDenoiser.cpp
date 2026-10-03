#include "OidnAuxiliary.h"
#include "OidnConfidence.h"
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
    result.samples = snapshot.samples;result.usedAuxiliary=snapshot.useAuxiliary;
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
        decodeOidnNormals(snapshot.normal.data(),snapshot.normal.data(),snapshot.normal.size(),result.normalMinimum,result.normalMaximum);
        result.color.resize(snapshot.color.size());
        bytes = quint64(snapshot.color.size()) * sizeof(float) * 4 +
            (snapshot.secondMoment.size()+snapshot.sampleCounts.size())*sizeof(float)+snapshot.confidenceEligible.size();
        const int w = snapshot.size.width(), h = snapshot.size.height();
        auto progress = [](void *user, double) { return !static_cast<std::atomic_bool *>(user)->load(); };
        albedoFilter.setImage("albedo", snapshot.albedo.data(), oidn::Format::Float3, w, h);
        albedoFilter.setImage("output", snapshot.albedo.data(), oidn::Format::Float3, w, h);
        normalFilter.setImage("normal", snapshot.normal.data(), oidn::Format::Float3, w, h);
        normalFilter.setImage("output", snapshot.normal.data(), oidn::Format::Float3, w, h);
        mainFilter.setImage("color", snapshot.color.data(), oidn::Format::Float3, w, h);
        if(snapshot.useAuxiliary) {
            mainFilter.setImage("normal",snapshot.normal.data(),oidn::Format::Float3,w,h);
            mainFilter.setImage("albedo",snapshot.albedo.data(),oidn::Format::Float3,w,h);
            mainFilter.set("cleanAux",true);
        } else {
            mainFilter.unsetImage("normal");mainFilter.unsetImage("albedo");mainFilter.set("cleanAux",false);
        }
        mainFilter.setImage("output", result.color.data(), oidn::Format::Float3, w, h);
        for (auto filter : snapshot.useAuxiliary?std::vector<oidn::FilterRef>{albedoFilter,normalFilter,mainFilter}:std::vector<oidn::FilterRef>{mainFilter})
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
        else if(snapshot.confidence && !snapshot.secondMoment.empty())
            result.protectedPixels=protectOidnOutput(snapshot.color.data(),result.color.data(),size_t(w)*h,snapshot.secondMoment,snapshot.sampleCounts,
                snapshot.confidenceEligible.empty()?nullptr:&snapshot.confidenceEligible);
    }
    catch (const std::exception &e)
    {
        result.color.clear();
        result.error = QString::fromUtf8(e.what());
    }
    result.milliseconds = timer.nsecsElapsed() / 1e6;
    return result;
}
