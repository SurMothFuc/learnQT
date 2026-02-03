# 执行流程说明（learnQT）

## 1. 启动入口
- 入口：`src/main.cpp`
- 流程：
  1) 创建 `QApplication`
  2) 构造 `learnQT` 主窗口
  3) `show()` 展示窗口并进入事件循环 `a.exec()`

## 2. 主窗口初始化（UI 事件绑定）
- `learnQT::learnQT()`（`src/learnQT.cpp`）
  - `Scene::getInstance()` 触发场景构建与资源加载
  - `ui.setupUi(this)` 加载 UI（`resources/learnQT.ui`）
  - 绑定 UI 控件的信号/槽：
    - `SaveImageButton` 保存帧缓冲
    - `pushButton` 更新材质
    - 多组 slider / lineEdit -> 更新材质参数
    - `DeNoisecheckBox` -> 渲染线程降噪
    - `useEnvironmentMapcheckBox` -> 切换环境贴图

## 3. GLWidget 初始化与渲染线程创建
- `GLWidget::initializeGL()`（`src/glwidget.cpp`）
  - 创建简单的 screen quad shader（用于显示纹理）
  - 构建 VAO/VBO
  - 调用 `initRenderThread()`：
    - 创建 `QOffscreenSurface`
    - 以主线程 OpenGL context 为共享上下文创建渲染线程 context
    - 启动 `RenderThread`
    - 建立信号连接：
      - `RenderThread::imageReady` -> UI `update()`
      - UI -> 渲染线程消息 (`recMegFromMain`, `setDenoise`)

## 4. 渲染线程主循环
- `RenderThread::run()`（`src/renderthread.cpp`）
  1) 延迟 400ms 以等待 UI 初始化
  2) 在渲染线程上下文中 `makeCurrent()`
  3) `TextureBuffer::createTexture()` 分配共享纹理
  4) 构造 `Renderer`，并进入循环：
     - `Renderer::render(width,height)`
     - `TextureBuffer::updateTexture()` 将结果复制到共享纹理
     - 发出 `imageReady` 信号

## 5. Renderer 主渲染流程
- `Renderer::render()`（`src/renderer.cpp`）
  1) `updateRenderParameters()`
     - 读取 `RenderParams` 变化
     - 切换环境贴图 -> 重建 shader
     - 低分辨率/分块参数变化 -> 重新计算分辨率与 tile
     - 如果参数需要更新 -> `updateparam()`
  2) `adjustScreenResolution()`
     - 处理窗口尺寸变化，重建纹理/FBO
  3) `displayRenderingStats()`
     - 统计 FPS/分块渲染帧数
  4) `executeRenderPass()`
     - 选择完整渲染或分块渲染
     - 渲染到 `RenderColorTex` 等纹理
  5) `processHistorySaving()`
     - 将本轮渲染结果写入历史缓存纹理
  6) `performDenoising()`
     - 通过 OIDN 对渲染纹理降噪
  7) `compositeToScreen()`
     - 最终合成到屏幕纹理

## 6. Scene 初始化与数据准备
- `Scene::Scene()`（`src/Scene.cpp`）
  - 初始化相机位置与默认材质
  - 加载 OBJ 模型（`MeshLoader::readObj`）
  - 构建 BVH（`BuildBVH::buildBVHwithSAH`）
  - 编码三角形与 BVH 数据为 GPU-friendly 格式
  - 加载 HDR 环境贴图，并计算 importance sampling cache

## 7. Shader 管线与数据流
- shader 来源：`shaders/` 与 `shaders/include/*`
- `processIncludes()` 处理 `#include` 以拼接 shader
- `injectDefines()` 根据运行参数注入 `#define`（如 `USEENVIRONMENTMAP`）
- 主要 Pass：
  - `pathtrace.frag`：主路径追踪
  - `historysave.frag`：历史帧缓存
  - `triangle.frag` + `triangle.vert`：最终合成/屏幕绘制

## 8. 交互与事件驱动流程
- 键盘：
  - `GLWidget::keyPressEvent` / `keyReleaseEvent`
  - 更新 `Scene::camera.keys` 并触发渲染更新
- 鼠标：
  - 左键拖拽 -> `camera.processMouseMovement()`
  - 滚轮 -> `camera.processMouseScroll()`
- UI 控件：
  - 滑动条 -> 更新材质 / 渲染参数
  - 复选框 -> 切换环境贴图 / 降噪

## 9. 线程与同步
- `param_mutex`：保护 `Scene::camera` 与 `Renderer::updateparam()` 的数据一致性
- `RenderParams`：使用原子变量，线程安全读取
- `TextureBuffer`：内部 `QMutex` 保护纹理读写

## 10. 关键调用链总结

```
main
  -> learnQT::learnQT
    -> Scene::getInstance
    -> GLWidget::initializeGL
      -> RenderThread::run
        -> Renderer::render (循环)

UI 交互 -> learnQT slots -> RenderParams/Scene -> GLWidget::sendM -> RenderThread::recMegFromMain -> Renderer::updateparam
```
