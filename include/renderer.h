#ifndef RENDERER_H
#define RENDERER_H

#include <QObject>
#include <QElapsedTimer>
#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QtMath>

#include <Eigen/Dense>

#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#include "OpenImageDenoise/oidn.hpp"

#include "RenderParams.h"
#include "Scene.h"
#include "SceneDirty.h"

class Renderer : public QObject, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit Renderer(int width, int height, const RenderParams::Snapshot& initialSnapshot, QObject* parent = nullptr);
    ~Renderer() override;

    void render(int width, int height, const RenderParams::Snapshot& snapshot, SceneDirtyFlags dirtyFlags);
    QOpenGLShaderProgram *getShaderProgram(
        std::string fshader,
        std::string vshader,
        const std::unordered_map<std::string, std::string> &defines_Vertex = {},
        const std::unordered_map<std::string, std::string> &defines_Fragment = {});
    GLuint getTextureRGB32F(int width, int height);
    GLuint bindData(std::vector<GLuint> colorAttachments);

private:
    struct RefreshActions {
        bool rebuildShader = false;
        bool resizeTargets = false;
        bool syncCameraUniforms = false;
        bool syncMaterialBuffer = false;
        bool syncSceneBuffers = false;
        bool resetAccumulation = false;
        bool refreshDenoisePolicy = false;
    };

    void init(int width, int height, const RenderParams::Snapshot& snapshot);
    void initOIDN();
    void uninit();
    void updateOIDNBuffers();
    void adjustSize();
    void updateSizeParam();
    void calResolution(bool renderLow);
    void updateTileGrid(int tileSize);
    void renderTile(int tileX, int tileY, int tileWidth, int tileHeight, int maxBounces); // 渲染单个块
    void renderFullImage(int maxBounces); // 渲染完整图像
    void rebuildPathtraceProgram(const RenderParams::Snapshot& snapshot);

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
    RefreshActions resolveRefreshActions(int width, int height, const RenderParams::Snapshot& snapshot, SceneDirtyFlags dirtyFlags) const;

    /**
     * @brief 执行帧首已经决策好的刷新动作
     */
    void applyRefreshActions(int width, int height, const RenderParams::Snapshot& snapshot, const RefreshActions& actions);
    void resetAccumulation();
    void clearTexture(GLuint texture);
    void syncCameraUniforms();
    void syncMaterialBuffer();
    void syncSceneBuffers();
    void uploadTriangleBuffer(bool recreateResources);
    void uploadNodeBuffer(bool recreateResources);
    void uploadLightBuffer(bool recreateResources);
    void uploadHdrTextures(bool recreateResources);
    void uploadMaterialTextures(bool recreateResources);

    /**
     * @brief 显示渲染统计信息
     */
    void displayRenderingStats();

    /**
     * @brief 执行渲染通道
     */
    void executeRenderPass(const RenderParams::Snapshot& snapshot);

    /**
     * @brief 处理历史帧保存
     */
    void processHistorySaving(const RenderParams::Snapshot& snapshot);

    /**
     * @brief 执行降噪处理
     */
    void performDenoising(const RenderParams::Snapshot& snapshot, bool forceCurrentFrame = false);

    /**
     * @brief 合成到屏幕
     */
    void compositeToScreen(const RenderParams::Snapshot& snapshot);

    /**
     * @brief 更新分块渲染状态
     */
    void updateTileRenderingState();

private: // 禁止拷贝和移动
    Renderer(const Renderer&) = delete;
    Renderer& operator =(const Renderer&) = delete;
    Renderer(const Renderer&&) = delete;
    Renderer& operator =(const Renderer&&) = delete;

private:
    int m_width = 0;  // 屏幕宽度
    int m_height = 0; // 屏幕高度
    int render_width = 0;  // 实际渲染宽度
    int render_height = 0; // 实际渲染高度
    int m_viewportX = 0;
    int m_viewportY = 0;

    int currentTileX = 0; // 当前渲染块的 X 坐标
    int currentTileY = 0; // 当前渲染块的 Y 坐标
    int tilesX = 0; // X 方向的块数
    int tilesY = 0; // Y 方向的块数

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
    GLuint pboIds[3] = { 0, 0, 0 };

    // 缓存上一帧已应用的快照，用于帧首差分判断。
    RenderParams::Snapshot m_lastAppliedSnapshot{};
    bool m_forceDenoiseRefresh = true;
    bool m_hasDenoisedFrame = false;
    unsigned int m_lastDenoisedFrameCounter = 0;
};

#endif // RENDERER_H
