# 项目结构分析

## 1. 目录结构概览

项目采用标准的 C++/CMake 项目结构，结合 Qt 框架规范。

```
learnQT/
├── CMakeLists.txt          # 项目构建配置文件，定义依赖和编译选项
├── src/                    # C++ 源代码目录
│   ├── main.cpp            # 程序入口点
│   ├── learnQT.cpp         # 主窗口 GUI 逻辑
│   ├── glwidget.cpp        # OpenGL 显示控件
│   ├── renderer.cpp        # 核心渲染器实现
│   ├── renderthread.cpp    # 渲染线程管理
│   ├── Scene.cpp           # 场景数据管理
│   ├── BVH.cpp             # 层次包围盒加速结构构建
│   ├── Mesh.cpp            # 网格数据处理
│   ├── Camera.cpp          # 相机控制
│   └── ...
├── include/                # C++ 头文件目录
│   ├── learnQT.h           # 主窗口类定义
│   ├── renderer.h          # 渲染器类定义
│   ├── Scene.h             # 场景类定义
│   ├── BVH.h               # BVH 结构定义
│   └── ...
├── shaders/                # GLSL 着色器代码
│   ├── pathtrace.frag      # 核心路径追踪片段着色器
│   ├── triangle.vert       # 基础顶点着色器
│   └── include/            # 着色器公共库
│       ├── bsdf.glsl       # 材质模型 (BSDF)
│       ├── pathtrace.glsl  # 路径追踪算法
│       └── ...
├── resources/              # 项目资源
│   ├── models/             # 3D 模型文件 (.obj)
│   ├── hdr/                # HDR 环境贴图
│   ├── learnQT.ui          # Qt Designer 界面文件
│   └── learnQT.qrc         # Qt 资源配置文件
├── libs/                   # 第三方依赖库
│   ├── Assimp/             # 3D 模型加载库
│   ├── Eigen/              # 线性代数数学库
│   └── oidn/               # Intel Open Image Denoise 降噪库
└── miscellaneous/          # 文档和杂项文件
```

## 2. 模块划分

项目主要分为以下几个核心模块：

### 2.1 GUI 交互模块
- **文件**: `src/learnQT.cpp`, `include/learnQT.h`, `resources/learnQT.ui`
- **职责**: 负责程序界面的显示、用户输入处理（材质参数调整、降噪开关等）、与渲染线程的通信。

### 2.2 OpenGL 显示模块
- **文件**: `src/glwidget.cpp`, `include/glwidget.h`
- **职责**: 继承自 `QOpenGLWidget`，负责 OpenGL 上下文的创建、管理以及最终渲染图像的屏幕绘制。

### 2.3 核心渲染模块
- **文件**: `src/renderer.cpp`, `include/renderer.h`, `src/renderthread.cpp`
- **职责**:
    - `RenderThread`: 独立线程，避免渲染阻塞 UI。
    - `Renderer`: 负责渲染管线的管理，包括着色器编译、纹理管理、FBO 管理、OIDN 降噪调用。

### 2.4 场景与数据模块
- **文件**: `src/Scene.cpp`, `src/BVH.cpp`, `src/Mesh.cpp`, `src/hdrloader.cpp`
- **职责**:
    - `Scene`: 单例类，管理所有场景数据。
    - `BVH`: 构建 SAH (Surface Area Heuristic) 策略的加速结构，优化射线求交。
    - `Mesh`: 加载和处理 OBJ 模型。
    - `HDRLoader`: 加载环境贴图。

### 2.5 GPU 着色器模块
- **文件**: `shaders/*.glsl`, `shaders/*.frag`
- **职责**: 实现核心的光线追踪算法（路径追踪、重要性采样、MIS）、材质评估（Disney BSDF 简化版）和随机数生成。
