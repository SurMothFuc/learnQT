# learnQT 光追待办

更新日期：2026-09-05。纹理与场景系统 v1 已阶段性完成，其验收代码基线为 `6ee2661`，
当时通过 Release 构建与 8/8 CTest，并生成卧室、路灯的实际渲染截图。
功能、测试范围及复现命令见 [texture_scene_v1.md](./texture_scene_v1.md)。
本轮基础直接光采样修复基于 `9cc6eda` 完成，新增 GPU 数值回归后通过 9/9 CTest；实现及固定阶段验收记录见 [direct_lighting.md](./direct_lighting.md)。
直接光采样可按本页记录的支持范围阶段性结束，验证依据与剩余边界见下方“直接光采样阶段状态与验收”。
本轮同时对照本地 `D:/program/GLSL-PathTracer-master` 的渲染器源码，将相机、渲染选项及性能差距归入下列优先级；未运行参考项目做同场景画质或性能比较。

## 已完成

- [x] 优化类结构。
- [x] 修改分块渲染逻辑。
- [x] 优化不同参数更新操作。
- [x] 使用统一参数对象集中管理渲染参数。
- [x] 接入基础 OIDN 降噪流程。
- [x] 统一表面 NEE、BSDF 命中发光三角形、解析光源和环境光的 MIS；覆盖多太阳盘与 HDR 重叠、零选择概率光源仍可被 BSDF 命中的情况。
- [x] 同步光源选择概率：材质修改后重建光源 CDF，并回写 GPU 三角形中的 `lightSelectPdf`；选择概率使用实际 float CDF 区间宽度，场景测试检查两侧一致性。
- [x] 修正 HDR 采样分布与 PDF：缓存 x 边缘 CDF、y 条件 CDF 及实际区间概率；按 texel 立体角构建权重，texel 内连续采样并返回 per-steradian PDF。GPU 回归覆盖全白、全黑、非 2:1、单亮点及极区贴图，不再沿用旧 sampling LUT 的轴/中心坐标修补方案。
- [x] 补齐 sphere / sun 的显式采样和主射线/BSDF 命中贡献、MIS 及可见性；球光源参与阴影遮挡，保留向外发光的实体球约定。完成球光源 NEE-only / BSDF-only / MIS 同样本数能量与方差对照。
- [x] 统一三角形双面发光测度，并让发光面的 Mask/Blend 覆盖率参与 NEE；验证发光面背面贡献、采样/PDF 一致性以及 alpha 发光面的采样与命中能量。
- [x] 接入理想 delta 反射/折射、全反射和折射率匹配的直通事件；纯 delta 跳过 NEE，下一次发光命中权重为 1，混合材质保留连续波瓣的 NEE/MIS。
- [x] 接入均匀吸收/散射/发光介质、8 层 LIFO 体积栈、自由程与 HG 相函数采样、体积内 NEE/phase MIS，以及阴影路径的介质透射率；验证嵌套介质退出后恢复外层。仅覆盖正确嵌套的闭合边界，复杂边界与初始化仍列待办。
- [x] 表面与阴影射线接入按坐标尺度计算的几何法线偏移，透明边界复用该策略；极端尺度、掠射角和薄片的专项验证仍列 P0。
- [x] 修复模型归一化包围盒 x/y/z 混用和 `n1/n2/n3` 零法线检查漏项。
- [x] 贯通 UV0、导入切线/handedness、CPU 三角形、20 向量 GPU 编码和命中点 TBN；缺失切线时使用 Assimp fallback。
- [x] 接通模型纹理数组和元数据上传、RGBA/尺寸统一、mipmap 生成、释放/重载，以及颜色/线性数据区分。
- [x] 支持 baseColor、metallic/roughness 通道、normal、emissive、opacity 采样和 normal map Y 翻转；修复 Lantern 的图片垂直方向。
- [x] 支持 `Opaque / Mask / Blend`、alphaCutoff、贴图 alpha，主射线与阴影/NEE 复用 BVH alpha 筛选；介质透射率另由 `ShadowTransmittance()` 沿路径累乘。
- [x] 发光贴图参与 CPU 光源选择权重和 GPU 实际采样点辐射度；权重精细化留待后续。
- [x] 使用 Assimp 导入 OBJ/glTF/GLB/FBX、节点变换、外部和可解码内嵌图片；增加 FBX 内嵌 PNG 及 glTF tangent/sampler/transform 夹具。
- [x] 导入 glTF sampler / `KHR_texture_transform` 元数据，实现 UV 缩放、偏移、旋转、wrap 和放大过滤选择；完整 minification/LOD 尚未实现。
- [x] 增加模型选择/重新加载与互斥的 `--model` / `--scene`；模型导入形成可保存场景。
- [x] 实现版本化 SceneDocument、稳定资源/材质 ID、相机轨道恢复、全部现有渲染参数持久化、相对路径与原子保存。
- [x] 实现场景列表、打开/保存/另存为/重载/便携导出、未保存提示；后台构建候选场景、失败保留旧场景、帧边界提交和累计/降噪历史重置。
- [x] 便携包复制实际依赖闭包，支持同名资源、中文/空格路径、OBJ/MTL 和 glTF/bin；搬移后严格禁止原路径回退。
- [x] 卧室与路灯迁移为独立 JSON 预设，默认卧室；路灯加入 12×12 双三角形石材地面、6×6 UV 重复和 Poly Haven CC0 2K 贴图。
- [x] 解析 sphere / sun 光源进入场景文档，不再依靠预设名称硬编码；最大反弹次数、最大累计帧数进入参数快照及 UI。
- [x] 保留纹理/场景阶段的 8 项 CTest，并新增 `lighting_numerical_regression`，当前共 9 项全部通过；包括场景往返/便携包、实际 UI 切换/取消/失败恢复和 OpenGL 数值/像素回归。

## P0 正确性优先修复

- [ ] 完善 ray epsilon 的专项验证。当前已使用 `max(1e-5, 2e-6 * max(abs(position)))` 的几何法线偏移；仍需覆盖极端场景尺度、远离原点、掠射角、薄片、连续透明边界和散射点邻近表面的自交/漏光，不能把现有回归当作所有尺度均已验证。
- [ ] 修正 OIDN normal 辅助输入范围。`shaders/pathtrace.frag` 将 normal 写成 `[0, 1]` 编码，`src/renderer.cpp` 读回后直接交给 OIDN；应在交给 OIDN 前恢复到 `[-1, 1]` 的世界空间或视空间法线。
- [ ] 验证透明材质与体积路径的 OIDN normal/albedo 语义。当前跳过旧 Transparent 边界，并记录首个表面或体积散射点；仍需对折射、连续透明穿透和体积散射进行降噪前后对照，不能仅凭渲染能量回归认定辅助输入正确。

## P1 采样与光照质量

- [ ] 实现像素内抗锯齿采样。参考 `tile.glsl` 使用 tent filter 抖动主射线；当前 `shaders/pathtrace.frag` 的 AA 代码仍注释，主射线固定经过像素中心。应在全分辨率/低分辨率和分块模式下使用一致的全图像素坐标，并做同 spp 边缘与累积重置回归。
- [ ] 实现景深相机。参考相机已有 aperture / focal distance 和镜头采样；当前只有针孔投影与 FOV。新增光圈/对焦距离及持久化，验证零光圈退化为现有针孔相机。
- [ ] 补全环境照明参数。HDR NEE/MIS 已完成基础修复，后续增加环境强度、旋转和纯色环境光；采样方向、辐射度查询、PDF 回查以及历史重置必须同步。
- [ ] 增加背景与发光体可见性控制。支持独立背景色、透明背景和仅对相机隐藏环境/解析光源，同时保持间接照明。当前输出 alpha 固定为 1；参考项目的透明背景也不代表完整的材质/体积 alpha 合成，应单独定义本项目的输出语义。
- [ ] 增加色调映射选项。参考支持 tonemap 开关、ACES fitted / simple fit；当前固定亮度压缩后做 Gamma。保持线性 HDR 累积和降噪输入，新增选项只作用于显示/输出阶段。
- [ ] 按需求补齐专用矩形面光源与理想方向光。当前两个双面发光三角形可表达矩形光面，太阳盘具有有限角度；仍缺专用单面矩形光源及严格 delta 方向光的类型/采样约定。属于光源模型扩展，不把现有有限太阳盘误报为未实现方向照明。
- [ ] 细化发光贴图功率估计。当前按整张纹理平均值给每个三角形加权，可能给实际 UV 区域不发光的面分配过多概率；需按 UV 覆盖区域估计功率，并评估纹理域重要性采样。
- [ ] 改进 Sobol 序列扰动。当前 shader 里仍使用 Cranley-Patterson rotation，后续应改成每像素数字扰动、Owen scrambling 或数字异或，并保持维度和 bounce 的可复现映射。
- [ ] 增加 roughness mollification 作为可选降 firefly 策略。只在完成同 spp 噪声、能量和高光形状对比后启用，避免把粗糙度钳制当作默认正确性修复。

## P2 材质、几何与资源管线

- [ ] 按兼容性需求集成独立参考 MikkTSpace，并增加镜像 UV、硬边、退化 UV 与复杂 normal map 接缝对照；当前 authored tangent 与 Assimp fallback 已贯通。
- [ ] 实现完整 minFilter/mipmap/LOD 采样。当前虽保存 sampler 参数并生成 mipmap，shader 仍显式读取 LOD 0，仅按 magFilter 区分 nearest/linear；需补足缩小过滤与射线 footprint/LOD 策略。
- [ ] 支持多 UV 集与分层纹理。当前仅 UV0、每槽第 0 张；请求非零 UV 的槽会警告并跳过，不能误用 UV0。
- [ ] 按实际模型需求补齐 AO、height/displacement、clearcoat/transmission/sheen 等扩展贴图；读取部分标量不等于完整 glTF 扩展支持。
- [ ] 扩充 FBX 和 glTF 兼容性样本。现有 FBX 内嵌 PNG 小夹具不代表所有 DCC 导出的 FBX 都已验证；需要复杂嵌入材质、动画静态姿态、镜像/非均匀节点变换等专项场景。
- [ ] 改进纹理预算与诊断。当前数组单边上限 2048 且层数受硬件限制，超层数回退为常量；增加明确的 UI 资源预算/缺图反馈，并评估按需缩放或多数组方案。
- [ ] 统一模型法线处理策略：保留平滑法线、面法线、法线方向统一三个独立步骤，并明确硬边模型如何拆点或按面保持法线。
- [ ] 扩充 Blend、旧 Transparent、折射玻璃与介质交互的组合回归。基础 alpha 发光面和闭合均匀介质透射率已有数值验证，尚未覆盖复杂叠层与玻璃内照明；当前直线 NEE 阴影穿过旧 Transparent 边界，但不会忽略玻璃折射而直接穿过其 BSDF 表面。
- [ ] 扩展均匀介质边界与初始化。当前 type/density/color/anisotropy 和 8 层 LIFO 体积栈已实现；相机位于单个吸收体内有回归，但初始多层介质栈仍未完整构建，任意相交/裁剪/非闭合体积不受支持。补充栈容量、复杂嵌套和介质中放置实体物体的专项验证；非均匀体积另按需求规划。
- [ ] 支持相邻非真空介质的折射率比。当前体积栈记录消光/散射参数，表面折射仍按 `1 / material.IOR` 或 `material.IOR` 计算；它不等同于折射率栈，多层不同 IOR 的接触边界需补充介质两侧 IOR 及回归。
- [ ] 规划 BSSRDF / subsurface 的真实落地方式，先区分 Disney subsurface 近似、随机游走 BSSRDF 和体积散射三条路径，避免只留一个无法验证的参数。

## P3 性能与架构

- [ ] 先增加 GPU timer query 和关键阶段计时，再决定是否改 compute shader。至少分别测 pathtrace pass、history save、denoise readback、OIDN execute、screen composite。
- [ ] 给 BVH 遍历增加 profile 指标：`nodeVisits`、`leafVisits`、`triTests`、`maxStackDepth`，先定位是遍历层级、三角形测试还是纹理缓冲访存瓶颈。
- [ ] 优化 OIDN 读回路径。`Renderer::performDenoising()` 使用 PBO 后立即 `glMapBuffer()`，后面还有 `glFinish()`，实际仍可能同步阻塞；应改成延迟一帧 map、fence sync 或只在最终帧读回。
- [ ] 将场景拆分为 TLAS/BLAS，支持实例化和动态更新。参考项目多个实例共享 mesh BLAS，通过 transform 和顶层 BVH 放置模型；当前仍展开为世界空间三角形。支持单个实例变换仅更新实例数据与 TLAS，验证重复模型的显存、构建时间和遍历成本。
- [ ] 分离 GPU 索引几何、材质表与实例变换表。参考项目分别存储顶点/法线/索引、材质和 transform；当前 20 向量三角形编码重复携带材质，材质编辑后重新上传整份三角形缓冲。与 TLAS/BLAS 规划协调，但先分别测量上传量和访存瓶颈。
- [ ] 增加有距离上限的阴影 AnyHit 快速路径。无介质的普通遮挡查询可参考 `AnyHit(ray, maxDist)`，命中遮挡即退出；保留 Mask/Blend 筛选、解析球光源遮挡和目标光源排除。需要逐段透射率的路径继续使用 `ShadowTransmittance()`，不能用二值遮挡替代体积积分。
- [ ] 后续按需增加对象树、对象变换和局部材质编辑器。首版场景管理已完成，当前实例矩阵可持久化，但运行时仍展开三角形，材质 UI 仍统一覆盖常量。
- [ ] 优化首次启动体验。运行时切换/重载/导入已后台化，首次窗口构造仍同步加载默认场景；需评估启动页、取消加载和更细粒度进度。
- [ ] 优化三角形和 BVH 数据布局。完成 profile 后再评估 AoS/SoA、节点压缩、三角形属性拆分、纹理缓冲访问顺序等方案。
- [ ] 将降噪部分进一步模块化，明确实时预览降噪、最终帧降噪、辅助 buffer 生成和 OIDN filter 生命周期的边界。
- [ ] 完善 preview / final 两种渲染路径。当前已有低分辨率与分块累积，但共用 shader 和反弹设置；参考项目交互预览使用独立 shader、较低反弹深度，并在正式结果可用后切换。评估独立预算与历史管理，预览优先响应和低同步成本，最终模式优先累计质量。
- [ ] 支持独立于窗口的渲染分辨率。当前正常渲染尺寸跟随视口，保存图片使用 `grabFramebuffer()`；已有 PNG/JPEG 保存功能，缺的是独立输出尺寸与对应的渲染结果导出。验证窗口缩放、预览与最终输出之间的尺寸/累计语义。
- [ ] 开放已有算法的控制参数：RR 开关及 `RRDepth`、降噪间隔、必要时独立 tile 宽/高。当前 RR 从第 3 次散射后启用、常规降噪间隔为 100 个完整累计轮次；已有 `maxBounces` 与 `maxRenderFrames`（0 为无限），不再把累计上限列为未实现。新增选项进入 `RenderParams::Snapshot` 或场景配置，并明确同 spp 验收计数。
- [ ] 建立 shader feature define 管理。按 `lights/env/alpha/medium/volume MIS/normal map/tonemap` 等功能组合生成 shader，而不是把所有路径硬塞进单一路径；每个 define 要有默认值、触发条件和回归场景。
- [ ] 评估 Sobol 预计算收益。先确认当前随机数生成在 CPU 上传 uniform 还是 shader 使用上是否真是瓶颈，再决定是否预计算或改为纹理/SSBO。

## 直接光采样阶段状态与验收

- 阶段判断：本轮概率同步、HDR 分布/PDF、解析光源两条采样路径及可见性、透明发光面、delta 和均匀介质直接光采样已有实现与自动验证，可以阶段性结束。这里的“两条采样路径”指 NEE 与 BSDF/phase 命中贡献，不改变球光源向外发光的约定。
- 支持边界：介质限均匀、闭合且正确嵌套的边界；体积栈最多 8 层，阴影遍历最多 128 层。复杂相交体积、相机初始多层介质、相邻介质 IOR 和穿过折射玻璃的复杂光路保留为后续任务。
- 2026-09-05 Release 构建及 9/9 CTest 通过，已核对实际 GPU 数值与场景回归记录；命令、测试范围和误差示例统一记录在 [direct_lighting.md](./direct_lighting.md)。文档收尾期间代码未变，未重复运行相同构建/测试。
- 本线程已查看卧室/路灯 Release 回归截图。尚未完成更广泛的复杂玻璃、多光源与体积组合场景的同 spp 噪声/firefly 比较；OIDN 法线范围及辅助输入语义也仍保留 P0，不能用采样测试替代降噪验收。
- 后续建议顺序：像素抗锯齿 → 环境/背景/色调映射选项 → 景深；性能改动先 profile，再决定 AnyHit、GPU 数据拆分和 TLAS/BLAS 的顺序。

## 验证要求

- 代码项实现后，先跑 `cmake --build build --config Release` 和 `ctest --test-dir build -C Release --output-on-failure`，保留现有 9 项测试。
- 已有卧室/路灯截图与像素检查是回归基线，不等于采样算法验证；保留本轮数值测试，并继续扩充 Veach 多光源、粗糙玻璃折射、环境贴图和复杂介质组合的定量对比场景。
- `--regression-frames` 统计展示事件，不等于严格固定 spp；同 spp 比较应使用明确的完整累计/采样计数。
- 采样类改动必须对比同样 spp 下的噪声、能量守恒和 firefly 情况，不能只看单张图是否变亮。
- 性能类改动必须先提交 profile 数据，再改数据布局或 shader 架构；避免在未定位瓶颈前直接重写渲染路径。
