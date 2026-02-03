# 技术栈与依赖说明

## 1. 核心技术栈

| 类别 | 技术/库 | 版本/说明 |
| :--- | :--- | :--- |
| **编程语言** | C++ | C++14 Standard |
| **构建系统** | CMake | Minimum 3.10 |
| **GUI 框架** | Qt | Version 5.x (Widgets, Gui, Core, OpenGL) |
| **图形 API** | OpenGL | Core Profile 3.3+ |
| **着色器语言** | GLSL | Version 330 core |
| **并行计算** | Multithreading | Qt QThread (GUI/Render 分离) |

## 2. 第三方依赖库

| 库名称 | 用途 | 路径 | 备注 |
| :--- | :--- | :--- | :--- |
| **Assimp** | 3D 模型导入 | `libs/Assimp` | 版本 6.0，支持 OBJ, FBX, GLTF 等格式 |
| **Eigen** | 线性代数运算 | `libs/Eigen` | 只有头文件，用于矩阵和向量计算 |
| **OpenImageDenoise (OIDN)** | AI 降噪 | `libs/oidn` | Intel 开发的基于深度学习的图像降噪库，版本 2.3.3 |

## 3. 开发环境要求

*   **操作系统**: Windows (推荐), Linux, macOS (需适配路径)
*   **编译器**: MSVC (Visual Studio 2017/2019/2022) 或 GCC/Clang
*   **Qt 环境**: 安装 Qt5 并配置 `Qt5_DIR` 环境变量
*   **硬件要求**: 支持 OpenGL 3.3+ 的显卡

## 4. 关键类与文件对应关系

| 类名 | 文件 | 描述 |
| :--- | :--- | :--- |
| `learnQT` | `learnQT.h/cpp` | 主窗口类，继承自 `QMainWindow` |
| `GLWidget` | `glwidget.h/cpp` | OpenGL 显示窗口，继承自 `QOpenGLWidget` |
| `Renderer` | `renderer.h/cpp` | 渲染器逻辑，管理 OpenGL 资源和渲染管线 |
| `RenderThread` | `renderthread.h/cpp` | 渲染线程，继承自 `QThread` |
| `Scene` | `Scene.h/cpp` | 场景管理器（单例），存储网格和 BVH |
| `BVH` | `BVH.h/cpp` | Bounding Volume Hierarchy 加速结构构建 |
| `HDRLoader` | `hdrloader.h/cpp` | HDR 图像加载器 |
