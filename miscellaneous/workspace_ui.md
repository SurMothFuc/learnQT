# 八页工作区与界面

界面基线为代码提交 `eb5f5cd`；2026-10-03 同步了 RR 控件、线性结果工具栏与 EXR 输出；当前渲染验收见 [2026-10-03 第四、五批渲染验收](./render_batches_4_5_2026-10-03.md)，前三批记录保留原范围。首页、场景、材质、照明、相机、渲染、资源、设置八页共享 EditorController、SceneDocument v2 与 GLWidget；采用 Qt 5 Widgets / Fusion 深蓝灰界面。验收汇总见 [2026-09-30 数据](./validation/workspace_ui_2026-09-30.json)。

## 页面职责与布局

| 页面 | 左侧 | 中央 | 右侧 | 底部与边界 |
| --- | --- | --- | --- | --- |
| 首页 | 无独立面板 | 新建、打开、浏览场景预设、真实最近项目 | 无独立面板 | 无；预设沿用打开场景流程。 |
| 场景 | 对象树与搜索 | 共享编辑视口 | 对象概览、变换、材质摘要及跳转 | 资源浏览器默认收起；对象树只表示对象和组织组。 |
| 材质 | 搜索、所选对象／全部场景筛选、选择使用者 | 真实场景预览 | 作用范围、基础参数、六类贴图、高级参数 | 无；节点示意已移出默认布局。 |
| 照明 | 场景灯光／HDR 环境标签，灯光列表或唯一 HDR 浏览区 | 共享场景预览 | sphere／sun 属性或环境开关、强度、旋转 | 无；未实现灯型和天空集中在可折叠功能边界。 |
| 相机 | 已保存相机列表，新建、重命名、删除 | 当前编辑相机视口 | 名称、位置、目标、垂直 FOV、定位所选 | 无；景深及物理镜头尚未实现。 |
| 渲染 | 按子模式组织 | 构图视口或选中任务结果 | 输出配置或只读任务快照 | 任务队列，构图默认收起、结果默认展开。 |
| 资源 | 分类与搜索 | 内置场景和当前文档引用资源卡片 | 来源、路径、引用、明确操作按钮 | 无；模型“追加导入”、场景“打开场景”、材质／纹理“定位使用者”。 |
| 设置 | 分类 | 偏好表单、布局、快捷键、吸附默认值、计算后端、自动导出目录 | 无独立面板 | 未实现设置以功能边界说明，不模拟成功。 |

场景页保留搜索、多选、分组、排序、复制、删除、隐藏、锁定、变换、吸附及撤销重做。详细材质参数只在材质页显示。材质浏览不改变绑定；只读原因、锁定状态与影响对象在属性区域显示，修改沿用局部作用域和共享材质按需隔离。

资源列表共用目录模型，各浏览区域保留自己的搜索条件；目录签名变化才重建。纹理使用真实图片缩略图，其他资源使用类型图标；资源浏览与属性编辑分开，不暗示已有通用材质库。

## 渲染两种模式

“构图与提交”中央显示输出画幅与临时草稿。右侧配置来源相机、比例与尺寸、采样、反弹、RR 起始深度、分块、降噪、曝光与映射、PNG／JPEG；提供保存为新相机与导出目录的设置跳转。模式栏固定显示“加入队列”和等待数量。

来源相机、临时草稿与任务相机快照分别管理：调整草稿不覆盖来源相机，保存为新相机产生文档命令；加入队列冻结当时的文档、相机与输出配置，后续编辑不追溯修改任务。

“结果与队列”中央显示选中任务结果，支持适应窗口、1:1、缩放、平移和导出。持有浮点结果时还提供曝光 -24～24 EV、旧曲线/ACES 近似/线性裁切、raw/denoised、beauty/guide normal/albedo/中心深度/亮度方差/有效计数。每个会话历史结果保留查看设置；无 Film 或无匹配降噪图时禁用对应控件。显示变化不重新采样。手动导出可选 HALF/FLOAT EXR，自动输出增加 FLOAT EXR。右侧只读展示相机快照、尺寸、采样与进度、反弹、分块、降噪、耗时、输出路径和错误。底部提供运行、暂停／继续、停止当前、停止队列，以及等待任务的重命名、排序、移除。

手动选择历史结果后，运行任务的更新不抢走查看目标。完成、停止、失败任务的统计冻结，编辑预览统计不覆盖元数据；结果页状态栏立即切换到查看任务。队列和结果只保留在当前会话，尚无跨会话恢复。

自动导出继续使用本机 `workspaceV4/autoExportPath`。运行队列前验证目录；派发时按现有命名及冲突避让规则生成路径，本轮不新增任务路径策略。

## 公共操作与布局

顶部工具栏只保留新建、打开、保存、导入、撤销、重做。根据用户反馈，重复的“渲染工作区、暂停、停止当前任务、停止队列”已移除；左侧导航进入渲染页，队列操作保留在底部面板及各页可访问的渲染菜单。其他页面按 F12 进入构图页，渲染页按 F12 加入队列。

对象工具、世界／局部轴、吸附和交互预览设置位于视口工具栏；正式输出参数只在渲染构图区域。预览和正式的 RR 起始深度独立，范围 0–64、默认 3；sampleSeed 在 render/output JSON 中保存，当前没有种子输入控件。性能与日志是公共诊断面板，默认收起，从状态栏展开，不长期占用场景属性区。

固定面板支持调整尺寸、隐藏和恢复，不支持自由浮动或重排。1600×900 逻辑窗口默认左栏约 240、右栏约 340；1366×768 使用约 220／320，长表单滚动并换行。用户主动展开紧凑窗口的面板后，普通 resize 不再反复隐藏。

切页保存布局、取消未完成变换，不重设 GLWidget 父级、不创建编辑场景副本、不增加 dirty 或 undo。实际视口尺寸变化仍重置预览累积。首页、资源、设置和结果模式隐藏编辑预览，队列继续运行。

## 状态、兼容与线程

- 页面描述表管理八页标题、面板和预览可见性。旧 Lights／Environment 导航及 `lights,environment` 截图参数映射到照明对应标签；新增 `lighting` 参数。
- 每页及渲染两模式使用稳定键，布局保存到 `workspaceV5/layout/`，Qt saveState 版本为 5；旧几何布局不直接恢复。模式与照明标签使用 V5；欢迎、最近文件、吸附、后端、目录等偏好继续保留 V4。搜索在会话内保留。
- 相机列表使用现有 `cameras`／`activeCameraId`，兼容 `camera` 取景；场景格式仍为 v2，render/output 新增向后兼容的 sampleSeed/rrMinDepth；缺字段默认 0/3，严格拒绝非整数、负 seed 和超范围深度，队列捕获对应设置。
- 编辑 Render Thread 消费版本化文档；独立 RenderQueueThread 根据任务快照构建运行时并串行出图。队列运行时允许继续编辑，预览临时使用光栅化；旧直接正式任务回归路径仍有编辑锁定，不能混同于队列。
- 测试与截图在 QApplication 前进入私有桌面，失败不回退前台；使用隔离偏好。审计和 framebuffer 图像共同验证后台运行。

实现入口见 [代码地图](./module_map.md)，线程边界见 [架构](./project_architecture.md)，任务流程见 [渲染流程](./render_flow.md)。

## 2026-10-03 前三批界面验收（历史时点）

最终 33/33 包含 aa_denoise_ui_regression：在私有桌面实际设置预览 RR 5、正式 RR 7，验证独立传递并恢复 3；持续相机/对象运动期间分别产生 4/146 个新显示帧。视口为 256×192 逻辑、512×384 物理，DPI 2；RR 表单和 framebuffer 已打开检查，审计 inputDesktopWindows=0。新拾取变体省去 beauty 材质/TBN，保留 ID/depth 和点 alpha。未验证用户活动桌面的真实焦点/遮挡，也未重复全部三档专用截图。证据见 [2026-10-03 三批渲染验收](./render_batches_2026-10-03.md)。

## 2026-09-30 验收

Windows、Qt 5.15.2、MSVC Release；独立构建 `build/workspace-v5`，最终代码 `eb5f5cd`。实际执行：

```powershell
cmake -S . -B build/workspace-v5 -DQt5_DIR=C:/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5
cmake --build build/workspace-v5 --config Release --parallel 2 -- /clp:ErrorsOnly /p:CL_MPCount=2
ctest --test-dir build/workspace-v5 -C Release --output-on-failure -j 1
ctest --test-dir build/workspace-v5 -C Release -R '^(workspace_pages_regression|render_queue_regression|workbench_ui_and_render_job|preview_panel_regression|background_ui_regression)$' --output-on-failure -j 1
# 删除重复顶栏入口后的最终复测
ctest --test-dir build/workspace-v5 -C Release -R '^(workspace_pages_regression|render_queue_regression)$' --output-on-failure -j 1
```

- 全量 **23/23，244.75 秒**，执行于结果页即时状态刷新修正前。
- 状态刷新修正后相关回归 **5/5，69.64 秒**。
- 删除重复顶栏入口后最终构建成功，页面与队列 **2/2，66.67 秒**；已打开最新渲染页截图检查。
- 100% 常规工作区回归、150% 与 200% 冷启动回归通过；200% 最终采用全屏一致倍率，实际紧凑窗口为 1366×768 逻辑尺寸。最初单屏倍率运行出现多屏 DPI 变化，不能当作布局通过。
- 专用截图：100% 为 1600×900 逻辑／物理；150% 为 1366×768 逻辑、2049×1152 物理；200% 为 1366×768 逻辑、2732×1536 物理。每档九张图覆盖八页与照明 HDR 标签，已检查主要操作、布局及真实 GL framebuffer。
- 专用三档截图生成于删除顶栏按钮之前；最终按钮移除后的截图另留在工作区／队列回归目录，未重复三档完整截图。
- 私有桌面审计均为 `inputDesktopWindows=0`。未验证活动桌面的真实遮挡、系统焦点路由，未新增画质或性能结论。

截图复现形式（本轮三个 case 分别实际执行）：

```powershell
$env:QT_SCREEN_SCALE_FACTORS = '1'
pwsh -NoProfile -File scripts/ui-capture.ps1 -Exe build/workspace-v5/Release/learnQT.exe -OutDir build/workspace-v5/captures -Scene resources/scenes/lantern.scene.json -Pages home,scene,material,lighting,camera,environment,render,resources,settings -Width 1366 -Height 768 -ScaleFactor 1.5 -Raster -SettleSec 1 -Tag dpi150
# 100%: Width 1600, Height 900, ScaleFactor 1, Tag dpi100
# 200%: Width 1366, Height 768, ScaleFactor 2, Tag dpi200
```

PNG、JSON、日志与隔离偏好位于未版本化 `build/workspace-v5/`。版本化汇总保存执行时点、最终源码哈希及覆盖边界。本次文档收尾核对记录、代码、链接和 diff，没有重跑代码测试。

## 历史与后续范围

[2026-09-09 数据](./validation/workspace_ui_2026-09-09.json)保留当时九页与表面求交阶段验收，不代表当前结构或本轮自动通过。求交、法线修复的边界见 [实现逻辑](./logic_overview.md)。节点材质、独立材质球、景深、物理天空、生产分解 AOV/Cryptomatte、完整颜色管理、透明背景、checkpoint 和持久队列仍为后续独立功能，见 [待办](./to-do.md)。

## 2026-10-03 第四/五批结果控件验收

最终完整 34/34（1015.18 秒）中的控件回归实际操作 PNG/JPEG/FLOAT EXR 三任务、-2 EV 重曝光、曲线与深度通道、历史切换后查看设置保留；raw beauty/spp 不变。视口 256×192 逻辑像素、DPI 2，相机/对象运动产生 3/128 个新显示帧，持续输入 2724/2504 ms。运动测试仍要求 3 个新帧，但允许首次运动冷启动最多 10 秒，属于活性检查，不能证明首帧/P99 延迟；私有桌面审计 inputDesktopWindows=0。用户活动桌面的焦点/遮挡与全部三档新控件截图未验证。完整构建、初跑/复测时点及已打开检查的图像见 [第四、五批验收](./render_batches_4_5_2026-10-03.md)。
