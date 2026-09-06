# learnQT 代码地图

建议先读 [架构](./project_architecture.md) 和 [渲染流程](./render_flow.md)，再沿下面入口阅读。UI 主要由代码构建，旧 `.ui` 文件不代表当前工作台布局。

| 范围 | 实现入口 | 联动检查 |
| --- | --- | --- |
| 启动 / CLI | [main.cpp](../src/main.cpp)、[RegressionCapture.cpp](../src/RegressionCapture.cpp) | 默认空场景、互斥入口、保存/导出、固定 spp 截图。 |
| 主窗口 | [learnQT.cpp](../src/learnQT.cpp)、[learnQT.h](../include/learnQT.h) | Fusion 停靠布局、菜单/快捷键、导入、dirty、正式出图。 |
| 编辑命令 | [EditorController.cpp](../src/EditorController.cpp)、[EditorController.h](../include/EditorController.h) | 选择去重、材质隔离、undo、缓存和后台导入。 |
| 对象树 | [SceneTreeModel.cpp](../src/SceneTreeModel.cpp) | 根限制、父子循环、拖动/顺序、可见/锁定、搜索代理。 |
| 属性与图表 | [WorkbenchPanels.cpp](../src/WorkbenchPanels.cpp)、[LightInspector.cpp](../src/LightInspector.cpp) | 多选混合值、纹理槽、灯光、结果查看和性能绘制缓存。 |
| 视口 / 相机 | [glwidget.cpp](../src/glwidget.cpp)、[Camera.cpp](../src/Camera.cpp) | 选择叠加、操纵器、多选中心、旋转方向、滚轮近限值、高 DPI。 |
| 控制与任务 | [renderthread.cpp](../src/renderthread.cpp)、[RenderJob.h](../include/RenderJob.h)、[RenderRateTracker.h](../include/RenderRateTracker.h) | 版本队列、批次安全边界、暂停/停止、完整快照、真实完成速率。 |
| 渲染调度 | [renderer.cpp](../src/renderer.cpp)、[renderer.h](../include/renderer.h) | 1–16 tile 批次、GPU 预算、每批绑定、按频率合成、资源上传。 |
| GPU 拾取 | [RendererPick.cpp](../src/RendererPick.cpp)、[pick.frag](../shaders/pick.frag) | 整数 ID/depth、PBO/fence、alpha 阈值和过期结果拒绝。 |
| 预览降噪 | [RendererPreview.cpp](../src/RendererPreview.cpp)、[PreviewDenoiser.cpp](../src/PreviewDenoiser.cpp) | 三路完整快照、PBO 延迟映射、取消和版本/尺寸匹配。 |
| 显示桥 | [texturebuffer.cpp](../src/texturebuffer.cpp) | 明确源 FBO、三槽所有权、未消费帧保护、跨上下文 fence。 |
| 场景格式 | [SceneDocument.cpp](../src/SceneDocument.cpp)、[SceneAssets.cpp](../src/SceneAssets.cpp) | v1→v2、稳定 ID、原子保存、路径/便携包限制。 |
| 运行时 / 导入 | [SceneRuntime.cpp](../src/SceneRuntime.cpp)、[Mesh.cpp](../src/Mesh.cpp) | 文件/源节点/实例层级、源尺寸、共享资源、材质和嵌入贴图。 |
| 加速与编码 | [SceneGraph.cpp](../src/SceneGraph.cpp)、[SceneEncoding.cpp](../src/SceneEncoding.cpp)、[BVH.cpp](../src/BVH.cpp) | 局部 BLAS、加权 SAH TLAS/refit、GPU 分表及动态实例。 |
| 实例求交 | [bvh_instances.glsl](../shaders/include/bvh_instances.glsl)、[scene_access.glsl](../shaders/include/scene_access.glsl) | 近节点优先、局部射线距离、镜像 TBN、同距选择、surface ID。 |
| 材质 / 光照 | [bvh_material.glsl](../shaders/include/bvh_material.glsl)、[light_sampling.glsl](../shaders/include/light_sampling.glsl)、[hdr_utils.glsl](../shaders/include/hdr_utils.glsl) | PBR/alpha、世界光源面积/PDF、环境旋转与强度。 |
| 积分器 | [pathtrace.glsl](../shaders/include/pathtrace.glsl)、[medium.glsl](../shaders/include/medium.glsl)、[bsdf.glsl](../shaders/include/bsdf.glsl) | NEE/MIS、delta、介质栈、阴影精简材质路径。 |
| 像素与后处理 | [pathtrace.frag](../shaders/pathtrace.frag)、[utils.glsl](../shaders/include/utils.glsl)、[historysave.frag](../shaders/historysave.frag)、[triangle.frag](../shaders/triangle.frag) | 像素中心、全图 RNG、完整历史、曝光/tone mapping/gamma。 |
| 资源转换 | [convert_glslpt_scenes.py](../tools/convert_glslpt_scenes.py)、[resources/scenes](../resources/scenes) | 外部场景转换、55 个预设发现、派生灯光/贴图、来源信息。 |

## 验证入口

| 测试代码 | 覆盖内容 |
| --- | --- |
| [SceneTests.cpp](../tests/SceneTests.cpp)、[EditorTests.cpp](../tests/EditorTests.cpp) | 场景/包往返、组和根约束、材质隔离、资源共享、失败原子性、相机及速率。 |
| [TraversalTests.cpp](../tests/TraversalTests.cpp) | GPU 对照暴力求交、TLAS 结构、镜像/非均匀变换、同距、alpha/拾取、TBN、介质阴影。 |
| [LightingTests.cpp](../tests/LightingTests.cpp) | HDR、球/太阳盘、MIS 方差、delta、alpha 发光、均匀介质数值对照。 |
| [SceneUiRegression.cpp](../tests/SceneUiRegression.cpp) | 实际 UI 场景切换、保存/放弃/取消、失败恢复及预设发现。 |
| [WorkbenchUiRegression.cpp](../tests/WorkbenchUiRegression.cpp) | 树/视口选择、变换与撤销、零 BLAS 重建/几何上传、正式任务和 PNG/JPEG。 |
| [InteractionRegression.cpp](../tests/InteractionRegression.cpp) | 空场景、连续滚轮、后台 OIDN、版本/尺寸失效、UI 延迟诊断。 |
| [PreviewModeRegression.cpp](../tests/PreviewModeRegression.cpp)、[PresentationTests.cpp](../tests/PresentationTests.cpp) | 整图/分块来回切换、test_mis 黑屏回归、延迟消费和明确源 FBO。 |
| [RendererBatchTests.cpp](../tests/RendererBatchTests.cpp) | 批次上限/中断、完整快照、同 spp 像素一致、预算失效、停止采样后的显示刷新；另提供可复现的离屏 benchmark CLI。 |
| [CMakeLists.txt](../CMakeLists.txt)、[tests](../tests) | CTest 注册、Python 导入/转换契约、Qt 运行时路径。 |

性能原始数据和截图位于未版本化 `build/`；可追溯的汇总与实际命令见 [工作台专题](./scene_workbench_v2.md)。旧 benchmark 副本不自动反映主代码，不把试验副本的结果当作正式版本验收。
