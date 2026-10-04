# 纹理与场景 v1 兼容性

本文保留 v1 的资产、纹理、路径和便携包约定及历史验收。当前工作台已升级到 v2，读取 v1 后在内存迁移，保存写 v2；操作和格式增量见 [scene_workbench_v2.md](./scene_workbench_v2.md)。历史 `6ee2661` / 8 项和直接光阶段的 9 项测试不代表新代码自动通过。

## 已实现的纹理链路

| 环节 | 当前行为 | 对应实现 |
| --- | --- | --- |
| 模型导入 | Assimp 读取 OBJ、glTF/GLB、FBX；处理节点变换、外部图片与可解码的内嵌图片 | `src/Mesh.cpp` |
| 几何属性 | UV0、顶点法线、导入切线及 handedness；无 authored tangent 时使用 Assimp fallback | `include/Mesh.h`, `src/Mesh.cpp` |
| 编码 | 当前使用几何 11 vec4、材质 10 vec4、实例 9 vec4 分表；旧 20 vec4 布局仅在兼容/数值测试路径保留 | `SceneEncoding.cpp`, `scene_access.glsl` |
| 纹理资源 | `GL_TEXTURE_2D_ARRAY` 加 sampler/UV 元数据 TBO；颜色/数据视图按尺寸与格式分为 2–4 池，线性颜色 mip、默认 512 MiB 预算与降尺寸报告，重载和释放资源 | `Renderer::uploadMaterialTextures()` |
| 材质采样 | base color、metallic、roughness、normal、emissive、opacity；颜色贴图转线性，标量贴图按通道读取 | `shaders/include/bvh_material.glsl` |
| 法线贴图 | TBN 和切线 handedness，`normalScale`、`normalMapFlipY`；支持场景文件中的 Y 方向约定 | `ApplyNormalMap()` |
| UV / sampler | 读取 Assimp 提供的 glTF sampler、`KHR_texture_transform`，保存缩放、偏移、旋转、wrap 和 filter 元数据 | `TextureAsset`, `TransformMaterialUV()` |
| Alpha | `Opaque / Mask / Blend`，保留旧 `Transparent`；base color alpha、opacity、cutoff 进入 BVH 命中筛选，阴影复用该筛选并沿介质段计算透射率 | `RejectAlphaIntersection()`, `ShadowTransmittance()` |
| 发光 | CPU 以世界面积、emissive 常量及三角形 UV/alpha 区域功率估计建选择分布；GPU 在实际采样点读取发光贴图，乘 Mask/Blend 覆盖率并用于双面发光/MIS；材质修改同步更新两侧选择概率 | `Scene::applyEditorDocument()`, `Scene::buildLightData()`, `light_sampling.glsl` |

`Renderer::baseColorTex` 仍是 OIDN 的 albedo 辅助输出，不是导入贴图；
导入贴图使用 `materialTextureArray` / `materialTextureExtraArrays` 和 `materialTextureInfoTexture`，同一源图片可建颜色与数据视图。
QImage 上传时进行垂直翻转，与 Assimp 导入后的 UV 约定对齐；Lantern 发光区有关键像素回归。

## 场景文件与入口

- 无参数建立空文档并默认显示欢迎首页，可在设置中关闭欢迎页；卧室预设：[bedroom.scene.json](../resources/scenes/bedroom.scene.json)。
- 路灯场景：[lantern.scene.json](../resources/scenes/lantern.scene.json)。
- 两者均通过 `Scene::prepareScene()` / `buildDocument()` 加载，不按预设名称分派硬编码构建函数。
- 首页与资源页提供场景预设，场景页对象树仍保留场景列表；文件菜单提供打开、保存、另存为、导出便携包。成功打开/保存的文件加入本机最近项目，可再次打开文件以重载。八页布局见 [工作区专题](./workspace_ui.md)。
- 工作台导入会追加模型并保留源尺寸、位置及节点变换；有单独指定缩放入口。
  CLI `--model` 保留独立场景适配能力；加载既有文档不重新按全场景包围盒缩放。

### v1 JSON 字段

| 字段 | 保存内容 |
| --- | --- |
| `version`, `name` | 输入格式版本 1；当前保存格式为 2、场景名 |
| `models` | 稳定 ID、模型路径、行主序 4×4 仿射矩阵、平滑/归一化选项、材质绑定、依赖别名 |
| `materials` | 稳定 ID、标量 PBR/alpha/介质参数、纹理槽到纹理 ID 的引用 |
| `textures` | 稳定 ID、外部图片路径或模型 ID + 内嵌 key、UV 变换和 sampler 参数 |
| `lights` | 稳定 ID、`sphere` / `sun` 类型、位置或方向、半径及 radiance |
| `hdr` | 环境 HDR 路径 |
| `camera` | `position`、`target`、`up`、垂直视场角 `fov`（度） |
| `render` | `denoise`, `renderLow`, `useTileRendering`, `tileSize`, `useEnvironmentMap`, `maxBounces`, `maxRenderFrames`, `sampleSeed`, `rrMinDepth`；v2 还含 AA/降噪模式等 |
| `portable`, `credits` | 便携包边界标记和资源来源说明 |

不保存 GPU 纹理编号、三角形/BVH 缓存、累计帧或降噪历史。
相机恢复会重算轨道半径与角度，并清除按键状态；默认垂直 FOV 约 53.130102°，保持原取景。

`Scene::loadScene()`、`saveScene()`、`exportScenePackage()` 是运行时接口，
`SceneDocument` 负责版本验证、路径重定位、原子写入和包资源管理。

### 保存与后台切换

普通保存把资源路径重算为相对于目标 JSON 的路径，跨盘时允许绝对路径。
`QSaveFile` 禁用直接写入回退，提交失败不覆盖旧文件。

首次场景 CPU 准备仍在窗口构造阶段同步执行，首页的 GL 初始化则推迟到首次显示编辑视口；运行中的切换/重载/模型导入使用
`QThread::create()` 后台构建独立候选 `Scene`，包含模型、图片、BVH、灯光和 HDR cache。
期间显示加载阶段并禁用冲突操作；CPU 构建失败保留原场景和未保存状态。

成功时 UI 安装候选文档并向 Render Thread 提交带版本号的候选场景。渲染线程在 GPU 批次安全边界采用候选，更新资源、清除旧累积和降噪结果；不使用 UI 等待整帧的帧互斥锁。恢复控件抑制信号，避免误写新材质。

相机、模型、材质和场景持久设置通过 undo 命令影响 dirty 状态；本机布局及应用偏好不进入场景 undo。打开/关闭前可保存、放弃或取消。追加导入不丢弃当前场景。对象树、局部材质隔离和变换编辑现已实现；选择、展开及布局不标记未保存，也不主动清空采样；布局引起实际视口尺寸改变时仍重置预览累积。

### 便携导出

目标必须是新建或空目录，输出 `scene.scene.json` 和 `assets/`。
`SceneAssets` 为 Assimp 的文件读取和外部贴图提供统一解析；依赖表记录模型实际读取的
MTL、bin、图片等文件，不递归复制无关目录。HDR 和场景直接引用的贴图同样纳入包内。

文件按 SHA-256 分目录避免同名冲突；模型原文不改写，内部相对或绝对引用通过别名
映射至包内文件。别名键可能保留原作者路径字符串，但不是包外回退地址。
`portable: true` 会约束实际资源解析不能逃出包目录。

导出先在目标旁的临时目录复制资源，再完整重导入验证，成功后才发布目录。
导出不改变当前保存位置，也不清除未保存标记。

## 两个预设

| 场景 | 内容 | 纹理 v1 基线 Release 结果 |
| --- | --- | --- |
| 卧室 | 原卧室网格、木板/墙纸/地板/装饰画 4 张贴图、原材质、两块发光面、附加球形灯、HDR 和相机 | 1,491,774 个三角形；截图 `build/bedroom_scene_release.png` |
| 路灯 | Lantern GLB 的 PBR/内嵌贴图、原缩放/相机/HDR/灯光，加独立石材平面 | 5,396 个三角形；截图 `build/lantern_scene_release.png` |

地面使用真正的两个三角形：[plane.obj](../resources/models/plane.obj)，完整 UV0 与朝上法线。
平面为 12×12，水平居中，位于模型最低点下方 0.001，UV 重复 6×6，metallic/transmission 均为 0，
不启用位移。使用 Poly Haven Stone Floor 的 2K Base Color、OpenGL Normal 和 Roughness；
授权及下载哈希见 [SOURCE.md](../resources/textures/stone_floor/SOURCE.md)。
原 `quad.obj` 是立方体，仅继续用于卧室原有发光几何，不作为路灯贴图地面。

## 验证与复现

当前介质/结果协议与 34 项测试覆盖见 [2026-10-03 第四、五批渲染验收](./render_batches_4_5_2026-10-03.md)；前三批纹理/采样改进见 [三批验收](./render_batches_2026-10-03.md)；以下 2026-09 的 8/9 项为历史范围。

2026-09-04，代码基线 `6ee2661` 已通过 Release 构建与 8/8 CTest。
这是固定基线的验收记录，不表示后来代码修改自动获得同样保证。
2026-09-05 的直接光采样阶段保留这 8 项并新增 `lighting_numerical_regression`，9/9 通过；实际 GPU 数值结果及本轮截图位置见 [direct_lighting.md](./direct_lighting.md)。

| 测试 | 验证内容 |
| --- | --- |
| `unit_middle_mouse_orbit_target` | 轨道观察目标与平移契约 |
| `unit_finite_analytic_lights` | 有限球形/太阳盘光源代码契约 |
| `unit_texture_rendering_support` | CPU/GPU 编码与采样链路、测试资源契约 |
| `gpu_render_regression` | 实际 OpenGL 图像有效性及 Lantern 黄色发光面关键区域 |
| `import_fbx_embedded_texture` | 含真实内嵌 PNG 的 FBX 小型夹具 |
| `import_gltf_tangent_sampler_transform` | authored tangent、sampler、UV transform 导入 |
| `scene_roundtrip_and_package` | 状态往返、中文/空格/跨盘/另存为、损坏/未知版本/缺资源/写入失败、地面 UV/接地、包搬移、OBJ/MTL/外部 glTF/bin、同名图片与禁止原路径回退 |
| `scene_ui_switch_regression` | 实际 Qt 界面的卧室→路灯→卧室、Save/Discard/Cancel、关闭取消、加载失败恢复、材质保存及往返图像差异 |

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

# 单独运行程序时，按本机 Qt 安装位置配置 DLL 搜索路径。
$env:PATH = 'C:\Qt\5.15.2\msvc2019_64\bin;' + $env:PATH
build\Release\learnQT.exe --scene resources/scenes/lantern.scene.json
build\Release\learnQT.exe --model path/to/model.glb --save-scene my.scene.json
build\Release\learnQT.exe --scene my.scene.json --export-scene-package new-empty-folder
build\Release\learnQT.exe --scene my.scene.json --validate-scene
```

`--scene` 与 `--model` 互斥。截图使用 `--render-regression <output.png>`，
可附加 `--regression-frames 512 --regression-denoise`；路灯可加 `--regression-lantern`。
当前 `--regression-frames` 等待指定完整 spp；开启回归降噪时还等待匹配的结果。历史阶段曾统计展示事件，不能将其旧截图误作同 spp 数值对照。
生成截图和临时测试包位于 `build/`，不是版本化资源。

## 明确保留的边界

- 只保存 UV0；请求 UV1/UV2 的材质槽会警告并跳过，不错误套用 UV0。每种槽只读第 0 张纹理。
- 已导入 authored tangent，但未集成独立的参考 MikkTSpace 生成器；复杂模型接缝仍需专项验证。
- ray cone 驱动 mip/minFilter/三线性；颜色先解码再过滤，normal 方差近似增加粗糙度。尚无 ray differentials、EWA 或斜视各向异性过滤。
- 非发光 Mask 的单个变化 alpha 源按 cutoff 保持 mip 覆盖率；两个同时变化的源仍回退点语义，发光 Mask 保留点采样以匹配 NEE。
- GPU 池单边上限仍为 2048、受硬件层数限制；超层数回退常量。预算会缩小池，LEARNQT_TEXTURE_BUDGET_MB 可设置开发预算；尚无完整场景预算/流式加载或重资产广泛验收。
- AO、height/displacement、clearcoat/transmission/sheen 等扩展贴图和多层纹理尚未贯通。
  读取部分扩展的标量不代表完整支持该 glTF 扩展。
- FBX 已有内嵌图片小型夹具通过，不代表任意 DCC 导出的复杂 FBX 都已验收。
- Blend 使用随机透过；基础 alpha 发光面、旧 Transparent 包围的均匀吸收/散射介质及嵌套透射率已在直接光阶段验证，新增 Blend 面 + 闭合吸收玻璃 + 贴图发光的组合回归及 delta/粗糙透射 OIDN 实图比较；更广泛真实 Mask/Blend/体积资产仍待验收。
- 发光选择已用 16×16 均匀面积 UV/alpha 功率估计，带保守支撑下限；估计近似只影响选灯概率。尚未实现发光纹理内部重要性分布及近距离立体角采样。
- 当前已实现局部 BLAS/实例 TLAS、动态变换和局部材质编辑；v1 文档通过迁移进入同一运行时。
- 闭合朝外边界支持初始正确嵌套身份/IOR 和精确配对接触；旧 Transparent fog IOR 保持 1，transmission 材质使用已有 IOR。Mask/Blend/open 边界不纳入具名初始化；未升级 v2 场景文件版本。
- 会话 RenderResult 和 EXR 元数据独立于场景 JSON，不保存累积到场景文件。输出支持 PNG/JPEG 和线性 HALF/FLOAT EXR；normal/albedo 为 guide、depth.center 为中心几何距离，不能当作通用生产 AOV。
- HDR PDF 一致性、delta 和基础 volume MIS 的实现及验证见 [direct_lighting.md](./direct_lighting.md)；OIDN 法线范围已修正；复杂介质与其他剩余项见 [to-do.md](./to-do.md)。
