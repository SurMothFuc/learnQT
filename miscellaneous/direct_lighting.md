# 基础直接光采样

更新日期：2026-09-05。本阶段以 `9cc6eda` 为父提交基础，代码与本文档一同交付。

概率同步、HDR 分布/PDF、解析光源 NEE 与命中贡献、alpha 发光面、delta，以及均匀介质中的直接光采样已阶段性完成。Release 构建与 9/9 CTest 通过。本文记录本阶段实际行为及验收，不将有限测试范围等同于任意场景均已验证；后续工作集中在 [to-do.md](./to-do.md)。

## 光源分布与 PDF

`Scene::buildLightData()` 在 BVH 三角形排序后建立光源列表：先是发光三角形，尾部是场景中的解析 sphere / sun。选择权重使用功率估计，double 累加后存储 float CDF；每个光源的 `selectPdf` 使用相邻 CDF 值之差，保证与实际二分选择的区间一致。

三角形 GPU 编码的 `textureParam1.z` 同时保存该概率。`Scene::updateMaterial()` 重建 light CDF 后回写此字段；下一帧同时上传三角形和 light buffer，更新 `nLights / nAnalyticLights` 并清空累计历史。材质编辑不会重建 BVH，但仍是全场景常量覆盖及整份三角形缓冲上传。

`SampleOneLight()` 每个表面或体积散射点总共选一个显式光源样本：环境与非环境列表同时存在时各占 0.5，仅有一类时占 1。非环境列表包含发光三角形、球光源和有限角度太阳盘；其中具体光源再按功率 CDF 选择。这里的列表命名不代表太阳盘位于有限距离。

| 光源 | 方向采样与辐射度 | 对应方向 PDF |
| --- | --- | --- |
| HDR 环境 | 按 texel CDF 选择，在选中的 texel 内按立体角连续采样，读取环境颜色 | 环境选择概率 × texel 实际概率质量 / texel 立体角 |
| 发光三角形 | 均匀面积采样，在实际 UV 处求发光贴图及 alpha 覆盖率；双面发光 | 列表选择概率 × 光源选择概率 × 距离平方 / (面积 × 几何法线绝对余弦) |
| 解析球 | 从球外采样可见立体角锥，射线与球求交取得距离；实体球向外发光 | 列表选择概率 × 光源选择概率 / 可见锥立体角 |
| 太阳盘 | 按给定角半径采样无限远圆盘方向；不依赖 HDR 开关 | 列表选择概率 × 光源选择概率 / 太阳盘立体角 |

HDR cache 的 R/G/B 通道分别为 x 边缘 CDF、给定 x 的 y 条件 CDF、两者实际 float 区间宽度的乘积。CPU 使用亮度乘 texel 精确立体角构建分布；全黑图回退到均匀立体角采样。GPU 二分 CDF 后利用区间内残差连续采样，不再将 cache 当作预计算 UV 查找表。

`SampleHdr()` 返回的 PDF 与 `hdrPdf(direction, ...)` 使用同一每单位立体角测度；不依赖图像长宽比为 2:1。极区纬度采用稳定的半角/atan 表达，太阳盘和球光源的小立体角也避免直接使用精度不足的 `1 - cos(theta)` 相减。

## NEE、命中贡献与 delta

表面 NEE 使用 BSDF、绝对余弦、光源辐射度和连接透射率，除以 light PDF，并以 power heuristic 与 BSDF PDF 做 MIS。体积 NEE 将 BSDF/余弦替换为 HG 相函数，与 phase PDF 做 MIS。

主路径保存上一真实散射点、该方向的连续 PDF 和 delta 标记。命中三角形或解析球时查询对应光源 PDF；逃逸时分别累计 HDR 和每个覆盖该方向的太阳盘，每份辐射度使用自身选择概率做 MIS。正辐射度光源即使因 CDF 量化得到零选择概率，仍可由 BSDF/phase 命中贡献，不能直接丢弃。

`SampleDisneyBSDF()` 对连续事件返回方向 PDF 和 `f * abs(NdotL) / pdf` 权重；对 delta 事件使用离散概率质量，并合并共享同一反射方向的波瓣。纯 delta 表面跳过 NEE，下一个发光命中权重为 1；含漫反射或其他连续波瓣的混合材质仍执行 NEE。覆盖粗糙度为零的反射/折射、全反射及 IOR 匹配的直通事件。

`Mask` 在 BVH 中按 cutoff 筛选，`Blend` 随机透过。发光面显式采样时按实际 UV 处的 Mask/Blend 覆盖率缩放辐射度；命中发光使用 BVH alpha 筛选后的结果，保持两条估计路径的覆盖语义一致。旧 `Transparent` 是直穿边界，保留前一散射位置/PDF/delta 标记，不增加散射深度。

## 介质与可见性

每条主路径持有 `MediumStack`，包含最多 8 层均匀介质的 type、density、color、anisotropy。边界进出根据三角形绕序几何法线判断，进入压栈、退出弹栈；正确嵌套的闭合边界可恢复外层介质。阴影查询复制主路径栈后独立遍历，不修改主路径状态。

| 介质类型 | 当前线段处理 |
| --- | --- |
| Absorb | `sigma_a = (1 - clamp(color, 0, 1)) * density`，按 Beer-Lambert 衰减 throughput |
| Scatter | 标量 `sigma_t = density` 采样指数自由程；发生事件时乘散射 albedo `color`，做体积 NEE 后采样 HG 方向；未发生事件的概率已包含消光，不再重复乘衰减 |
| Emissive | 沿线段累积 `throughput * color * density * distance`；当前该类型不同时表达吸收或散射 |

先取得最近几何/解析球交点，处理到该端点前的介质自由程、衰减或发光，再累计端点/无限远发光，防止光源贡献绕过介质衰减。散射深度只统计真实表面或体积事件，RR 在完成第 3 次散射后启用；透明边界另有循环上限。

`ShadowTransmittance()` 最多遍历 128 层，逐段累乘介质透射率，复用 BVH 的 alpha 筛选；旧 Transparent 边界可直穿并更新栈。解析球参与遮挡，目标光源用光源/三角形 ID 排除自遮挡。折射玻璃的 BSDF 表面阻断直线 NEE 连接，当前不采用“忽略折射直接穿过玻璃”的近似。

表面与阴影射线沿几何法线向出射侧偏移，距离为 `max(1e-5, 2e-6 * maxComponent(abs(position)))`；不以着色法线决定介质内外。极端尺度、掠射角和薄片仍需专项验证。

## 本阶段验证记录

2026-09-05 在 Windows / Release、NVIDIA GeForce RTX 5070 Ti 上执行以下命令并通过：

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Release
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build -C Release --output-on-failure
```

最新完整 CTest 记录为北京时间 18:49–18:52，9/9 通过，总耗时约 227.77 秒。其中 `lighting_numerical_regression` 约 130.24 秒。文档收尾时核对代码改动、测试日志和文件时间，代码在该轮测试后未变化；文档检查未重复运行相同构建/测试。

| 测试 | 本阶段验证范围 |
| --- | --- |
| `lighting_numerical_regression` | 实际 OpenGL shader：HDR 采样/PDF、球/太阳盘能量与遮挡、同样本方差、alpha 发光面、delta/TIR、吸收/嵌套/自由程/HG/体积 MIS |
| `scene_roundtrip_and_package` | 现有场景/便携包回归，新增材质编辑后 GPU 三角形与 light list 选择概率一致性 |
| `scene_ui_switch_regression` | 卧室/路灯切换、保存/放弃/取消、失败恢复及回切图像 |
| `gpu_render_regression` | Lantern 实际渲染有效性与关键像素 |
| `unit_middle_mouse_orbit_target` | 轨道相机输入契约 |
| `unit_finite_analytic_lights` | 解析光源配置与实现契约 |
| `unit_texture_rendering_support` | 纹理 CPU/GPU 链路契约 |
| `import_fbx_embedded_texture` | FBX 内嵌 PNG 导入 |
| `import_gltf_tangent_sampler_transform` | glTF 切线、sampler 和 UV transform 导入 |

部分数值结果如下，容差与夹具见 [LightingTests.cpp](../tests/LightingTests.cpp)：

| 检查 | 测量 | 参考 |
| --- | --- | --- |
| HDR Lambert 半球积分 | 3.14302 | pi，约 3.14159 |
| 完整积分器球光源 MIS | 0.187489 | 0.1875 |
| 完整积分器太阳盘 MIS | 0.637292 | 0.637642 |
| 重叠太阳盘加 HDR | 2.30804 | 2.311 |
| Blend 发光面命中平均贡献 | 1.00024 | 1 |
| delta 镜面命中球/太阳盘 | 2.4 | 2.4 |
| delta 折射辐射度传输 | 0.466943 | 0.466667 |
| 闭合吸收体阴影透射率 | 0.135337 | 0.135335 |
| 嵌套吸收体恢复外层 | 0.0497891 | 0.0497871 |
| 保守散射白炉，反弹上限 12 | 0.989327 | 1；保留有限深度误差 |

球光源夹具在同样 65,536 样本下，BSDF-only 方差约为 `0.5295`，MIS 方差约为 `1.0966e-5`，NEE-only 约为 `3.0521e-6`。该结果说明这一个夹具中 MIS 相对 BSDF-only 降低了方差，不证明所有场景均优于 NEE-only，也不是同耗时性能比较。

日志与截图位于未版本化的 `build/`：`Testing/Temporary/LastTest.log`、`scene-ui-regression/bedroom-before.png`、`scene-ui-regression/lantern.png`、`scene-ui-regression/bedroom-after.png`、`render_regression.png`。本线程查看过卧室与路灯回归截图；它们用于画面回归，不替代数值验收。`--regression-frames` 统计展示事件，不是严格 spp。

可用 `ctest --test-dir build -C Release -R '^lighting_numerical_regression$' --output-on-failure` 单独复现数值测试；本阶段验收执行的是上面的完整 9 项命令。

## 已知边界与后续范围

- 介质只支持均匀、闭合且正确嵌套的边界；栈满或阴影层数耗尽会终止该路径/连接。任意相交、裁剪、非闭合及非均匀体积不受支持。
- 相机初始在一个吸收体内已有回归；实现仅根据首段背面命中推断一个介质，未构建完整初始多层栈。
- 介质栈不包含 IOR，表面折射仍按真空与当前材质的 IOR 比计算；相邻非真空介质与复杂玻璃光路另需实现和验证。
- 发光贴图权重仍使用整张纹理平均值，未按三角形 UV 覆盖区域估算功率，也未做纹理域重要性采样。
- OIDN 法线仍以 `[0, 1]` 编码读回，尚未恢复到 `[-1, 1]`；透明/体积路径的辅助输入语义也未专门验收。本阶段采样能量测试不覆盖这些降噪问题。
- 未完成复杂玻璃、多光源和体积组合的广泛同 spp 噪声/firefly 对照；现有几何偏移尚未证明适用于所有尺度。
- 本地参考 `D:/program/GLSL-PathTracer-master` 的相机 AA、景深、环境/背景控制、ACES、roughness mollification、实例化、AnyHit 和独立输出/预览等差距已加入待办。对比为源码检查，未运行参考项目做画质或性能基准。

参考项目没有发光网格重要性采样，也没有体积栈；当前已实现这两项。参考的矩形面光源和理想方向光与当前双面三角形、有限太阳盘的模型不同，后续按实际需求补齐专用类型。
