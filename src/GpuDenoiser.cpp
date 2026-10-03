#include "GpuDenoiser.h"
#include "renderer.h"
#include <algorithm>
#include <stdexcept>

GpuDenoiser::~GpuDenoiser() { release(); }
void GpuDenoiser::ensure(Renderer &renderer, QSize requested)
{
    if (requested == size) return;
    release();
    initializeOpenGLFunctions();
    GLint attachments = 0;
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &attachments);
    if (attachments < 8) throw std::runtime_error("GPU denoiser requires eight render targets");
    try
    {
        temporalProgram.reset(renderer.getShaderProgram(getShaderPath("denoise_temporal.frag"), getShaderPath("triangle.vert")));
        spatialProgram.reset(renderer.getShaderProgram(getShaderPath("denoise_spatial.frag"), getShaderPath("triangle.vert")));
        acceptanceProgram.reset(renderer.getShaderProgram(getShaderPath("denoise_accept.frag"), getShaderPath("triangle.vert")));
        auto texture = [&] { return renderer.getTextureRGB32F(requested.width(), requested.height()); };
        for (auto &t : sampleTextures) t = texture();
        for (auto &t : previousGuides) t = texture();
        for (int i = 0; i < 2; ++i)
        {
            colors[i] = texture(); moments[i] = texture(); spatial[i] = texture();
            temporalFbos[i] = renderer.bindData({colors[i], moments[i]});
            spatialFbos[i] = renderer.bindData({spatial[i]});
        }
        sampleFbo = renderer.bindData({sampleTextures[0], sampleTextures[1], sampleTextures[2], sampleTextures[3], sampleTextures[4]});
        glGenBuffers(1, &transformBuffer);
        glGenTextures(1, &transformTex);
        glGenQueries(1, &timer); glGenQueries(1, &acceptedQuery);
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error("GPU denoiser resource allocation failed");
        size = requested;
        invalidate();
    }
    catch (...) { release(); throw; }
}
void GpuDenoiser::release()
{
    if (!sampleTextures[0]) { temporalProgram.reset(); spatialProgram.reset(); acceptanceProgram.reset(); return; }
    glDeleteTextures(5, sampleTextures); glDeleteTextures(4, previousGuides);
    glDeleteTextures(2, colors); glDeleteTextures(2, moments); glDeleteTextures(2, spatial);
    glDeleteFramebuffers(2, temporalFbos); glDeleteFramebuffers(2, spatialFbos);
    glDeleteFramebuffers(1, &sampleFbo);
    glDeleteBuffers(1, &transformBuffer); glDeleteTextures(1, &transformTex);
    glDeleteQueries(1, &timer); glDeleteQueries(1, &acceptedQuery);
    std::fill(std::begin(sampleTextures), std::end(sampleTextures), 0);
    std::fill(std::begin(previousGuides), std::end(previousGuides), 0);
    std::fill(std::begin(colors), std::end(colors), 0);
    std::fill(std::begin(moments), std::end(moments), 0);
    std::fill(std::begin(spatial), std::end(spatial), 0);
    std::fill(std::begin(temporalFbos), std::end(temporalFbos), 0);
    std::fill(std::begin(spatialFbos), std::end(spatialFbos), 0);
    sampleFbo = transformBuffer = transformTex = timer = acceptedQuery = filtered = 0;
    size = {}; timerPending = queryPending = false;
    temporalProgram.reset(); spatialProgram.reset(); acceptanceProgram.reset();
    invalidate();
}
void GpuDenoiser::invalidate()
{
    history = false; filtered = 0; acceptance = 0; ++historyRevision;
    previousTransforms.clear(); stableIds.clear(); nextId = 1;
}
void GpuDenoiser::prepare(const Scene &scene)
{
    moving = history && (previousView != scene.camera.getViewMatrix() || previousFov != scene.camera.zoom);
    std::vector<QVector4D> data;
    for (const auto &instance : scene.instances)
    {
        if (!stableIds.contains(instance.id)) {
            if (nextId >= 16777216u) throw std::runtime_error("GPU denoiser stable identity capacity exceeded");
            stableIds[instance.id] = nextId++;
        }
        const bool valid = history && previousTransforms.contains(instance.id);
        const auto previous = previousTransforms.value(instance.id, instance.transform);
        moving = moving || (valid && previous != instance.transform);
        const auto delta = previous * instance.inverse;
        for (int c = 0; c < 4; ++c) data.push_back(delta.column(c));
        data.emplace_back(float(stableIds[instance.id]), valid ? 1.f : 0.f, 0, 0);
    }
    if (data.empty()) data.resize(5);
    glBindBuffer(GL_TEXTURE_BUFFER, transformBuffer);
    glBufferData(GL_TEXTURE_BUFFER, GLsizeiptr(data.size() * sizeof(QVector4D)), data.data(), GL_DYNAMIC_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, transformTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, transformBuffer);
}
void GpuDenoiser::bind(QOpenGLShaderProgram &p, const char *name, int unit, GLuint t, GLenum target)
{
    p.setUniformValue(name, unit); glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(target, t);
}
void GpuDenoiser::draw(QOpenGLShaderProgram &p, GLuint fbo, GLuint vao)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, size.width(), size.height());
    glBindVertexArray(vao); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
    glDrawArrays(GL_TRIANGLES, 0, 6); p.release();
}
void GpuDenoiser::poll()
{
    GLint available = 0;
    if (timerPending)
    {
        glGetQueryObjectiv(timer, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available) { GLuint64 ns = 0; glGetQueryObjectui64v(timer, GL_QUERY_RESULT, &ns); milliseconds = ns / 1e6; timerPending = false; }
    }
    if (queryPending)
    {
        glGetQueryObjectiv(acceptedQuery, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available) { GLuint count = 0; glGetQueryObjectuiv(acceptedQuery, GL_QUERY_RESULT, &count); if(queryRevision==historyRevision) acceptance = double(count) / (size.width() * size.height()); queryPending = false; }
    }
}
void GpuDenoiser::filter(GLuint vao, const Scene &scene, GLuint accumulated, unsigned samples, bool final, GLuint accumulatedNormal, GLuint accumulatedAlbedo)
{
    poll();
    const bool timed = !timerPending;
    if (timed) glBeginQuery(GL_TIME_ELAPSED, timer);
    GLuint source = accumulated;
    if (!final)
    {
        const int write = 1 - read;
        auto &p = *temporalProgram; p.bind();
        const char *currentNames[] = {"currentColor", "positionGuide", "normalGuide", "albedoGuide", "materialGuide"};
        for (int i = 0; i < 5; ++i) bind(p, currentNames[i], i, sampleTextures[i]);
        const char *oldNames[] = {"previousPosition", "previousNormal", "previousAlbedo", "previousMaterial"};
        for (int i = 0; i < 4; ++i) bind(p, oldNames[i], 5 + i, previousGuides[i]);
        bind(p, "previousColor", 9, colors[read]); bind(p, "previousMoments", 10, moments[read]);
        bind(p, "transforms", 11, transformTex, GL_TEXTURE_BUFFER);
        p.setUniformValue("previousView", previousView); p.setUniformValue("previousFov", previousFov);
        bind(p,"accumulatedColor",12,accumulated);bind(p,"accumulatedAlbedo",13,accumulatedAlbedo);
        bind(p,"accumulatedNormal",14,accumulatedNormal);
        p.setUniformValue("hasAccumulation",accumulated!=0 && accumulatedAlbedo!=0 && accumulatedNormal!=0);
        p.setUniformValue("historyValid", history); p.setUniformValue("moving", moving);
        p.setUniformValue("rejectStaleMotion",!qEnvironmentVariableIsSet("LEARNQT_LEGACY_MOTION_FILTER"));
        draw(p, temporalFbos[write], vao);
        if (!queryPending)
        {
            auto &a = *acceptanceProgram; a.bind(); bind(a, "moments", 0, moments[write]);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glBeginQuery(GL_SAMPLES_PASSED, acceptedQuery);
            draw(a, spatialFbos[0], vao);
            glEndQuery(GL_SAMPLES_PASSED); queryPending = true; queryRevision=historyRevision;
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        // Save only complete, matching guides; temporal history is unfiltered color.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sampleFbo);
        for (int i = 0; i < 4; ++i)
        {
            glReadBuffer(GL_COLOR_ATTACHMENT1 + i); glBindTexture(GL_TEXTURE_2D, previousGuides[i]);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, size.width(), size.height());
        }
        previousView = scene.camera.getViewMatrix(); previousFov = scene.camera.zoom;
        previousTransforms.clear();
        for (const auto &i : scene.instances) previousTransforms[i.id] = i.transform;
        history = true; read = write; source = colors[read];
    }
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        auto &p = *spatialProgram; p.bind();
        bind(p, "color", 0, source); bind(p, "positionGuide", 1, sampleTextures[1]);
        bind(p, "normalGuide", 2, sampleTextures[2]); bind(p, "albedoGuide", 3, sampleTextures[3]);
        bind(p, "materialGuide", 4, sampleTextures[4]);
        p.setUniformValue("stepWidth", 1 << iteration);
        p.setUniformValue("accumulatedInput", final && iteration == 0);
        p.setUniformValue("finalFiltering",final);p.setUniformValue("protectVolume",accumulatedAlbedo!=0);
        p.setUniformValue("stationaryFiltering",!final && !moving && accumulatedNormal!=0 && accumulatedAlbedo!=0);
        bind(p,"averageNormal",5,accumulatedNormal);bind(p,"averageAlbedo",6,accumulatedAlbedo);
        p.setUniformValue("samples", float(samples));
        draw(p, spatialFbos[iteration % 2], vao);
        source = spatial[iteration % 2];
    }
    filtered = source; ++rounds;
    if (timed) { glEndQuery(GL_TIME_ELAPSED); timerPending = true; }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("GPU denoising pass failed");
}
quint64 GpuDenoiser::allocatedBytes() const
{
    return ready() ? quint64(size.width()) * size.height() * 15 * 16 + quint64(stableIds.size()) * 5 * 16 : 0;
}
