# 纹理与场景系统 v1

更新日期：2026-09-04。实现基线：`6ee2661`。

本阶段已收尾：常用 PBR 贴图从导入、CPU 编码、GPU 上传到命中点采样的链路已贯通，
并接入场景保存、切换和便携导出。后续以具体模型的兼容性问题为驱动，不把 v1
等同于所有格式、所有 glTF 扩展或完整材质编辑器的支持。

## 已实现的纹理链路

| 环节 | 当前行为 | 对应实现 |
| --- | --- | --- |
| 模型导入 | Assimp 读取 OBJ、glTF/GLB、FBX；处理节点变换、外部图片与可解码的内嵌图片 | `src/Mesh.cpp` |
| 几何属性 | UV0、顶点法线、导入切线及 handedness；无 authored tangent 时使用 Assimp fallback | `include/Mesh.h`, `src/Mesh.cpp` |
| 编码 | `Triangle_encoded` 为 20 个 `QVector4D`，含 UV、材质纹理索引、alpha/normal 参数和切线 | `include/Scene.h`, `Scene::DataEncode()` |
| 纹理资源 | `GL_TEXTURE_2D_ARRAY` 加 sampler/UV 元数据 TBO；统一 RGBA 像素和尺寸，生成 mipmap，重载和释放资源 | `Renderer::uploadMaterialTextures()` |
| 材质采样 | base color、metallic、roughness、normal、emissive、opacity；颜色贴图转线性，标量贴图按通道读取 | `shaders/include/bvh_material.glsl` |
| 法线贴图 | TBN 和切线 handedness，`normalScale`、`normalMapFlipY`；支持场景文件中的 Y 方向约定 | `ApplyNormalMap()` |
| UV / sampler | 读取 Assimp 提供的 glTF sampler、`KHR_texture_transform`，保存缩放、偏移、旋转、wrap 和 filter 元数据 | `TextureAsset`, `TransformMaterialUV()` |
| Alpha | `Opaque / Mask / Blend`，保留旧 `Transparent`；base color alpha、opacity、cutoff 进入 BVH 命中筛选，阴影/NEE 复用该路径 | `RejectAlphaIntersection()`, `IsDirectLightVisible()` |
| 发光 | CPU 以面积、emissive 常量和贴图平均值建选择分布；GPU 在实际采样点读取发光贴图并用于 MIS | `Scene::buildLightData()`, `light_sampling.glsl` |

`Renderer::baseColorTex` 仍是 OIDN 的 albedo 辅助输出，不是导入贴图；
导入贴图使用 `materialTextureArray` / `materialTextureInfoTexture`。
QImage 上传时进行垂直翻转，与 Assimp 导入后的 UV 约定对齐；Lantern 发光区有关键像素回归。

## 场景文件与入口

- 默认场景：[bedroom.scene.json](../resources/scenes/bedroom.scene.json)。
- 路灯场景：[lantern.scene.json](../resources/scenes/lantern.scene.json)。
- 两者均通过 `Scene::prepareScene()` / `buildDocument()` 加载，不按预设名称分派硬编码构建函数。
- 侧栏提供场景列表、打开、保存、另存为、重新加载、导出便携包；外部场景加入本次会话列表。
- 导入模型会替换当前内容并形成未保存的新场景。首次导入会居中并把最大边缩放到 3，
  然后保存实际矩阵；加载场景文件不会再次按全场景包围盒缩放。

### v1 JSON 字段

| 字段 | 保存内容 |
| --- | --- |
| `version`, `name` | 格式版本（当前为 1）、场景名 |
| `models` | 稳定 ID、模型路径、行主序 4×4 仿射矩阵、平滑/归一化选项、材质绑定、依赖别名 |
| `materials` | 稳定 ID、标量 PBR/alpha/介质参数、纹理槽到纹理 ID 的引用 |
| `textures` | 稳定 ID、外部图片路径或模型 ID + 内嵌 key、UV 变换和 sampler 参数 |
| `lights` | 稳定 ID、`sphere` / `sun` 类型、位置或方向、半径及 radiance |
| `hdr` | 环境 HDR 路径 |
| `camera` | `position`、`target`、`up`、垂直视场角 `fov`（度） |
| `render` | `denoise`, `renderLow`, `useTileRendering`, `tileSize`, `useEnvironmentMap`, `maxBounces`, `maxRenderFrames` |
| `portable`, `credits` | 便携包边界标记和资源来源说明 |

不保存 GPU 纹理编号、三角形/BVH 缓存、累计帧或降噪历史。
相机恢复会重算轨道半径与角度，并清除按键状态；默认垂直 FOV 约 53.130102°，保持原取景。

`Scene::loadScene()`、`saveScene()`、`exportScenePackage()` 是运行时接口，
`SceneDocument` 负责版本验证、路径重定位、原子写入和包资源管理。

### 保存与后台切换

普通保存把资源路径重算为相对于目标 JSON 的路径，跨盘时允许绝对路径。
`QSaveFile` 禁用直接写入回退，提交失败不覆盖旧文件。

首次启动仍在窗口构造阶段同步准备场景；运行中的切换/重载/模型导入使用
`QThread::create()` 后台构建独立候选 `Scene`，包含模型、图片、BVH、灯光和 HDR cache。
期间显示加载阶段并禁用冲突操作；CPU 构建失败保留原场景和未保存状态。

成功时主线程调用 `GLWidget::replaceScene()`，通过 `RenderThread::replaceScene()`
取得帧互斥锁及 `param_mutex` 后替换场景、应用参数并标记 dirty。
渲染线程下一帧上传新资源，重置累计帧和降噪历史。恢复控件时使用 `QSignalBlocker`，
避免材质滑块的信号意外覆盖刚加载的材质。

相机、材质及持久渲染设置修改后标题显示 `*`。切换、重载、模型导入、关闭前可保存、
放弃或取消。鼠标按下/松开产生的临时 `renderLow` 切换不单独标记文档 dirty。
当前材质 UI 仍统一覆盖全场景常量，不包含对象树、变换编辑器或局部材质选择器。

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

| 场景 | 内容 | 最近一次 Release 结果 |
| --- | --- | --- |
| 卧室 | 原卧室网格、木板/墙纸/地板/装饰画 4 张贴图、原材质、两块发光面、附加球形灯、HDR 和相机 | 1,491,774 个三角形；截图 `build/bedroom_scene_release.png` |
| 路灯 | Lantern GLB 的 PBR/内嵌贴图、原缩放/相机/HDR/灯光，加独立石材平面 | 5,396 个三角形；截图 `build/lantern_scene_release.png` |

地面使用真正的两个三角形：[plane.obj](../resources/models/plane.obj)，完整 UV0 与朝上法线。
平面为 12×12，水平居中，位于模型最低点下方 0.001，UV 重复 6×6，metallic/transmission 均为 0，
不启用位移。使用 Poly Haven Stone Floor 的 2K Base Color、OpenGL Normal 和 Roughness；
授权及下载哈希见 [SOURCE.md](../resources/textures/stone_floor/SOURCE.md)。
原 `quad.obj` 是立方体，仅继续用于卧室原有发光几何，不作为路灯贴图地面。

## 验证与复现

2026-09-04，代码基线 `6ee2661` 已通过 Release 构建与 8/8 CTest。
这是固定基线的验收记录，不表示后来代码修改自动获得同样保证。

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
这里的 frames 统计展示事件，不等于严格固定的完整 spp，不能替代采样算法的同 spp 对比。
生成截图和临时测试包位于 `build/`，不是版本化资源。

## 明确保留的边界

- 只保存 UV0；请求 UV1/UV2 的材质槽会警告并跳过，不错误套用 UV0。每种槽只读第 0 张纹理。
- 已导入 authored tangent，但未集成独立的参考 MikkTSpace 生成器；复杂模型接缝仍需专项验证。
- 已保存 min/mag filter 元数据并生成 mipmap，但当前 shader 显式采样 LOD 0，
  仅按 magFilter 选择 nearest/linear；尚无 ray differentials、自动 LOD 或完整 minification 行为。
- GPU 数组单边上限当前为 2048，并受硬件层数限制；超出层数的贴图回退为常量。
- AO、height/displacement、clearcoat/transmission/sheen 等扩展贴图和多层纹理尚未贯通。
  读取部分扩展的标量不代表完整支持该 glTF 扩展。
- FBX 已有内嵌图片小型夹具通过，不代表任意 DCC 导出的复杂 FBX 都已验收。
- Blend 使用随机透过；旧 Transparent 与体积路径仍需单独的透射率/辅助特征验证。
- 发光贴图的选择权重使用整张图片平均值，尚未做按三角形 UV 覆盖区域的功率估计或重要性分布。
- 场景保存了实例矩阵，但运行时仍预变换并展开三角形，未实现 TLAS/BLAS、动态对象编辑。
- OIDN 法线输入范围、HDR PDF 一致性、delta/volume MIS 等渲染正确性项仍见 [待办.md](./待办.md)，不因纹理 v1 验收而自动完成。
