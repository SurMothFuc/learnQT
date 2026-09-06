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
        if (!formal && snapshot.denoise && result.version == stats.accumulationVersion &&
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
    if (formal || !snapshot.denoise || previewSnapshot.version != stats.accumulationVersion ||
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
    if (!snapshot.denoise || formal || previewDenoiseFailed || previewDenoiser.busy() ||
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
    const GLuint textures[] = {normal_texture, baseColorTex, RenderColorTex};
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
