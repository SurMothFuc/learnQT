#include "OidnAuxiliary.h"
#include "renderer.h"

#include "MaterialTextureImage.h"
#include "MaterialMaskTextures.h"
#include "MaterialTexturePlan.h"
#include <cstring>
#include "PathDiagnostics.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLFunctions_4_3_Core>
#include <mutex>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <regex>
#include <vector>

extern QMutex param_mutex;

namespace
{
std::vector<int> textureViewSignature(const Scene &scene)
{
    std::set<int> indices;
    for(const auto &material:scene.materials)
        for(int index:{material.baseColorTex,material.emissiveTex})
            if(index>=0 && index<int(scene.textures.size())) indices.insert(index);
    for(const auto &material:scene.materials)
        for(int index:{material.normalTex,material.metallicTex,material.roughnessTex,material.opacityTex})
            if(index>=0 && index<int(scene.textures.size())) indices.insert(int(scene.textures.size())+index);
    std::vector<int> result(indices.begin(),indices.end());
    for(const auto &m:scene.materials) {
        result.insert(result.end(),{m.alphaMode,m.baseColorTex,m.opacityTex});
        for(float value:{m.opacity,m.alphaCutoff,m.emissive.x(),m.emissive.y(),m.emissive.z()}) {
            int bits;std::memcpy(&bits,&value,sizeof(bits));result.push_back(bits);
        }
    }
    return result;
}
int clampMaxBounces(int maxBounces)
{
    return std::max(0, std::min(maxBounces, static_cast<int>(MAX_BOUNCES_LIMIT)));
}

bool isRenderFrameLimitReached(const RenderParams::Snapshot &snapshot, unsigned int frameCounter)
{
    return snapshot.maxRenderFrames > 0 &&
           frameCounter >= static_cast<unsigned int>(snapshot.maxRenderFrames);
}
} // namespace

std::string processIncludes(const std::string &source, const std::string &shaderPath)
{
    static std::recursive_mutex cacheMutex;
    std::lock_guard<std::recursive_mutex> guard(cacheMutex);
    static std::unordered_map<std::string, std::string> includeCache;
    static std::unordered_map<std::string, bool> processing; // 防止循环包含

    // 主文件缓存检查
    if (includeCache.find(shaderPath) != includeCache.end())
    {
        return includeCache[shaderPath];
    }
    processing[shaderPath] = true;

    QDir dir = QFileInfo(QString::fromStdString(shaderPath)).dir().path();
    std::regex includeRegex(R"(^\s*#include\s*\"([^\"]+)\")", std::regex::ECMAScript);
    std::smatch match;
    std::string result = source;

    while (std::regex_search(result, match, includeRegex))
    {
        std::string includeFile = match[1].str();
        std::string includePath = dir.filePath(QString::fromStdString(includeFile)).toStdString();

        // 检查循环包含
        if (processing[includePath])
        {
            qWarning() << "Circular include detected: " << QString::fromStdString(includePath);
            result = match.prefix().str() + match.suffix().str();
            continue;
        }

        // 读取包含文件
        std::string includeContent;
        if (includeCache.find(includePath) != includeCache.end())
        {
            includeContent = includeCache[includePath];
        }
        else
        {
            QFile file(QString::fromStdString(includePath));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                qWarning() << "Failed to open include file: " << QString::fromStdString(includePath);
                result = match.prefix().str() + match.suffix().str();
                includeCache[includePath] = "";
                continue;
            }
            // 必须按 UTF-8 读取：着色器源码含中文注释，用本地编码解码会破坏字节并吞掉换行。
            includeContent = QString::fromUtf8(file.readAll()).toStdString();
            file.close();

            // 递归处理包含文件中的 #include
            includeContent = processIncludes(includeContent, includePath);
        }

        // 替换 #include 指令为文件内容
        result = match.prefix().str() + includeContent + match.suffix().str();
    }

    includeCache[shaderPath] = result;
    processing[shaderPath] = false;

    return result;
}

std::string injectDefines(const std::string &source,
                          const std::unordered_map<std::string, std::string> &defines)
{
    // 注入动态 #define，确保在 #version 之后添加
    std::string definesStr;
    for (const auto &keyValue : defines)
    {
        definesStr += "#define " + keyValue.first + " " + keyValue.second + "\n";
    }

    const size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos)
    {
        size_t lineEnd = source.find("\n", versionPos);
        if (lineEnd == std::string::npos)
        {
            lineEnd = source.size();
        }
        return source.substr(0, lineEnd + 1) + definesStr + source.substr(lineEnd + 1);
    }

    return definesStr + source;
}

GLuint Renderer::getTextureRGB32F(int width, int height)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

QOpenGLShaderProgram *Renderer::getShaderProgram(
    std::string fshader, std::string vshader,
    const std::unordered_map<std::string, std::string> &defines_Vertex,
    const std::unordered_map<std::string, std::string> &defines_Fragment)
{
    auto shaderProgram = std::make_unique<QOpenGLShaderProgram>();

    if (defines_Fragment.count("COMPUTE_PATH"))
    {
        QFile file(QString::fromStdString(fshader));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            throw std::runtime_error("Cannot read path tracing shader");
        auto source = processIncludes(file.readAll().toStdString(), fshader);
        const auto version = source.find("#version 330 core");
        if (version == std::string::npos)
            throw std::runtime_error("Missing path tracing shader version");
        source.replace(version, std::string("#version 330 core").size(), "#version 430 core");
        source = injectDefines(source, defines_Fragment);
        if (!shaderProgram->addCacheableShaderFromSourceCode(QOpenGLShader::Compute, source.c_str()) ||
            !shaderProgram->link())
            throw std::runtime_error(shaderProgram->log().toStdString());
        return shaderProgram.release();
    }

    // 加载并处理顶点着色器
    QFile vFile(QString::fromStdString(vshader));
    if (!vFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qDebug() << "Failed to open vertex shader: " << vshader.c_str();
        throw std::runtime_error("Cannot load or compile shader: " + fshader + " / " + vshader + " " +
                                 shaderProgram->log().toStdString());
    }
    std::string vSource = QString::fromUtf8(vFile.readAll()).toStdString();
    vFile.close();
    vSource = processIncludes(vSource, vshader);
    vSource = injectDefines(vSource, defines_Vertex);

    bool success = shaderProgram->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex, vSource.c_str());
    if (!success)
    {
        qDebug() << "Vertex shader compilation failed:\n" << shaderProgram->log();
        throw std::runtime_error("Cannot load or compile shader: " + fshader + " / " + vshader + " " +
                                 shaderProgram->log().toStdString());
    }

    // 加载并处理片段着色器
    QFile fFile(QString::fromStdString(fshader));
    if (!fFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qDebug() << "Failed to open fragment shader: " << fshader.c_str();
        throw std::runtime_error("Cannot load or compile shader: " + fshader + " / " + vshader + " " +
                                 shaderProgram->log().toStdString());
    }
    std::string fSource = QString::fromUtf8(fFile.readAll()).toStdString();
    fFile.close();
    fSource = processIncludes(fSource, fshader);
    fSource = injectDefines(fSource, defines_Fragment);

    success = shaderProgram->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, fSource.c_str());
    if (!success)
    {
        qDebug() << "Fragment shader compilation failed:\n" << shaderProgram->log();
        throw std::runtime_error("Cannot load or compile shader: " + fshader + " / " + vshader + " " +
                                 shaderProgram->log().toStdString());
    }

    success = shaderProgram->link();
    if (!success)
    {
        throw std::runtime_error("Shader linking failed: " + shaderProgram->log().toStdString());
    }
    return shaderProgram.release();
}

GLuint Renderer::bindData(std::vector<GLuint> colorAttachments)
{
    // colorAttachments 为颜色缓冲，返回值为 FBO。
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    if (!colorAttachments.empty())
    {
        std::vector<GLenum> attachments;
        attachments.reserve(colorAttachments.size());
        for (int i = 0; i < static_cast<int>(colorAttachments.size()); ++i)
        {
            glBindTexture(GL_TEXTURE_2D, colorAttachments[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D,
                                   colorAttachments[i], 0);
            attachments.push_back(GL_COLOR_ATTACHMENT0 + i);
        }
        glDrawBuffers(static_cast<GLsizei>(attachments.size()), attachments.data());
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        glDeleteFramebuffers(1, &fbo);
        throw std::runtime_error("Render framebuffer allocation failed");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

// 路径追踪只画全屏四边形，它的 FBO 没有也不需要深度附件。
// 光栅化交互预览需要深度测试，所以单独建一个只含「RenderColorTex + 深度」的 FBO，
// 颜色靶与路径追踪共用，但绝不改动 pathtrace_fbo 本身，避免影响既有预览行为。
void Renderer::ensureDepthAttachment(bool antialiasing)
{
    if (!pathtrace_fbo || render_width <= 0 || render_height <= 0)
        return;
    GLint maximumSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maximumSamples);
    const int requestedSamples = antialiasing ? std::max(1, std::min(4, maximumSamples)) : 1;
    if (rasterFbo && rasterDepthSize == QSize(render_width, render_height) && rasterSampleRequest == requestedSamples)
        return;
    glDeleteFramebuffers(1, &multisampleFbo);
    glDeleteRenderbuffers(1, &multisampleColor);
    glDeleteRenderbuffers(1, &multisampleDepth);
    multisampleFbo = multisampleColor = multisampleDepth = 0;
    if (!rasterFbo)
        glGenFramebuffers(1, &rasterFbo);
    if (depthRenderbuffer)
        glDeleteRenderbuffers(1, &depthRenderbuffer);
    glGenRenderbuffers(1, &depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, render_width, render_height);
    glBindFramebuffer(GL_FRAMEBUFFER, rasterFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, RenderColorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRenderbuffer);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Raster preview framebuffer is incomplete");
    rasterSampleRequest = requestedSamples;
    multisampleCount = 1;
    for (int candidate=requestedSamples; candidate>1; candidate/=2) {
        glGenFramebuffers(1, &multisampleFbo); glBindFramebuffer(GL_FRAMEBUFFER, multisampleFbo);
        glGenRenderbuffers(1, &multisampleColor); glBindRenderbuffer(GL_RENDERBUFFER, multisampleColor);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, candidate, GL_RGBA32F, render_width, render_height);
        GLint actual=0; glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_SAMPLES,&actual);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, multisampleColor);
        glGenRenderbuffers(1, &multisampleDepth); glBindRenderbuffer(GL_RENDERBUFFER, multisampleDepth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, candidate, GL_DEPTH_COMPONENT24, render_width, render_height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, multisampleDepth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE && glGetError()==GL_NO_ERROR) {
            multisampleCount=actual; break;
        }
        glDeleteFramebuffers(1, &multisampleFbo); glDeleteRenderbuffers(1, &multisampleColor); glDeleteRenderbuffers(1, &multisampleDepth);
        multisampleFbo = multisampleColor = multisampleDepth = 0;
        while (glGetError()!=GL_NO_ERROR) {}
    }
    if (antialiasing && multisampleCount<4) qWarning()<<"Raster MSAA actual samples:"<<multisampleCount;
    stats.rasterSamples = multisampleCount;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    rasterDepthSize = QSize(render_width, render_height);
}

Renderer::Renderer(int width, int height, const RenderParams::Snapshot &initialSnapshot, QObject *parent,
                   Scene *scene)
    : QObject(parent), m_scene(scene ? *scene : Scene::getInstance())
{
    init(width, height, initialSnapshot);
    glGenBuffers(3, pboIds);
    previewDenoiseClock.start();
    compositeClock.start();
}

Renderer::~Renderer()
{
    invalidatePreviewDenoise();
    if (workFence)
        glDeleteSync(workFence);
    if (pickFence)
        glDeleteSync(pickFence);
    releaseRasterResources();
    glDeleteQueries(1, &rasterTimerQuery);
    glDeleteQueries(1, &pickTimerQuery);
    glDeleteBuffers(1, &pickPbo);
    glDeleteFramebuffers(1, &pickFbo);
    glDeleteTextures(2, pickTextures);
    glDeleteQueries(12, timerQueries);
    glDeleteBuffers(5, instanceBuffers);
    glDeleteTextures(5, instanceTextures);
    uninit();
}

bool Renderer::waitForGpuBoundary()
{
    if (!workFence)
        return true;
    // Wait on the GPU event itself. Sleep(1) polling can oversleep by a Windows timer tick.
    constexpr GLuint64 timeoutNanoseconds = 1000000;
    const auto status = glClientWaitSync(workFence, 0, timeoutNanoseconds);
    if (status == GL_WAIT_FAILED)
        throw std::runtime_error("GPU completion wait failed");
    if (status == GL_TIMEOUT_EXPIRED)
        return false;
    glDeleteSync(workFence);
    workFence = nullptr;
    return true;
}
void Renderer::submitGpuBoundary()
{
    if (workFence)
        glDeleteSync(workFence);
    workFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!workFence)
        throw std::runtime_error("GPU completion fence creation failed");
    glFlush();
}
void Renderer::pollGpuTimers()
{
    if (gpuDenoiser.ready()) { gpuDenoiser.poll(); stats.realtimeDenoiseMs=gpuDenoiser.milliseconds; stats.historyAcceptance=gpuDenoiser.acceptance; }
    if (rasterTimerQuery && rasterTimerPending)
    {
        GLint ready = 0;
        glGetQueryObjectiv(rasterTimerQuery, GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready)
        {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(rasterTimerQuery, GL_QUERY_RESULT, &ns);
            stats.rasterMs = ns / 1e6;
            rasterTimerPending = false;
        }
    }
    if (pickTimerQuery && pickTimerPending)
    {
        GLint ready = 0;
        glGetQueryObjectiv(pickTimerQuery, GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready)
        {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(pickTimerQuery, GL_QUERY_RESULT, &ns);
            stats.pickMs = ns / 1e6;
            // 同时记一份「本帧完成」的量，供渲染线程按窗口累计后清零。
            pickCompletedMs = stats.pickMs;
            pickTimerPending = false;
        }
    }
    for (int i = 0; i < 12; ++i)
        if (timerPending[i])
        {
            GLint ready = 0;
            glGetQueryObjectiv(timerQueries[i], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (ready)
            {
                GLuint64 ns = 0;
                glGetQueryObjectui64v(timerQueries[i], GL_QUERY_RESULT, &ns);
                if (timerEpoch[i] == stats.accumulationVersion)
                {
                    const double milliseconds = ns / 1e6;
                    (i % 3 == 0   ? stats.gpuMs
                     : i % 3 == 1 ? stats.gpuHistoryMs
                                  : stats.gpuCompositeMs) = milliseconds;
                    if (i % 3 == 0)
                        estimatedTileMs = std::max(milliseconds, estimatedTileMs * .95);
                }
                timerPending[i] = false;
            }
        }
}
void Renderer::render(int width, int height, const RenderParams::Snapshot &snapshot,
                      SceneDirtyFlags dirtyFlags, int maxTiles, const std::function<bool()> &interrupted)
{
    const RefreshActions actions = resolveRefreshActions(width, height, snapshot, dirtyFlags);
    const bool resetHistory = (dirtyFlags & (toSceneDirtyFlags(SceneDirtyFlag::Material) | toSceneDirtyFlags(SceneDirtyFlag::SceneBuffers))) ||
        actions.resizeTargets || actions.rebuildShader || actions.refreshDenoisePolicy ||
        snapshot.antialiasing != m_lastAppliedSnapshot.antialiasing || snapshot.maxBounces != m_lastAppliedSnapshot.maxBounces;
    if (resetHistory) { gpuDenoiser.invalidate(); realtimeFailed = false; stats.denoiseError.clear(); }
    if (actions.resizeTargets || actions.rebuildShader || snapshot.antialiasing != m_lastAppliedSnapshot.antialiasing) previewSequence=0;
    applyRefreshActions(width, height, snapshot, actions);

    // 交互回退决策：进入或离开光栅化时重置累积，避免两种模式的画面互相残留。
    const bool raster = m_rasterActive && m_rasterCapable;
    if (raster != m_rasterRequested)
    {
        resetAccumulation();
        m_rasterRequested = raster;
    }
    if (raster)
    {
        if (!m_rasterGeometryUploaded)
            uploadRasterGeometry();
        if (!m_rasterInstancesUploaded)
            uploadRasterInstances();
        // Resize the depth attachment only when the render resolution changes.
        ensureDepthAttachment(snapshot.antialiasing);
        gpuDenoiser.invalidate();
        if (renderRasterPreview(snapshot))
            return;
        // 光栅化资源不可用时回到路径追踪，本帧不再重复重置累积。
        setRasterActive(false);
        m_rasterRequested = false;
    }

    prepareRealtime(snapshot);
    const bool previewChanged = pollPreviewDenoise(snapshot);
    stats.batchTiles = 0;
    if (!samplingActive(snapshot))
    {
        refreshRealtimeGuides(snapshot);
        if (snapshot.effectiveDenoiseMode()==DenoiseMode::Realtime) realtimeDenoise(snapshot,true);
        performDenoising(snapshot, true);
        compositePreview(snapshot, previewChanged || actions.refreshDenoisePolicy || actions.refreshDisplay,
                         actions.refreshDisplay);
        return;
    }
    if (!timerQueries[0])
        glGenQueries(12, timerQueries);
    pollGpuTimers();
    const int limit =
        snapshot.useTileRendering && estimatedTileMs > 0
            ? std::max(1, std::min(std::min(16, maxTiles), int(8. / (estimatedTileMs * 1.3 + .03))))
            : 1;
    stats.batchLimit = limit;
    const auto initialRound = frameCounter;
    bindPathtraceInputs(clampMaxBounces(snapshot.maxBounces), snapshot);
    QElapsedTimer submission;
    submission.start();
    for (int step = 0; step < limit; ++step)
    {
        if (step && ((interrupted && interrupted()) || submission.elapsed() >= 4))
            break;
        int query = (timerCursor++ % 4) * 3;
        bool timed = !timerPending[query] && !timerPending[query + 1];
        if (timed)
            glBeginQuery(GL_TIME_ELAPSED, timerQueries[query]);
        executeRenderPass(snapshot);
        if (timed)
        {
            glEndQuery(GL_TIME_ELAPSED);
            timerPending[query] = true;
            timerEpoch[query] = stats.accumulationVersion;
            glBeginQuery(GL_TIME_ELAPSED, timerQueries[query + 1]);
        }
        processHistorySaving(snapshot);
        if (timed)
        {
            glEndQuery(GL_TIME_ELAPSED);
            timerPending[query + 1] = true;
            timerEpoch[query + 1] = stats.accumulationVersion;
        }
        ++stats.batchTiles;
        // Never cross a sampling round: snapshots, pause and final OIDN retain complete-round semantics.
        if (frameCounter != initialRound)
            break;
    }
    if (frameCounter != initialRound) {
        refreshRealtimeGuides(snapshot);
        ++previewSequence;
        if (snapshot.effectiveDenoiseMode() == DenoiseMode::Realtime) realtimeDenoise(snapshot, false);
    }
    const bool finished = !samplingActive(snapshot);
    if (!formal)
        performDenoising(snapshot, finished);
    compositePreview(snapshot, frameCounter != initialRound, finished || actions.refreshDisplay);
}

void Renderer::compositePreview(const RenderParams::Snapshot &snapshot, bool changed, bool force)
{
    if (!m_rasterActive && !frameCounter) return;
    displayDirty |= changed;
    // A new raster image remains pending until the throttled composite consumes it.
    const bool needed = m_rasterActive ? rasterNeedsComposite : displayDirty;
    if (formal || !needed ||
        (!m_rasterActive && !firstComposite && !force && compositeClock.isValid() &&
         compositeClock.elapsed() < 16))
        return;
    int query = ((std::max(1, timerCursor) - 1) % 4) * 3 + 2;
    bool timed = timerQueries[query] && !timerPending[query];
    if (timed)
        glBeginQuery(GL_TIME_ELAPSED, timerQueries[query]);
    compositeToScreen(snapshot);
    if (timed)
    {
        glEndQuery(GL_TIME_ELAPSED);
        timerPending[query] = true;
        timerEpoch[query] = stats.accumulationVersion;
    }
    compositeClock.restart();
    rasterNeedsComposite = false;
    firstComposite = displayDirty = false;
}

bool Renderer::samplingActive(const RenderParams::Snapshot &snapshot) const
{
    if (isRenderFrameLimitReached(snapshot, frameCounter))
        return false;
    return formal || frameCounter == 0 || previewHasGeometry;
}

void Renderer::init(int width, int height, const RenderParams::Snapshot &snapshot)
{
    batchTextureSettings.clear();
    m_width = width;
    m_height = height;
    calResolution(snapshot.renderLow);
    updateTileGrid(snapshot.tileSize);
    m_viewportX = 0;
    m_viewportY = 0;
    initializeOpenGLFunctions();
    GLint textureUnits=16;glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&textureUnits);
    auto computeFunctions=QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_4_3_Core>();
    if(computeFunctions && computeFunctions->initializeOpenGLFunctions()) {
        GLint computeUnits=16;glGetIntegerv(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS,&computeUnits);
        textureUnits=std::min(textureUnits,computeUnits);
    }
    materialTexturePoolCapacity=textureUnits>=18?4:textureUnits>=17?3:2;

    qDebug() << reinterpret_cast<const char *>(glGetString(GL_VERSION));

    rebuildPathtraceProgram(snapshot);

    preRenderColorTex = getTextureRGB32F(render_width, render_height);
    RenderColorTex = getTextureRGB32F(render_width, render_height);
    normal_texture = getTextureRGB32F(render_width, render_height);
    baseColorTex = getTextureRGB32F(render_width, render_height);
    previousNormalTex = getTextureRGB32F(render_width, render_height);
    previousAlbedoTex = getTextureRGB32F(render_width, render_height);
    batchTextureSettings.insert(batchTextureSettings.end(),
                                {preRenderColorTex, RenderColorTex, normal_texture, baseColorTex, previousNormalTex, previousAlbedoTex});

    pathtrace_fbo = bindData(std::vector<GLuint>{RenderColorTex, normal_texture, baseColorTex});
    ensureDepthAttachment();

    historysave_program.reset(
        getShaderProgram(getShaderPath("historysave.frag"), getShaderPath("triangle.vert")));
    historysave_fbo = bindData(std::vector<GLuint>{preRenderColorTex, previousNormalTex, previousAlbedoTex});

    RenderColorTexfiltered = getTextureRGB32F(render_width, render_height);
    batchTextureSettings.push_back(RenderColorTexfiltered);

    m_program.reset(getShaderProgram(getShaderPath("triangle.frag"), getShaderPath("triangle.vert")));
    rebuildRasterProgram(snapshot);
    m_texture = getTextureRGB32F(m_width, m_height);
    m_fbo = bindData(std::vector<GLuint>{m_texture});

    glEnable(GL_DEPTH_TEST);

    const std::vector<QVector3D> square = {QVector3D(-1, -1, 0), QVector3D(1, -1, 0), QVector3D(-1, 1, 0),
                                           QVector3D(1, 1, 0),   QVector3D(-1, 1, 0), QVector3D(1, -1, 0)};

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QVector3D) * square.size(), nullptr, GL_STATIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(QVector3D) * square.size(), square.data());

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, reinterpret_cast<GLvoid *>(0));
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_lastAppliedSnapshot = snapshot;
    m_forceDenoiseRefresh = true;
}

void Renderer::initOIDN()
{
    oidnDevice = oidn::newDevice();
    oidnDevice.commit();

    oidnAlbedoFilter = oidnDevice.newFilter("RT");
    oidnNormalFilter = oidnDevice.newFilter("RT");

    oidnMainFilter = oidnDevice.newFilter("RT");
    oidnMainFilter.set("hdr", true);
    oidnMainFilter.set("cleanAux", true);

    updateOIDNBuffers();
}

void Renderer::uninit()
{
    gpuDenoiser.release();
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteFramebuffers(1, &pathtrace_fbo);
    glDeleteFramebuffers(1, &historysave_fbo);
    glDeleteRenderbuffers(1, &depthRenderbuffer);
    glDeleteFramebuffers(1, &rasterFbo);
    glDeleteFramebuffers(1, &multisampleFbo);
    glDeleteRenderbuffers(1, &multisampleColor);glDeleteRenderbuffers(1, &multisampleDepth);
    multisampleFbo=multisampleColor=multisampleDepth=0;multisampleCount=1;rasterSampleRequest=0;

    glDeleteTextures(1, &m_texture);
    for (auto &perTex : batchTextureSettings)
    {
        glDeleteTextures(1, &perTex);
        perTex = 0;
    }

    glDeleteTextures(1, &hdrMap);
    glDeleteTextures(1, &hdrCache);
    glDeleteTextures(1, &trianglesTextureBuffer);
    glDeleteTextures(1, &nodesTextureBuffer);
    glDeleteTextures(1, &lightsTextureBuffer);
    glDeleteTextures(1, &materialTextureArray);
    glDeleteTextures(int(materialTextureExtraArrays.size()),materialTextureExtraArrays.data());
    materialTextureExtraArrays={};
    glDeleteTextures(1, &materialTextureInfoTexture);

    if (VAO)
    {
        glDeleteVertexArrays(1, &VAO);
    }
    if (VBO)
    {
        glDeleteBuffers(1, &VBO);
    }

    if (tbo0)
    {
        glDeleteBuffers(1, &tbo0);
    }
    if (tbo1)
    {
        glDeleteBuffers(1, &tbo1);
    }
    if (tboLights)
    {
        glDeleteBuffers(1, &tboLights);
    }
    if (materialTextureInfoBuffer)
    {
        glDeleteBuffers(1, &materialTextureInfoBuffer);
    }

    if (pboIds[0] != 0 || pboIds[1] != 0 || pboIds[2] != 0)
    {
        glDeleteBuffers(3, pboIds);
        std::fill(std::begin(pboIds), std::end(pboIds), 0);
    }

    historysave_fbo = m_fbo = pathtrace_fbo = rasterFbo = 0;
    depthRenderbuffer = 0;
    rasterDepthSize = {};
    m_texture = 0;
    hdrMap = hdrCache = trianglesTextureBuffer = nodesTextureBuffer = lightsTextureBuffer =
        materialTextureArray = materialTextureInfoTexture = 0;
    VAO = VBO = tbo0 = tbo1 = tboLights = materialTextureInfoBuffer = 0;
    materialTextureLayerCount = 0;
}

void Renderer::updateOIDNBuffers()
{
    const size_t bufferSize =
        static_cast<size_t>(render_width) * static_cast<size_t>(render_height) * 3 * sizeof(float);

    oidnColorBuf = oidnDevice.newBuffer(bufferSize);
    oidnAlbedoBuf = oidnDevice.newBuffer(bufferSize);
    oidnNormalBuf = oidnDevice.newBuffer(bufferSize);
    oidnOutputBuf = oidnDevice.newBuffer(bufferSize);

    oidnAlbedoFilter.setImage("albedo", oidnAlbedoBuf, oidn::Format::Float3, render_width, render_height);
    oidnAlbedoFilter.setImage("output", oidnAlbedoBuf, oidn::Format::Float3, render_width, render_height);
    oidnAlbedoFilter.commit();

    oidnNormalFilter.setImage("normal", oidnNormalBuf, oidn::Format::Float3, render_width, render_height);
    oidnNormalFilter.setImage("output", oidnNormalBuf, oidn::Format::Float3, render_width, render_height);
    oidnNormalFilter.commit();

    oidnMainFilter.setImage("color", oidnColorBuf, oidn::Format::Float3, render_width, render_height);
    oidnMainFilter.setImage("albedo", oidnAlbedoBuf, oidn::Format::Float3, render_width, render_height);
    oidnMainFilter.setImage("normal", oidnNormalBuf, oidn::Format::Float3, render_width, render_height);
    oidnMainFilter.setImage("output", oidnOutputBuf, oidn::Format::Float3, render_width, render_height);
    oidnMainFilter.commit();
    oidnSize = QSize(render_width, render_height);
    ensureDenoisePbos();
}

void Renderer::ensureDenoisePbos()
{
    const QSize size(render_width, render_height);
    if (denoisePboSize == size)
        return;
    const size_t bufferSize = size_t(render_width) * render_height * 3 * sizeof(float);
    for (int i = 0; i < 3; ++i)
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pboIds[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bufferSize), nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    denoisePboSize = size;
}

void Renderer::adjustSize()
{
    if(rasterDepthSize!=QSize(render_width,render_height)) {
        glDeleteFramebuffers(1,&multisampleFbo);glDeleteRenderbuffers(1,&multisampleColor);glDeleteRenderbuffers(1,&multisampleDepth);
        multisampleFbo=multisampleColor=multisampleDepth=0;multisampleCount=1;rasterSampleRequest=0;
    }
    while (glGetError() != GL_NO_ERROR)
    {
    }
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    for (auto &perTex : batchTextureSettings)
    {
        glBindTexture(GL_TEXTURE_2D, perTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, render_width, render_height, 0, GL_RGBA, GL_FLOAT,
                     nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    invalidatePreviewDenoise();

    m_viewportX = 0;
    m_viewportY = 0;
}

void Renderer::updateSizeParam()
{
    if (!pathtrace_program)
    {
        return;
    }
    pathtrace_program->bind();
    pathtrace_program->setUniformValue("width", render_width);
    pathtrace_program->setUniformValue("height", render_height);
    pathtrace_program->release();
}

void Renderer::calResolution(bool renderLow)
{
    if (renderLow)
    {
        if (m_width <= MAX_LOW_RESOLUTION && m_height <= MAX_LOW_RESOLUTION)
        {
            render_width = m_width;
            render_height = m_height;
        }
        else
        {
            const double scaleWidth = static_cast<double>(MAX_LOW_RESOLUTION) / m_width;
            const double scaleHeight = static_cast<double>(MAX_LOW_RESOLUTION) / m_height;
            const double scale = std::min(scaleWidth, scaleHeight);

            render_width = static_cast<int>(std::round(m_width * scale));
            render_height = static_cast<int>(std::round(m_height * scale));
        }
    }
    else
    {
        render_width = m_width;
        render_height = m_height;
    }
}

void Renderer::updateTileGrid(int tileSize)
{
    const int safeTileSize = std::max(1, tileSize);
    tilesX = (render_width + safeTileSize - 1) / safeTileSize;
    tilesY = (render_height + safeTileSize - 1) / safeTileSize;
}

void Renderer::bindPathtraceInputs(int maxBounces, const RenderParams::Snapshot &snapshot)
{
    const unsigned int sobolBounceCount = static_cast<unsigned int>(std::max(1, maxBounces));
    const unsigned sequence = formal || snapshot.effectiveDenoiseMode() != DenoiseMode::Realtime ? frameCounter : previewSequence;
    const auto sobelNumber = getSobelRandomNumber(sequence, sobolBounceCount);
    const auto sampleBits=getSobolBits(sequence);
    if(sampleBits.size()!=120)throw std::runtime_error("Unexpected Sobol dimension table");

    pathtrace_program->bind();
    {
        const GLint frameLocation = pathtrace_program->uniformLocation("frameCounter");
        glUniform1ui(frameLocation, frameCounter);
        const GLint sobelLocation = pathtrace_program->uniformLocation("sobelNumber");
        glUniform1fv(sobelLocation, static_cast<GLsizei>(sobolBounceCount * 2u), sobelNumber.data());
        pathtrace_program->setUniformValue("maxBounces", maxBounces);
        glUniform1ui(pathtrace_program->uniformLocation("sampleSequence"), sequence);
        pathtrace_program->setUniformValue("useUnifiedSampler",!qEnvironmentVariableIsSet("LEARNQT_LEGACY_SAMPLER"));
        glUniform1ui(pathtrace_program->uniformLocation("samplerSeed"),snapshot.sampleSeed);
        glUniform1ui(pathtrace_program->uniformLocation("samplerIndex"),sequence);
        pathtrace_program->setUniformValue("useEtaScaleRR",!qEnvironmentVariableIsSet("LEARNQT_LEGACY_RR"));
        pathtrace_program->setUniformValue("rrMinDepth",snapshot.rrMinDepth);
        glUniform4uiv(pathtrace_program->uniformLocation("samplerSobol"),int(sampleBits.size()/4),sampleBits.data());
        // R2 low-discrepancy sequence is independent of the path's Sobol dimensions.
        pathtrace_program->setUniformValue("aaSample", QVector2D(float(std::fmod((sequence+.5)*.7548776662466927,1.0)),
                                                               float(std::fmod((sequence+.5)*.5698402909980532,1.0))));
        pathtrace_program->setUniformValue("antialiasing", snapshot.antialiasing);
        pathtrace_program->setUniformValue("realtimeGuides", realtimeAttached && !realtimeFailed);
        pathtrace_program->setUniformValue("previousNormal", 13);
        glActiveTexture(GL_TEXTURE13); glBindTexture(GL_TEXTURE_2D, previousNormalTex);
        pathtrace_program->setUniformValue("previousAlbedo", 14);
        glActiveTexture(GL_TEXTURE14); glBindTexture(GL_TEXTURE_2D, previousAlbedoTex);
        pathtrace_program->setUniformValue("reprojectionTable", 15);
        glActiveTexture(GL_TEXTURE15); glBindTexture(GL_TEXTURE_BUFFER, gpuDenoiser.transformTexture());

        pathtrace_program->setUniformValue("triangles", 0);
        pathtrace_program->setUniformValue("nodes", 1);
        pathtrace_program->setUniformValue("hdrMap", 2);
        pathtrace_program->setUniformValue("hdrCache", 3);
        pathtrace_program->setUniformValue("lights", 5);
        pathtrace_program->setUniformValue("materialTextures", 6);
        pathtrace_program->setUniformValue("materialTextureInfo", 7);
        pathtrace_program->setUniformValue("materialTextureInfoStride",4);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_BUFFER, trianglesTextureBuffer);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, nodesTextureBuffer);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, hdrMap);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, hdrCache);

        pathtrace_program->setUniformValue("preRenderColor", 4);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, preRenderColorTex);

        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_BUFFER, lightsTextureBuffer);

        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D_ARRAY, materialTextureArray);

        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_BUFFER, materialTextureInfoTexture);

        bindInstanceBuffers(pathtrace_program.get());
        bindMaterialTextureInputs(pathtrace_program.get(),6,7);
    }
    pathtrace_program->release();
}

void Renderer::renderTile(int tileX, int tileY, int tileWidth, int tileHeight, int)
{
    if (stats.computePathtrace)
    {
        auto gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_4_3_Core>();
        pathtrace_program->bind();
        gl->glUniform2i(pathtrace_program->uniformLocation("traceTileOrigin"), tileX, tileY);
        gl->glUniform2i(pathtrace_program->uniformLocation("traceTileSize"), tileWidth, tileHeight);
        gl->glBindImageTexture(0, RenderColorTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        gl->glBindImageTexture(1, normal_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        gl->glBindImageTexture(2, baseColorTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        if (realtimeAttached && !realtimeFailed)
            for (int i=0; i<5; ++i) gl->glBindImageTexture(3+i, gpuDenoiser.sample(i), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        // Includes image writes, following sampler reads/history blits, and target reuse.
        gl->glMemoryBarrier(GL_ALL_BARRIER_BITS);
        gl->glDispatchCompute((tileWidth + 7) / 8, (tileHeight + 7) / 8, 1);
        gl->glMemoryBarrier(GL_ALL_BARRIER_BITS);
        pathtrace_program->release();
        return;
    }
    glBindVertexArray(VAO);
    glDisable(GL_DEPTH_TEST);
    pathtrace_program->bind();
    glBindFramebuffer(GL_FRAMEBUFFER, pathtrace_fbo);
    glViewport(tileX, tileY, tileWidth, tileHeight);
    glEnable(GL_SCISSOR_TEST);
    glScissor(tileX, tileY, tileWidth, tileHeight);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    pathtrace_program->release();
}

void Renderer::renderFullImage(int maxBounces)
{
    renderTile(0, 0, render_width, render_height, maxBounces);
    renderComplete = true;
}

bool Renderer::rebuildPathtraceProgram(const RenderParams::Snapshot &snapshot)
{
    int depth = bvhMaximumDepth(m_scene.tlas);
    for (const auto &mesh : m_scene.meshes)
        depth = std::max(depth, mesh->maximumDepth > 0 ? mesh->maximumDepth : bvhMaximumDepth(mesh->nodes));
    // Round upwards to reduce variant churn; never truncate a deeper tree to 64.
    const int capacity = std::max(16, ((depth + 2 + 3) / 4) * 4);
    const bool media = std::any_of(m_scene.materials.begin(), m_scene.materials.end(),
                                  [](const Material &m) { return m.mediumtype != 0; });
    auto gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_4_3_Core>();
    bool compute = snapshot.computePathtrace && gl && gl->initializeOpenGLFunctions();
    QString fallback;
    if (snapshot.computePathtrace && !compute)
        fallback = tr("当前设备不支持计算着色器，使用兼容路径追踪");
    const auto baseKey = std::to_string(capacity) + ":" + std::to_string(media) + ":" +
                         std::to_string(snapshot.useEnvironmentMap)+":"+std::to_string(materialTexturePoolCapacity)+
                         (qEnvironmentVariableIsSet("LEARNQT_TRACE_PROFILE")?":profile":":beauty")+
                         (qEnvironmentVariableIsSet("LEARNQT_LEGACY_SAMPLER")?":legacy":":unified");
    if (compute && failedComputePrograms.count(baseKey))
    {
        compute = false;
        fallback = tr("计算着色器编译失败，使用兼容路径追踪");
    }
    std::unordered_map<std::string, std::string> defines{{"INSTANCED_SCENE", "1"},
        {"MAX_BOUNCES_LIMIT", std::to_string(MAX_BOUNCES_LIMIT)},
        {"BVH_STACK_CAPACITY", std::to_string(capacity)}};
    defines.emplace("MATERIAL_TEXTURE_POOL_COUNT",std::to_string(materialTexturePoolCapacity));
    defines.emplace("PACKED_SURFACE_PDF","1");
    defines.emplace(qEnvironmentVariableIsSet("LEARNQT_LEGACY_SAMPLER")?"LEGACY_SAMPLER":"UNIFIED_SAMPLER","1");
    if(qEnvironmentVariableIsSet("LEARNQT_TRACE_PROFILE")) {
        if(snapshot.effectiveDenoiseMode()!=DenoiseMode::None)
            throw std::runtime_error("Trace profiling requires denoising disabled");
        defines.emplace("TRACE_PROFILE","1");
        defines.emplace("TRACE_TRAVERSAL_PROFILE","1");
    }
    if (!media) defines.emplace("NO_PARTICIPATING_MEDIA", "1");
    if (snapshot.useEnvironmentMap) defines.emplace("USEENVIRONMENTMAP", "");
    auto select = [&](bool useCompute) {
        const auto key = baseKey + (useCompute ? ":compute" : ":fragment");
        const auto found = pathtracePrograms.find(key);
        if (found != pathtracePrograms.end()) return found->second;
        if (useCompute) defines["COMPUTE_PATH"] = "1";
        else defines.erase("COMPUTE_PATH");
        std::shared_ptr<QOpenGLShaderProgram> program(getShaderProgram(
            getShaderPath("pathtrace.frag"), getShaderPath("triangle.vert"), {}, defines));
        if (pathtracePrograms.size() >= 8) pathtracePrograms.erase(pathtracePrograms.begin());
        pathtracePrograms.emplace(key, program);
        return program;
    };
    std::shared_ptr<QOpenGLShaderProgram> selected;
    try { selected = select(compute); }
    catch (const std::exception &error)
    {
        if (!compute) throw;
        qWarning() << "Compute path tracing unavailable:" << error.what();
        failedComputePrograms.insert(baseKey);
        compute = false;
        fallback = tr("计算着色器编译失败，使用兼容路径追踪");
        selected = select(false);
    }
    const bool changed = selected != pathtrace_program;
    pathtrace_program = std::move(selected);
    stats.computePathtrace = compute;
    stats.pathtraceBackend = !fallback.isEmpty() ? fallback :
        (compute ? tr("计算着色器路径追踪") : tr("兼容路径追踪"));
    return changed;
}

void Renderer::syncPathtraceUniforms()
{
    pathtrace_program->bind();
    pathtrace_program->setUniformValue("nTriangles", static_cast<int>(m_scene.triangles.size()));
    pathtrace_program->setUniformValue("nNodes", static_cast<int>(m_scene.nodes_encoded.size()));
    pathtrace_program->setUniformValue("nLights", static_cast<int>(m_scene.lights_encoded.size()));
    pathtrace_program->setUniformValue("nAnalyticLights", std::min(m_scene.document.root["lights"].toArray().size(),
                                                               static_cast<int>(m_scene.lights_encoded.size())));
    pathtrace_program->setUniformValue("width", render_width);
    pathtrace_program->setUniformValue("height", render_height);
    pathtrace_program->setUniformValue("hdrResolution", m_scene.hdrResolution);
    pathtrace_program->setUniformValue("materialTextureCount", materialTextureLayerCount);
    pathtrace_program->release();
    syncCameraUniforms();
}

void Renderer::adjustScreenResolution(int width, int height, bool renderLow)
{
    const int oldWidth = m_width;
    const int oldHeight = m_height;
    const int oldRenderWidth = render_width;
    const int oldRenderHeight = render_height;

    m_width = width;
    m_height = height;
    calResolution(renderLow);

    if (!targetsValid || oldRenderWidth != render_width || oldRenderHeight != render_height ||
        oldWidth != m_width || oldHeight != m_height)
    {
        qDebug() << "Adjust frame size to:" << width << height << "Adjust render size to:" << render_width
                 << render_height;
        targetsValid = false;
        adjustSize();
        updateSizeParam();
    }
    if (glGetError() != GL_NO_ERROR)
        throw std::runtime_error("GPU cannot allocate the requested render resolution");
    targetsValid = true;
}

Renderer::RefreshActions Renderer::resolveRefreshActions(int width, int height,
                                                         const RenderParams::Snapshot &snapshot,
                                                         SceneDirtyFlags dirtyFlags) const
{
    // 所有刷新动作都在帧首一次性决策，后续阶段只执行这个结果。
    RefreshActions actions;
    actions.refreshDisplay = hasSceneDirtyFlag(dirtyFlags, SceneDirtyFlag::Display);

    const bool environmentMapChanged = snapshot.useEnvironmentMap != m_lastAppliedSnapshot.useEnvironmentMap;
    const bool renderLowChanged = snapshot.renderLow != m_lastAppliedSnapshot.renderLow;
    const bool tileModeChanged = snapshot.useTileRendering != m_lastAppliedSnapshot.useTileRendering;
    const bool tileSizeChanged = snapshot.tileSize != m_lastAppliedSnapshot.tileSize;
    const bool denoiseChanged = snapshot.effectiveDenoiseMode() != m_lastAppliedSnapshot.effectiveDenoiseMode();
    const bool aaChanged = snapshot.antialiasing != m_lastAppliedSnapshot.antialiasing;
    const bool maxBouncesChanged = snapshot.maxBounces != m_lastAppliedSnapshot.maxBounces;
    const bool sizeChanged = width != m_width || height != m_height;

    if (environmentMapChanged)
    {
        actions.rebuildShader = true;
        // 光栅化预览程序也要跟着重建：环境开关决定是否注入 USEENVIRONMENTMAP。
        actions.refreshRasterProgram = true;
        actions.syncSceneBuffers = true;
        actions.syncCameraUniforms = true;
        actions.resetAccumulation = true;
    }

    if (snapshot.computePathtrace != m_lastAppliedSnapshot.computePathtrace)
    {
        actions.rebuildShader = true;
        actions.resetAccumulation = true;
    }

    if (!targetsValid || renderLowChanged || sizeChanged)
    {
        actions.resizeTargets = true;
        actions.resetAccumulation = true;
    }

    if (tileModeChanged || tileSizeChanged)
    {
        actions.resetAccumulation = true;
    }

    if (maxBouncesChanged || aaChanged || snapshot.sampleSeed!=m_lastAppliedSnapshot.sampleSeed ||
        snapshot.rrMinDepth!=m_lastAppliedSnapshot.rrMinDepth)
    {
        actions.resetAccumulation = true;
    }

    if (denoiseChanged)
    {
        actions.refreshDenoisePolicy = true;
    }

    if (hasSceneDirtyFlag(dirtyFlags, SceneDirtyFlag::Camera))
    {
        actions.syncCameraUniforms = true;
        actions.resetAccumulation = true;
    }

    if (hasSceneDirtyFlag(dirtyFlags, SceneDirtyFlag::Material) || hasSceneDirtyFlag(dirtyFlags, SceneDirtyFlag::Transform))
    {
        actions.syncMaterialBuffer = true;
        actions.resetAccumulation = true;
    }

    if (hasSceneDirtyFlag(dirtyFlags, SceneDirtyFlag::SceneBuffers))
    {
        actions.syncSceneBuffers = true;
        actions.resetAccumulation = true;
    }

    // 全量场景同步已经覆盖材质缓冲，不再重复走材质脏路径。
    if (actions.syncSceneBuffers)
    {
        actions.syncMaterialBuffer = false;
    }

    return actions;
}

void Renderer::applyRefreshActions(int width, int height, const RenderParams::Snapshot &snapshot,
                                   const RefreshActions &actions)
{
    // 这里只执行帧首已经决策好的刷新动作。
    const bool programChanged = (actions.rebuildShader || actions.syncMaterialBuffer || actions.syncSceneBuffers)
                                    && rebuildPathtraceProgram(snapshot);

    if (actions.refreshRasterProgram)
    {
        rebuildRasterProgram(snapshot);
    }

    if (actions.resizeTargets)
    {
        adjustScreenResolution(width, height, snapshot.renderLow);
    }

    if (actions.resizeTargets || snapshot.tileSize != m_lastAppliedSnapshot.tileSize ||
        snapshot.useTileRendering != m_lastAppliedSnapshot.useTileRendering)
    {
        updateTileGrid(snapshot.tileSize);
    }

    if (actions.refreshDenoisePolicy)
    {
        invalidatePreviewDenoise();
        m_forceDenoiseRefresh = true;
        m_hasDenoisedFrame=false; stats.denoisedVersion=0; stats.denoisedSamples=0; stats.oidnMs=0;
    }

    if (actions.syncSceneBuffers)
    {
        syncSceneBuffers();
    }
    else if (actions.syncMaterialBuffer)
    {
        syncMaterialBuffer();
    }

    if (programChanged) syncPathtraceUniforms();

    if (actions.syncCameraUniforms)
    {
        syncCameraUniforms();
    }

    if (actions.resetAccumulation)
    {
        resetAccumulation();
    }

    m_lastAppliedSnapshot = snapshot;
}

void Renderer::clearTexture(GLuint texture)
{
    if (texture == 0)
    {
        return;
    }

    GLuint clearFbo = 0;
    glGenFramebuffers(1, &clearFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, clearFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    {
        const GLfloat clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        glClearBufferfv(GL_COLOR, 0, clearColor);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &clearFbo);
}

void Renderer::resetAccumulation()
{
    estimatedTileMs = 0;
    firstComposite = displayDirty = true;
    const auto &instances = m_scene.instances;
    previewHasGeometry =
        std::any_of(instances.begin(), instances.end(), [](const SceneInstance &i) { return i.visible; });
    invalidatePreviewDenoise();
    previewDenoiseFailed = false;
    previewDenoiseClock.restart();
    ++stats.accumulationVersion;
    stats.oidnMs = 0;
    stats.denoisedSamples = 0;
    stats.denoisedVersion = 0;
    stats.auxiliarySize = {};
    // 重置累计状态，并清空历史纹理避免旧帧残留参与新累计。
    clearTexture(preRenderColorTex);
    clearTexture(RenderColorTex);
    clearTexture(normal_texture);
    clearTexture(baseColorTex);
    clearTexture(previousNormalTex);
    clearTexture(previousAlbedoTex);
    clearTexture(RenderColorTexfiltered);
    // The published image stays visible until a complete replacement is available.

    lasttime = clock();
    lastframeCounter = 0;
    frameCounter = 0;
    chunkedRenderingCount = 0;
    lastChunkedRenderingCount = 0;
    nowChunkedCount = 0;
    currentTileX = 0;
    currentTileY = 0;
    renderComplete = false;
    m_forceDenoiseRefresh = true;
    m_hasDenoisedFrame = false;
    m_lastDenoisedFrameCounter = 0;
}

void Renderer::uploadTriangleBuffer(bool recreateResources)
{
    stats.geometryUploadBytes += m_scene.geometryData.size() * sizeof(QVector4D);
    const auto &trianglesEncoded = m_scene.geometryData;
    const GLsizeiptr bufferSize = static_cast<GLsizeiptr>(trianglesEncoded.size() * sizeof(QVector4D));
    const void *data = trianglesEncoded.empty() ? nullptr : trianglesEncoded.data();

    if (recreateResources && tbo0 != 0)
    {
        glDeleteBuffers(1, &tbo0);
        tbo0 = 0;
    }
    if (recreateResources && trianglesTextureBuffer != 0)
    {
        glDeleteTextures(1, &trianglesTextureBuffer);
        trianglesTextureBuffer = 0;
    }

    if (tbo0 == 0)
    {
        glGenBuffers(1, &tbo0);
    }
    glBindBuffer(GL_TEXTURE_BUFFER, tbo0);
    glBufferData(GL_TEXTURE_BUFFER, std::max<GLsizeiptr>(16, bufferSize), data, GL_STATIC_DRAW);

    if (trianglesTextureBuffer == 0)
    {
        glGenTextures(1, &trianglesTextureBuffer);
    }
    glBindTexture(GL_TEXTURE_BUFFER, trianglesTextureBuffer);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, tbo0);
}

void Renderer::uploadNodeBuffer(bool recreateResources)
{
    const auto &nodesEncoded = m_scene.nodes_encoded;
    const GLsizeiptr bufferSize = static_cast<GLsizeiptr>(nodesEncoded.size() * sizeof(BVHNode_encoded));
    const void *data = nodesEncoded.empty() ? nullptr : nodesEncoded.data();

    if (recreateResources && tbo1 != 0)
    {
        glDeleteBuffers(1, &tbo1);
        tbo1 = 0;
    }
    if (recreateResources && nodesTextureBuffer != 0)
    {
        glDeleteTextures(1, &nodesTextureBuffer);
        nodesTextureBuffer = 0;
    }

    if (tbo1 == 0)
    {
        glGenBuffers(1, &tbo1);
    }
    glBindBuffer(GL_TEXTURE_BUFFER, tbo1);
    glBufferData(GL_TEXTURE_BUFFER, std::max<GLsizeiptr>(16, bufferSize), data, GL_STATIC_DRAW);

    if (nodesTextureBuffer == 0)
    {
        glGenTextures(1, &nodesTextureBuffer);
    }
    glBindTexture(GL_TEXTURE_BUFFER, nodesTextureBuffer);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32F, tbo1);
}

void Renderer::uploadLightBuffer(bool recreateResources)
{
    const auto &lightsEncoded = m_scene.lights_encoded;
    const GLsizeiptr bufferSize = static_cast<GLsizeiptr>(lightsEncoded.size() * sizeof(Light_encoded));
    const void *data = lightsEncoded.empty() ? nullptr : lightsEncoded.data();

    if (recreateResources && tboLights != 0)
    {
        glDeleteBuffers(1, &tboLights);
        tboLights = 0;
    }
    if (recreateResources && lightsTextureBuffer != 0)
    {
        glDeleteTextures(1, &lightsTextureBuffer);
        lightsTextureBuffer = 0;
    }

    if (tboLights == 0)
    {
        glGenBuffers(1, &tboLights);
    }
    glBindBuffer(GL_TEXTURE_BUFFER, tboLights);
    glBufferData(GL_TEXTURE_BUFFER, std::max<GLsizeiptr>(16, bufferSize), data, GL_STATIC_DRAW);

    if (lightsTextureBuffer == 0)
    {
        glGenTextures(1, &lightsTextureBuffer);
    }
    glBindTexture(GL_TEXTURE_BUFFER, lightsTextureBuffer);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, tboLights);
}

void Renderer::uploadHdrTextures(bool recreateResources)
{
    const auto &scene = m_scene;
    const QSize hdrSize(scene.hdrRes.width, scene.hdrRes.height);
    const QString hdrPath = scene.document.root["hdr"].toString();
    // Scene HDR pixels are immutable; transforms and environment toggles reuse them.
    if (rasterEnvironmentSource != scene.hdrRes.cols || rasterEnvironmentSize != hdrSize ||
        rasterEnvironmentPath != hdrPath)
    {
        rasterEnvironment = rasterDiffuseEnvironment(scene.hdrRes.cols, hdrSize.width(), hdrSize.height());
        rasterEnvironmentSource = scene.hdrRes.cols;
        rasterEnvironmentSize = hdrSize;
        rasterEnvironmentPath = hdrPath;
    }
    const auto uploadTexture = [&](GLuint &texture, float *data) {
        if (recreateResources && texture != 0)
        {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
        if (texture == 0)
        {
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, texture);
        }

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, m_scene.hdrRes.width,
                     m_scene.hdrRes.height, 0, GL_RGB, GL_FLOAT, data);
    };

    uploadTexture(hdrMap, m_scene.hdrRes.cols);
    uploadTexture(hdrCache, m_scene.cache);
}

void Renderer::uploadMaterialTextures(bool recreateResources)
{
    Q_UNUSED(recreateResources);
    const auto mask=planMaterialMaskTextures(m_scene.textures,m_scene.materials);
    materialTextureSourceOverrides=mask.materialSources;
    GLint maxSize=1,maxLayers=1;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxSize);glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS,&maxLayers);
    quint64 budget=512ull*1024*1024;
    const auto setting=qEnvironmentVariable("LEARNQT_TEXTURE_BUDGET_MB");
    if(!setting.isEmpty()) {
        bool valid=false;const auto mb=setting.toULongLong(&valid);
        if(!valid||mb==0||mb>16384)throw std::invalid_argument("Invalid LEARNQT_TEXTURE_BUDGET_MB");
        budget=mb*1024*1024;
    }
    const auto plan=planMaterialTextures(mask,m_scene.materials,materialTexturePoolCapacity,
        std::min(maxSize,2048),maxLayers,budget);
    uploadedTextureViewSignature=textureViewSignature(m_scene);
    glDeleteTextures(1,&materialTextureArray);
    glDeleteTextures(int(materialTextureExtraArrays.size()),materialTextureExtraArrays.data());
    materialTextureExtraArrays={};glGenTextures(1,&materialTextureArray);
    if(materialTexturePoolCapacity>1)glGenTextures(materialTexturePoolCapacity-1,materialTextureExtraArrays.data());
    auto texture=[&](int pool){return pool==0?materialTextureArray:materialTextureExtraArrays[pool-1];};
    for(int i=0;i<int(plan.pools.size());++i) {
        const auto &pool=plan.pools[i];glBindTexture(GL_TEXTURE_2D_ARRAY,texture(i));
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage3D(GL_TEXTURE_2D_ARRAY,0,pool.floating?GL_RGBA16F:GL_RGBA8,
            pool.size.width(),pool.size.height(),std::max(1,pool.layers),0,GL_RGBA,GL_FLOAT,nullptr);
        if(pool.layers==0) {
            const float white[]={1,1,1,1};
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,0,1,1,1,GL_RGBA,GL_FLOAT,white);
        }
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(const auto &view:plan.views) {
        const auto &pool=plan.pools[view.placement.pool];const auto &asset=mask.textures[view.source];
        glBindTexture(GL_TEXTURE_2D_ARRAY,texture(view.placement.pool));
        const auto pixels=prepareMaterialTexturePixels(asset.image,pool.size,view.color);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,view.placement.layer,pool.size.width(),pool.size.height(),1,
            GL_RGBA,GL_FLOAT,pixels.data());
    }
    for(int i=0;i<int(plan.pools.size());++i) {
        glBindTexture(GL_TEXTURE_2D_ARRAY,texture(i));glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    }
    for(const auto &view:plan.views)if(mask.recipes[view.source].channel>=0) {
        const auto &pool=plan.pools[view.placement.pool];
        const auto chain=prepareMaskTextureMips(mask.textures[view.source].image,pool.size,view.color,mask.recipes[view.source]);
        glBindTexture(GL_TEXTURE_2D_ARRAY,texture(view.placement.pool));
        for(int level=0;level<int(chain.size());++level)
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY,level,0,0,view.placement.layer,chain[level].size.width(),chain[level].size.height(),1,
                GL_RGBA,GL_FLOAT,chain[level].pixels.data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);glBindTexture(GL_TEXTURE_2D_ARRAY,0);
    materialTextureLayerCount=int(mask.textures.size());textureArrayBytes=plan.bytes;
    std::vector<QVector4D> info(size_t(std::max(1,materialTextureLayerCount))*5);
    for(int i=0;i<materialTextureLayerCount;++i) {
        const auto &asset=mask.textures[i];const auto data=plan.data[i],color=plan.color[i];
        const auto recipe=mask.recipes[i];
        bool downscaled=false;
        if(data.pool>=0) {const auto size=plan.pools[data.pool].size;downscaled=asset.image.width()>size.width()||asset.image.height()>size.height();}
        info[i*5]=QVector4D(asset.uvScale.x(),asset.uvScale.y(),asset.uvOffset.x(),asset.uvOffset.y());
        info[i*5+1]=QVector4D(asset.uvRotation,asset.wrapS,asset.wrapT,0);
        info[i*5+2]=QVector4D(asset.minFilter,asset.magFilter,color.layer,1);
        info[i*5+3]=QVector4D(recipe.cutoff,recipe.channel,recipe.channel>=0&&data.pool>=0?1:0,downscaled?1:0);
        info[i*5+4]=QVector4D(data.pool,data.layer,color.pool,color.layer);
    }
    if(!materialTextureInfoBuffer)glGenBuffers(1,&materialTextureInfoBuffer);
    glBindBuffer(GL_TEXTURE_BUFFER,materialTextureInfoBuffer);
    glBufferData(GL_TEXTURE_BUFFER,info.size()*sizeof(QVector4D),info.data(),GL_STATIC_DRAW);
    if(!materialTextureInfoTexture)glGenTextures(1,&materialTextureInfoTexture);
    glBindTexture(GL_TEXTURE_BUFFER,materialTextureInfoTexture);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,materialTextureInfoBuffer);
    glBindTexture(GL_TEXTURE_BUFFER,0);glBindBuffer(GL_TEXTURE_BUFFER,0);
    QJsonArray pools,sources;
    for(int i=0;i<int(plan.pools.size());++i) {
        const auto &pool=plan.pools[i];
        pools.append(QJsonObject{{"pool",i},{"width",pool.size.width()},{"height",pool.size.height()},
            {"requestedWidth",pool.requested.width()},{"requestedHeight",pool.requested.height()},
            {"layers",pool.layers},{"format",pool.floating?"RGBA16F":"RGBA8"},{"bytes",double(materialPoolBytes(pool))}});
    }
    for(int i=0;i<int(mask.textures.size());++i) {
        const auto data=plan.data[i],color=plan.color[i];
        sources.append(QJsonObject{{"source",i},{"path",QString::fromStdString(mask.textures[i].sourcePath)},
            {"dataPool",data.pool},{"dataLayer",data.layer},{"colorPool",color.pool},{"colorLayer",color.layer}});
    }
    textureResourceReport=QJsonObject{{"bytes",double(plan.bytes)},{"budgetBytes",double(budget)},
        {"capacity",materialTexturePoolCapacity},{"sources",materialTextureLayerCount},{"views",int(plan.views.size())},
        {"unusedSources",plan.unusedSources},{"missingViews",plan.missingViews},{"downscaled",plan.reduced},
        {"compositeMaskPointFallbacks",mask.compositePointFallbacks},{"pools",pools},{"placements",sources}};
    if(plan.reduced)qWarning()<<"Texture budget applied:"<<textureResourceReport;
    if(plan.missingViews)qWarning()<<"Texture layer capacity exceeded; scalar fallback views:"<<plan.missingViews;
    if(mask.compositePointFallbacks)qWarning()<<"Composite Mask point fallbacks:"<<mask.compositePointFallbacks;
    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Material texture pool allocation/upload failed");
}
void Renderer::bindMaterialTextureInputs(QOpenGLShaderProgram *program,int unit,int infoUnit)
{
    program->setUniformValue("materialTextures",unit);glActiveTexture(GL_TEXTURE0+unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY,materialTextureArray);
    const int units[]={12,16,17};
    for(int i=1;i<materialTexturePoolCapacity;++i) {
        program->setUniformValue(("materialTextures"+std::to_string(i)).c_str(),units[i-1]);
        glActiveTexture(GL_TEXTURE0+units[i-1]);glBindTexture(GL_TEXTURE_2D_ARRAY,materialTextureExtraArrays[i-1]);
    }
    program->setUniformValue("materialTextureInfo",infoUnit);glActiveTexture(GL_TEXTURE0+infoUnit);
    glBindTexture(GL_TEXTURE_BUFFER,materialTextureInfoTexture);
    program->setUniformValue("materialTextureInfoStride",5);
    program->setUniformValue("materialTextureCount",materialTextureLayerCount);
}

void Renderer::syncCameraUniforms()
{
    QMatrix4x4 inverseView;
    QVector3D eye;
    float fov;
    {
        QMutexLocker lock(&param_mutex);
        const QMatrix4x4 view = m_scene.camera.getViewMatrix();
        inverseView = view.inverted();
        eye = m_scene.camera.position;
        fov = m_scene.camera.zoom;
    }

    pathtrace_program->bind();
    pathtrace_program->setUniformValue("view", inverseView);
    pathtrace_program->setUniformValue("eye", eye);
    pathtrace_program->setUniformValue("cameraFov", fov);
    pathtrace_program->release();
}

void Renderer::uploadInstanceBuffers(bool topology)
{
    const auto &scene = m_scene;
    auto upload = [&](int i, GLenum format, const void *data, size_t bytes) {
        if (!instanceBuffers[i])
            glGenBuffers(1, &instanceBuffers[i]);
        if (!instanceTextures[i])
            glGenTextures(1, &instanceTextures[i]);
        glBindBuffer(GL_TEXTURE_BUFFER, instanceBuffers[i]);
        if (bytes)
            glBufferData(GL_TEXTURE_BUFFER, GLsizeiptr(bytes), data, GL_DYNAMIC_DRAW);
        else
        {
            const float zero[4] = {};
            glBufferData(GL_TEXTURE_BUFFER, sizeof(zero), zero, GL_DYNAMIC_DRAW);
        }
        glBindTexture(GL_TEXTURE_BUFFER, instanceTextures[i]);
        glTexBuffer(GL_TEXTURE_BUFFER, format, instanceBuffers[i]);
    };
    upload(0, GL_RGBA32F, scene.instanceData.data(), scene.instanceData.size() * sizeof(QVector4D));
    auto materialData=scene.materialData;
    for(int i=0;i<int(materialTextureSourceOverrides.size());++i) {
        materialData[i*10+6].setZ(materialTextureSourceOverrides[i][0]);
        materialData[i*10+7].setW(materialTextureSourceOverrides[i][1]);
    }
    surfacePdfOffset=int(materialData.size());
    materialData.resize(materialData.size()+(scene.surfacePdfs.size()+3)/4);
    for(size_t i=0;i<scene.surfacePdfs.size();++i)materialData[surfacePdfOffset+i/4][int(i%4)]=scene.surfacePdfs[i];
    upload(1, GL_RGBA32F, materialData.data(), materialData.size() * sizeof(QVector4D));
    upload(2, GL_RGB32F, scene.tlasData.data(), scene.tlasData.size() * sizeof(BVHNode_encoded));
    if (topology)
        upload(3, GL_RG32UI, scene.surfaces.data(), scene.surfaces.size() * sizeof(SurfaceReference));
}
void Renderer::bindInstanceBuffers(QOpenGLShaderProgram *program)
{
    const char *names[] = {"instanceTable", "materialTable", "topNodes", "surfaceTable", "surfacePdfTable"};
    for (int i = 0; i < 4; ++i)
    {
        glActiveTexture(GL_TEXTURE8 + i);
        glBindTexture(GL_TEXTURE_BUFFER, instanceTextures[i]);
        program->setUniformValue(names[i], 8 + i);
    }
    program->setUniformValue("nTopNodes", int(m_scene.tlas.size()));
    program->setUniformValue("surfacePdfOffset",surfacePdfOffset);
    const bool binary=std::none_of(m_scene.materials.begin(),m_scene.materials.end(),[](const Material &m) {
        return m.alphaMode==Transparent || m.mediumtype!=None;
    });
    program->setUniformValue("shadowBinaryScene",binary);
    program->setUniformValue("shadowAnyHit",!qEnvironmentVariableIsSet("LEARNQT_DISABLE_ANYHIT"));
    program->setUniformValue("picking", false);
    auto env = m_scene.document.root["environment"].toObject();
    program->setUniformValue("environmentIntensity", float(env["intensity"].toDouble(1)));
    program->setUniformValue("usePowerLightGroups",!qEnvironmentVariableIsSet("LEARNQT_LEGACY_LIGHT_GROUPS"));
    program->setUniformValue("environmentSelectProbability",m_scene.environmentSelectionProbability());
    program->setUniformValue("environmentRotation", float(env["rotation"].toDouble() * PI / 180));
}

void Renderer::syncMaterialBuffer()
{
    QMutexLocker lock(&param_mutex);
    if(textureViewSignature(m_scene)!=uploadedTextureViewSignature) uploadMaterialTextures(false);
    // 材质脏路径只重传三角形编码缓冲，不触碰 BVH / HDR 资源。
    uploadInstanceBuffers(false);
    // 光栅化预览从 instance/material 表读取，材质编辑后实例属性必须重传。
    m_rasterInstancesUploaded = false;
    uploadLightBuffer(tboLights == 0 || lightsTextureBuffer == 0);
    pathtrace_program->bind();
    pathtrace_program->setUniformValue("materialTextureCount",materialTextureLayerCount);
    pathtrace_program->setUniformValue("nLights",
                                       static_cast<int>(m_scene.lights_encoded.size()));
    pathtrace_program->setUniformValue(
        "nAnalyticLights", std::min(m_scene.document.root["lights"].toArray().size(),
                                    static_cast<int>(m_scene.lights_encoded.size())));
    pathtrace_program->release();
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void Renderer::syncSceneBuffers()
{
    QElapsedTimer uploadTimer;
    uploadTimer.start();
    QMutexLocker lock(&param_mutex);

    // 全量场景同步：triangles / nodes / HDR / 相关 uniform 一次性更新。
    QStringList meshKeys;
    for (auto &mesh : m_scene.meshes)
        meshKeys.append(mesh->key);
    if (meshKeys != uploadedMeshes || !tbo0)
    {
        uploadTriangleBuffer(tbo0 == 0 || trianglesTextureBuffer == 0);
        uploadNodeBuffer(tbo1 == 0 || nodesTextureBuffer == 0);
        uploadedMeshes = meshKeys;
    }
    uploadMaterialTextures(materialTextureArray == 0 || materialTextureInfoTexture == 0);
    uploadInstanceBuffers(true);
    uploadLightBuffer(tboLights == 0 || lightsTextureBuffer == 0);
    uploadHdrTextures(hdrMap == 0 || hdrCache == 0);
    // 场景或几何变化后光栅化的顶点缓冲与实例属性都要重建。
    m_rasterGeometryUploaded = false;
    m_rasterInstancesUploaded = false;

    pathtrace_program->bind();
    pathtrace_program->setUniformValue("nTriangles", static_cast<int>(m_scene.triangles.size()));
    pathtrace_program->setUniformValue("nNodes", static_cast<int>(m_scene.nodes_encoded.size()));
    pathtrace_program->setUniformValue("nLights",
                                       static_cast<int>(m_scene.lights_encoded.size()));
    pathtrace_program->setUniformValue(
        "nAnalyticLights", std::min(m_scene.document.root["lights"].toArray().size(),
                                    static_cast<int>(m_scene.lights_encoded.size())));
    pathtrace_program->setUniformValue("width", render_width);
    pathtrace_program->setUniformValue("height", render_height);
    pathtrace_program->setUniformValue("hdrResolution", m_scene.hdrResolution);
    pathtrace_program->setUniformValue("materialTextureCount", materialTextureLayerCount);
    pathtrace_program->release();

    stats.uploadMs = uploadTimer.nsecsElapsed() / 1e6;

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void Renderer::displayRenderingStats()
{
    const int nowtime = clock();
    if (nowtime - lasttime > 200)
    {
        printf("\r                                                                                           "
               "       ");
        std::cout << "\rframeCounter: " << frameCounter << " FPS: "
                  << int((frameCounter - lastframeCounter) / (1.0 * (nowtime - lasttime) / 1000.0))
                  << " | Chunked Rendering: " << chunkedRenderingCount << " Chunked Rendering FPS: "
                  << int((chunkedRenderingCount - lastChunkedRenderingCount) /
                         (1.0 * (nowtime - lasttime) / 1000.0));

        lastframeCounter = frameCounter;
        lastChunkedRenderingCount = chunkedRenderingCount;
        lasttime = nowtime;
    }
}

void Renderer::executeRenderPass(const RenderParams::Snapshot &snapshot)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    const int maxBounces = clampMaxBounces(snapshot.maxBounces);

    if (snapshot.useTileRendering)
    {
        // 分块渲染模式
        if (currentTileX < tilesX && currentTileY < tilesY)
        {
            const int tileSize = std::max(1, snapshot.tileSize);
            const int tileWidth = std::min(tileSize, render_width - currentTileX * tileSize);
            const int tileHeight = std::min(tileSize, render_height - currentTileY * tileSize);

            renderTile(currentTileX * tileSize, currentTileY * tileSize, tileWidth, tileHeight, maxBounces);
            updateTileRenderingState();
        }
    }
    else
    {
        // 完整图像渲染模式
        renderFullImage(maxBounces);
    }
}

void Renderer::updateTileRenderingState()
{
    // 更新下一个要渲染的块的位置。
    currentTileX++;
    if (currentTileX >= tilesX)
    {
        currentTileX = 0;
        currentTileY++;

        // 如果所有块都渲染完成，标记一轮分块累计结束。
        if (currentTileY >= tilesY)
        {
            renderComplete = true;
            currentTileX = 0;
            currentTileY = 0;
        }
    }
}

void Renderer::processHistorySaving(const RenderParams::Snapshot &snapshot)
{
    chunkedRenderingCount++;
    nowChunkedCount++;

    // 只有完整图像或完成一整轮分块后才保存历史帧。
    if (!snapshot.useTileRendering || renderComplete)
    {
        nowChunkedCount = 0;
        frameCounter++;
        ++stats.completedRounds;

        historysave_program->bind();
        {
            glBindVertexArray(VAO);
            glDisable(GL_DEPTH_TEST);
            glBindFramebuffer(GL_FRAMEBUFFER, historysave_fbo);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, RenderColorTex);
            historysave_program->setUniformValue("RenderColor", 0);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, normal_texture);
            historysave_program->setUniformValue("NormalColor", 1);
            glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, baseColorTex);
            historysave_program->setUniformValue("BaseColor", 2);

            glViewport(m_viewportX, m_viewportY, render_width, render_height);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        historysave_program->release();

        if (snapshot.useTileRendering)
        {
            renderComplete = false;
        }
    }
}

void Renderer::performDenoising(const RenderParams::Snapshot &snapshot, bool forceCurrentFrame)
{
    if (snapshot.effectiveDenoiseMode() != DenoiseMode::OIDN) {
        if (formal && forceCurrentFrame && snapshot.effectiveDenoiseMode() == DenoiseMode::Realtime)
            realtimeDenoise(snapshot, true);
        return;
    }
    if (!formal)
    {
        requestPreviewDenoise(snapshot, forceCurrentFrame);
        return;
    }
    QElapsedTimer denoiseTimer;
    denoiseTimer.start();
    if (!snapshot.denoise)
    {
        m_forceDenoiseRefresh = false;
        return;
    }

    const bool hasCompleteFrame = (nowChunkedCount == 0 && frameCounter > 0);
    if (!hasCompleteFrame)
    {
        return;
    }

    if (m_hasDenoisedFrame && m_lastDenoisedFrameCounter == frameCounter)
    {
        m_forceDenoiseRefresh = false;
        return;
    }

    const bool shouldDenoise =
        forceCurrentFrame || m_forceDenoiseRefresh || frameCounter % 100 == 0 || frameCounter == 1;
    if (!shouldDenoise)
    {
        return;
    }

    if (!oidnDevice)
        initOIDN();
    else if (oidnSize != QSize(render_width, render_height))
        updateOIDNBuffers();
    ensureDenoisePbos();

    const bool refreshAuxiliaryBuffers = true; // A fresh, matching complete-frame snapshot for each denoise.

    // 使用 PBO 异步读取数据；降噪只在完整累计帧上触发。
    const GLenum formats[] = {GL_RGB, GL_RGB, GL_RGB};
    const GLuint textures[] = {previousNormalTex, previousAlbedoTex, preRenderColorTex};
    float *srcPtrs[3] = {nullptr, nullptr, nullptr};

    for (int i = refreshAuxiliaryBuffers ? 0 : 2; i < 3; i++)
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pboIds[i]);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glGetTexImage(GL_TEXTURE_2D, 0, formats[i], GL_FLOAT, 0);
        srcPtrs[i] = reinterpret_cast<float *>(glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    for (auto ptr : srcPtrs)
        if (!ptr)
        {
            for (int i = 0; i < 3; ++i)
                if (srcPtrs[i])
                {
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, pboIds[i]);
                    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                }
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            throw std::runtime_error("OIDN auxiliary readback failed");
        }

    if (refreshAuxiliaryBuffers)
    {
        auto *normals = static_cast<float *>(oidnNormalBuf.getData());
        stats.normalMinimum = 1;
        stats.normalMaximum = -1;
        decodeOidnNormals(normals,srcPtrs[0],oidnNormalBuf.getSize()/sizeof(float),stats.normalMinimum,stats.normalMaximum);
        stats.auxiliarySize = QSize(render_width, render_height);
        std::memcpy(oidnAlbedoBuf.getData(), srcPtrs[1], oidnAlbedoBuf.getSize());
    }
    std::memcpy(oidnColorBuf.getData(), srcPtrs[2], oidnColorBuf.getSize());

    for (int i = refreshAuxiliaryBuffers ? 0 : 2; i < 3; i++)
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pboIds[i]);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    auto progress = [](void *user, double) -> bool {
        auto stop = static_cast<std::atomic_bool *>(user);
        return !stop || !stop->load();
    };
    oidnAlbedoFilter.setProgressMonitorFunction(progress, cancel);
    oidnNormalFilter.setProgressMonitorFunction(progress, cancel);
    oidnMainFilter.setProgressMonitorFunction(progress, cancel);
    if (denoising)
        denoising();
    if (refreshAuxiliaryBuffers)
    {
        oidnAlbedoFilter.execute();
        oidnNormalFilter.execute();
    }

    oidnMainFilter.execute();

    const char *errorMessage = nullptr;
    const auto error = oidnDevice.getError(errorMessage);
    if (error == oidn::Error::Cancelled)
        return;
    if (error != oidn::Error::None)
        throw std::runtime_error(errorMessage ? errorMessage : "OIDN failed");

    glBindTexture(GL_TEXTURE_2D, RenderColorTexfiltered);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, render_width, render_height, GL_RGB, GL_FLOAT,
                    oidnOutputBuf.getData());
    m_hasDenoisedFrame = true;
    stats.denoisedVersion = stats.accumulationVersion;
    m_lastDenoisedFrameCounter = frameCounter;
    m_forceDenoiseRefresh = false;
    stats.oidnMs = denoiseTimer.nsecsElapsed() / 1e6;
}

void Renderer::compositeToScreen(const RenderParams::Snapshot &snapshot)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    ++m_imageRevision;
    ++stats.compositeCount;
    m_program->bind();
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        glActiveTexture(GL_TEXTURE5);
        if (!m_rasterActive && snapshot.effectiveDenoiseMode() != DenoiseMode::None && m_hasDenoisedFrame)
        {
            glBindTexture(GL_TEXTURE_2D, snapshot.effectiveDenoiseMode() == DenoiseMode::Realtime ? gpuDenoiser.output() : RenderColorTexfiltered);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, m_rasterActive ? RenderColorTex : preRenderColorTex);
        }
        m_program->setUniformValue("texPass1", 5);
        auto display = m_scene.document.root["display"].toObject();
        m_program->setUniformValue("exposure", float(display["exposure"].toDouble()));
        m_program->setUniformValue("tonemap", display["tonemap"].toInt());
        glActiveTexture(GL_TEXTURE6);

        // 渲染到屏幕尺寸，所以这里使用窗口尺寸。
        glViewport(m_viewportX, m_viewportY, m_width, m_height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 全屏四边形的 VAO 必须在绘制前绑定：光栅化交互预览会解绑它，
        // 否则合成阶段没有顶点数据，显示纹理会一直是黑的。
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }
    m_program->release();
}

void Renderer::setRasterActive(bool active)
{
    m_rasterActive = active && m_rasterCapable && !formal;
    stats.rasterActive = m_rasterActive;
}

// 光栅化交互预览程序。启用环境贴图时注入 USEENVIRONMENTMAP，与路径追踪的开关语义一致；
// 编译失败只降级到路径追踪，不影响应用其余功能。
void Renderer::rebuildRasterProgram(const RenderParams::Snapshot &snapshot)
{
    std::unordered_map<std::string, std::string> defines;
    defines.insert({"INSTANCED_SCENE", "1"});
    defines.insert({"MATERIAL_TEXTURE_POOL_COUNT",std::to_string(materialTexturePoolCapacity)});
    if (snapshot.useEnvironmentMap)
        defines.insert({"USEENVIRONMENTMAP", ""});
    try
    {
        raster_program.reset(
            getShaderProgram(getShaderPath("raster.frag"), getShaderPath("raster.vert"), {}, defines));
        rasterBackgroundProgram.reset(getShaderProgram(getShaderPath("raster_background.frag"),
                                                       getShaderPath("triangle.vert"), {}, defines));
        m_rasterCapable = raster_program && raster_program->isLinked() &&
                          rasterBackgroundProgram && rasterBackgroundProgram->isLinked();
    }
    catch (const std::exception &error)
    {
        qWarning() << "Raster preview shader unavailable:" << error.what();
        raster_program.reset();
        rasterBackgroundProgram.reset();
        m_rasterCapable = false;
    }
    if (!m_rasterCapable)
    {
        setRasterActive(false);
        qWarning() << "Raster preview disabled: falling back to the path traced preview";
    }
}

// 光栅化交互预览：一次前向着色，不累积、不分块、不做降噪。
// 不支持阴影、环境镜面反射、法线贴图、折射与透明混合、体积与 AO；透明材质按不透明处理。
bool Renderer::renderRasterPreview(const RenderParams::Snapshot &snapshot)
{
    if (!raster_program || !rasterBackgroundProgram || !rasterVao)
        return false;

    QMutexLocker lock(&param_mutex);

    // 光栅化耗时用 GPU 计时查询测，和路径追踪的 gpuMs 口径一致；
    // 查询未就绪时保留上一帧数值，不冒充本帧结果。
    pollGpuTimers();
    if (!rasterTimerQuery)
        glGenQueries(1, &rasterTimerQuery);
    const bool rasterTimed = !rasterTimerPending;
    if (rasterTimed)
        glBeginQuery(GL_TIME_ELAPSED, rasterTimerQuery);

    // 光栅化写进专用 FBO（颜色靶与路径追踪共用 RenderColorTex），不触碰 pathtrace_fbo。
    glBindFramebuffer(GL_FRAMEBUFFER, multisampleFbo ? multisampleFbo : rasterFbo);
    glEnable(GL_MULTISAMPLE);
    glViewport(0, 0, render_width, render_height);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    rasterNeedsComposite = true;

    raster_program->bind();
    const QMatrix4x4 view = m_scene.camera.getViewMatrix();
    // 路径追踪把视场角烘进光线方向，光栅化必须自己做透视投影。
    const float fov = float(qBound(1.0, double(m_scene.camera.zoom), 179.0));
    const float aspect = render_height > 0 ? float(render_width) / float(render_height) : 1.0f;
    QMatrix4x4 projection;
    projection.perspective(fov, aspect, 0.01f, 1.0e6f);
    raster_program->setUniformValue("projection", projection);
    raster_program->setUniformValue("view", view);
    raster_program->setUniformValue("eye", m_scene.camera.position);
    raster_program->setUniformValue("nLights", int(m_scene.lights_encoded.size()));
    raster_program->setUniformValue(
        "nAnalyticLights",
        std::min(m_scene.document.root["lights"].toArray().size(),
                 int(m_scene.lights_encoded.size())));

    raster_program->setUniformValue("lights", 4);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_BUFFER, lightsTextureBuffer);

    raster_program->setUniformValue("materialTextures", 5);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, materialTextureArray);

    raster_program->setUniformValue("materialTextureInfo", 6);
    raster_program->setUniformValue("materialTextureInfoStride",4);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_BUFFER, materialTextureInfoTexture);

    raster_program->setUniformValue("materialTextureCount", materialTextureLayerCount);
    bindMaterialTextureInputs(raster_program.get(),5,6);
    raster_program->setUniformValue("materialTable", 8);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_BUFFER, instanceTextures[1]);

    // 环境背景与环境项：开关由 USEENVIRONMENTMAP 决定，强度/旋转跟随文档设置。
    const auto environment = m_scene.document.root["environment"].toObject();
    raster_program->setUniformValue("environmentIntensity", float(environment["intensity"].toDouble(1)));
    raster_program->setUniformValue("environmentRotation",
                                    float(environment["rotation"].toDouble() * PI / 180));
    raster_program->setUniformValue("hdrResolution", m_scene.hdrResolution);
    raster_program->setUniformValueArray("diffuseEnvironment", rasterEnvironment.data(), 9);
    raster_program->setUniformValue("hdrMap", 9);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, hdrMap);
    raster_program->setUniformValue("hdrCache", 10);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, hdrCache);

    // Fill uncovered pixels independently of mesh fragments. Geometry then overwrites
    // the background using its own depth buffer and unmodified material radiance.
    rasterBackgroundProgram->bind();
    rasterBackgroundProgram->setUniformValue("view", view.inverted());
    rasterBackgroundProgram->setUniformValue("cameraFov", fov);
    rasterBackgroundProgram->setUniformValue("width", render_width);
    rasterBackgroundProgram->setUniformValue("height", render_height);
    rasterBackgroundProgram->setUniformValue("hdrMap", 9);
    rasterBackgroundProgram->setUniformValue("environmentIntensity", float(environment["intensity"].toDouble(1)));
    rasterBackgroundProgram->setUniformValue("environmentRotation", float(environment["rotation"].toDouble() * PI / 180));
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    raster_program->bind();

    glBindVertexArray(rasterVao);
    // 实例属性缓冲按实例顺序排列；同 mesh 的实例区间在构建时已保证连续。
    GLint instanceOffset = 0;
    for (size_t mesh = 0; mesh < rasterRanges.size(); ++mesh)
    {
        int instances = 0;
        while (instanceOffset + instances < rasterInstanceCount &&
               rasterInstanceMesh[size_t(instanceOffset + instances)] == int(mesh))
            ++instances;
        if (instances > 0 && rasterRanges[mesh].count > 0)
        {
            // OpenGL 3.3 has no base-instance draw: offset the instanced attributes per mesh.
            glBindBuffer(GL_ARRAY_BUFFER, rasterInstanceBuffer);
            const size_t base = size_t(instanceOffset) * 5 * sizeof(QVector4D);
            for (GLuint column = 0; column < 5; ++column)
                glVertexAttribPointer(3 + column, column == 4 ? 1 : 4, GL_FLOAT, GL_FALSE,
                                      5 * sizeof(QVector4D),
                                      reinterpret_cast<const void *>(base + column * sizeof(QVector4D)));
            glDrawArraysInstanced(GL_TRIANGLES, rasterRanges[mesh].first, rasterRanges[mesh].count, instances);
        }
        instanceOffset += instances;
    }
    glBindVertexArray(0);
    raster_program->release();
    if (multisampleFbo) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, multisampleFbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rasterFbo);
        glBlitFramebuffer(0,0,render_width,render_height,0,0,render_width,render_height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    if (rasterTimed)
    {
        glEndQuery(GL_TIME_ELAPSED);
        rasterTimerPending = true;
    }
    pollGpuTimers();
    stats.rasterActive = true;
    // 显示桥仍然按 16 ms 节奏消费，这里走与路径追踪相同的合成路径，
    // 曝光与色调映射因此对两种预览完全一致。
    compositePreview(snapshot, displayDirty, firstComposite);
    return true;
}
void Renderer::uploadRasterGeometry()
{
    // 顶点按 mesh 展开，记录每个 mesh 的连续区间；绘制时同 mesh 的实例合并成一次实例化绘制。
    // 实例化绘制没有 baseVertex 重映射，所以这里必须按 mesh 展开，而不是共用一份索引缓冲。
    const auto &scene = m_scene;
    std::vector<float> vertices;
    size_t totalVertices = 0;
    for (const auto &mesh : scene.meshes)
        totalVertices += mesh ? mesh->triangles.size() * 3u : 0u;
    vertices.reserve(totalVertices * 8u);
    rasterRanges.assign(scene.meshes.size(), RasterDrawRange{});
    for (size_t i = 0; i < scene.meshes.size(); ++i)
    {
        const auto &mesh = scene.meshes[i];
        if (!mesh)
            continue;
        rasterRanges[i].first = GLint(vertices.size() / 8u);
        for (const auto &triangle : mesh->triangles)
        {
            const QVector3D positions[3] = {triangle.p1, triangle.p2, triangle.p3};
            const QVector3D normals[3] = {triangle.n1, triangle.n2, triangle.n3};
            const QVector2D uvs[3] = {triangle.uv1, triangle.uv2, triangle.uv3};
            for (int corner = 0; corner < 3; ++corner)
            {
                vertices.insert(vertices.end(), {positions[corner].x(), positions[corner].y(),
                                                 positions[corner].z(), normals[corner].x(),
                                                 normals[corner].y(), normals[corner].z(),
                                                 uvs[corner].x(), uvs[corner].y()});
            }
        }
        rasterRanges[i].count = GLsizei(vertices.size() / 8u) - rasterRanges[i].first;
    }
    rasterVertexCount = vertices.size() / 8u;

    if (!rasterVertexBuffer)
        glGenBuffers(1, &rasterVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, rasterVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 GLsizeiptr(std::max<size_t>(vertices.size() * sizeof(float), sizeof(float) * 8u)),
                 vertices.empty() ? nullptr : vertices.data(), GL_STATIC_DRAW);

    if (!rasterVao)
        glGenVertexArrays(1, &rasterVao);
    glBindVertexArray(rasterVao);
    glBindBuffer(GL_ARRAY_BUFFER, rasterVertexBuffer);
    const GLsizei stride = GLsizei(8 * sizeof(float));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid *>(0));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid *>(3 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid *>(6 * sizeof(float)));
    for (GLuint location = 0; location < 3; ++location)
        glEnableVertexAttribArray(location);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_rasterGeometryUploaded = true;
    m_rasterInstancesUploaded = false; // 几何变化后实例的 mesh 分组也要重建
    qInfo() << "Raster preview geometry:" << rasterVertexCount << "vertices" << rasterRanges.size() << "meshes";
}

void Renderer::uploadRasterInstances()
{
    // Each visible instance stores four matrix columns and one material vector, grouped by mesh.
    const auto &scene = m_scene;
    std::vector<QVector4D> data;
    rasterInstanceMesh.clear();
    std::vector<const SceneInstance *> sorted;
    for (const auto &instance : scene.instances)
        if (instance.visible)
            sorted.push_back(&instance);
    std::stable_sort(sorted.begin(), sorted.end(), [](const SceneInstance *a, const SceneInstance *b) {
        return a->mesh < b->mesh;
    });
    for (const auto *entry : sorted)
    {
        const auto &instance = *entry;
        if (!instance.visible)
            continue;
        // 材质下标越界时回退到 0，避免着色器读到材质表外的数据。
        const int material = instance.material >= 0 && instance.material < int(scene.materials.size())
                                 ? instance.material
                                 : 0;
        for (int column = 0; column < 4; ++column)
            data.push_back(instance.transform.column(column));
        data.push_back(QVector4D(float(material), 0.0f, 0.0f, 0.0f));
        rasterInstanceMesh.push_back(instance.mesh);
    }
    rasterInstanceCount = GLsizei(rasterInstanceMesh.size());

    if (!rasterInstanceBuffer)
        glGenBuffers(1, &rasterInstanceBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, rasterInstanceBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 GLsizeiptr(std::max<size_t>(data.size() * sizeof(QVector4D), sizeof(QVector4D))),
                 data.empty() ? nullptr : data.data(), GL_DYNAMIC_DRAW);
    if (!rasterVao)
        glGenVertexArrays(1, &rasterVao);
    glBindVertexArray(rasterVao);
    glBindBuffer(GL_ARRAY_BUFFER, rasterInstanceBuffer);
    const GLsizei instStride = GLsizei(5 * sizeof(QVector4D));
    for (int column = 0; column < 4; ++column)
    {
        const GLuint location = 3 + column;
        glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, instStride,
                              reinterpret_cast<GLvoid *>(size_t(column) * sizeof(QVector4D)));
        glEnableVertexAttribArray(location);
        glVertexAttribDivisor(location, 1);
    }
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, instStride,
                          reinterpret_cast<GLvoid *>(4 * sizeof(QVector4D)));
    glEnableVertexAttribArray(7);
    glVertexAttribDivisor(7, 1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_rasterInstancesUploaded = true;
}

void Renderer::releaseRasterResources()
{
    if (rasterVao)
        glDeleteVertexArrays(1, &rasterVao);
    if (rasterVertexBuffer)
        glDeleteBuffers(1, &rasterVertexBuffer);
    if (rasterInstanceBuffer)
        glDeleteBuffers(1, &rasterInstanceBuffer);
    rasterVao = rasterVertexBuffer = rasterInstanceBuffer = 0;
    raster_program.reset();
    rasterBackgroundProgram.reset();
}

QJsonObject Renderer::pathDiagnostics()
{
    if(!completeRound()) throw std::runtime_error("Diagnostics require a complete sample round");
    std::vector<float> normal(size_t(render_width)*render_height*4), albedo(normal.size());
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glBindTexture(GL_TEXTURE_2D,previousNormalTex);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,normal.data());
    glBindTexture(GL_TEXTURE_2D,previousAlbedoTex);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,albedo.data());
    if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("Diagnostic readback failed");
    return summarizePathDiagnostics(normal,albedo,render_width,frameCounter,stats.accumulationVersion);
}
QJsonObject Renderer::traceProfile()
{
    if(!qEnvironmentVariableIsSet("LEARNQT_TRACE_PROFILE") || !completeRound())
        throw std::runtime_error("Trace profile requires its diagnostic variant and a complete round");
    std::vector<float> a(size_t(render_width)*render_height*4),b(a.size());
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glBindTexture(GL_TEXTURE_2D,previousNormalTex);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,a.data());
    glBindTexture(GL_TEXTURE_2D,previousAlbedoTex);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,b.data());
    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Trace profile readback failed");
    double values[6]={};
    for(size_t i=0;i<a.size();i+=4)for(int c=0;c<3;++c) {
        values[c]+=a[i+c]/double(render_width*render_height);
        values[c+3]+=b[i+c]/double(render_width*render_height);
    }
    QJsonObject result{{"samples",int(frameCounter)},{"pixels",render_width*render_height},
        {"basis","Per pixel per sample averages. Instrumented diagnostic shader; not production timing."},
        {"hardwareCounters","Registers, spills, bandwidth and occupancy require an external hardware profiler; unavailable here."}};
    const char *names[]={"nodeVisits","triangleTests","scatters","materialTexelFetches","lightAttempts","invalidLightSamples"};
    for(int i=0;i<6;++i)result[names[i]]=values[i];
    result["invalidLightFraction"]=values[4]>0?values[5]/values[4]:0;
    return result;
}
QImage Renderer::result(const RenderParams::Snapshot &snapshot)
{
    if (!frameCounter)
        return {};
    bool previous = formal;
    formal = true;
    compositeToScreen(snapshot);
    formal = previous;
    QImage image(m_width, m_height, QImage::Format_RGBA8888);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    return image.mirrored().convertToFormat(QImage::Format_RGB32);
}
void Renderer::finishDenoise(const RenderParams::Snapshot &snapshot)
{
    performDenoising(snapshot, true);
    compositeToScreen(snapshot);
}
void Renderer::prepareJob(QSize size, const RenderParams::Snapshot &snapshot, SceneDirtyFlags dirty)
{
    formal = true;
    setRasterActive(false);
    GLint maximum = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    if (size.width() > maximum || size.height() > maximum)
        throw std::runtime_error("Output resolution exceeds the GPU texture limit");
    auto actions = resolveRefreshActions(size.width(), size.height(), snapshot,
                                         dirty | toSceneDirtyFlags(SceneDirtyFlag::Camera));
    applyRefreshActions(size.width(), size.height(), snapshot, actions);
    resetAccumulation();
}
quint64 Renderer::allocatedBytes() const
{
    const auto &s = m_scene;
    quint64 bytes = textureArrayBytes + quint64(m_width) * m_height * 16 +
                    quint64(render_width) * render_height * 7 * 16 +
                    quint64(denoisePboSize.width()) * denoisePboSize.height() * 3 * 12 +
                    quint64(pickSize.width()) * pickSize.height() * 8;
    bytes += s.geometryData.size() * sizeof(QVector4D) + s.nodes_encoded.size() * sizeof(BVHNode_encoded) +
             s.instanceData.size() * sizeof(QVector4D) + s.materialData.size() * sizeof(QVector4D) +
             s.tlasData.size() * sizeof(BVHNode_encoded) + s.surfaces.size() * sizeof(SurfaceReference) +
             ((s.surfacePdfs.size()+3)/4) * sizeof(QVector4D) + s.lights_encoded.size() * sizeof(Light_encoded) +
             std::max(1, materialTextureLayerCount) * 5 * sizeof(QVector4D) +
             quint64(s.hdrRes.width) * s.hdrRes.height * 24;
    if (oidnColorBuf)
        bytes += oidnColorBuf.getSize() + oidnAlbedoBuf.getSize() + oidnNormalBuf.getSize() +
                 oidnOutputBuf.getSize();
    bytes += previewDenoiser.allocatedBytes() + gpuDenoiser.allocatedBytes();
    if (multisampleFbo) bytes += quint64(render_width)*render_height*multisampleCount*20;
    bytes += quint64(rasterVertexCount) * 8 * sizeof(float) +
             quint64(rasterInstanceCount) * 5 * sizeof(QVector4D) +
             quint64(rasterDepthSize.width()) * rasterDepthSize.height() * 4;
    return bytes;
}
