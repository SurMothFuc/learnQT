# 技术栈清单（learnQT）

## 1. 语言与编译
- C++14（`CMakeLists.txt` 指定）
- CMake 3.10+
- MSVC（CMake 中对 MSVC Debugger Working Directory 有配置）

## 2. GUI 与系统依赖
- Qt5
  - Widgets
  - OpenGL
  - Core
  - Gui

## 3. 渲染与图形
- OpenGL 3.3 Core Profile（`QOpenGLFunctions_3_3_Core`）
- GLSL shader（`shaders/*`）

## 4. 数学/几何/加速结构
- Eigen（`libs/Eigen`）
- BVH 构建（SAH）
- Sobol 采样（`common.cpp`）

## 5. 资源与模型
- Assimp 6.0（`libs/Assimp`）
- OBJ 模型解析（自实现 + 模型资源）
- HDR 环境贴图读取（`hdrloader.cpp`）

## 6. 图像/降噪
- OpenImageDenoise 2.3.3（`libs/oidn`）

## 7. 架构/模式
- 单例：`Scene`, `RenderParams`, `TextureBuffer`
- 线程模型：UI 线程 + 渲染线程
- 信号槽驱动：Qt signals/slots

## 8. 本地目录中的第三方依赖
- `libs/Assimp/*`
- `libs/oidn/*`
- `libs/Eigen/*`

> 这些依赖通过 `CMakeLists.txt` 中的 `CMAKE_PREFIX_PATH` 与 `target_include_directories` 引入。
