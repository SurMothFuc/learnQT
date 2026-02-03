# 项目配置与部署指南

## 1. 环境配置

### 1.1 前置要求
确保系统已安装以下软件：
*   **CMake** (3.10 或更高版本)
*   **Qt 5.x** (包含 MSVC 组件)
*   **Visual Studio** (推荐 2019 或 2022，带 C++ 开发工作负载)

### 1.2 依赖库配置
项目自带了关键依赖库 (`libs/` 目录下)，但在 CMake 中需要正确识别。
*   **Qt5**: CMake 会查找系统中的 Qt5 安装。如果找不到，需设置 `Qt5_DIR` 变量指向 `lib/cmake/Qt5`。
*   **Assimp & OIDN**: 位于 `libs/` 目录下，CMakeLists.txt 已经配置了相对路径查找：
    ```cmake
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} 
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/Assimp/lib/cmake
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/oidn/lib/cmake
    )
    ```

## 2. 编译与构建

### 2.1 使用命令行构建
```bash
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH="C:/path/to/Qt/5.15.2/msvc2019_64" 
cmake --build . --config Release
```

### 2.2 使用 Visual Studio 构建
1.  打开 Visual Studio。
2.  选择 "打开本地文件夹" 并选择项目根目录。
3.  VS 会自动检测 `CMakeLists.txt` 并进行配置。
4.  在 CMake 设置中，可能需要手动添加 Qt 的 CMake 路径。
5.  选择 `learnQT.exe` 作为启动项进行编译运行。

## 3. 运行时配置

### 3.1 资源路径
项目使用相对路径加载着色器和资源。
*   `RESOURCE_DIR` 和 `SHADER_DIR` 宏在 CMake 中定义：
    ```cmake
    target_compile_definitions(learnQT PRIVATE
        RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources"
        SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
    )
    ```
*   确保运行时工作目录正确，或者程序能通过绝对路径访问资源。

### 3.2 动态链接库 (DLL)
编译生成的可执行文件需要以下 DLL 才能运行：
*   `Qt5Core.dll`, `Qt5Gui.dll`, `Qt5Widgets.dll`, `Qt5OpenGL.dll`
*   `assimp-vc143-mt.dll` (在 `libs/Assimp/bin`)
*   `OpenImageDenoise.dll` (在 `libs/oidn/bin`)
*   `tbb12.dll` (OIDN 依赖)

通常需要将 `libs/*/bin` 下的 DLL 复制到构建输出目录（与 `.exe` 同级）。

## 4. 常见问题
*   **找不到 DLL**: 检查是否将第三方库的 DLL 拷贝到了可执行文件目录，或将 `libs/*/bin` 添加到了系统 PATH。
*   **着色器编译失败**: 检查 `shaders/` 目录路径是否正确，以及显卡驱动是否支持 OpenGL 3.3。
*   **内存不足**: 高分辨率渲染或大型模型可能导致显存/内存不足，尝试降低分辨率或简化模型。
