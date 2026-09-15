cmake_minimum_required(VERSION 3.10)
project(learnQT VERSION 1.0 LANGUAGES CXX)

include(CTest)

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

# 设置第三方库的CMake模块路径
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} 
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/Assimp/lib/cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/oidn/lib/cmake
)

# 查找Assimp依赖
find_package(assimp 6.0 REQUIRED)

message(STATUS "Found assimp at: ${assimp_DIR}")

# 查找OpenImageDenoise依赖
find_package(OpenImageDenoise 2.3.3 REQUIRED)

message(STATUS "Found OpenImageDenoise at: ${OpenImageDenoise_DIR}")

set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
)

# 使用显式 Qt 代码生成规则。它兼容最低 CMake 3.10，也避免某些受限
# Windows 环境中 cmake_autogen/libuv 无法启动 moc 子进程的问题。
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)



# 定义源文件
file(GLOB SOURCES src/*.cpp)

# 定义UI文件
set(UI_FILE resources/learnQT.ui)
qt5_wrap_ui(UI_HEADERS ${UI_FILE})

# 定义资源文件
set(RCC_FILES resources/learnQT.qrc)

# 列出所有需要 MOC 处理的头文件
file(GLOB QT_HEADERS include/*.h)
set(MOC_HEADERS
    include/glwidget.h
    include/learnQT.h
    include/renderer.h
    include/renderthread.h
    include/RenderParams.h
    include/EditorController.h
    include/WorkbenchPanels.h
)
qt5_wrap_cpp(MOC_SOURCES ${MOC_HEADERS})
qt5_add_resources(RCC_SOURCES ${RCC_FILES})

# SHADERS
file(GLOB_RECURSE SHADERS shaders/*.*)

set(QT_RESOURCES
    resources/learnQT.ui
    resources/learnQT.qrc
)

# 添加可执行目标
add_executable(learnQT ${SOURCES} ${QT_HEADERS} ${UI_HEADERS} ${MOC_SOURCES} ${RCC_SOURCES} ${SHADERS})

# 着色器文件夹
foreach(shader ${SHADERS})
    # 获取着色器文件相对于shaders目录的相对路径
    get_filename_component(shader_dir ${shader} DIRECTORY)
    file(RELATIVE_PATH shader_rel_dir ${CMAKE_CURRENT_SOURCE_DIR}/shaders ${shader_dir})
    # 创建带层次结构的源文件组
    source_group("Shaders/${shader_rel_dir}" FILES ${shader})
endforeach()
#source_group("Shaders" FILES ${SHADERS})
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
    libs/Assimp/include
)

target_compile_definitions(learnQT PRIVATE
    RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources"
    SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
)

# 可选：添加调试时的工作目录设置（VS专用）
if(MSVC)
    target_compile_options(learnQT PRIVATE /utf-8)
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
target_link_libraries(learnQT PRIVATE OpenImageDenoise_core OpenImageDenoise)

# 链接Assimp库
target_link_libraries(learnQT PRIVATE assimp::assimp)

# 复制OIDN运行时DLL
file(GLOB OIDNDLLS libs/oidn/bin/*.dll)
add_custom_command(TARGET learnQT POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OIDNDLLS}
        $<TARGET_FILE_DIR:learnQT>
)

# 复制Assimp运行时DLL
file(GLOB ASSIMPDLLS libs/Assimp/bin/*.dll)
add_custom_command(TARGET learnQT POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ASSIMPDLLS}
        $<TARGET_FILE_DIR:learnQT>
)

if(BUILD_TESTING)
    add_executable(renderer_batch_tests tests/RendererBatchTests.cpp
        src/renderer.cpp src/RendererPick.cpp src/RendererPreview.cpp src/PreviewDenoiser.cpp
        src/Scene.cpp src/SceneRuntime.cpp src/SceneDocument.cpp src/SceneGraph.cpp src/SceneEncoding.cpp
        src/SceneAssets.cpp src/Mesh.cpp src/Camera.cpp src/BVH.cpp src/common.cpp src/hdrloader.cpp src/RenderParams.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/include/moc_renderer.cpp ${CMAKE_CURRENT_BINARY_DIR}/include/moc_RenderParams.cpp)
    target_include_directories(renderer_batch_tests PRIVATE include libs/Eigen libs/Assimp/include libs/oidn/include)
    target_compile_options(renderer_batch_tests PRIVATE /utf-8)
    target_compile_definitions(renderer_batch_tests PRIVATE RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources" SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders")
    target_link_libraries(renderer_batch_tests PRIVATE Qt5::Widgets Qt5::OpenGL opengl32 OpenImageDenoise_core OpenImageDenoise assimp::assimp)
    add_dependencies(renderer_batch_tests learnQT)
    add_test(NAME renderer_batch_regression COMMAND renderer_batch_tests)
    add_test(NAME environment_sphere_regression COMMAND renderer_batch_tests
        --environment-regression ${CMAKE_CURRENT_BINARY_DIR}/environment-sphere-regression)
    set_tests_properties(environment_sphere_regression PROPERTIES LABELS "gpu;render;lighting" TIMEOUT 120)
    set_tests_properties(renderer_batch_regression PROPERTIES LABELS "gpu;render" TIMEOUT 60)
    add_executable(presentation_tests tests/PresentationTests.cpp src/texturebuffer.cpp)
    target_include_directories(presentation_tests PRIVATE include)
    target_compile_options(presentation_tests PRIVATE /utf-8)
    target_link_libraries(presentation_tests PRIVATE Qt5::Core Qt5::Gui Qt5::OpenGL opengl32)
    add_test(NAME display_buffer_handoff_regression COMMAND presentation_tests)
    set_tests_properties(display_buffer_handoff_regression PROPERTIES LABELS "gpu;ui" TIMEOUT 20)
    add_executable(traversal_tests tests/TraversalTests.cpp src/SceneGraph.cpp src/BVH.cpp src/common.cpp)
    target_include_directories(traversal_tests PRIVATE include)
    target_compile_options(traversal_tests PRIVATE /utf-8)
    target_compile_definitions(traversal_tests PRIVATE
        RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources" SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders")
    target_link_libraries(traversal_tests PRIVATE Qt5::Core Qt5::Gui Qt5::OpenGL opengl32)
    add_test(NAME instanced_traversal_regression COMMAND traversal_tests)
    set_tests_properties(instanced_traversal_regression PROPERTIES LABELS "gpu;scene" TIMEOUT 120)
    add_executable(lighting_tests tests/LightingTests.cpp src/common.cpp)
    target_include_directories(lighting_tests PRIVATE include)
    target_compile_options(lighting_tests PRIVATE /utf-8)
    target_compile_definitions(lighting_tests PRIVATE
        RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources" SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders")
    target_link_libraries(lighting_tests PRIVATE Qt5::Core Qt5::Gui Qt5::OpenGL opengl32)
    add_test(NAME lighting_numerical_regression COMMAND lighting_tests)
    set_tests_properties(lighting_numerical_regression PROPERTIES LABELS "gpu;lighting" TIMEOUT 300)
    target_sources(learnQT PRIVATE tests/SceneUiRegression.cpp tests/WorkbenchUiRegression.cpp tests/InteractionRegression.cpp tests/PreviewModeRegression.cpp tests/WorkspaceUiRegression.cpp tests/PreviewPanelRegression.cpp tests/RasterPreviewRegression.cpp)
    target_compile_definitions(learnQT PRIVATE SCENE_TESTING)
    add_test(NAME workspace_pages_regression COMMAND $<TARGET_FILE:learnQT> --workspace-regression ${CMAKE_CURRENT_BINARY_DIR}/workspace-regression)
    set_tests_properties(workspace_pages_regression PROPERTIES LABELS "scene;gpu;ui;workspace" TIMEOUT 120)
    add_test(NAME scene_ui_switch_regression COMMAND $<TARGET_FILE:learnQT>
        --scene ${CMAKE_CURRENT_SOURCE_DIR}/resources/scenes/bedroom.scene.json
        --scene-switch-regression ${CMAKE_CURRENT_BINARY_DIR}/scene-ui-regression)
    set_tests_properties(scene_ui_switch_regression PROPERTIES LABELS "scene;gpu;ui" TIMEOUT 300)
    add_executable(scene_tests tests/SceneTests.cpp src/Scene.cpp src/SceneRuntime.cpp src/SceneDocument.cpp src/SceneGraph.cpp src/SceneEncoding.cpp
        tests/EditorTests.cpp src/EditorController.cpp src/SceneTreeModel.cpp ${CMAKE_CURRENT_BINARY_DIR}/include/moc_EditorController.cpp
        src/SceneAssets.cpp src/Mesh.cpp src/Camera.cpp src/BVH.cpp src/common.cpp src/hdrloader.cpp src/RenderParams.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/include/moc_RenderParams.cpp)
    target_include_directories(scene_tests PRIVATE include libs/Assimp/include)
    target_compile_options(scene_tests PRIVATE /utf-8)
    target_compile_definitions(scene_tests PRIVATE RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources" SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders")
    target_link_libraries(scene_tests PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets assimp::assimp)
    add_dependencies(scene_tests learnQT)
    add_test(NAME scene_roundtrip_and_package COMMAND scene_tests)
    set_tests_properties(scene_roundtrip_and_package PROPERTIES LABELS "scene;package" TIMEOUT 180)
    add_test(NAME workbench_ui_and_render_job COMMAND $<TARGET_FILE:learnQT> --workbench-regression ${CMAKE_CURRENT_BINARY_DIR}/workbench-regression)
    set_tests_properties(workbench_ui_and_render_job PROPERTIES LABELS "scene;gpu;ui;job" TIMEOUT 150)
    add_test(NAME preview_interaction_regression COMMAND $<TARGET_FILE:learnQT> --preview-regression ${CMAKE_CURRENT_BINARY_DIR}/preview-regression)
    set_tests_properties(preview_interaction_regression PROPERTIES LABELS "gpu;ui;preview" TIMEOUT 90)
    add_test(NAME preview_mode_switch_regression COMMAND $<TARGET_FILE:learnQT> --preview-mode-regression ${CMAKE_CURRENT_BINARY_DIR}/preview-mode-regression)
    set_tests_properties(preview_mode_switch_regression PROPERTIES LABELS "gpu;ui;preview" TIMEOUT 120)
    add_test(NAME preview_mis_mode_switch_regression COMMAND $<TARGET_FILE:learnQT> --preview-mode-regression ${CMAKE_CURRENT_BINARY_DIR}/preview-mis-mode-regression --mode-mis)
    set_tests_properties(preview_mis_mode_switch_regression PROPERTIES LABELS "gpu;ui;preview" TIMEOUT 120)
    add_test(NAME preview_panel_regression COMMAND $<TARGET_FILE:learnQT> --preview-panel-regression ${CMAKE_CURRENT_BINARY_DIR}/preview-panel-regression)
    set_tests_properties(preview_panel_regression PROPERTIES LABELS "gpu;ui;preview;workspace" TIMEOUT 90)
    add_test(NAME raster_preview_regression COMMAND $<TARGET_FILE:learnQT>
        --scene ${CMAKE_CURRENT_SOURCE_DIR}/resources/scenes/glslpt_test.scene.json
        --raster-regression ${CMAKE_CURRENT_BINARY_DIR}/raster-preview-regression)
    set_tests_properties(raster_preview_regression PROPERTIES LABELS "gpu;ui;preview;raster" TIMEOUT 180)
    if(CMAKE_VERSION VERSION_LESS 3.12)
        find_package(PythonInterp REQUIRED)
        set(TEST_PYTHON_EXECUTABLE ${PYTHON_EXECUTABLE})
    else()
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set(TEST_PYTHON_EXECUTABLE ${Python3_EXECUTABLE})
    endif()

    add_test(
        NAME unit_middle_mouse_orbit_target
        COMMAND ${TEST_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_middle_mouse_orbit_target.py
    )
    add_test(
        NAME unit_finite_analytic_lights
        COMMAND ${TEST_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_finite_analytic_lights.py
    )
    add_test(
        NAME unit_texture_rendering_support
        COMMAND ${TEST_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_texture_rendering_support.py
    )
    add_test(NAME glslpt_conversion
        COMMAND ${TEST_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_glslpt_conversion.py)
    set_tests_properties(glslpt_conversion PROPERTIES
        LABELS "scene;conversion"
        ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/test-python")
    add_test(
        NAME gpu_render_regression
        COMMAND $<TARGET_FILE:learnQT>
            --model ${CMAKE_CURRENT_SOURCE_DIR}/resources/models/texture_test/Lantern/Lantern.glb
            --render-regression ${CMAKE_CURRENT_BINARY_DIR}/render_regression.png
            --regression-frames 12
            --regression-lantern
    )
    set_tests_properties(gpu_render_regression PROPERTIES
        LABELS "gpu;render"
        TIMEOUT 180
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(
        NAME import_fbx_embedded_texture
        COMMAND $<TARGET_FILE:learnQT>
            --model ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/embedded_texture_triangle.fbx
            --validate-model-import
            --expect-embedded-textures 1
    )
    set_tests_properties(import_fbx_embedded_texture PROPERTIES
        LABELS "import;fbx"
        TIMEOUT 60
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(
        NAME import_gltf_tangent_sampler_transform
        COMMAND $<TARGET_FILE:learnQT>
            --model ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/texture_transform_triangle.gltf
            --validate-model-import
            --expect-embedded-textures 1
            --expect-tangents
            --expect-texture-transform
            --expect-non-default-wrap
            --expect-non-default-filter
    )
    set_tests_properties(import_gltf_tangent_sampler_transform PROPERTIES
        LABELS "import;gltf"
        TIMEOUT 60
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    # Tests locate the configured Qt runtime without requiring a hand-edited PATH.
    if(WIN32)
        add_test(NAME background_ui_regression COMMAND ${TEST_PYTHON_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_background_ui.py $<TARGET_FILE:learnQT>
            ${CMAKE_CURRENT_BINARY_DIR}/background-ui-regression)
        set_tests_properties(background_ui_regression PROPERTIES LABELS "gpu;ui;background" TIMEOUT 180)
    endif()
    get_target_property(QT_CORE_RUNTIME Qt5::Core IMPORTED_LOCATION_RELEASE)
    get_filename_component(QT_RUNTIME_DIRECTORY "${QT_CORE_RUNTIME}" DIRECTORY)
    set(SCENE_TEST_PATH "${QT_RUNTIME_DIRECTORY};$ENV{PATH}")
    string(REPLACE ";" "\\;" SCENE_TEST_PATH "${SCENE_TEST_PATH}")
    set_tests_properties(workspace_pages_regression instanced_traversal_regression lighting_numerical_regression scene_ui_switch_regression scene_roundtrip_and_package gpu_render_regression workbench_ui_and_render_job preview_interaction_regression preview_mode_switch_regression preview_mis_mode_switch_regression display_buffer_handoff_regression renderer_batch_regression environment_sphere_regression
        import_fbx_embedded_texture import_gltf_tangent_sampler_transform preview_panel_regression raster_preview_regression
        PROPERTIES ENVIRONMENT "PATH=${SCENE_TEST_PATH}")
    if(WIN32)
        set_tests_properties(background_ui_regression PROPERTIES ENVIRONMENT "PATH=${SCENE_TEST_PATH}")
    endif()
    # Even with ctest -j, GPU/UI workloads share one lane to limit foreground contention.
    get_property(LEARNQT_TESTS DIRECTORY PROPERTY TESTS)
    foreach(test IN LISTS LEARNQT_TESTS)
        get_test_property(${test} LABELS labels)
        if(labels MATCHES "(^|;)gpu(;|$)")
            set_tests_properties(${test} PROPERTIES RESOURCE_LOCK learnqt_gpu)
        endif()
    endforeach()
endif()
