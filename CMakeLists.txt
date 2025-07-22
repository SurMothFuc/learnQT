cmake_minimum_required(VERSION 3.10)
project(learnQT VERSION 1.0 LANGUAGES CXX)

# 设置构建类型
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo" CACHE STRING "Configuration types" FORCE)
# 多处理器编译
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
# 设置 Visual Studio 的调试工作目录

# 设置C++标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找Qt依赖
find_package(Qt5 COMPONENTS Widgets OpenGL Core Gui REQUIRED)

message(STATUS "Found Qt5 at: ${Qt5_DIR}")

set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
)

# 启用Qt自动处理工具
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_VERBOSE ON)  # 启用详细输出



# 定义源文件
file(GLOB SOURCES src/*.cpp)

# 定义UI文件
set(UI_FILE resources/learnQT.ui)
qt5_wrap_ui(UI_HEADERS ${UI_FILE})

# 定义资源文件
set(RCC_FILES resources/learnQT.qrc)

# 列出所有需要 MOC 处理的头文件
file(GLOB QT_HEADERS include/*.h)

# SHADERS
file(GLOB SHADERS shaders/*.*)

set(QT_RESOURCES
    resources/learnQT.ui
    resources/learnQT.qrc
)

# 添加可执行目标
add_executable(learnQT ${SOURCES} ${QT_HEADERS} ${UI_HEADERS} ${RCC_FILES} ${SHADERS})

# 着色器文件夹
source_group("Shaders" FILES ${SHADERS})
# Qt资源文件夹（包含UI和QRC文件）
source_group("Qt Resources" FILES ${QT_RESOURCES})
# 保留现有的其他分组
source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SOURCES})

target_include_directories(learnQT PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}                          # 备用路径
    include
    libs/Eigen
    libs/oidn/include
)

target_compile_definitions(learnQT PRIVATE
    RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources"
    SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
)

# 可选：添加调试时的工作目录设置（VS专用）
if(MSVC)
    set_property(TARGET learnQT PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# 链接Qt库
target_link_libraries(learnQT PRIVATE 
    Qt5::Widgets 
    Qt5::OpenGL 
    Qt5::Core 
    Qt5::Gui
    opengl32
)

# 链接OIDN库
target_link_libraries(learnQT PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/oidn/lib/OpenImageDenoise.lib)
target_link_libraries(learnQT PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/oidn/lib/OpenImageDenoise_core.lib)

# 复制OIDN运行时DLL
file(GLOB OIDNDLLS libs/oidn/bin/*.dll)
add_custom_command(TARGET learnQT POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OIDNDLLS}
        $<TARGET_FILE_DIR:learnQT>
)