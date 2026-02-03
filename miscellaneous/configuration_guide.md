# 配置与部署指南（learnQT）

## 1. 构建配置（CMake）
- `CMakeLists.txt`：
  - C++ 标准：C++14
  - Qt 组件：Widgets / OpenGL / Core / Gui
  - 第三方库：Assimp 6.0、OpenImageDenoise 2.3.3、Eigen
  - 自动处理：AUTOMOC / AUTOUIC / AUTORCC
  - Shader 与资源目录宏：
    - `RESOURCE_DIR` -> `${CMAKE_SOURCE_DIR}/resources`
    - `SHADER_DIR` -> `${CMAKE_SOURCE_DIR}/shaders`

## 2. 运行时路径策略
- 资源路径通过 `getResourcePath()`：
  - Debug 模式：使用 `RESOURCE_DIR`
  - Release 模式：使用 `exe目录/resources`
- Shader 路径通过 `getShaderPath()`：
  - Debug 模式：使用 `SHADER_DIR`
  - Release 模式：使用 `exe目录/shaders`

## 3. UI 与渲染参数配置
- UI 控件位于 `resources/learnQT.ui`：
  - `DeNoisecheckBox`：启用/关闭 OIDN
  - `useEnvironmentMapcheckBox`：启用/关闭环境贴图
  - Slider 控制材质参数（roughness/metallic/...）
- `RenderParams` 通过原子变量保存当前渲染模式

## 4. 第三方依赖管理
- `libs/Assimp` / `libs/oidn` / `libs/Eigen`
- CMake 自动拷贝运行时 DLL：
  - `oidn/bin/*.dll`
  - `Assimp/bin/*.dll`

## 5. 部署/发布注意事项
- 需打包：
  - 可执行文件（`learnQT.exe`）
  - `resources/` 和 `shaders/` 目录
  - Assimp / OIDN 相关 DLL
- 若在新机器运行：
  - 确保 Qt5 环境已正确安装/部署
  - 确保 OpenGL 驱动支持 3.3 Core

## 6. 环境变量与外部配置
- 未检测到数据库连接、网络 API 或环境变量依赖
- 配置主要由 CMake 与 UI 参数驱动
