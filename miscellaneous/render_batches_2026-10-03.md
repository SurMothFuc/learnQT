# 三批渲染改进与验收（2026-10-03）

代码提交 `7558eb0da5aede4fbfb40b4692bc0ee0bc18dd6b`，独立构建 `build/render-foundation`，Windows / Qt 5.15.2 / MSVC Release。三批指定范围已实现，最终完整 CTest **33/33 通过，802.85 秒**。当前代码范围见架构/流程/实现逻辑，完成与剩余子项见 [待办](./to-do.md)。本次收尾不改 README。

## 构建与实际回归

```powershell
cmake --build build/render-foundation --config Release --target learnQT aa_denoise_tests render_third_batch_tests render_second_batch_tests render_foundation_tests lighting_tests traversal_tests renderer_batch_tests scene_tests presentation_tests bvh_build_tests background_failure_tests --parallel 2 -- /p:CL_MPCount=2 /verbosity:quiet
$env:PATH = 'C:\Qt\5.15.2\msvc2019_64\bin;' + $env:PATH
ctest --test-dir build/render-foundation -C Release -j 1 --output-on-failure
```

GPU 测试串行，UI 进入 Windows 私有桌面，不切换输入桌面；审计 inputDesktopWindows=0。实际 GL/控件链路、持续运动、队列、场景/包、光照、求交、三批专项及 compute 回归均在 33 项内。AA UI 新显示帧：相机 4、对象 146；DPI 2，视口 256×192 逻辑/512×384 物理。RR 控件与视口截图已实际打开检查。活动桌面真实焦点/遮挡未验证，未重跑专用三档完整 UI 截图。

## 三批范围和图像证据

| 批次 | 指定实现 | 证据 |
| --- | --- | --- |
| 第一批 | C01 PDF/数值基础、C02 作者切线、Q01 线性过滤、Q02 cone/mip、C05 异常诊断 | [验证及图像](../output/render-first-batch/2026-10-02/validation.json) |
| 第二批 | C03 offset、C04 出射契约、Q03 normal/Mask mip、A02 纹理池、A01 AnyHit | [验证及图像](../output/render-second-batch/2026-10-02/validation.json) |
| 第三批 | A03 profile、Q05 固定采样、Q06 etaScale RR、Q07 组概率、Q08 UV 功率 | [完整验证](../output/render-third-batch/2026-10-03/validation.json) |

第一批与第二批保存原阶段记录；第二批初次全量 31/32，光照修复后定向复测通过，不改写为该时点全量通过。当前 33/33 为最终代码新执行结果。

| 第三批 | 同 spp 线性 MSE 变化 | 实际前后图 |
| --- | --- | --- |
| R-A03 profile | beauty/二阶矩位相同；工具不宣称画质或速度提升 | [对照](../output/render-third-batch/2026-10-03/R-A03-comparison.png) |
| R-Q05 固定维度 | Lantern 16 spp，平均 MSE 降低 67.01% | [对照](../output/render-third-batch/2026-10-03/R-Q05-comparison.png) |
| R-Q06 etaScale RR | 双闭合 IOR 2.5 玻璃 32 spp，降低 99.90% | [对照](../output/render-third-batch/2026-10-03/R-Q06-comparison.png) |
| R-Q07 组概率 | 强 HDR 16 spp 降低 88.21%，强灯 90.84% | [对照](../output/render-third-batch/2026-10-03/R-Q07-comparison.png) |
| R-Q08 UV 功率 | 明/暗 UV 共用发光纹理 16 spp，降低 77.69% | [对照](../output/render-third-batch/2026-10-03/R-Q08-comparison.png) |

所有图像来自实际 GPU 读回，不使用生成图。对照为逐项实施时点，保留之前改进；开发开关隔离旧采样/RR/组概率/UV 权重。参考使用独立 seed 137，2048/4096 spp、相同反弹上限；不是无限样本或无限深度真值。低 spp 取种子 0/1/2，比较 256×192 线性 RGB，均关闭降噪；显示图只用于观看，MSE 来自原始 float。同时间误差、命令、进程时间和 shader/源码 SHA 见交付 JSON；0.5 秒短计时不证明所有硬件长期稳定收益。

tile 128/17 与整图位相同；实际 compute 最大差异低于 1e-5。profile 单独插桩并验证 beauty/二阶矩位相同，生产吞吐用不插桩版本；应用 texel 请求次数不是硬件带宽/cache 测量。固定采样编译裁剪和 PICKING_PASS 修复发生在部分图像之后，最终语义/交互由完整回归覆盖。

## 工具与修复取舍

`tools/render_benchmark.py` 保存固定 spp、生产多次计时、独立 profile、参考、raw float/PNG/JSON；`tools/render_compare.py` 驱动开发开关对照、多种子、同时间误差和 tile/compute 一致性；`tools/make_render_sampling_fixtures.py` 生成闭合玻璃、强 HDR/强灯和 UV 发光夹具。每个交付 report 保存实际 CLI，无需凭文档重建参数；临时 raw 大文件与完整构建日志留在 build/render-third-batch。

未量化功率组概率曾导致强太阳盘低 spp 方差恶化，最终以 1/16 边界保持组分层并复测两种场景。持续运动暴露拾取 pass 仍携带 beauty 法线/材质/TBN 工作，现已专门裁剪，仅保留最近命中、点 alpha、ID/depth；控件与运动回归通过。缺失法线的 60° crease 改动及作者法线保留回归也纳入当前提交。

## 未完成范围

三批是原大条目的阶段实现：C01/C04 完整物理能量与终止线、C05 首异常位置/状态、C06/C07 相邻 IOR/初始多层介质仍未完成。Q02 ray differentials/EWA、Q03 复合 alpha/发光 Mask LOD、MikkTSpace 和更广泛高位深资产仍待补。

第三批剩余扩展：完整 Owen/蓝噪声、分波瓣路径预算、空间 Light Tree、发光纹理内部重要性/立体角采样、硬件寄存器/spill/带宽/occupancy。UV quadrature 和组 proxy 是近似选择权重，不改实际辐射度；复杂粗糙玻璃图像及真实大资产/多设备矩阵未全面验收。

Film/线性导出/EXR/AOV、完整颜色管理、复杂降噪质量与带宽优化、完整场景预算/流式加载、焦散、景深/动画/持久队列、wavefront/硬件 RT 等后续事项全部保留在待办。未运行 Cycles/pbrt 做对外参考基准。

机器可读的当前汇总见 [验收 JSON](./validation/render_batches_2026-10-03.json)。文档收尾只核对源码、记录、链接和 diff，没有将文档检查计入 GPU 测试。
