# 项目结构文档（learnQT）

## 1. 总览
learnQT 是一个基于 Qt5 + OpenGL 的桌面渲染程序，核心功能为路径追踪/光线追踪渲染，包含：
- Qt 界面（`QMainWindow` + `QOpenGLWidget`）
- 独立渲染线程（`QThread`）
- 路径追踪 shader 管线
- BVH 加速结构与 OBJ 模型加载
- OIDN 降噪

## 2. 目录树（精简）
说明：`libs/` 与 `build/` 体量较大，树中仅展示核心子目录结构与关键文件。

```
learnQT/
├─ CMakeLists.txt
├─ README.md
├─ include/
│  ├─ BVH.h
│  ├─ Camera.h
│  ├─ Material.h
│  ├─ Mesh.h
│  ├─ RenderParams.h
│  ├─ Scene.h
│  ├─ common.h
│  ├─ glwidget.h
│  ├─ hdrloader.h
│  ├─ learnQT.h
│  ├─ renderer.h
│  ├─ renderthread.h
│  └─ texturebuffer.h
├─ src/
│  ├─ BVH.cpp
│  ├─ Camera.cpp
│  ├─ Mesh.cpp
│  ├─ RenderParams.cpp
│  ├─ Scene.cpp
│  ├─ common.cpp
│  ├─ glwidget.cpp
│  ├─ hdrloader.cpp
│  ├─ learnQT.cpp
│  ├─ main.cpp
│  ├─ renderer.cpp
│  ├─ renderthread.cpp
│  └─ texturebuffer.cpp
├─ shaders/
│  ├─ pathtrace.frag
│  ├─ triangle.frag
│  ├─ triangle.vert
│  ├─ historysave.frag
│  ├─ mixframe.frag
│  └─ include/
│     ├─ bsdf.glsl
│     ├─ bvh_material.glsl
│     ├─ defines.glsl
│     ├─ hdr_utils.glsl
│     ├─ pathtrace.glsl
│     ├─ structs.glsl
│     ├─ uniforms.glsl
│     └─ utils.glsl
├─ resources/
│  ├─ learnQT.ui
│  ├─ learnQT.qrc
│  ├─ hdr/
│  └─ models/
├─ libs/
│  ├─ Assimp/
│  ├─ Eigen/
│  └─ oidn/
├─ output/
├─ build/
└─ miscellaneous/
   ├─ README.md
   ├─ project_structure.md
   ├─ execution_flow.md
   ├─ tech_stack.md
   └─ configuration_guide.md
```

## 3. 模块划分与职责

### 3.1 UI 与交互层（Qt）
- `learnQT`（`include/learnQT.h`, `src/learnQT.cpp`）
  - 主窗口与 UI 事件绑定
  - 负责 slider/checkbox 的交互与参数下发
- `GLWidget`（`include/glwidget.h`, `src/glwidget.cpp`）
  - `QOpenGLWidget` 实现，负责显示纹理
  - 初始化 OpenGL 与启动渲染线程
  - 捕获键鼠输入并驱动相机

### 3.2 渲染线程与同步
- `RenderThread`（`include/renderthread.h`, `src/renderthread.cpp`）
  - 独立渲染线程，持有独立 `QOpenGLContext`
  - 周期调用 `Renderer::render()`
  - 与 UI 线程通过 signal/slot 交互
- `TextureBuffer`（`include/texturebuffer.h`, `src/texturebuffer.cpp`）
  - 渲染线程 -> UI 线程间共享纹理
  - 内部加锁保护纹理读写
- `RenderParams`（`include/RenderParams.h`, `src/RenderParams.cpp`）
  - 原子参数容器（降噪/低分辨率/分块渲染/环境贴图等）

### 3.3 场景与几何
- `Scene`（`include/Scene.h`, `src/Scene.cpp`）
  - 单例场景管理器
  - 模型加载、BVH 构建、数据编码
- `MeshLoader`（`include/Mesh.h`, `src/Mesh.cpp`）
  - 解析 OBJ 并生成三角形列表
- `BVH`（`include/BVH.h`, `src/BVH.cpp`）
  - SAH 分割构建 BVH

### 3.4 渲染核心
- `Renderer`（`include/renderer.h`, `src/renderer.cpp`）
  - shader 管线与渲染 Pass 管理
  - 纹理/FBO 管理、tile 渲染、历史帧缓存
  - OIDN 降噪与最终合成
- `common`（`include/common.h`, `src/common.cpp`）
  - HDR 采样 cache 计算
  - Sobol 随机数
  - 资源路径辅助

### 3.5 资源与 shader
- `resources/`：UI/模型/HDR 资源
- `shaders/`：路径追踪、历史帧保存、合成等 GLSL 程序
- shader 通过 `#include` 预处理拼接（在 `renderer.cpp` 实现）

## 4. 依赖关系与模块关联

```
main.cpp
  -> learnQT (UI)
    -> GLWidget (OpenGL 显示)
      -> RenderThread (QThread)
        -> Renderer (渲染核心)
          -> Scene (几何 + BVH + HDR)
          -> RenderParams (渲染参数)
          -> shaders/* (GLSL)

Scene
  -> MeshLoader
  -> BVH
  -> HDRLoader
  -> common (HDR cache / Sobol)
```

## 5. 资源与配置
- `resources/models/*.obj`：场景模型
- `resources/hdr/*.hdr`：环境贴图
- `resources/learnQT.ui`：Qt Designer UI 定义
- `resources/learnQT.qrc`：资源索引（当前为空资源）

## 6. 文件类型统计（递归扫描结果）
> 统计来自整个仓库（含 `build/` 与 `libs/`）

- `.h`: 395
- `.cpp`: 29
- `.glsl`: 8
- `.frag`: 4
- `.vert`: 1
- `.obj`: 50
- `.hdr`: 4
- `.dll`: 45
- `.lib`: 4
- `.exe`: 6
- `.cmake`: 12
- `.md`: 8
- 其它：`.json/.txt/.png/.vcxproj/.tlog/...`

> 提示：`build/` 与 `libs/` 包含大量第三方/构建产物，文档中以结构概述为主，避免完整列举。
