#include "renderer.h"

#include "MaterialTextureImage.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <regex>
#include <vector>

extern QMutex param_mutex;

namespace
{
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

    bool success = shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vSource.c_str());
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

    success = shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fSource.c_str());
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
void Renderer::ensureDepthAttachment()
{
    if (!pathtrace_fbo || render_width <= 0 || render_height <= 0)
        return;
    if (rasterFbo && rasterDepthSize == QSize(render_width, render_height))
        return;
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
    rasterDepthSize = QSize(render_width, render_height);
}

Renderer::Renderer(int width, int height, const RenderParams::Snapshot &initialSnapshot, QObject *parent)
    : QObject(parent)
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
        ensureDepthAttachment();
        if (renderRasterPreview(snapshot))
            return;
        // 光栅化资源不可用时回到路径追踪，本帧不再重复重置累积。
        setRasterActive(false);
        m_rasterRequested = false;
    }

    const bool previewChanged = pollPreviewDenoise(snapshot);
    stats.batchTiles = 0;
    if (!samplingActive(snapshot))
    {
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
    bindPathtraceInputs(clampMaxBounces(snapshot.maxBounces));
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
    const bool finished = !samplingActive(snapshot);
    if (!formal)
        performDenoising(snapshot, finished);
    compositePreview(snapshot, true, finished || actions.refreshDisplay);
}

void Renderer::compositePreview(const RenderParams::Snapshot &snapshot, bool changed, bool force)
{
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

    qDebug() << reinterpret_cast<const char *>(glGetString(GL_VERSION));

    rebuildPathtraceProgram(snapshot);

    preRenderColorTex = getTextureRGB32F(render_width, render_height);
    RenderColorTex = getTextureRGB32F(render_width, render_height);
    normal_texture = getTextureRGB32F(render_width, render_height);
    baseColorTex = getTextureRGB32F(render_width, render_height);
    batchTextureSettings.insert(batchTextureSettings.end(),
                                {preRenderColorTex, RenderColorTex, normal_texture, baseColorTex});

    pathtrace_fbo = bindData(std::vector<GLuint>{RenderColorTex, normal_texture, baseColorTex});
    ensureDepthAttachment();

    historysave_program.reset(
        getShaderProgram(getShaderPath("historysave.frag"), getShaderPath("triangle.vert")));
    historysave_fbo = bindData(std::vector<GLuint>{preRenderColorTex});

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
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteFramebuffers(1, &pathtrace_fbo);
    glDeleteFramebuffers(1, &historysave_fbo);
    glDeleteRenderbuffers(1, &depthRenderbuffer);
    glDeleteFramebuffers(1, &rasterFbo);

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

void Renderer::bindPathtraceInputs(int maxBounces)
{
    const unsigned int sobolBounceCount = static_cast<unsigned int>(std::max(1, maxBounces));
    const auto sobelNumber = getSobelRandomNumber(frameCounter, sobolBounceCount);

    pathtrace_program->bind();
    {
        const GLint frameLocation = pathtrace_program->uniformLocation("frameCounter");
        glUniform1ui(frameLocation, frameCounter);
        const GLint sobelLocation = pathtrace_program->uniformLocation("sobelNumber");
        glUniform1fv(sobelLocation, static_cast<GLsizei>(sobolBounceCount * 2u), sobelNumber.data());
        pathtrace_program->setUniformValue("maxBounces", maxBounces);

        pathtrace_program->setUniformValue("triangles", 0);
        pathtrace_program->setUniformValue("nodes", 1);
        pathtrace_program->setUniformValue("hdrMap", 2);
        pathtrace_program->setUniformValue("hdrCache", 3);
        pathtrace_program->setUniformValue("lights", 5);
        pathtrace_program->setUniformValue("materialTextures", 6);
        pathtrace_program->setUniformValue("materialTextureInfo", 7);

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
    }
    pathtrace_program->release();
}

void Renderer::renderTile(int tileX, int tileY, int tileWidth, int tileHeight, int)
{
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

void Renderer::rebuildPathtraceProgram(const RenderParams::Snapshot &snapshot)
{
    std::unordered_map<std::string, std::string> defines_Fragment = {};
    defines_Fragment.insert({"INSTANCED_SCENE", "1"});
    std::unordered_map<std::string, std::string> defines_Vertex = {};
    defines_Fragment.insert({"MAX_BOUNCES_LIMIT", std::to_string(MAX_BOUNCES_LIMIT)});
    if (snapshot.useEnvironmentMap)
    {
        defines_Fragment.insert({"USEENVIRONMENTMAP", ""});
    }
    pathtrace_program.reset(getShaderProgram(getShaderPath("pathtrace.frag"), getShaderPath("triangle.vert"),
                                             defines_Vertex, defines_Fragment));
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
    const bool denoiseChanged = snapshot.denoise != m_lastAppliedSnapshot.denoise;
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

    if (!targetsValid || renderLowChanged || sizeChanged)
    {
        actions.resizeTargets = true;
        actions.resetAccumulation = true;
    }

    if (tileModeChanged || tileSizeChanged)
    {
        actions.resetAccumulation = true;
    }

    if (maxBouncesChanged)
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

    if (hasSceneDirtyFlag(dirtyFlags, SceneDirtyFlag::Material))
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
    if (actions.rebuildShader)
    {
        rebuildPathtraceProgram(snapshot);
    }

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
    }

    if (actions.syncSceneBuffers)
    {
        syncSceneBuffers();
    }
    else if (actions.syncMaterialBuffer)
    {
        syncMaterialBuffer();
    }

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
    const auto &instances = Scene::getInstance().instances;
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
    clearTexture(RenderColorTexfiltered);
    clearTexture(m_texture);

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
    stats.geometryUploadBytes += Scene::getInstance().geometryData.size() * sizeof(QVector4D);
    const auto &trianglesEncoded = Scene::getInstance().geometryData;
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
    const auto &nodesEncoded = Scene::getInstance().nodes_encoded;
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
    const auto &lightsEncoded = Scene::getInstance().lights_encoded;
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

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, Scene::getInstance().hdrRes.width,
                     Scene::getInstance().hdrRes.height, 0, GL_RGB, GL_FLOAT, data);
    };

    uploadTexture(hdrMap, Scene::getInstance().hdrRes.cols);
    uploadTexture(hdrCache, Scene::getInstance().cache);
}

void Renderer::uploadMaterialTextures(bool recreateResources)
{
    constexpr int maxTextureDimension = 2048;
    const auto &sourceTextures = Scene::getInstance().textures;

    GLint hardwareMaxSize = 1;
    GLint hardwareMaxLayers = 1;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &hardwareMaxSize);
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &hardwareMaxLayers);

    const int layerCount = std::min(static_cast<int>(sourceTextures.size()), hardwareMaxLayers);
    int textureWidth = 1;
    int textureHeight = 1;
    for (int i = 0; i < layerCount; ++i)
    {
        textureWidth = std::max(textureWidth, sourceTextures[i].width);
        textureHeight = std::max(textureHeight, sourceTextures[i].height);
    }
    textureWidth = std::min(textureWidth, std::min(hardwareMaxSize, maxTextureDimension));
    textureHeight = std::min(textureHeight, std::min(hardwareMaxSize, maxTextureDimension));

    if (static_cast<int>(sourceTextures.size()) > layerCount)
    {
        qWarning() << "Material texture count exceeds GL_MAX_ARRAY_TEXTURE_LAYERS; extra textures use scalar "
                      "fallbacks:"
                   << sourceTextures.size() << hardwareMaxLayers;
    }

    if (recreateResources && materialTextureArray != 0)
    {
        glDeleteTextures(1, &materialTextureArray);
        materialTextureArray = 0;
    }
    if (recreateResources && materialTextureInfoTexture != 0)
    {
        glDeleteTextures(1, &materialTextureInfoTexture);
        materialTextureInfoTexture = 0;
    }
    if (recreateResources && materialTextureInfoBuffer != 0)
    {
        glDeleteBuffers(1, &materialTextureInfoBuffer);
        materialTextureInfoBuffer = 0;
    }
    if (materialTextureArray == 0)
    {
        glGenTextures(1, &materialTextureArray);
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, materialTextureArray);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Per-material wrap is applied in the shader; clamp here keeps an exact
    // coordinate of 1.0 on the edge for clamp/mirror modes.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const int allocatedLayers = std::max(1, layerCount);
    textureArrayBytes = 0;
    for (int w = textureWidth, h = textureHeight;; w = std::max(1, w / 2), h = std::max(1, h / 2))
    {
        textureArrayBytes += quint64(w) * h * allocatedLayers * 4;
        if (w == 1 && h == 1)
            break;
    }
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, textureWidth, textureHeight, allocatedLayers, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (layerCount == 0)
    {
        const unsigned char white[] = {255, 255, 255, 255};
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
    }
    else
    {
        for (int layer = 0; layer < layerCount; ++layer)
        {
            const QImage image =
                prepareMaterialTextureImage(sourceTextures[layer].image, QSize(textureWidth, textureHeight));
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, textureWidth, textureHeight, 1, GL_RGBA,
                            GL_UNSIGNED_BYTE, image.constBits());
        }
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    std::vector<QVector4D> textureInfo(static_cast<size_t>(std::max(1, layerCount)) * 3u);
    textureInfo[0] = QVector4D(1.0f, 1.0f, 0.0f, 0.0f);
    textureInfo[1] = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);
    textureInfo[2] = QVector4D(9987.0f, 9729.0f, 0.0f, 0.0f);
    for (int layer = 0; layer < layerCount; ++layer)
    {
        const TextureAsset &source = sourceTextures[layer];
        textureInfo[static_cast<size_t>(layer) * 3u] =
            QVector4D(source.uvScale.x(), source.uvScale.y(), source.uvOffset.x(), source.uvOffset.y());
        textureInfo[static_cast<size_t>(layer) * 3u + 1u] = QVector4D(
            source.uvRotation, static_cast<float>(source.wrapS), static_cast<float>(source.wrapT), 0.0f);
        textureInfo[static_cast<size_t>(layer) * 3u + 2u] =
            QVector4D(static_cast<float>(source.minFilter), static_cast<float>(source.magFilter), 0.0f, 0.0f);
    }

    if (materialTextureInfoBuffer == 0)
    {
        glGenBuffers(1, &materialTextureInfoBuffer);
    }
    glBindBuffer(GL_TEXTURE_BUFFER, materialTextureInfoBuffer);
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(textureInfo.size() * sizeof(QVector4D)),
                 textureInfo.data(), GL_STATIC_DRAW);
    if (materialTextureInfoTexture == 0)
    {
        glGenTextures(1, &materialTextureInfoTexture);
    }
    glBindTexture(GL_TEXTURE_BUFFER, materialTextureInfoTexture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, materialTextureInfoBuffer);
    glBindTexture(GL_TEXTURE_BUFFER, 0);

    materialTextureLayerCount = layerCount;
    qDebug() << "Uploaded material texture array:" << materialTextureLayerCount << "layers at" << textureWidth
             << "x" << textureHeight;
}

void Renderer::syncCameraUniforms()
{
    QMatrix4x4 inverseView;
    QVector3D eye;
    float fov;
    {
        QMutexLocker lock(&param_mutex);
        const QMatrix4x4 view = Scene::getInstance().camera.getViewMatrix();
        inverseView = view.inverted();
        eye = Scene::getInstance().camera.position;
        fov = Scene::getInstance().camera.zoom;
    }

    pathtrace_program->bind();
    pathtrace_program->setUniformValue("view", inverseView);
    pathtrace_program->setUniformValue("eye", eye);
    pathtrace_program->setUniformValue("cameraFov", fov);
    pathtrace_program->release();
}

void Renderer::uploadInstanceBuffers(bool topology)
{
    const auto &scene = Scene::getInstance();
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
    upload(1, GL_RGBA32F, scene.materialData.data(), scene.materialData.size() * sizeof(QVector4D));
    upload(2, GL_RGB32F, scene.tlasData.data(), scene.tlasData.size() * sizeof(BVHNode_encoded));
    if (topology)
        upload(3, GL_RG32UI, scene.surfaces.data(), scene.surfaces.size() * sizeof(SurfaceReference));
    upload(4, GL_R32F, scene.surfacePdfs.data(), scene.surfacePdfs.size() * sizeof(float));
}
void Renderer::bindInstanceBuffers(QOpenGLShaderProgram *program)
{
    const char *names[] = {"instanceTable", "materialTable", "topNodes", "surfaceTable", "surfacePdfTable"};
    for (int i = 0; i < 5; ++i)
    {
        glActiveTexture(GL_TEXTURE8 + i);
        glBindTexture(GL_TEXTURE_BUFFER, instanceTextures[i]);
        program->setUniformValue(names[i], 8 + i);
    }
    program->setUniformValue("nTopNodes", int(Scene::getInstance().tlas.size()));
    program->setUniformValue("picking", false);
    auto env = Scene::getInstance().document.root["environment"].toObject();
    program->setUniformValue("environmentIntensity", float(env["intensity"].toDouble(1)));
    program->setUniformValue("environmentRotation", float(env["rotation"].toDouble() * PI / 180));
}

void Renderer::syncMaterialBuffer()
{
    QMutexLocker lock(&param_mutex);
    // 材质脏路径只重传三角形编码缓冲，不触碰 BVH / HDR 资源。
    uploadInstanceBuffers(false);
    // 光栅化预览从 instance/material 表读取，材质编辑后实例属性必须重传。
    m_rasterInstancesUploaded = false;
    uploadLightBuffer(tboLights == 0 || lightsTextureBuffer == 0);
    pathtrace_program->bind();
    pathtrace_program->setUniformValue("nLights",
                                       static_cast<int>(Scene::getInstance().lights_encoded.size()));
    pathtrace_program->setUniformValue(
        "nAnalyticLights", std::min(Scene::getInstance().document.root["lights"].toArray().size(),
                                    static_cast<int>(Scene::getInstance().lights_encoded.size())));
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
    for (auto &mesh : Scene::getInstance().meshes)
        meshKeys.append(mesh->key);
    if (meshKeys != uploadedMeshes || !tbo0)
    {
        uploadTriangleBuffer(tbo0 == 0 || trianglesTextureBuffer == 0);
        uploadNodeBuffer(tbo1 == 0 || nodesTextureBuffer == 0);
        uploadedMeshes = meshKeys;
    }
    uploadInstanceBuffers(true);
    uploadLightBuffer(tboLights == 0 || lightsTextureBuffer == 0);
    uploadHdrTextures(hdrMap == 0 || hdrCache == 0);
    uploadMaterialTextures(materialTextureArray == 0 || materialTextureInfoTexture == 0);
    // 场景或几何变化后光栅化的顶点缓冲与实例属性都要重建。
    m_rasterGeometryUploaded = false;
    m_rasterInstancesUploaded = false;

    pathtrace_program->bind();
    pathtrace_program->setUniformValue("nTriangles", static_cast<int>(Scene::getInstance().triangles.size()));
    pathtrace_program->setUniformValue("nNodes", static_cast<int>(Scene::getInstance().nodes_encoded.size()));
    pathtrace_program->setUniformValue("nLights",
                                       static_cast<int>(Scene::getInstance().lights_encoded.size()));
    pathtrace_program->setUniformValue(
        "nAnalyticLights", std::min(Scene::getInstance().document.root["lights"].toArray().size(),
                                    static_cast<int>(Scene::getInstance().lights_encoded.size())));
    pathtrace_program->setUniformValue("width", render_width);
    pathtrace_program->setUniformValue("height", render_height);
    pathtrace_program->setUniformValue("hdrResolution", Scene::getInstance().hdrResolution);
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

        historysave_program->bind();
        {
            glBindVertexArray(VAO);
            glDisable(GL_DEPTH_TEST);
            glBindFramebuffer(GL_FRAMEBUFFER, historysave_fbo);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, RenderColorTex);
            historysave_program->setUniformValue("RenderColor", 0);

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
    const GLuint textures[] = {normal_texture, baseColorTex, RenderColorTex};
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
        for (size_t i = 0; i < oidnNormalBuf.getSize() / sizeof(float); ++i)
        {
            normals[i] = srcPtrs[0][i] * 2.0f - 1.0f;
            stats.normalMinimum = std::min(stats.normalMinimum, double(normals[i]));
            stats.normalMaximum = std::max(stats.normalMaximum, double(normals[i]));
        }
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
        if (!m_rasterActive && snapshot.denoise && m_hasDenoisedFrame)
        {
            glBindTexture(GL_TEXTURE_2D, RenderColorTexfiltered);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, formal ? preRenderColorTex : RenderColorTex);
        }
        m_program->setUniformValue("texPass1", 5);
        auto display = Scene::getInstance().document.root["display"].toObject();
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
// 明确不支持阴影、IBL、法线贴图、折射与透明混合、体积与 AO，透明材质按不透明处理。
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
    glBindFramebuffer(GL_FRAMEBUFFER, rasterFbo);
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
    const QMatrix4x4 view = Scene::getInstance().camera.getViewMatrix();
    // 路径追踪把视场角烘进光线方向，光栅化必须自己做透视投影。
    const float fov = float(qBound(1.0, double(Scene::getInstance().camera.zoom), 179.0));
    const float aspect = render_height > 0 ? float(render_width) / float(render_height) : 1.0f;
    QMatrix4x4 projection;
    projection.perspective(fov, aspect, 0.01f, 1.0e6f);
    raster_program->setUniformValue("projection", projection);
    raster_program->setUniformValue("view", view);
    raster_program->setUniformValue("eye", Scene::getInstance().camera.position);
    raster_program->setUniformValue("nLights", int(Scene::getInstance().lights_encoded.size()));
    raster_program->setUniformValue(
        "nAnalyticLights",
        std::min(Scene::getInstance().document.root["lights"].toArray().size(),
                 int(Scene::getInstance().lights_encoded.size())));

    raster_program->setUniformValue("lights", 4);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_BUFFER, lightsTextureBuffer);

    raster_program->setUniformValue("materialTextures", 5);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, materialTextureArray);

    raster_program->setUniformValue("materialTextureInfo", 6);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_BUFFER, materialTextureInfoTexture);

    raster_program->setUniformValue("materialTextureCount", materialTextureLayerCount);
    raster_program->setUniformValue("materialTable", 8);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_BUFFER, instanceTextures[1]);

    // 环境背景与环境项：开关由 USEENVIRONMENTMAP 决定，强度/旋转跟随文档设置。
    const auto environment = Scene::getInstance().document.root["environment"].toObject();
    raster_program->setUniformValue("environmentIntensity", float(environment["intensity"].toDouble(1)));
    raster_program->setUniformValue("environmentRotation",
                                    float(environment["rotation"].toDouble() * PI / 180));
    raster_program->setUniformValue("hdrResolution", Scene::getInstance().hdrResolution);
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
    const auto &scene = Scene::getInstance();
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
    const auto &scene = Scene::getInstance();
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
    const auto &s = Scene::getInstance();
    quint64 bytes = textureArrayBytes + quint64(m_width) * m_height * 16 +
                    quint64(render_width) * render_height * 5 * 16 +
                    quint64(denoisePboSize.width()) * denoisePboSize.height() * 3 * 12 +
                    quint64(pickSize.width()) * pickSize.height() * 8;
    bytes += s.geometryData.size() * sizeof(QVector4D) + s.nodes_encoded.size() * sizeof(BVHNode_encoded) +
             s.instanceData.size() * sizeof(QVector4D) + s.materialData.size() * sizeof(QVector4D) +
             s.tlasData.size() * sizeof(BVHNode_encoded) + s.surfaces.size() * sizeof(SurfaceReference) +
             s.surfacePdfs.size() * sizeof(float) + s.lights_encoded.size() * sizeof(Light_encoded) +
             std::max(1, materialTextureLayerCount) * 3 * sizeof(QVector4D) +
             quint64(s.hdrRes.width) * s.hdrRes.height * 24;
    if (oidnColorBuf)
        bytes += oidnColorBuf.getSize() + oidnAlbedoBuf.getSize() + oidnNormalBuf.getSize() +
                 oidnOutputBuf.getSize();
    bytes += previewDenoiser.allocatedBytes();
    bytes += quint64(rasterVertexCount) * 8 * sizeof(float) +
             quint64(rasterInstanceCount) * 5 * sizeof(QVector4D) +
             quint64(rasterDepthSize.width()) * rasterDepthSize.height() * 4;
    return bytes;
}
