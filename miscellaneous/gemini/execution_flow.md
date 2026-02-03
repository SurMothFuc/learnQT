# 执行流程说明

## 1. 启动流程

1.  **入口点 (`src/main.cpp`)**:
    *   初始化 `QApplication`。
    *   创建 `learnQT` 主窗口实例。
    *   调用 `w.show()` 显示窗口。
    *   进入 Qt 事件循环 `a.exec()`。

2.  **主窗口初始化 (`src/learnQT.cpp`)**:
    *   构造函数中调用 `Scene::getInstance()` 初始化单例场景。
    *   设置 UI (`ui.setupUi`)。
    *   连接信号与槽（如材质滑动条 -> `updateMaterial`）。

3.  **OpenGL 初始化 (`src/glwidget.cpp`)**:
    *   `GLWidget` 被创建（作为主窗口的一部分）。
    *   `initializeGL()` 被 Qt 自动调用：
        *   初始化 OpenGL 函数 (`initializeOpenGLFunctions`).
        *   编译基础显示着色器 (`m_program`).
        *   创建全屏四边形 VAO/VBO。
        *   调用 `initRenderThread()` 启动渲染线程。

4.  **渲染线程启动 (`src/renderthread.cpp`)**:
    *   `RenderThread` 构造时创建一个与主线程共享的 OpenGL 上下文 (`m_renderContext`)。
    *   `run()` 方法开始执行：
        *   `m_renderContext->makeCurrent(m_surface)`：在当前线程激活 OpenGL 上下文。
        *   创建 `Renderer` 实例。
        *   进入 `while(m_running)` 渲染循环。

## 2. 渲染循环 (Core Render Loop)

渲染循环在 `RenderThread::run()` 中持续运行：

1.  **参数同步**:
    *   获取最新的渲染分辨率 (`width`, `height`)。
    *   `Renderer::render(width, height)` 被调用。

2.  **渲染过程 (`src/renderer.cpp`)**:
    *   **参数更新 (`updateparam`)**:
        *   将场景数据（BVH节点、三角形数据、HDR贴图）通过 Texture Buffer Object (TBO) 上传至 GPU。
        *   更新相机矩阵 Uniforms。
    *   **路径追踪 (`pathtrace.frag`)**:
        *   绑定 `pathtrace_fbo`。
        *   执行 Fragment Shader，进行路径追踪计算。
        *   输出：`RenderColor` (当前帧颜色), `Normal` (法线), `BaseColor` (基础色)。
        *   **帧累积**: 利用混合模式或着色器逻辑 (`mix`) 将当前帧与历史帧混合，随着帧数增加降低噪点。
    *   **降噪 (Optional)**:
        *   如果开启 OIDN (`denoise` 为 true)，将 OpenGL 纹理数据读回 CPU (或通过 CUDA/互操作性)。
        *   调用 Intel OIDN 库对图像进行降噪处理。
        *   将降噪后的结果写回 OpenGL 纹理。
    *   **纹理更新**:
        *   `TextureBuffer::updateTexture` 将最终结果纹理 ID 传递给主线程可访问的容器。
        *   发送 `imageReady` 信号。

3.  **屏幕绘制 (`src/glwidget.cpp`)**:
    *   主线程收到 `imageReady` 信号。
    *   `GLWidget::paintGL()` 被调用。
    *   绑定包含渲染结果的纹理。
    *   绘制全屏四边形，将纹理显示在屏幕上。

## 3. 核心算法流程

### 3.1 路径追踪 (`shaders/include/pathtrace.glsl`)
*   **射线生成**: 根据像素坐标和相机逆矩阵生成相机射线。
*   **BVH 求交 (`hitBVH`)**: 在 GLSL 中遍历纹理化的 BVH 结构，查找最近交点。
*   **材质评估**:
    *   使用 Disney BSDF 模型评估材质属性（BaseColor, Metallic, Roughness, etc.）。
    *   支持多层材质（Clearcoat, Sheen, Transmission）。
*   **采样策略**:
    *   **重要性采样**: 根据 BSDF 分布采样出射方向。
    *   **环境光采样**: 对 HDR 环境贴图进行重要性采样。
    *   **MIS (Multiple Importance Sampling)**: 结合 BSDF 采样和光源采样的权重，优化收敛速度。
*   **递归**: 追踪反弹光线，直到达到最大深度 (`maxBounce`) 或被吸收。

### 3.2 BVH 构建 (`src/BVH.cpp`)
*   采用 **SAH (Surface Area Heuristic)** 启发式算法。
*   递归地将三角形集合划分为左右子节点，最小化表面积代价函数。
*   最终生成的树结构被扁平化并编码为纹理数据传输给 GPU。
