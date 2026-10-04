# learnQT 代码地图

建议先读 [架构](./project_architecture.md) 和 [渲染流程](./render_flow.md)，再沿下面入口阅读。UI 主要由代码构建，旧 `.ui` 文件不代表当前工作台布局。

| 范围 | 实现入口 | 联动检查 |
| --- | --- | --- |
| 启动 / CLI | [main.cpp](../src/main.cpp)、[RegressionCapture.cpp](../src/RegressionCapture.cpp) | 空文档/欢迎页、互斥入口、保存/导出、固定 spp 截图、回归偏好隔离。 |
| 主窗口 | [learnQT.cpp](../src/learnQT.cpp)、[learnQT.h](../include/learnQT.h) | 固定分区、菜单/快捷键、导入、dirty、冷启动渲染请求及任务展示冻结。 |
| 工作区 / 样式 | [WorkspacePages.cpp](../src/WorkspacePages.cpp)、[WorkspaceUi.h](../include/WorkspaceUi.h)、[WorkbenchStyle.h](../include/WorkbenchStyle.h) | 八页描述表路由、照明标签、渲染两模式、共享视口、资源目录、最近文件、QSettings V5 布局／V4 偏好、功能边界说明、布局恢复和窄窗口。 |
| 编辑命令 | [EditorController.cpp](../src/EditorController.cpp)、[EditorController.h](../include/EditorController.h) | 选择去重、材质隔离、undo、缓存和后台导入。 |
| 对象树 | [SceneTreeModel.cpp](../src/SceneTreeModel.cpp) | 根限制、父子循环、拖动/顺序、可见/锁定、搜索代理。 |
| 属性与图表 | [WorkbenchPanels.cpp](../src/WorkbenchPanels.cpp)、[LightInspector.cpp](../src/LightInspector.cpp) | 多选混合值、材质页只读浏览、基础/纹理/高级分组、灯光、结果空状态和性能绘制缓存。 |
| 视口 / 相机 | [glwidget.cpp](../src/glwidget.cpp)、[Camera.cpp](../src/Camera.cpp) | 选择叠加、操纵器、多选中心、旋转方向、滚轮近限值、高 DPI。 |
| 控制与任务 | [renderthread.cpp](../src/renderthread.cpp)、[RenderJob.h](../include/RenderJob.h)、[RenderRateTracker.h](../include/RenderRateTracker.h) | 版本队列、批次安全边界、暂停/停止、完整快照、真实完成速率。 |
| 队列 / 构图 | [RenderQueueUi.cpp](../src/RenderQueueUi.cpp)、[RenderQueueThread.cpp](../src/RenderQueueThread.cpp)、[RenderQueueThread.h](../include/RenderQueueThread.h) | 来源相机／草稿／请求快照、独立运行时、串行控制、结果固定浏览、冻结元数据及自动导出。 |
| 渲染调度 | [renderer.cpp](../src/renderer.cpp)、[renderer.h](../include/renderer.h) | 1–16 tile 批次、GPU 预算、每批绑定、按频率合成、资源上传。 |
| 线性结果 / EXR | [RenderResult.h](../include/RenderResult.h)、[WorkbenchPanels.cpp](../src/WorkbenchPanels.cpp) | 不可变完整 Film、历史重曝光/曲线/guide AOV、CPU sRGB 与 HALF/FLOAT 原子 EXR；分离显示与线性输出。 |
| 初始介质 / 接触 | [InitialMedia.h](../include/InitialMedia.h)、[MediumInterfaces.h](../include/MediumInterfaces.h)、[SceneGraph.cpp](../src/SceneGraph.cpp) | 按需闭合判定、CPU BLAS 内外、8 层身份/IOR、精确配对接触，不含任意交叠。 |
| 诊断捕获 / OIDN 保护 | [PathDiagnosticCapture.h](../include/PathDiagnosticCapture.h)、[OidnConfidence.h](../include/OidnConfidence.h) | 可选事件/像素/首状态捕获，三路 RGBA/moments/策略掩码，raw 历史不变。 |
| GPU 拾取 | [RendererPick.cpp](../src/RendererPick.cpp)、[pick.frag](../shaders/pick.frag) | 整数 ID/depth、PBO/fence、alpha 阈值和过期结果拒绝。 |
| 预览降噪 | [RendererPreview.cpp](../src/RendererPreview.cpp)、[PreviewDenoiser.cpp](../src/PreviewDenoiser.cpp) | 三路完整快照、PBO 延迟映射、取消和版本/尺寸匹配。 |
| 显示桥 | [texturebuffer.cpp](../src/texturebuffer.cpp) | 明确源 FBO、三槽所有权、未消费帧保护、跨上下文 fence。 |
| 场景格式 | [SceneDocument.cpp](../src/SceneDocument.cpp)、[SceneAssets.cpp](../src/SceneAssets.cpp) | v1→v2、稳定 ID、原子保存、路径/便携包限制。 |
| 运行时 / 导入 | [SceneRuntime.cpp](../src/SceneRuntime.cpp)、[Mesh.cpp](../src/Mesh.cpp) | 文件/源节点/实例层级、源尺寸、共享资源、材质和嵌入贴图。 |
| 加速与编码 | [SceneGraph.cpp](../src/SceneGraph.cpp)、[SceneEncoding.cpp](../src/SceneEncoding.cpp)、[BVH.cpp](../src/BVH.cpp) | 局部 BLAS、加权 SAH TLAS/refit、GPU 分表及动态实例。 |
| 实例求交 | [bvh_instances.glsl](../shaders/include/bvh_instances.glsl)、[scene_access.glsl](../shaders/include/scene_access.glsl) | 近节点优先、局部射线距离、镜像 TBN、同距选择、surface ID。 |
| 三角形精度 | [triangle_intersection.glsl](../shaders/include/triangle_intersection.glsl)、[utils.glsl](../shaders/include/utils.glsl) | 沿射线投影、共享边符号一致性、无重心 padding、表面重建和无效着色法线回退；实例/兼容路径共用。 |
| 材质 / 光照 | [bvh_material.glsl](../shaders/include/bvh_material.glsl)、[light_sampling.glsl](../shaders/include/light_sampling.glsl)、[hdr_utils.glsl](../shaders/include/hdr_utils.glsl) | PBR/alpha、世界光源面积/PDF、环境旋转与强度。 |
| 积分器 | [pathtrace.glsl](../shaders/include/pathtrace.glsl)、[medium.glsl](../shaders/include/medium.glsl)、[bsdf.glsl](../shaders/include/bsdf.glsl) | NEE/MIS、delta、介质栈、阴影精简材质路径。 |
| 像素与后处理 | [pathtrace.frag](../shaders/pathtrace.frag)、[utils.glsl](../shaders/include/utils.glsl)、[historysave.frag](../shaders/historysave.frag)、[triangle.frag](../shaders/triangle.frag) | AA 像素抖动/中心、固定维度/种子、完整历史、曝光/tone mapping/准确 sRGB。 |
| 纹理视图 / 预算 | [MaterialTextureImage.h](../include/MaterialTextureImage.h)、[MaterialMaskTextures.h](../include/MaterialMaskTextures.h)、[MaterialTexturePlan.h](../include/MaterialTexturePlan.h)、[material_texture_sampling.glsl](../shaders/include/material_texture_sampling.glsl) | 线性颜色/data 视图、ray cone/mip、normal 方差、cutoff 覆盖率、2–4 池及预算。 |
| 采样 / 诊断 / 发光权重 | [sampler.glsl](../shaders/include/sampler.glsl)、[common.cpp](../src/common.cpp)、[PathDiagnostics.h](../include/PathDiagnostics.h)、[EmissionTexturePower.h](../include/EmissionTexturePower.h)、[Scene.cpp](../src/Scene.cpp) | 固定维度、uint Sobol、seed、RR、异常位、UV/alpha 功率、HDR/非环境组概率。 |
| 性能 / 图像比较 | [render_benchmark.py](../tools/render_benchmark.py)、[render_compare.py](../tools/render_compare.py)、[make_render_sampling_fixtures.py](../tools/make_render_sampling_fixtures.py) | 生产与插桩分离、固定 spp/多种子/参考、原始线性误差、tile/compute、可再生夹具。 |
| 资源转换 | [convert_glslpt_scenes.py](../tools/convert_glslpt_scenes.py)、[resources/scenes](../resources/scenes) | 外部场景转换、55 个预设发现、派生灯光/贴图、来源信息。 |

## 验证入口

| 测试代码 | 覆盖内容 |
| --- | --- |
| [SceneTests.cpp](../tests/SceneTests.cpp)、[EditorTests.cpp](../tests/EditorTests.cpp) | 场景/包往返、组和根约束、材质隔离、资源共享、失败原子性、相机及速率。 |
| [TraversalTests.cpp](../tests/TraversalTests.cpp) | GPU 对照暴力求交、TLAS、同距/alpha/拾取、TBN/介质阴影；新增掠射法线、96 条长射线、island 边缘、共享顶点/边、真实缝隙及退化夹具。 |
| [LightingTests.cpp](../tests/LightingTests.cpp) | HDR、球/太阳盘、MIS 方差、delta、alpha 发光、均匀介质数值对照。 |
| [SceneUiRegression.cpp](../tests/SceneUiRegression.cpp) | 实际 UI 场景切换、保存/放弃/取消、失败恢复及预设发现。 |
| [WorkbenchUiRegression.cpp](../tests/WorkbenchUiRegression.cpp) | 树/视口选择、变换与撤销、零 BLAS 重建/几何上传、正式任务和 PNG/JPEG。 |
| [WorkspaceUiRegression.cpp](../tests/WorkspaceUiRegression.cpp) | 八页导航、旧灯光／环境兼容、渲染模式、文档/undo 不变、单一视口/线程、多相机/灯光/HDR、只读材质、构图不覆盖来源、任务快照隔离、等待任务管理与结果固定浏览、布局与搜索保留、跨页任务、输出元数据冻结、本地偏好、实际 1366×768 和冷启动。 |
| [PreviewPanelRegression.cpp](../tests/PreviewPanelRegression.cpp) | 视口预览入口、即时设置、正式预算隔离。 |
| [test_background_ui.py](../tests/test_background_ui.py)、[BackgroundTestSession.cpp](../src/BackgroundTestSession.cpp) | 私有桌面审计、真实 GL 图像及超时清理。 |
| [InteractionRegression.cpp](../tests/InteractionRegression.cpp) | 空场景、连续滚轮、后台 OIDN、版本/尺寸失效、UI 延迟诊断。 |
| [PreviewModeRegression.cpp](../tests/PreviewModeRegression.cpp)、[PresentationTests.cpp](../tests/PresentationTests.cpp) | 整图/分块来回切换、test_mis 黑屏回归、延迟消费和明确源 FBO。 |
| [RendererBatchTests.cpp](../tests/RendererBatchTests.cpp) | 批次上限/中断、完整快照、同 spp 像素一致、预算失效、停止采样后的显示刷新；另提供可复现的离屏 benchmark CLI。 |
| [RenderFoundationTests.cpp](../tests/RenderFoundationTests.cpp)、[RenderSecondBatchTests.cpp](../tests/RenderSecondBatchTests.cpp)、[RenderThirdBatchTests.cpp](../tests/RenderThirdBatchTests.cpp) | 三批数值/纹理/offset/AnyHit/采样/RR/光源概率/UV 功率；共用 LightingAudit 和 RenderEvidence。 |
| [RenderFourthFifthTests.cpp](../tests/RenderFourthFifthTests.cpp) | 初始嵌套/边界错配/精确接触、两侧 IOR、clearcoat 解析与互易性/连续材质矩阵、部分 tile Film、sRGB/HALF 原子失败、材质/边界变换/全量同步后结果与新建渲染器一致。 |
| [AntialiasingDenoiseTests.cpp](../tests/AntialiasingDenoiseTests.cpp)、[AaDenoiseUiRegression.cpp](../tests/AaDenoiseUiRegression.cpp) | 实际离屏 AA、compute、采样/二阶矩/特征；私有桌面持续运动及 preview/output RR 控件链路、PNG/JPEG/EXR 队列、历史重曝光/曲线/深度及查看设置保留。 |
| [CMakeLists.txt](../CMakeLists.txt)、[tests](../tests) | CTest 注册、Python 导入/转换契约、Qt 运行时路径。 |

当前独立构建 build/render-foundation 注册 34 项。2026-10-03 最终全量 34/34（1015.18 秒），在初始介质更新补修和全部测试夹具修正之后执行；初跑 33/34 与随后定向复测保留历史时点。命令、故障经过与边界见 [2026-10-03 第四、五批渲染验收](./render_batches_4_5_2026-10-03.md)。第四/五批夹具由 [make_render_quality_fixtures.py](../tools/make_render_quality_fixtures.py) 生成，[render_quality_evidence.py](../tools/render_quality_evidence.py) 串行执行 GPU 对照、运动序列和独立 EXR 解码。交付位于 output/render-fourth-fifth/2026-10-03。三批交付图像及 JSON 位于版本化 output/render-first-batch、output/render-second-batch、output/render-third-batch。原始线性大文件及临时日志仍位于未版本化 `build/`；可追溯的汇总与实际命令见 [工作台专题](./scene_workbench_v2.md) 和 [八页工作区验收](./workspace_ui.md)。旧 benchmark 副本不自动反映主代码，不把试验副本的结果当作正式版本验收。
