#include "renderer.h"
#include <QDebug>
#include <cstring>
#include <stdexcept>

void Renderer::invalidatePreviewDenoise()
{
    previewDenoiser.cancel();
    if (denoiseReadbackFence)
    {
        glDeleteSync(denoiseReadbackFence);
        denoiseReadbackFence = nullptr;
    }
    previewSnapshot = {};
    stats.previewDenoiseSamples = 0;
}

bool Renderer::pollPreviewDenoise(const RenderParams::Snapshot &snapshot)
{
    PreviewDenoiser::Result result;
    bool changed = false;
    if (previewDenoiser.take(result))
    {
        if (!formal && snapshot.effectiveDenoiseMode() == DenoiseMode::OIDN && result.version == stats.accumulationVersion &&
            result.size == QSize(render_width, render_height))
        {
            if (!result.error.isEmpty())
            {
                qWarning() << "Preview OIDN:" << result.error;
                previewDenoiseFailed = true;
            }
            else if (!result.color.empty())
            {
                glBindTexture(GL_TEXTURE_2D, RenderColorTexfiltered);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, render_width, render_height, GL_RGB, GL_FLOAT,
                                result.color.data());
                m_hasDenoisedFrame = true;
                m_lastDenoisedFrameCounter = result.samples;
                m_forceDenoiseRefresh = false;
                stats.denoisedVersion = result.version;
                stats.auxiliarySize = result.size;
                stats.normalMinimum = result.normalMinimum;
                stats.normalMaximum = result.normalMaximum;
                stats.oidnMs = result.milliseconds;
                stats.denoisedSamples = int(result.samples);
                changed = true;
            }
        }
        previewDenoiseClock.restart();
    }
    if (!denoiseReadbackFence)
        return changed;
    const GLenum ready = glClientWaitSync(denoiseReadbackFence, 0, 0);
    if (ready == GL_TIMEOUT_EXPIRED)
        return changed;
    if (ready == GL_WAIT_FAILED)
        throw std::runtime_error("Preview OIDN readback fence failed");
    glDeleteSync(denoiseReadbackFence);
    denoiseReadbackFence = nullptr;
    if (formal || snapshot.effectiveDenoiseMode() != DenoiseMode::OIDN || previewSnapshot.version != stats.accumulationVersion ||
        previewSnapshot.size != QSize(render_width, render_height))
    {
        previewSnapshot = {};
        return changed;
    }
    // The fence covers all three copies from the same completed sampling round. Never map an unfinished copy.
    const size_t values = size_t(render_width) * render_height * 3;
    std::vector<float> *destinations[] = {&previewSnapshot.normal, &previewSnapshot.albedo,
                                          &previewSnapshot.color};
    for (int i = 0; i < 3; ++i)
    {
        destinations[i]->resize(values);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pboIds[i]);
        auto source = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (!source)
        {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            throw std::runtime_error("Preview OIDN auxiliary readback failed");
        }
        std::memcpy(destinations[i]->data(), source, values * sizeof(float));
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    previewDenoiser.start(std::move(previewSnapshot));
    previewSnapshot = {};
    return changed;
}

void Renderer::requestPreviewDenoise(const RenderParams::Snapshot &snapshot, bool force)
{
    if (snapshot.effectiveDenoiseMode() != DenoiseMode::OIDN || formal || previewDenoiseFailed || previewDenoiser.busy() ||
        denoiseReadbackFence || !completeRound() || m_lastDenoisedFrameCounter == frameCounter)
        return;
    if (!previewHasGeometry)
        return;
    // Coalesce interactive edits and space background jobs by time, not by an arbitrary sample modulo.
    if (previewDenoiseClock.elapsed() < (m_hasDenoisedFrame ? 1000 : 250) || (!force && frameCounter < 4))
        return;
    ensureDenoisePbos();
    previewSnapshot.size = QSize(render_width, render_height);
    previewSnapshot.version = stats.accumulationVersion;
    previewSnapshot.samples = frameCounter;
    stats.previewDenoiseSamples = int(frameCounter);
    const GLuint textures[] = {previousNormalTex, previousAlbedoTex, preRenderColorTex};
    for (int i = 0; i < 3; ++i)
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pboIds[i]);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    denoiseReadbackFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!denoiseReadbackFence)
        throw std::runtime_error("Preview OIDN readback fence creation failed");
    glFlush();
}

void Renderer::prepareRealtime(const RenderParams::Snapshot &snapshot)
{
    const bool requested = snapshot.effectiveDenoiseMode() == DenoiseMode::Realtime &&
                           (previewHasGeometry || !m_scene.lights_encoded.empty());
    stats.denoiseMode = denoiseModeName(snapshot.effectiveDenoiseMode());
    if (requested && !realtimeFailed)
    {
        try
        {
            if (!realtimeAttached) realtimeNeedsGuides = roundInProgress() || (!samplingActive(snapshot) && frameCounter > 0);
            gpuDenoiser.ensure(*this, {render_width, render_height});
            if (!roundInProgress()) gpuDenoiser.prepare(m_scene);
            glBindFramebuffer(GL_FRAMEBUFFER, pathtrace_fbo);
            realtimeAttached = true; // Also detach partially attached guides on framebuffer failure.
            for (int i=0; i<5; ++i)
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3+i, GL_TEXTURE_2D, gpuDenoiser.sample(i), 0);
            const GLenum attachments[] = {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3,
                                          GL_COLOR_ATTACHMENT4,GL_COLOR_ATTACHMENT5,GL_COLOR_ATTACHMENT6,GL_COLOR_ATTACHMENT7};
            glDrawBuffers(8, attachments);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("GPU denoiser guide framebuffer failed");
            realtimeAttached = true;
            gpuDenoiser.poll();
            stats.realtimeDenoiseMs = gpuDenoiser.milliseconds;
            stats.historyAcceptance = gpuDenoiser.acceptance;
            stats.denoiseBytes = gpuDenoiser.allocatedBytes();
        }
        catch (const std::exception &e)
        {
            realtimeFailed = true; stats.denoiseError = QString::fromUtf8(e.what());
            qWarning() << "GPU denoiser:" << stats.denoiseError;
            if (formal) throw;
        }
    }
    if (!requested || realtimeFailed)
    {
        stats.realtimeDenoiseMs = 0;
        stats.historyAcceptance = 0;
        stats.denoiseBytes = 0;
    }
    if ((!requested || realtimeFailed) && realtimeAttached)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, pathtrace_fbo);
        for (int i=0; i<5; ++i) glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT3+i,GL_TEXTURE_2D,0,0);
        const GLenum attachments[] = {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2};
        glDrawBuffers(3, attachments); realtimeAttached=false;
        gpuDenoiser.release(); stats.denoiseBytes=0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}
void Renderer::refreshRealtimeGuides(const RenderParams::Snapshot &snapshot)
{
    if (!realtimeNeedsGuides || !frameCounter || roundInProgress() || realtimeFailed) return;
    bindPathtraceInputs(std::min(int(MAX_BOUNCES_LIMIT),snapshot.maxBounces), snapshot);
    pathtrace_program->bind(); pathtrace_program->setUniformValue("guidesOnly",true); pathtrace_program->release();
    renderFullImage(snapshot.maxBounces);
    pathtrace_program->bind(); pathtrace_program->setUniformValue("guidesOnly",false); pathtrace_program->release();
    realtimeNeedsGuides=false;
}
void Renderer::realtimeDenoise(const RenderParams::Snapshot &snapshot, bool final)
{
    if (!frameCounter || realtimeFailed || !gpuDenoiser.ready())
    {
        if (formal && realtimeFailed) throw std::runtime_error(stats.denoiseError.toStdString());
        return;
    }
    if (m_hasDenoisedFrame && m_lastDenoisedFrameCounter == frameCounter && !m_forceDenoiseRefresh) return;
    try
    {
        if (formal && cancel && cancel->load()) throw std::runtime_error("Render stopped before GPU denoising");
        gpuDenoiser.filter(VAO,m_scene,preRenderColorTex,frameCounter,final || formal,previousNormalTex,previousAlbedoTex);
        m_hasDenoisedFrame=true; m_lastDenoisedFrameCounter=frameCounter; m_forceDenoiseRefresh=false;
        stats.denoisedVersion=stats.accumulationVersion;
        stats.denoisedSamples=int(frameCounter); stats.auxiliarySize={render_width,render_height};
        stats.denoiseRounds=gpuDenoiser.rounds;
    }
    catch (const std::exception &e)
    {
        realtimeFailed=true; m_hasDenoisedFrame=false;
        stats.denoiseError=QString::fromUtf8(e.what()); qWarning() << "GPU denoiser:" << stats.denoiseError;
        if (formal) throw;
    }
}
