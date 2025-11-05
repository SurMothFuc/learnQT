#ifndef RENDERER_H
#define RENDERER_H

//#include "sierpinski.h"

#include <QObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QImage>
#include <memory>
#include <QDebug>
#include <QElapsedTimer>
#include <QtMath>
#include <QMatrix4x4>
#include <iostream>
#include <qmutex.h>
#include "Scene.h"
#include <Eigen/Dense>
#include <unordered_map>
#include "OpenImageDenoise/oidn.hpp"

class Renderer : public QObject, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit Renderer(int width, int height, QObject* parent = nullptr);
    ~Renderer();

    void render(int width, int height);
    QOpenGLShaderProgram *getShaderProgram(std::string fshader, std::string vshader, const std::unordered_map<std::string, std::string> &defines_Vertex = {}, const std::unordered_map<std::string, std::string> &defines_Fragment = {});
    GLuint getTextureRGB32F(int width, int height);
    GLuint bindData(std::vector<GLuint> colorAttachments);
    GLuint VBO = 0, VAO = 0, EBO = 0;
    void updateparam();

    bool needupdate = true;
    bool renderLow = false;
    bool denoise = false;
    bool updateDenoise = true;
    bool useTileRendering = true; // 是否使用分块渲染
    bool renderComplete = false; // 渲染是否完成一轮
    
    // 切换渲染模式
    void setTileRendering(bool enable);

private:
    void init(int width, int height);
    void initOIDN();
    void uninit();
    void updateOIDNBuffers();
    void adjustSize();
    void updateSizeParam();
    void calResolution();
    void renderTile(int tileX, int tileY, int tileWidth, int tileHeight); // 渲染单个块
    void renderFullImage(); // 渲染完整图像

    /**
     * @brief 设置屏幕分辨率 并更新缓冲
     * 
     * @param width 
     * @param height 
     */
    void adjustScreenResolution(int width, int height);
    
    /**
     * @brief 更新渲染参数
     */
    void updateRenderParameters();
    
    /**
     * @brief 显示渲染统计信息
     */
    void displayRenderingStats();
    
    /**
     * @brief 执行渲染通道
     */
    void executeRenderPass();
    
    /**
     * @brief 处理历史帧保存
     */
    void processHistorySaving();
    
    /**
     * @brief 执行降噪处理
     */
    void performDenoising();
    
    /**
     * @brief 合成到屏幕
     */
    void compositeToScreen();
    
    /**
     * @brief 更新分块渲染状态
     */
    void updateTileRenderingState();

private://静止赋值操作
    Renderer(const Renderer&) = delete;
    Renderer& operator =(const Renderer&) = delete;
    Renderer(const Renderer&&) = delete;
    Renderer& operator =(const Renderer&&) = delete;

private:

    int m_width = 0;  //屏幕宽度
    int m_height = 0; //屏幕高度
    int render_width = 0; //实际渲染宽度
    int render_height = 0; //实际渲染高度
    int m_viewportX = 0;
    int m_viewportY = 0;
    bool m_sizeChanged = true;
    
    // 分块渲染相关参数
    int tileSize = 240; // 块的大小
    int currentTileX = 0; // 当前渲染块的X坐标
    int currentTileY = 0; // 当前渲染块的Y坐标
    int tilesX = 0; // X方向的块数
    int tilesY = 0; // Y方向的块数


    unsigned m_fbo = 0;
    unsigned pathtrace_fbo = 0;

   // unsigned mixframe_fbo_ping = 0;
    //unsigned mixframe_fbo_pong = 0;

   // unsigned directLight_fbo_filtered = 0;
   // unsigned indirectLight_fbo_filtered = 0;

    unsigned historysave_fbo = 0;

    //unsigned m_rbo = 0;
    unsigned m_texture = 0; 
    //unsigned pathtrace_texture = 0;
    //unsigned mixframe_texture = 0;

    unsigned preRenderColorTex = 0;

    unsigned RenderColorTex = 0;
    unsigned normal_texture = 0;
    unsigned baseColorTex = 0;
    unsigned RenderColorTexfiltered = 0;

    unsigned int frameCounter = 0;//累计渲染帧数
    unsigned int lastframeCounter = 0;


    int lasttime = 0;
    bool first_render = true;

    GLuint tbo0 = 0;
    GLuint tbo1 = 0;
    GLuint trianglesTextureBuffer = 0;
    GLuint nodesTextureBuffer = 0;
    GLuint hdrMap = 0;
    GLuint hdrCache = 0;

    std::unique_ptr<QOpenGLShaderProgram> m_program = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> pathtrace_program = nullptr;
    //std::unique_ptr<QOpenGLShaderProgram> mixframe_program = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> historysave_program = nullptr;

    std::vector<unsigned> batchTextureSettings;

    //OIDN
    oidn::DeviceRef oidnDevice;
    oidn::FilterRef oidnMainFilter;
    oidn::FilterRef oidnAlbedoFilter;
    oidn::FilterRef oidnNormalFilter;
    oidn::BufferRef oidnColorBuf;
    oidn::BufferRef oidnAlbedoBuf;
    oidn::BufferRef oidnNormalBuf;
    oidn::BufferRef oidnOutputBuf;

    // PBO对象
    GLuint pboIds[3] = { 0, 0, 0 }; // 分别用于color, normal, albedo

    // std::unique_ptr<Sierpinski> m_sierpinski;
};

#endif // RENDERER_H
