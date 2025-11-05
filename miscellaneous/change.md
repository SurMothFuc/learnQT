在当前项目中，Renderer 和 Scene 等类各自维护了一些运行时参数，其中部分参数需要在多个线程间共享。目前这些共享参数被分散存储在不同文件中，存在重复定义和同步困难的问题。我希望设计一个通用的全局参数管理类，采用单例模式实现，并通过互斥锁（mutex）或原子操作保证线程安全访问，从而确保所有线程都能获取到一致的参数状态。


好的。为解决参数在不同线程与不同模块（Renderer、Scene 等）之间重复维护、同步不一致的问题，我建议引入一个“全局参数中心”单例类，集中管理跨线程共享的运行时参数，并提供统一的加锁接口来保护对 Scene 的修改。这样可以做到：

- 全局唯一：所有线程都从同一处读写参数
- 线程安全：通过内部锁或原子变量管理并发
- 降低耦合：UI 线程不再直接操作 Renderer 的成员，Renderer 统一从全局中心拉取最新参数
- 兼容现有架构：保留 Scene 单例，Geometry/Camera 仍在 Scene 中；全局中心只管理“运行时开关”和“更新事件”

我将添加一个 GlobalParams 单例类，并最小化改动现有代码，让它接入全局中心。

为什么需要修改或创建代码
- 创建 GlobalParams 单例类，集中管理 renderLow、denoise、tileRendering、场景更新标志，并提供对 Scene 修改的统一互斥锁。
- 替换现有的全局 QMutex param_mutex，改为 GlobalParams 的场景锁（sceneMutex），避免到处散落的 extern。
- RenderThread 不再直接改 Renderer 的标志位，而是写入 GlobalParams；Renderer 在每帧从 GlobalParams 拉取最新值，并消费更新事件。
- 保留 Scene 作为数据源，只有对 Scene 的修改需要通过 GlobalParams 的锁保护。

下面是具体代码改动与新增。

```cpp
// ... existing code ...
#pragma once
#include <QMutex>
#include <atomic>

/**
 * 全局参数中心（单例）
 * - 管理跨线程共享的运行时参数（如 renderLow、denoise、tileRendering）
 * - 提供对 Scene 修改的统一互斥锁（sceneMutex）
 * - 提供场景参数更新事件（notify / consume）
 */
class GlobalParams {
public:
    static GlobalParams& getInstance() {
        static GlobalParams instance;
        return instance;
    }

    // 运行时开关（线程安全）
    void setRenderLow(bool v) {
        QMutexLocker locker(&m_paramMutex);
        m_renderLow = v;
    }
    bool getRenderLow() {
        QMutexLocker locker(&m_paramMutex);
        return m_renderLow;
    }

    void setDenoise(bool v) {
        QMutexLocker locker(&m_paramMutex);
        m_denoise = v;
    }
    bool getDenoise() {
        QMutexLocker locker(&m_paramMutex);
        return m_denoise;
    }

    void setTileRendering(bool v) {
        QMutexLocker locker(&m_paramMutex);
        m_tileRendering = v;
    }
    bool getTileRendering() {
        QMutexLocker locker(&m_paramMutex);
        return m_tileRendering;
    }

    // 场景更新事件（UI线程通知，渲染线程消费）
    void notifySceneUpdated() {
        m_sceneUpdated.store(true, std::memory_order_release);
    }
    bool consumeSceneUpdated() {
        // 仅消费一次
        bool expected = true;
        return m_sceneUpdated.compare_exchange_strong(expected, false, std::memory_order_acq_rel);
    }

    // 对 Scene 的统一互斥锁（用于修改 Scene 数据/相机）
    QMutex& sceneMutex() { return m_sceneMutex; }

private:
    GlobalParams() = default;
    ~GlobalParams() = default;
    GlobalParams(const GlobalParams&) = delete;
    GlobalParams& operator=(const GlobalParams&) = delete;

private:
    QMutex m_paramMutex;
    QMutex m_sceneMutex;

    std::atomic<bool> m_sceneUpdated{false};

    bool m_renderLow = false;
    bool m_denoise = false;
    bool m_tileRendering = true;
}
// ... existing code ...
```

```cpp
// ... existing code ...
#include "globalparams.h"

// 当前实现全部在头文件内（内联 + 静态局部变量单例）
// 如需后续扩展复杂逻辑，可将方法主体迁移到此 cpp。
// ... existing code ...
```

```cpp|CODE_EDIT_BLOCK|d:\program\learnQT\include\glwidget.h
// ... existing code ...
#ifndef GLWIDGET_H
#define GLWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>

#include <QMutex>
#include <memory>
#include <qtimer.h>
#include "Scene.h"

// Deleted:extern QMutex param_mutex;

class RenderThread;

class GLWidget : public QOpenGLWidget,protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    GLWidget(QWidget *parent = nullptr);
    ~GLWidget() override;
    void sendM();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void keyPressEvent(QKeyEvent* event) Q_DECL_OVERRIDE;
    void keyReleaseEvent(QKeyEvent* event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent* event) Q_DECL_OVERRIDE;
    void mouseReleaseEvent(QMouseEvent* event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent* event) Q_DECL_OVERRIDE;
    void wheelEvent(QWheelEvent* event) Q_DECL_OVERRIDE;

private:
    void initRenderThread();

private:
    unsigned m_vao = 0;
    unsigned m_vbo = 0;
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    RenderThread *m_thread = nullptr;

    bool m_bLeftPressed;
    QPoint m_lastPos;
signals:
    //给子线程发消息
    void sengMsgToThread();
    void sendSetDenoise(bool isChecked);
};

#endif // GLWIDGET_H
// ... existing code ...
```

```cpp|CODE_EDIT_BLOCK|d:\program\learnQT\src\glwidget.cpp
// ... existing code ...
#include "glwidget.h"
#include "texturebuffer.h"
#include "renderthread.h"
//#include "debug.h"
//#include "fpscounter.h"

#include <QOpenGLContext>
#include <QDebug>
#include <QWindow>
#include <QSurface>

#include <iostream>
#include "globalparams.h"

// Deleted:QMutex param_mutex;

// ... existing code ...
void GLWidget::sendM()
{
    // 发送给渲染线程，并标记场景已更新
    emit sengMsgToThread();
    GlobalParams::getInstance().notifySceneUpdated();
}
// ... existing code ...
void GLWidget::keyPressEvent(QKeyEvent* event)
{
    int key = event->key();
    if (key >= 0 && key < 1024) {
        auto& mutex = GlobalParams::getInstance().sceneMutex();
        mutex.lock();
        {
            Scene::getInstance().camera.keys[key] = true;
            Scene::getInstance().camera.processInput(1.0f);
        }
        mutex.unlock();
        sendM();
    }
}
// ... existing code ...
void GLWidget::keyReleaseEvent(QKeyEvent* event)
{
    int key = event->key();
    if (key >= 0 && key < 1024) {
        auto& mutex = GlobalParams::getInstance().sceneMutex();
        mutex.lock();
        {
            Scene::getInstance().camera.keys[key] = false;
        }
        mutex.unlock();
    }
}
// ... existing code ...
void GLWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_bLeftPressed) {
        int xpos = event->pos().x();
        int ypos = event->pos().y();

        int xoffset = xpos - m_lastPos.x();
        int yoffset = m_lastPos.y() - ypos;
        m_lastPos = event->pos();
        auto& mutex = GlobalParams::getInstance().sceneMutex();
        mutex.lock();
       {
            Scene::getInstance().camera.processMouseMovement(xoffset, yoffset);
            //qDebug() << param.camera.yaw << "    " << param.camera.picth << "    " ;
       }
       mutex.unlock();
       sendM();
    }
}
// ... existing code ...
void GLWidget::wheelEvent(QWheelEvent* event)
{
    QPoint offset = event->angleDelta();
    auto& mutex = GlobalParams::getInstance().sceneMutex();
    mutex.lock();
    {
        Scene::getInstance().camera.processMouseScroll(offset.y());
    }
    mutex.unlock();
    sendM();
}
// ... existing code ...
```

```cpp|CODE_EDIT_BLOCK|d:\program\learnQT\src\renderthread.cpp
// ... existing code ...
#include "renderthread.h"
#include "globalparams.h"

// ... existing code ...
void RenderThread::setRenderLow(bool _renderlow)
{
    GlobalParams::getInstance().setRenderLow(_renderlow);
}
// ... existing code ...
void RenderThread::setDenoise(bool _isdenoise)
{
    GlobalParams::getInstance().setDenoise(_isdenoise);
    if (point_render == NULL)
        return;
    point_render->updateDenoise = true;//保证降噪一次
}
// ... existing code ...
void RenderThread::recMegFromMain()
{
    // 标记场景数据已更新（Renderer 将在下一帧消费）
    GlobalParams::getInstance().notifySceneUpdated();
    if (point_render == NULL)
        return;
    point_render->needupdate = true;
}
// ... existing code ...
```

```cpp|CODE_EDIT_BLOCK|d:\program\learnQT\src\renderer.cpp
// ... existing code ...
#include "renderer.h"
#include <QFile>
#include <QTextStream>
#include <regex>
#include <QFileInfo>
#include <QDir>

#include "globalparams.h"

// Deleted:extern QMutex param_mutex;
// ... existing code ...
void Renderer::updateparam()
{
    auto& sceneMutex = GlobalParams::getInstance().sceneMutex();
    sceneMutex.lock();
    {
        //绑定三角形到texture

        //更新的时候要把旧的纹理缓冲删除，不然会导致显存泄漏
        glDeleteBuffers(1, &tbo0);
        glDeleteTextures(1, &trianglesTextureBuffer);
        glDeleteBuffers(1, &tbo1);
        glDeleteTextures(1, &nodesTextureBuffer);
        glDeleteTextures(1, &hdrMap);
        glDeleteTextures(1, &hdrCache);

        glGenBuffers(1, &tbo0);
        glBindBuffer(GL_TEXTURE_BUFFER, tbo0);
        glBufferData(GL_TEXTURE_BUFFER, Scene::getInstance().triangles_encoded.size() * sizeof(Triangle_encoded), &Scene::getInstance().triangles_encoded[0], GL_STATIC_DRAW);
        glGenTextures(1, &trianglesTextureBuffer);
        glBindTexture(GL_TEXTURE_BUFFER, trianglesTextureBuffer);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, tbo0);

        glGenBuffers(1, &tbo1);
        glBindBuffer(GL_TEXTURE_BUFFER, tbo1);
        glBufferData(GL_TEXTURE_BUFFER, Scene::getInstance().nodes_encoded.size() * sizeof(BVHNode_encoded), &Scene::getInstance().nodes_encoded[0], GL_STATIC_DRAW);
        glGenTextures(1, &nodesTextureBuffer);
        glBindTexture(GL_TEXTURE_BUFFER, nodesTextureBuffer);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32F, tbo1);

        hdrMap = getTextureRGB32F(Scene::getInstance().hdrRes.width, Scene::getInstance().hdrRes.height);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, Scene::getInstance().hdrRes.width, Scene::getInstance().hdrRes.height, 0, GL_RGB, GL_FLOAT, Scene::getInstance().hdrRes.cols);

        hdrCache = getTextureRGB32F(Scene::getInstance().hdrRes.width, Scene::getInstance().hdrRes.height);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, Scene::getInstance().hdrRes.width, Scene::getInstance().hdrRes.height, 0, GL_RGB, GL_FLOAT, Scene::getInstance().cache);

        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        pathtrace_program->bind();
        QMatrix4x4 view = Scene::getInstance().camera.getViewMatrix();
        float viewStore[16];
        view.copyDataTo(viewStore);
        Eigen::Matrix4f viewM(viewStore);
        viewM = Eigen::Matrix4f(viewM.inverse());//因为是要求光线的方向，所以求逆矩阵
        float* newViewStore = viewM.data();
        view = QMatrix4x4(newViewStore);
        pathtrace_program->setUniformValue("view", view);
        pathtrace_program->setUniformValue("eye", Scene::getInstance().camera.position);
        pathtrace_program->setUniformValue("nTriangles", (int)Scene::getInstance().triangles.size());
        pathtrace_program->setUniformValue("nNodes", (int)Scene::getInstance().nodes_encoded.size());
        pathtrace_program->setUniformValue("width", render_width);
        pathtrace_program->setUniformValue("height", render_height);
        pathtrace_program->setUniformValue("hdrResolution", Scene::getInstance().hdrResolution);
        pathtrace_program->release();

        lasttime = clock();
        lastframeCounter = 0;
        frameCounter = 0;

        first_render = true;

        // 重置分块渲染状态
        currentTileX = 0;
        currentTileY = 0;
        renderComplete = false;
    }
    sceneMutex.unlock();
}
// ... existing code ...
void Renderer::updateRenderParameters()
{
    // 从全局中心拉取运行时开关
    renderLow = GlobalParams::getInstance().getRenderLow();
    denoise = GlobalParams::getInstance().getDenoise();
    setTileRendering(GlobalParams::getInstance().getTileRendering());

    // 消费一次场景更新事件（或保留现有 needupdate 兼容）
    if (GlobalParams::getInstance().consumeSceneUpdated()) {
        needupdate = true;
    }

    if (needupdate) {
        updateparam();
        needupdate = false;
    }
}
// ... existing code ...
```


使用方式和说明
- UI线程修改相机或场景数据时，务必通过 GlobalParams::getInstance().sceneMutex() 加锁（已在 glwidget.cpp 中替换完成）。
- UI线程修改渲染开关（降噪、低分辨率、分块渲染）时，调用 GlobalParams 的 setXXX 方法（已在 renderthread.cpp 中替换完成）。
- UI线程在参数变更后，调用 sendM() 或直接调用 GlobalParams::notifySceneUpdated() 通知渲染线程有参数更新。渲染线程每帧会 consume 一次更新事件并执行 updateparam。
- Renderer 每帧从 GlobalParams 拉取开关值，并消费更新事件，保持统一的全局状态。
- 现有 Scene 单例保留为几何/纹理数据源，不迁移数据结构，改动最小且风险低。

这样设计后，全局参数统一由 GlobalParams 管理，跨线程访问有锁保护，消除了散落的 extern QMutex 和模块间直接写入对方成员的做法，保证了线程安全与参数一致性。是否需要把更多参数（比如 tileSize）也纳入全局中心，可以按需追加 getter/setter。