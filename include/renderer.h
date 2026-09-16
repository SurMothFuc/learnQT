#ifndef RENDERER_H
#define RENDERER_H

#include <QElapsedTimer>
#include <QImage>
#include <QMatrix4x4>
#include <QObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QtMath>

#include <Eigen/Dense>

#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#include "OpenImageDenoise/oidn.hpp"

#include "PreviewDenoiser.h"
#include "RenderJob.h"
#include "RenderParams.h"
#include "Scene.h"
#include "SceneDirty.h"
#include "RasterEnvironment.h"
#include <atomic>

class Renderer : public QObject, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
  public:
    explicit Renderer(int width, int height, const RenderParams::Snapshot &initialSnapshot,
                      QObject *parent = nullptr);
    ~Renderer() override;

    void render(int width, int height, const RenderParams::Snapshot &snapshot, SceneDirtyFlags dirtyFlags,
                int maxTiles = 1, const std::function<bool()> &interrupted = {});
    // 交互回退：由渲染线程决定本帧是否用光栅化交互预览。
    void setRasterActive(bool active);
    bool rasterActive() const
    {
        return m_rasterActive;
    }
    QSize renderSize() const
    {
        return {render_width, render_height};
    }
    int samples() const
    {
        return int(frameCounter);
    }
    int completedTiles() const
    {
        return int(chunkedRenderingCount);
    }
    bool completeRound() const
    {
        return nowChunkedCount == 0 && frameCounter > 0;
    }
    bool formal = false;
    quint64 imageRevision() const
    {
        return m_imageRevision;
    }
    bool samplingActive(const RenderParams::Snapshot &snapshot) const;
    bool previewDenoising() const
    {
        return previewDenoiser.busy() || denoiseReadbackFence;
    }
    std::atomic_bool *cancel = nullptr;
    RenderStats stats;
    std::function<void()> denoising;
    QImage result(const RenderParams::Snapshot &snapshot);
    void finishDenoise(const RenderParams::Snapshot &snapshot);
    void prepareJob(QSize size, const RenderParams::Snapshot &snapshot, SceneDirtyFlags dirty = 0);
    // Wait for the preceding tile/frame with a 1 ms timeout so the caller can recheck shutdown.
    bool waitForGpuBoundary();
    void submitGpuBoundary();
    void pollGpuTimers();
    quint64 allocatedBytes() const;
    // 返回本次调用是否真的重绘了拾取缓冲；只有版本或尺寸变化才会重绘。
    bool updatePick(int width, int height, quint64 version);
    // 最近一次拾取 pass 的 GPU 执行耗时，用于区分拾取与光栅化预览的开销。
    double pickGpuMs() const
    {
        return stats.pickMs;
    }
    // 取走自上次调用以来新完成的拾取耗时；没有新结果时返回 0，避免重复累计同一帧。
    double takePickGpuMs()
    {
        const double completed = pickCompletedMs;
        pickCompletedMs = 0;
        return completed;
    }
    int pickPasses() const
    {
        return stats.pickPasses;
    }
    void requestPick(QPoint pixel, quint64 request);
    bool pollPick(quint64 &request, unsigned &id, quint64 &version);
    GLuint pickFramebuffer() const
    {
        return pickFbo;
    }
    GLuint displayFramebuffer() const
    {
        return m_fbo;
    }
    QOpenGLShaderProgram *getShaderProgram(
        std::string fshader, std::string vshader,
        const std::unordered_map<std::string, std::string> &defines_Vertex = {},
        const std::unordered_map<std::string, std::string> &defines_Fragment = {});
    GLuint getTextureRGB32F(int width, int height);
    GLuint bindData(std::vector<GLuint> colorAttachments);

  private:
    struct RefreshActions
    {
        bool rebuildShader = false;
        // 环境贴图开关变化时，光栅化预览程序也要跟着重建（USEENVIRONMENTMAP define）。
        bool refreshRasterProgram = false;
        bool resizeTargets = false;
        bool syncCameraUniforms = false;
        bool syncMaterialBuffer = false;
        bool syncSceneBuffers = false;
        bool resetAccumulation = false;
        bool refreshDenoisePolicy = false;
        bool refreshDisplay = false;
    };

    void init(int width, int height, const RenderParams::Snapshot &snapshot);
    void initOIDN();
    void uninit();
    void updateOIDNBuffers();
    void adjustSize();
    void updateSizeParam();
    void calResolution(bool renderLow);
    void updateTileGrid(int tileSize);
    void bindPathtraceInputs(int maxBounces);
    void compositePreview(const RenderParams::Snapshot &snapshot, bool changed, bool force);
    void renderTile(int tileX, int tileY, int tileWidth, int tileHeight, int maxBounces); // 渲染单个块
    void renderFullImage(int maxBounces);                                                 // 渲染完整图像
    void rebuildPathtraceProgram(const RenderParams::Snapshot &snapshot);

    /**
     * @brief 设置屏幕分辨率并更新缓冲
     *
     * @param width
     * @param height
     * @param renderLow
     */
    void adjustScreenResolution(int width, int height, bool renderLow);

    /**
     * @brief 在帧首统一决策本帧需要执行的刷新动作
     */
    RefreshActions resolveRefreshActions(int width, int height, const RenderParams::Snapshot &snapshot,
                                         SceneDirtyFlags dirtyFlags) const;

    /**
     * @brief 执行帧首已经决策好的刷新动作
     */
    void applyRefreshActions(int width, int height, const RenderParams::Snapshot &snapshot,
                             const RefreshActions &actions);
    void resetAccumulation();
    void clearTexture(GLuint texture);
    void syncCameraUniforms();
    void syncMaterialBuffer();
    void syncSceneBuffers();
    void uploadTriangleBuffer(bool recreateResources);
    void uploadNodeBuffer(bool recreateResources);
    void uploadLightBuffer(bool recreateResources);
    void uploadInstanceBuffers(bool topology);
    void bindInstanceBuffers(QOpenGLShaderProgram *program);
    GLuint instanceBuffers[5] = {}, instanceTextures[5] = {};
    QStringList uploadedMeshes;
    void uploadHdrTextures(bool recreateResources);
    void uploadMaterialTextures(bool recreateResources);

    // 光栅化交互预览：几何按 mesh 分组上传，实例参数按实例步进的属性缓冲提供。
    bool renderRasterPreview(const RenderParams::Snapshot &snapshot);
    void rebuildRasterProgram(const RenderParams::Snapshot &snapshot);
    void ensureDepthAttachment();
    void uploadRasterGeometry();
    void uploadRasterInstances();
    void releaseRasterResources();

    /**
     * @brief 显示渲染统计信息
     */
    void displayRenderingStats();

    /**
     * @brief 执行渲染通道
     */
    void executeRenderPass(const RenderParams::Snapshot &snapshot);

    /**
     * @brief 处理历史帧保存
     */
    void processHistorySaving(const RenderParams::Snapshot &snapshot);

    /**
     * @brief 执行降噪处理
     */
    void performDenoising(const RenderParams::Snapshot &snapshot, bool forceCurrentFrame = false);
    bool pollPreviewDenoise(const RenderParams::Snapshot &snapshot);
    void requestPreviewDenoise(const RenderParams::Snapshot &snapshot, bool force);
    void invalidatePreviewDenoise();
    void ensureDenoisePbos();

    /**
     * @brief 合成到屏幕
     */
    void compositeToScreen(const RenderParams::Snapshot &snapshot);

    /**
     * @brief 更新分块渲染状态
     */
    void updateTileRenderingState();

  private: // 禁止拷贝和移动
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    Renderer(const Renderer &&) = delete;
    Renderer &operator=(const Renderer &&) = delete;

  private:
    int m_width = 0;       // 屏幕宽度
    int m_height = 0;      // 屏幕高度
    int render_width = 0;  // 实际渲染宽度
    int render_height = 0; // 实际渲染高度
    int m_viewportX = 0;
    int m_viewportY = 0;

    int currentTileX = 0; // 当前渲染块的 X 坐标
    int currentTileY = 0; // 当前渲染块的 Y 坐标
    int tilesX = 0;       // X 方向的块数
    int tilesY = 0;       // Y 方向的块数

    unsigned m_fbo = 0;
    unsigned pathtrace_fbo = 0;
    unsigned historysave_fbo = 0;

    unsigned m_texture = 0;
    unsigned preRenderColorTex = 0;
    unsigned RenderColorTex = 0;
    unsigned normal_texture = 0;
    unsigned baseColorTex = 0;
    unsigned RenderColorTexfiltered = 0;

    unsigned int frameCounter = 0; // 累计渲染帧数
    unsigned int lastframeCounter = 0;
    unsigned int chunkedRenderingCount = 0; // 分块渲染帧次数
    unsigned int lastChunkedRenderingCount = 0;
    unsigned int nowChunkedCount = 0; // 当前渲染块数
    bool renderComplete = false;

    int lasttime = 0;

    GLuint tbo0 = 0;
    GLuint tbo1 = 0;
    GLuint tboLights = 0;
    GLuint trianglesTextureBuffer = 0;
    GLuint nodesTextureBuffer = 0;
    GLuint lightsTextureBuffer = 0;
    GLuint materialTextureArray = 0;
    GLuint materialTextureInfoBuffer = 0;
    GLuint materialTextureInfoTexture = 0;
    GLuint hdrMap = 0;
    GLuint hdrCache = 0;
    GLuint VBO = 0;
    GLuint VAO = 0;
    GLuint EBO = 0;
    int materialTextureLayerCount = 0;

    std::unique_ptr<QOpenGLShaderProgram> m_program = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> pathtrace_program = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> historysave_program = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> raster_program = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> rasterBackgroundProgram = nullptr;

    // 光栅化交互预览资源。区间按 mesh 分组，绘制时同 mesh 的实例合并为一次实例化绘制。
    struct RasterDrawRange
    {
        GLint first = 0;
        GLsizei count = 0;
    };
    GLuint rasterVao = 0, rasterVertexBuffer = 0, rasterInstanceBuffer = 0;
    // 光栅化预览专用 FBO：颜色靶复用 RenderColorTex，另带自己的深度附件。
    GLuint rasterFbo = 0;
    GLuint depthRenderbuffer = 0;
    QSize rasterDepthSize;
    std::vector<RasterDrawRange> rasterRanges;      // 下标与 Scene::meshes 对齐
    std::vector<int> rasterInstanceMesh;            // 每条实例属性对应的 mesh 下标
    GLsizei rasterInstanceCount = 0;
    size_t rasterVertexCount = 0;
    bool m_rasterActive = false, m_rasterRequested = false;
    bool m_rasterCapable = false;
    std::array<QVector3D, 9> rasterEnvironment{};
    const float *rasterEnvironmentSource = nullptr;
    QSize rasterEnvironmentSize;
    QString rasterEnvironmentPath;
    bool m_rasterGeometryUploaded = false, m_rasterInstancesUploaded = false;

    std::vector<unsigned> batchTextureSettings;

    oidn::DeviceRef oidnDevice;
    oidn::FilterRef oidnMainFilter;
    oidn::FilterRef oidnAlbedoFilter;
    oidn::FilterRef oidnNormalFilter;
    oidn::BufferRef oidnColorBuf;
    oidn::BufferRef oidnAlbedoBuf;
    oidn::BufferRef oidnNormalBuf;
    oidn::BufferRef oidnOutputBuf;

    // PBO 对象，分别用于 color / normal / albedo。
    GLuint pboIds[3] = {0, 0, 0};
    QSize denoisePboSize, oidnSize;
    PreviewDenoiser previewDenoiser;
    GLsync denoiseReadbackFence = nullptr;
    PreviewDenoiser::Snapshot previewSnapshot;
    QElapsedTimer previewDenoiseClock;
    quint64 m_imageRevision = 0;
    bool previewDenoiseFailed = false;
    bool previewHasGeometry = true;

    // 缓存上一帧已应用的快照，用于帧首差分判断。
    RenderParams::Snapshot m_lastAppliedSnapshot{};
    bool m_forceDenoiseRefresh = true;
    bool m_hasDenoisedFrame = false;
    bool targetsValid = true;
    GLuint timerQueries[12] = {};
    bool timerPending[12] = {};
    quint64 timerEpoch[12] = {};
    // 光栅化交互预览的 GPU 计时查询，单独一个槽位，不参与路径追踪的三段计时。
    GLuint rasterTimerQuery = 0;
    bool rasterTimerPending = false;
    // GPU 拾取 pass 的计时查询，同样独立于路径追踪与光栅化的计时槽位。
    GLuint pickTimerQuery = 0;
    bool pickTimerPending = false;
    // 最近一次查询完成时结算的耗时，由渲染线程取走后清零。
    double pickCompletedMs = 0;
    double estimatedTileMs = 0;
    QElapsedTimer compositeClock;
    bool displayDirty = true, firstComposite = true;
    // 光栅化预览每帧都需要重新合成到显示纹理，与路径追踪的 displayDirty 语义分开。
    bool rasterNeedsComposite = false;
    int timerCursor = 0;
    quint64 textureArrayBytes = 0;
    GLuint pickFbo = 0, pickTextures[2] = {}, pickPbo = 0;
    GLsync pickFence = nullptr;
    GLsync workFence = nullptr;
    QSize pickSize;
    quint64 pickVersion = ~quint64(0), pickRequest = 0, readVersion = 0;
    std::unique_ptr<QOpenGLShaderProgram> pickProgram;
    unsigned int m_lastDenoisedFrameCounter = 0;
};

#endif // RENDERER_H
