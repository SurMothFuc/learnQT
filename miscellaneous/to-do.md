# learnQT 渲染与工作台待办

本页于 2026-10-03 按代码提交 7558eb0 及三批实际验收整理，保留 Blender/Cycles 与 pbrt 的渲染改进分析。优先级表示建议实施顺序，不表示已确认的缺陷严重程度；评估项不承诺采用某个算法或获得固定提速。

最终 Release 构建及当前完整 CTest 33/33 通过（802.85 秒）；三批指定范围已实现并保存前后图。部分大条目拆为完成子项和未完成扩展，不将夹具通过泛化为普遍物理正确性或固定性能收益。实际命令、数据与范围见 [2026-10-03 三批渲染验收](./render_batches_2026-10-03.md)。历史记录保留原日期，不冒充本轮测试。

## 阅读与状态约定

- [项目文档入口](./project_docs_index.md)统一本项目术语；当前工作区说明见[八页工作区](./workspace_ui.md)，运行时与旧性能记录见[场景工作台 v2](./scene_workbench_v2.md)。
- [纹理与场景 v1](./texture_scene_v1.md)保留资产/兼容边界；[直接光采样](./direct_lighting.md)保留 PDF、MIS、均匀介质及历史数值验收。
- 已完成区的勾选依据既有实现与历史验收，不表示本轮重新测试通过。当前回归覆盖与未验收扩展单列，避免把实现状态与测试状态混为一谈。
- 后续任务采用稳定编号，便于引用和合并。相同能力只在主要所属章节保留一项；相关任务通过编号说明依赖。
- P0：正确性与诊断基线；P1：通用画质、采样效率和输出；P2：能力与资源架构扩展；专项：困难光路；长期：后端或渲染模型扩展。
- 保留固定 spp、关闭降噪和原始线性结果作为对照；有偏近似、深度截断与显示调整应明确标识，不能作为物理正确性修复。

## 已完成：既有实现与历史验收

### 渲染、求交与光照

- [x] 集中管理渲染参数快照、版本化 dirty 更新与批次安全边界；统一参数对象，避免 UI 等待整帧的大锁。
- [x] 实现分块渲染及按 GPU 耗时估算的 1–16 tile 批次、每批绑定与按显示频率合成；保持完整轮次、暂停/停止和控制中断边界。
- [x] 统一表面 NEE、BSDF 命中发光三角形/解析光源/环境的 MIS；覆盖重叠太阳盘、HDR 和零选择概率光源仍可被 BSDF 命中的情况。
- [x] 材质/实例变化同步光源 CDF、世界面积与 GPU surface/PDF；选择概率使用实际 float CDF 区间宽度。
- [x] 修正 HDR texel 立体角分布、连续采样与 per-steradian PDF 回查；覆盖白/黑、非 2:1、单亮点与极区贴图。
- [x] 实现 sphere / 有限太阳盘的 NEE、主射线/BSDF 命中、MIS 和可见性；球灯参与阴影遮挡，保持实体球向外发光约定。
- [x] 统一双面三角形发光测度，Mask/Blend 覆盖率进入 NEE 与命中贡献；发光贴图进入功率估计及实际采样点辐射度。
- [x] 实现理想 delta 反射/折射、全反射、IOR 匹配直通与混合连续波瓣；离散事件使用概率质量，纯 delta 跳过普通 NEE。
- [x] 实现均匀吸收/散射/发光介质、自由程、HG 相函数、体积 NEE/MIS、分段阴影透射率及最多 8 层 LIFO 嵌套恢复；限正确嵌套闭合边界。
- [x] 实现几何法线尺度偏移、共用沿射线投影的三角形求交、表面命中点重建和无效着色法线回退；历史 GPU 夹具覆盖共享边/顶点、长射线、镜像掠射、真实缝隙、平行与退化情况。
- [x] 修复模型归一化包围盒 x/y/z 混用及零法线漏检；极端尺度与更复杂模型仍需 R-C03/R-A04 验证。
- [x] 实现共享局部 BLAS、实例 TLAS、变换 refit/重建及加权 SAH；历史变换验证中已有 BLAS 重建与几何上传均为零。
- [x] GPU 几何、材质、实例、surface 引用/PDF 与加速表分离；材质更新不重建 BVH、不重复上传整份几何。
- [x] 实现 HDR 强度/旋转，同步采样方向、辐射度、PDF 与失效逻辑。
- [x] 实现曝光、旧曲线、ACES 近似与线性裁切；显示修改不重启采样，达到采样上限后仍可刷新。

### 纹理、资源与场景

- [x] 贯通 UV0、authored tangent/handedness、Assimp fallback、命中点 TBN 与 normal map Y 翻转。
- [x] 实现纹理数组、元数据、RGBA/尺寸统一、mipmap、释放/重载，支持 baseColor、metallic、roughness、normal、emissive、opacity 与通道选择；颜色先解码再过滤和线性 mip 已接通，格式扩展另见 R-Q01。
- [x] 实现 Opaque/Mask/Blend、alphaCutoff 与贴图 alpha 的主射线/阴影筛选，旧 Transparent 保持直穿介质边界语义。
- [x] 导入 OBJ/glTF/GLB/FBX、源节点变换、外部与可解码内嵌图片；已有 FBX 内嵌 PNG、glTF tangent/sampler/UV transform 夹具。
- [x] 导入 sampler 与 KHR_texture_transform，支持 UV 缩放/偏移/旋转、wrap 与放大过滤；ray cone/minFilter/mip 已接通，更高阶过滤另见 R-Q02。
- [x] 实现 SceneDocument v2、稳定 ID、v1 内存迁移、原子保存、相对路径、相机/现有场景设置持久化及严格便携包依赖闭包。
- [x] 实现场景打开/保存/另存为/重载、互斥 CLI model/scene、追加与指定缩放导入、后台候选场景及失败原子性。
- [x] 卧室/路灯迁移为独立 JSON，保留路灯石材双三角形地面、6×6 UV 与贴图来源；预设不按名称硬编码渲染行为。

### 工作台、交互与任务

- [x] 实现对象树、唯一根/组约束、选择、实例变换、局部材质、六类纹理槽、隐藏/锁定及 undo/redo。
- [x] 默认空文档与欢迎首页、首个编辑视口延迟初始化 GL；修复旋转方向、滚轮越过轨道中心、空场景周期性停顿。
- [x] 实现八页工作区、照明标签、固定分区、单一编辑视口、V5 页面/渲染模式布局、V4 其他偏好及独立搜索。
- [x] 实现材质只读原因/作用域、相机管理、构图草稿与保存为新相机；草稿不覆盖来源，导航本身不修改文档。
- [x] 实现共享本地资源目录、真实最近文件与本机偏好；页面/队列 UI 状态不写场景 JSON。
- [x] 实现独立预览/正式预算、任务快照、会话串行队列、跨页暂停/停止、等待任务管理、固定历史结果与终态元数据；旧直接任务仍锁定编辑。
- [x] 正式尺寸独立于窗口，PNG/JPEG 从完整结果导出，不含编辑叠加；当前输出 alpha 固定不透明。
- [x] 显示桥使用三槽消费保护、短锁、明确源 FBO 与跨上下文 fence；历史回归覆盖整图/分块与 test_mis 切换。
- [x] 固定全图像素/sampleIndex/seed/dimension 保持同像素同 spp 不随 tile/批次布局变化；AA 开关开启后主射线会抖动，关闭时仍为像素中心。
- [x] 实现异步 GPU pass 计时、上传/BLAS/TLAS/OIDN 与资源估算、完整轮次速率和诊断曲线；重复展示不计新 spp。

### OIDN 生命周期与历史回归

- [x] 接通线性 HDR OIDN、normal 从 [0,1] 解码至 [-1,1]、albedo 线性语义及版本/尺寸检查。
- [x] 预览三路 PBO 在 fence 完成后映射，独立 CPU 后台过滤、取消/过期结果拒绝；辅助预过滤不污染历史，正式最终过滤支持取消。
- [x] 既有回归覆盖场景/包、编辑、导入、GPU 求交、光照数值、显示桥、队列和实际 UI；具体通过时点见下方历史记录，不自动延伸到新增能力。

## 当前能力与本轮验收范围

| 能力 | 已实现与本轮覆盖 | 剩余边界 |
| --- | --- | --- |
| 像素 AA 与辅助图累积 | 固定维度像素抖动、有效样本、normal/albedo/二阶矩；AA CPU/GPU/UI 回归通过。 | 通用重建滤波尚无。 |
| 光栅 MSAA | 按设备申请最多 4×，回归记录实际样本数。 | 不承诺所有 GPU 4×。 |
| GPU 实时降噪 | 重投影/身份/时域矩/空间过滤、复杂路径保护；持续运动及静止收敛通过。 | R-Q10 的真实复杂场景质量未全面验收。 |
| compute | 实际 GL 4.3 后端回归和三批图像容差一致性通过。 | 非 wavefront/硬件 RT，多设备吞吐待测。 |
| shader 变体 | 容量/介质/环境/compute、统一/旧采样、专用 profile/pick 已接通。 | 完整组合矩阵及首次编译停顿见 R-A07。 |

## 三批完成范围

- 第一批：R-C01 数值矩阵基础、R-C02 作者切线、R-Q01 线性颜色过滤、R-Q02 ray cone/mip、R-C05 异常位/有效累积。
- 第二批：R-C03 offset 误差界、R-C04 几何/着色事件契约、R-Q03 normal/Mask mip、R-A02 纹理池预算、R-A01 二值 AnyHit。
- 第三批：R-A03 可复现 profile、R-Q05 固定采样维度/seed、R-Q06 etaScale RR/起始深度、R-Q07 组概率、R-Q08 UV/alpha 功率估计。
- 每项图像、数值与部分完成边界见 [2026-10-03 三批渲染验收](./render_batches_2026-10-03.md)；下列稳定编号保留未完成扩展。

## P0：正确性与诊断基线

- [x] **R-C01 已完成范围：材质/PDF/物理测试矩阵。**基础连续 PDF/采样直方图、白 HDR、delta/TIR 与现有数值矩阵已通过；当前白 HDR 期望 29/28，验证估计器一致性。
- [ ] **R-C01 剩余范围。**补普遍白炉能量/方向反射率/互易性、clearcoat 归一化与底层衰减、混合波瓣和趋零粗糙度的完整物理矩阵；29/28 不等于普遍守恒。
- [x] **R-C02 已完成范围：各向异性作者切线。**已贯通 authored tangent、handedness、各向异性方向和 normal map 后坐标架，旋转/镜像/变换夹具通过。
- [ ] **R-C02 剩余范围。**参考 MikkTSpace 生成和真实复杂接缝/硬边资产覆盖归 R-E05，仍未完成。
- [x] **R-C03 已完成范围：ray offset 误差界。**重心/仿射误差传播、向外舍入、阴影两端和射线坐标 deltaT 已实现；尺度、远原点、邻近薄面回归通过。
- [ ] **R-C03 剩余范围。**补接触界面、连续透明边界、掠射折射、散射点邻近表面与更多硬件/真实资产矩阵。
- [x] **R-C04 已完成范围：着色法线事件契约。**已约束出射几何半球/着色事件一致，不合法样本为零；修正粗糙折射 PDF 和错误宏观方向，第二批数值/图像通过。
- [ ] **R-C04 剩余范围。**完整 shading-normal 能量补偿、normal map 能量/互易性和普遍 shadow terminator 修复仍待实现；几何法线继续决定介质身份。
- [x] **R-C05 已完成范围：异常路径可观测。**NaN/Inf、BVH/介质栈/边界超限诊断位及有效样本/二阶矩已接通，故障注入和累积回归通过。
- [ ] **R-C05 剩余范围。**补数量/位置、完整首异常状态和高能路径丢失统计；有效样本过滤不能掩盖系统性丢样。
- [ ] **R-C06 相邻介质 IOR。**栈当前仅存 type/density/color/anisotropy，表面仍按真空与材质 IOR 求比；增加边界两侧 IOR/介质身份，覆盖空气—玻璃—水、接触界面、全反射及进入退出恢复。
- [ ] **R-C07 初始介质与边界身份。**当前最多 8 层 LIFO，首段背面命中仅推断单介质；构建初始多层状态并识别退出边界。任意相交/裁剪/非闭合体积另定义支持与重叠优先级；验证容量和介质内实体。
- [ ] **R-C08 复杂 alpha/玻璃/介质组合。**扩展 Blend、旧 Transparent、Mask、连续透明和折射玻璃内照明的能量/可见性回归；保持直穿边界与折射 BSDF 的区别。
- [ ] **R-C09 OIDN 辅助特征语义。**普通表面、理想镜面、玻璃、粗糙透射与体积分别定义特征；评估沿 delta 路径取后续表面、beauty-only 回退及 Fresnel 特征混合，比较 AA 平均法线保留与重新归一化。验收降噪前后能量、纹理和边缘，不以采样回归替代降噪验收。

## P1：画质、采样效率与基础输出

### 纹理与材质质量

- [x] **R-Q01 已完成范围：线性域颜色过滤。**8 位颜色先 sRGB 解码再缩放/插值/mip，同图颜色/data 分视图；数值和 Lantern 对照通过。
- [ ] **R-Q01 剩余范围。**HDR/高位深发光材质贴图、更多格式及颜色往返矩阵尚未完成。
- [x] **R-Q02 已完成范围：光追 footprint/minification。**ray cone、minFilter/mip/三线性及 UV footprint 已实现，棋盘/后端回归通过。
- [ ] **R-Q02 剩余范围。**ray differentials、EWA/斜视各向异性和复杂反射/透射 footprint 验证仍未完成。
- [x] **R-Q03 已完成范围：normal/alpha 抗锯齿。**normal 过滤方差近似增粗糙度，单变化 alpha 源的 Mask 按 cutoff 建覆盖率 mip；图像及数值通过。
- [ ] **R-Q03 剩余范围。**两张同时变化 alpha 源仍回退点语义，发光 Mask 保留点采样；真实远景叶片/栏杆/细 normal 时间序列待测。
- [ ] **R-Q04 材质多次散射与涂层。**先完成 R-C01，评估粗糙金属/玻璃的多次微表面散射、经验证的能量补偿与清漆底层衰减；保留现有 Disney 模型的兼容结果，不把换模型造成的外观变化当作纯性能优化。

### 采样与直接光

- [x] **R-Q05 已完成范围：统一采样维度与种子。**前 120 维数字移位 Sobol、后续 counter hash、独立 alpha/边界事件流和 seed 持久化已完成；多种子、tile/full/compute 和同时间误差对照通过。
- [ ] **R-Q05 剩余范围。**完整 Owen scrambling、蓝噪声和多设备/CPU uniform 性能评估未完成；镜头维度仅预留。
- [x] **R-Q06 已完成范围：etaScale RR 与起始深度。**etaScale RR 和独立 preview/output 起始深度（0–64）已实现；无 RR 能量、玻璃多界面方差、UI/保存/队列链路通过。
- [ ] **R-Q06 剩余范围。**diffuse/glossy/transmission/volume 分路径预算及默认 4 次反弹收敛扫描未完成，粗糙玻璃广泛图像验收待补。
- [x] **R-Q07 已完成范围：光源组概率。**HDR 立体角亮度/非环境全局 proxy、1/16 概率边界与共享 Sample/PMF/MIS 已完成；强 HDR/强灯图像和能量测试通过。
- [ ] **R-Q07 剩余范围。**空间 Light BVH/Tree 尚未实现，需上一散射点上下文共享与同耗时误差验证；alias table 不能代替空间选择。
- [x] **R-Q08 已完成范围：发光 UV 功率估计。**世界面积 × 16×16 UV/alpha 功率 quadrature、支持下限与共享缓存已实现；明暗 UV/alpha/wrap/颜色及 GPU 图像通过。
- [ ] **R-Q08 剩余范围。**quadrature 为近似 proposal；发光纹理内部重要性采样、近距离三角形/矩形立体角采样仍未实现。
- [ ] **R-Q09 可选降 firefly 策略。**评估 path regularization/roughness mollification、独立直接/间接 clamp，记录其偏差与高光形状变化；先修正 PDF/数值异常，只在同 spp/同耗时/高 spp 能量对照后开放，不默认伪装为正确性修复。

### 降噪与线性结果

- [ ] **R-Q10 GPU 降噪真实场景质量。**已有重投影、身份匹配、复杂路径保护与静态累积回接；扩展相机/对象持续运动、遮挡显露、间接光/材质变化、玻璃/体积及停手收敛。同时看参考误差、拖影、纹理、偏色与时间序列；接受率和相邻帧差异不能单独证明质量。
- [ ] **R-Q11 降噪显存/带宽与精度。**当前额外 15 张 RGBA32F，3840×2160 仅这部分约 1.85 GiB，为代码容量估算而非驱动实测。分别评估 normal/albedo 半精度、身份整数、位置/二阶矩精度、缓冲复用与正式过滤按需分配；防 FP16 溢出和方差消差。拆分 OIDN 读回/预过滤/过滤/上传耗时，按需评估 GPU OIDN 或其他后端。
- [ ] **R-Q12 Film/RenderResult 与线性浮点保存。**当前结果经过显示变换读成 8 位 QImage；保存原始/降噪 beauty、样本与设置，分离显示/降噪/导出，支持历史结果重新曝光及前后对比，不污染采样历史。
- [ ] **R-Q13 EXR 与基础 AOV。**在 R-Q12 上支持 half/float EXR、normal/albedo/depth/variance/sample count，随后直接/间接、反射/透射、体积/发光与灯光组。各通道定义单位、空间、累积和有效性，补线性往返与组合重建验证。
- [ ] **R-Q14 颜色管理。**明确工作/纹理/显示/输出空间，先实现准确 sRGB 编码，再评估 OCIO、AgX、PBR Neutral、白平衡和曝光诊断。ACES 近似曲线不等同于完整 ACES 管线；线性结果与显示变换分开验收。

## P1/P2：加速、资源与调度

- [x] **R-A01 已完成范围：阴影 AnyHit。**二值查询 maxDistance/早退出、alpha/球遮挡和目标排除已实现；透明/介质仍用有序透射率，数值与图像通过。
- [ ] **R-A01 剩余范围。**夹具访问量明显下降，Lantern 提速未证明稳定普遍；需多真实场景生产 profile。
- [x] **R-A02 已完成范围：纹理分组与预算。**2–4 尺寸/格式池、默认 512 MiB 含 mip 预算、自动降尺寸/缺图报告和打包 PDF 已实现，预算/图像回归通过。
- [ ] **R-A02 剩余范围。**完整场景预算 UI、16K/大量贴图重资产、多设备/层数边界、压缩/缓存/流式加载仍未完成。
- [x] **R-A03 已完成范围：常规 profile。**可复现生产与插桩分离工具、node/triangle/scatter/材质 texel/光源尝试/无效计数及线性误差已实现；beauty 与二阶矩位相同。
- [ ] **R-A03 剩余范围。**按路径类别/长度分布、硬件寄存器/spill/带宽/occupancy、完整读回/导出成本拆分待补；插桩时间不代表生产吞吐。
- [ ] **R-A04 BLAS/TLAS 构建与缓存（P2）。**当前 BLAS 每层多轴排序及临时数组；评估 binned SAH、质心预计算、索引排序、临时内存复用、受控并行与带版本/几何签名的磁盘缓存。比较 SAH/HLBVH 的加载与后续遍历总成本；用质量退化指标决定 TLAS 重建。
- [ ] **R-A05 几何/BVH 布局（P2）。**当前每三角形 11 vec4/176 字节；先拆求交位置与命中后着色属性，再评估共享顶点、AoS/SoA、整数索引、normal/tangent 压缩、保守节点压缩与宽 BVH。已有 watertight/同距/镜像回归必须保留，先 profile 再改。
- [ ] **R-A06 compute 同步与吞吐（P2）。**已有 compute 路径，测量与 fragment 的真实吞吐/栈压力；根据 image 写、sampler 读、历史 blit 和目标复用依赖评估收窄 GL_ALL_BARRIER_BITS，不能只改 API 名称宣称提速。
- [ ] **R-A07 shader 变体管理（P2，部分已实现）。**扩展现有容量/介质/环境/后端缓存，按收益评估 lights/alpha/normal map/volume/guide 功能裁剪；为 define 定义默认值、触发、缓存键、失效、回退和组合回归，控制编译数量及首次切换停顿。
- [ ] **R-A08 交互/正式任务资源预算（P2）。**提交前估算 Film/降噪/贴图/几何和编辑预览总需求，明确降级与错误反馈；评估共享不可变 GPU 几何/BLAS/纹理池，独立保留实例与累积，验证跨上下文 fence、版本和释放。
- [ ] **R-A09 发布与控制延迟（P2）。**保留完整轮次快照与三槽显示桥，评估正式预览低频/低分辨率异步读回；记录 tile、整图 pass、最终降噪与 P95/P99 输入延迟。必要时评估有界细分/分阶段调度、独立 tile 宽高和降噪节奏，不擅自改用户保存预算。
- [ ] **R-A10 加载取消与预热（P2）。**CLI 大场景仍在窗口构造前同步准备；增加细进度、取消、缓存、shader 预热及失败原子性，不关闭用户实例或抢前台。
- [ ] **R-A11 更多设备/重场景预算验证（P2）。**验证批次估算、峰值显存与低占用后台运行；单 tile/整图可能超过约 8 ms 预算，历史 400 像素块收益不得泛化为所有 GPU/场景最优。

## P2：材质、介质、资产与相机扩展

- [ ] **R-E01 金属 Fresnel 与材质基准。**按需求增加复 IOR η/k、金/铜/铝及测量材质预设，验证 RGB/光谱约定与当前 metallic 兼容。
- [ ] **R-E02 真实 subsurface。**现有参数只改变 Disney 漫反射形状；区分 BSSRDF、随机游走与体积路径，先定义皮肤/蜡/玉石厚度、半径和单位，再落地可验收方案。
- [ ] **R-E03 统一介质物理参数。**在 R-C06/R-C07 后评估 σa/σs/Le 组合、彩色消光、密度与场景单位；现有 Absorb/Scatter/Emissive 分类型行为需要兼容迁移。
- [ ] **R-E04 异质介质与真实云雾。**按需求接密度场/3D 纹理、majorant、delta/ratio tracking、缓存/稀疏网格/OpenVDB；联合验证自由程、阴影透射率、体积 MIS 和降噪。McGuire cloud 当前只是表面网格，不代表体积功能。
- [ ] **R-E05 MikkTSpace 与法线策略。**集成参考切线生成并覆盖镜像/退化 UV、硬边与接缝；区分保留平滑法线、面法线、方向统一，明确拆点规则，和 R-C02 联合验证。
- [ ] **R-E06 多 UV/分层与扩展贴图。**当前 UV0/每槽第一张，非零 UV 警告跳过；增加多 UV、分层，贯通 AO、height、clearcoat/transmission/sheen 等通道。scalar 支持不代表完整 glTF 扩展。
- [ ] **R-E07 OBJ/MTL 与导入诊断。**为 map_Ks/Ns 建 specular/roughness 策略，为 map_Ka 定义兼容规则，不能把镜面贴图伪装 metallic。报告不支持通道、替代/缺图、降采样、UV、退化面与预算，提供可追溯适配报告。
- [ ] **R-E08 复杂导入与元数据。**扩充 FBX/glTF 的 DCC 内嵌材质、静态动画姿态、镜像/非均匀节点变换；导入可用相机/灯光，OBJ/MTL 可用可追溯 sidecar，减少手工适配。
- [ ] **R-E09 薄透镜景深与相机参数。**增加 aperture/focal distance、点击对焦、持久化/undo/任务快照；零光圈退化为针孔。再扩展焦距/传感器/FOV 约定、光圈形状和更多构图辅助；联合采样、guide、重投影验证。
- [ ] **R-E10 通用像素重建滤波。**AA 已实现，后续定义 box/tent/Gaussian/Mitchell 等支持范围、权重与 Film 边界，联合 beauty/辅助图/LOD/降噪验收，不能以单纯抖动代替完整滤波。
- [ ] **R-E11 自适应采样。**现有二阶矩/有效样本数仅是基础；增加稳定方差、最小样本、活动状态、保守邻域与罕见路径保护。可先 tile 分配再逐像素/活动压缩，修改进度为 spp 分布与活动比例；检验相关样本/停止偏差，保留固定 spp。
- [ ] **R-E12 专用光源与物理环境。**矩形单面灯、严格 delta 方向灯、聚光灯、IES、物理天空及明确辐射/功率单位。现有双面三角形可表达光面、有限太阳盘提供方向照明，不重复当作缺失能力。
- [ ] **R-E13 背景/可见性与透明输出。**增加独立纯色环境/背景、仅对相机隐藏环境/发光体而保留照明；先定义透明背景、预乘/直通 alpha，再明确玻璃/体积彩色透射合成限制，不把 alpha=0 当作通用修复。
- [ ] **R-E14 表面与特殊几何。**按目标资源评估 bump、真正位移/细分、解析图元、曲线/毛发；定义几何缓存、加速、精度与显存影响，避免把 normal map 误称真实位移。

## 专项：困难间接光与折射焦散

- [ ] **R-S01 折射连接与焦散。**覆盖 island 水下散射、玻璃内照明；先完成 IOR/边界与收敛基线，再用平面等可解析案例原型化受限连接，扩展曲面。需 Snell/Fresnel/TIR、分段衰减、路径测度 PDF/Jacobian 与 MIS。不得把阴影折向后沿用直线 PDF，也不得忽略折射直穿后标记物理正确。
- [ ] **R-S02 困难间接光。**评估环境 portal 与 Path Guiding，定义训练、分布/PDF、数据预算和场景失效；Blender 4.5 手册的 CPU 实现不可直接推定当前 GLSL 可用。它与通用焦散求解分开验收。
- [ ] **R-S03 专项算法选择。**比较 MNEE/流形方法、BDPT、VCM、MLT、光子方法的目标光路与成本；MNEE 的粗糙/曲面/体积/法线贴图限制必须记录，不能把参考图自动当真值。保持现有 PT 对照，不预先承诺一个算法解决全部问题。

### island 历史复现与未完成验收

2026-09-07 历史命令：`renderer_batch_tests.exe --benchmark resources/scenes/glslpt_tropical_island.scene.json build/island-debug/before.json 640 550 128 128`。4 次反弹、关闭降噪、130 总 spp（2 预热 + 128 测量）水体偏暗；临时阴影直穿后变亮，参考 enablevolumemis 也采用类似近似。该对照只证明近似对观感的影响，未证明物理正确或当前代码收敛；实验 shader 已还原，证据在未版本化 build/island-debug/。

尚需平面/曲面、理想/粗糙玻璃、全反射、嵌套介质，与解析或可信收敛参考比较同 spp/同耗时误差、能量、firefly、反弹扫描与降噪前后结果；不得以单张图变亮作为通过。

## P2/长期：结果流程、模块与后端

- [ ] **R-W01 持久队列与历史。**会话队列已实现；保存任务状态、结果元数据、文档/相机/设置/资产签名，定义缺资源、版本升级与错误恢复，不自动改变当前固定结果浏览行为。
- [ ] **R-W02 渲染 checkpoint 与复现。**基于 Film 保存线性累积、计数、采样索引/种子、算法/后端/shader 版本和资产哈希，支持重启续算；区分精确复现与统计一致，不能只保存 PNG/队列名。
- [ ] **R-W03 对象蒙版与生产通道。**基础 AOV 后增加有覆盖率的对象/材质蒙版、Cryptomatte/Light Groups；当前拾取 ID/depth 不能直接替代含透明/景深/运动覆盖的输出通道。
- [ ] **R-W04 Renderer 职责分离。**逐步分离 GPU 资源、积分器、Film/结果、降噪、显示与 RenderSession，使采样/后端可独立验证；保留版本化更新、不可变任务快照、完整轮次和跨上下文资源约束。
- [ ] **R-W05 材质预览与编辑。**先实现独立实时材质球和通用材质库，再评估节点中间表示/编译/执行与可编辑图；不把 UI 示意当渲染功能。
- [ ] **R-W06 其他工作台扩展。**保留资源收藏/在线资源、拖放赋材质、自动保存、插件、多语言/主题及 GPU/缓存预算控制，按用户需求实施，不因本轮渲染整理删除原待办。
- [ ] **R-L01 wavefront 原型。**当前 compute 仍为完整路径循环；在 profile 后分离求交/着色/阴影/介质队列、按材质分类和活动压缩，比较分歧/寄存器与队列流量/dispatch 成本，保留简单场景 megakernel 基线。
- [ ] **R-L02 硬件光追后端。**先抽象后端接口，再评估 OptiX/DXR/Vulkan RT 的设备资源、同步、shader、降噪和 Qt 显示桥；GLSL 软件 BVH 不自动使用 RT Core，保留 GL 3.3 兼容与能力回退。
- [ ] **R-L03 光谱渲染。**按色散/薄膜/严格颜色需求评估 sampled wavelengths、材质/纹理/介质/Film 全链路，明确 RGB 与光谱参考差异，不作为近期默认重写。
- [ ] **R-L04 动画与物理镜头。**增加 ray 时间、快门与刚体运动后再做变形运动、运动 BVH；畸变/真实镜头按需求独立专题，与 R-E09 景深区分。
- [ ] **R-L05 交互预览研究。**按需求评估专用预览 shader、ReSTIR/辐射缓存，定义时间复用、场景失效和偏差；与正式出图分开验收，不以平滑预览替代正确渲染。

## 验证、测量与执行顺序

建议先 R-C01–R-C09 基线；纹理颜色/LOD、AnyHit/预算、采样/RR、Light Tree、降噪与 Film/EXR 为通用收益方向。水体/玻璃为当前目标时可前移 R-S01，但仍依赖 IOR/边界与可信参考。wavefront/硬件 RT 须由 profile 决定，不预设倍率。

### 待建立的验证场景矩阵

- [x] **R-V01 已完成范围：数值与图像基线。**三批合成夹具、Lantern、种子/尺寸/后端/命令记录与持续相机/对象运动已建立。
- [ ] **R-V01 剩余范围。**小窗室内、嵌套折射真实资产、树叶、大场景、多小贴图、4K 与多设备完整矩阵未完成。
- [x] **R-V02 已完成范围：同 spp/同耗时误差。**第三批多种子、独立 seed 137 高 spp 有限深度参考、原始线性 MSE/能量、生产计时与误差热图已记录。
- [ ] **R-V02 剩余范围。**同误差收敛、广泛真实资产及完整加载/预热/追踪/读回/降噪/导出成本拆分、时间序列质量仍待补。
- [ ] **R-V03 对外参考一致性。**与 Cycles/pbrt 匹配几何、单位、材质/灯光定义、相机、工作色空间与线性输出；默认显示变换、RGB/光谱模型或有偏近似造成差异时明确记录，不直接断言积分器错误。
- [x] **R-V04 当前构建/测试清单复核。**独立 build/render-foundation 最终 Release 构建成功，当前 33 项完整串行回归全部通过，802.85 秒；实际命令和源码版本见本轮验收。旧根 build 缓存不作为本轮结果。

### 执行要求

- 代码改动按风险配置/构建 Release、运行定向与必要 CTest；默认 `ctest --test-dir <构建目录> -C Release -j 1 --output-on-failure`，重型 GPU 测试串行，避免影响前台。
- 本轮代码已验收并先提交，文档单独检查内容/链接/diff 后提交；不将文档检查算为 GPU 回归。
- UI/图像验证遵循根 [AGENTS.md](../AGENTS.md) 的私有桌面/进程内测试要求，检查真实 GL 图像与 inputDesktopWindows=0；系统焦点/桌面遮挡未覆盖时明确记录。
- 调度/AA/后端改动保留 tile/整图同像素同样本契约、完整快照、取消与过期结果拒绝；开启新滤波/自适应后另定义相应的一致性标准。
- `--regression-frames` 当前等待完整 spp 与匹配结果；旧按展示事件计数的截图不能用于同 spp 比较。
- 性能修改先保留 profile、对照版本与原始证据；资源容量估算不等于实际显存，完成/发布/显示新帧分别统计。

### 历史验收索引

- 2026-09-04：纹理/场景阶段 6ee2661，Release 与 8/8；范围见纹理专题。
- 2026-09-05：直接光阶段 Release 与 9/9，含 lighting_numerical_regression；数值/限制见直接光专题，不代替复杂降噪验收。
- 2026-09-06：工作台 17 项完整测试及后续 6 项定向；2026-09-09：18 项全量及最终界面复测；时点见工作台/工作区专题。
- 2026-09-30：八页全量 23/23 在结果状态补丁前，随后定向 5/5，顶栏去重后最终 2/2 与构建；三档截图在去重前，范围与未验证项见工作区专题。
- 2026-10-02 第一批记录与第二批初次 31/32、修复后定向复测分别保留原时点；2026-10-03 最终当前代码 33/33 完整通过，具体命令及三批图像见 [2026-10-03 三批渲染验收](./render_batches_2026-10-03.md)。

## 借鉴资料

以下为 2026-10-02 分析查阅的官方参考，用于任务设计；特定版本/后端限制以实际实施时资料与代码为准。

- [Cycles 采样：自适应、Light Tree 与 Path Guiding](https://docs.blender.org/manual/sl/4.5/render/cycles/render_settings/sampling.html)、[Principled BSDF / Multiscatter GGX](https://docs.blender.org/manual/sr/4.5/render/shader_nodes/shader/principled.html)。
- [Blender 颜色管理](https://docs.blender.org/manual/sr/4.5/render/color_management.html)、[AOV / Cryptomatte / Light Groups](https://docs.blender.org/manual/en/4.5/render/layers/passes.html)、[Cycles GPU 后端](https://docs.blender.org/manual/vi/4.5/render/cycles/gpu_rendering.html)、[焦散/MNEE 支持限制](https://docs.blender.org/manual/ca/4.3/render/cycles/object_settings/object_data.html)。
- [pbrt 材质分层](https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Scattering_from_Layered_Materials)、[更完善的路径追踪器 / etaScale](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer)、[光源采样](https://www.pbr-book.org/4ed/Light_Sources/Light_Sampling)。
- [pbrt 图像纹理 / EWA](https://www.pbr-book.org/4ed/Textures_and_Materials/Image_Texture)、[浮点误差](https://www.pbr-book.org/4ed/Shapes/Managing_Rounding_Error)、[BVH](https://www.pbr-book.org/4ed/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies)。
- [pbrt 采样研究](https://www.pbr-book.org/4ed/Sampling_and_Reconstruction/Further_Reading)、[重建滤波](https://www.pbr-book.org/4ed/Sampling_and_Reconstruction/Image_Reconstruction)、[投影相机](https://www.pbr-book.org/4ed/Cameras_and_Film/Projective_Camera_Models)。
- [pbrt GPU wavefront](https://www.pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Mapping_Path_Tracing_to_the_GPU)、[体积积分器](https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Volume_Scattering_Integrators)、[困难光输运参考](https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/Further_Reading)、[光谱表示](https://www.pbr-book.org/4ed/Radiometry%2C_Spectra%2C_and_Color/Representing_Spectral_Distributions)。
- [OIDN 辅助图与过滤规范](https://www.openimagedenoise.org/documentation.html)。
