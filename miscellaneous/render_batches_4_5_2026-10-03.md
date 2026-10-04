# 2026-10-03 第四、五批渲染验收

实现提交 `3c0e589`，初始介质更新补修 `e2caa02`，基于前三批 `7558eb0` / 文档 `9440842`，分支 codex/render-optimization。以下是当前代码的实际验证；不把计划、历史记录或单一夹具通过推广为普遍物理正确性。逐项真实 GPU 图像及 JSON 已版本化到 [交付目录](../output/render-fourth-fifth/2026-10-03/validation.json)，大尺寸原始线性数据与日志仍在未版本化 build/render-fourth-fifth。没有运行 Cycles/pbrt 做外部渲染基准。

## 实现与前后图

| 项目 | 当前完成范围 | 证据 |
| --- | --- | --- |
| R-C05 | 异常事件、像素与首状态 | [前后对照](../output/render-fourth-fifth/2026-10-03/R-C05-comparison.png) |
| R-C07 | 初始嵌套介质/边界身份 | [前后对照](../output/render-fourth-fifth/2026-10-03/R-C07-comparison.png) |
| R-C06 | 两侧 IOR/玻璃—水 | [前后对照](../output/render-fourth-fifth/2026-10-03/R-C06-comparison.png) |
| R-C01-C04 | 清漆归一化与物理审计 | [前后对照](../output/render-fourth-fifth/2026-10-03/R-C01-C04-comparison.png) |
| R-C08 | Blend/玻璃/吸收/贴图发光组合 | [前后对照](../output/render-fourth-fifth/2026-10-03/R-C08-comparison.png) |
| R-Q12 | 线性 Film/历史重曝光 | [前后对照](../output/render-fourth-fifth/2026-10-03/R-Q12-comparison.png) |
| R-Q14 | 准确 sRGB 显示 | [前后对照](../output/render-fourth-fifth/2026-10-03/R-Q14-comparison.png) |
| R-Q13 | HALF/FLOAT EXR/基础 AOV | [前后对照](../output/render-fourth-fifth/2026-10-03/R-Q13-comparison.png) |
| R-C09 | OIDN 特征与细节保护 | [前后对照](../output/render-fourth-fifth/2026-10-03/R-C09-comparison.png) |
| R-Q10 | 实际运动/陈旧历史拒绝 | [前后对照](../output/render-fourth-fifth/2026-10-03/R-Q10-comparison.png) |

另有 [玻璃—水精确接触对照](../output/render-fourth-fifth/2026-10-03/R-C06-contact-comparison.png) 和 [实际结果工具栏](../output/render-fourth-fifth/2026-10-03/ui-result-controls.png)。所有比较及 UI PNG 已实际打开检查。图片来自渲染读回或 Qt 进程内控件操作，未使用生成式图片。C05 的 beauty/诊断图、Q12 的同 Film 曝光、Q13 的真实 EXR 通道是能力对照；C08 前后基本一致是回归保留，不声称都能降低噪声。

第四批诊断可选 `LEARNQT_TRACE_DIAGNOSTICS=1` / `--diagnostics --capture-diagnostics`，每完整轮次读专用 MRT；七类事件次数与受影响像素分开统计。首次事件按样本和像素遍历顺序选，保存 pixel/imagePixel、stage/depth、所属路径 origin/direction/throughput/eta/etaScale/mediumCount；尚非完整介质栈/阴影重放。容量夹具捕获 mediumOverflow 71970 次，首 mediumCount=8。致命路径错误丢弃整样本，不继续把部分能量当有效结果。

初始介质只对闭合、朝外、Opaque/旧 Transparent 边界执行按需拓扑检查、三方向 CPU BLAS 内外判定和最多八层嵌套初始化；GPU 栈记 IOR/实例，退出错配失败，反射保留、透射更新。玻璃—水 eta 使用当前/目标 IOR；精确同顶点三角形且法线相反的接触直接切换。不按 epsilon 合并薄缝，任意交叠、不同剖分接触、裁剪/Mask/Blend/open 体积尚不支持，歧义计数尚无 UI 提示。

清漆补齐 4·NdotL·NdotV 分母，底层采用互易单次 Fresnel 衰减，兼顾 delta 基底。GPU 解析项、双向互易性、四类材质双角度 sample/eval RGB/PDF 质量、BTDF eta² 已验证。白炉图 64 spp 线性 RGB 均值 0.202736→0.188683，两图使用同 0 EV 显示。该薄层是近似模型，未实现普遍白炉守恒/完整 shading-normal/多次散射；旧 Disney 29/28 历史测试保留模型兼容性。

初始栈还依赖边界材质、可见性和变换，不能只在相机变化或 shader 切换时上传。收尾核对发现复用 shader 时会保留旧介质状态；补修同步后，新增 GPU 回归比较修改密度、移动边界、全量表同步后的线性图与新建 Renderer，结果一致。专项 1/1（54.17 秒）通过。全部交付图/EXR/误差随后用补修后的二进制重新生成。

第五批 RenderResult 保存最后完整轮次的 top-down scene-linear Rec.709 D65 raw/同版本同 spp denoised、计数与相机/环境/seed/RR/预算等设置。部分 tile 不混入 Film。结果页可重新曝光 -24～24 EV、切曲线/raw/denoised/通道，各历史结果保留查看设置，不修改累积。仅会话持有，没有 checkpoint/持久队列/磁盘 Film 重开。

CPU/GPU 显示采用准确 sRGB 分段编码；ACES 近似大值计算避免中间溢出，不等于完整 ACES/OCIO。EXR 为 v2 无压缩 single-part scanline：RGB/guide 可 HALF 或 FLOAT；depth.center/variance/sampleCount 始终 FLOAT。HALF 采用最近偶数舍入，超 65504 或非有限值失败且保留旧文件。自动队列 FLOAT、手动 HALF/FLOAT；导出保持线性 HDR、chromaticities 和 learnqtSettings，不烘焙曝光。独立 OpenEXR 3.5.2 解码验证 FLOAT beauty 位相同，HALF 等于 numpy f16，HDR 最大 3.133690>1。

AOV 定义：normal/albedo 是 AA 累积降噪 guide（delta 链可取后续表面），不是通用首表面生产通道；depth.center 为像素中心最近几何距离、背景 0、点 alpha 阈值、不含解析球/无限远深度、非 AA 平均；variance 是有效样本亮度无偏样本方差、n<2 为 0；sampleCount 是有效样本。未提供 deep、压缩、直接/间接分解、Cryptomatte 或通用透明覆盖。

OIDN 普通表面取非 delta 首表面，镜面/玻璃沿 delta 链，粗糙透射首表面 albedo 向 1 混合；参与体积事件整图 beauty-only。三路 RGBA PBO 同步带计数/二阶矩/掩码。普通/delta 以三倍亮度标准误差保护细节，粗糙透射/体积绕过标量限制，raw 与二阶矩 before/after 位相同。32 spp 与独立 seed137/1024 spp 原始参考：delta 旧过滤 MSE 0.00153428382→0.00136840402，raw 0.00130782855 仍更好；rough 0.00144892376→0.00143772323。这是有限两夹具评估，不承诺普遍收益，完整 RGB 方差和 Fresnel 分支特征仍待研究。

Lantern 运动使用实际 Renderer：12 步相机、12 步对象，材质/环境亮度变化，停手每 8 spp 记录到 96 spp；各目标 pose 与最终状态用 seed137/512 spp 参考。运动期在足够匹配邻域时拒绝超 3σ 的陈旧重投影 radiance，静止 raw 无偏累积不变。数值/能量与逐帧记录见 report.json 的 quality-before/after 和独立 sequence JSON，不把接受率或帧间差异单独作为质量证明。尚非 4K/多设备/广泛玻璃体积场景验收。

相机末步 filtered MSE 0.00221969210→0.00206720667（降低约 6.87%），对象末步 0.00265706301→0.00221994624（约 16.45%）；对应 raw MSE 在前后完全相同。停手 96 spp filtered MSE 约 1.86574e-6、raw 2.23082e-6。上述是单一场景/有限深度参考的误差变化，不是通用提速或普遍收敛承诺。

## 实际命令与测试时点

Windows / Qt 5.15.2 / OIDN 2.3.3 / RTX 5070 Ti / OpenGL 4.3 NVIDIA 610.88 / Release，重型 GPU 测试全部串行。主程序 UI 默认私有桌面自动隔离，审计 inputDesktopWindows=0；证据工具使用 QOffscreenSurface，不操作前台或真实输入。没有覆盖用户活动桌面的真实焦点/遮挡。

```powershell
$env:PATH='C:\Qt\5.15.2\msvc2019_64\bin;'+$env:PATH
cmake --build build/render-foundation --config Release --target learnQT aa_denoise_tests render_fourth_fifth_tests render_third_batch_tests render_second_batch_tests render_foundation_tests lighting_tests traversal_tests renderer_batch_tests scene_tests presentation_tests bvh_build_tests background_failure_tests --parallel 2 -- /p:CL_MPCount=2 /verbosity:quiet
python tools/make_render_quality_fixtures.py build/render-fourth-fifth/fixtures
python tools/render_quality_evidence.py --exe build/render-foundation/Release/aa_denoise_tests.exe --fixtures build/render-fourth-fifth/fixtures --output build/render-fourth-fifth/delivery-evidence --openexr-path build/render-fourth-fifth/python-deps
ctest --test-dir build/render-foundation -C Release -j 1 --output-on-failure
# 以下为测试夹具修正后的中间定向复测，之后介质同步补修再次执行上述完整构建、证据和完整 CTest。
cmake --build build/render-foundation --config Release --target learnQT --parallel 2 -- /p:CL_MPCount=2 /verbosity:quiet
ctest --test-dir build/render-foundation -C Release -j 1 -R '^(render_queue_regression|aa_denoise_ui_regression|render_fourth_fifth_regression)$' --output-on-failure
```

Python 证据工具需要 numpy/Pillow 与独立 OpenEXR reader；OpenEXR 包仅安装在上述忽略的测试目录，项目运行时无新增依赖。每次渲染的实际完整命令、比较开关、分辨率/spp/反弹/种子、exe/fixture/production source SHA256 见 [report.json](../output/render-fourth-fifth/2026-10-03/report.json)。before 为相同当前二进制的明确 legacy 算法开关（media/clearcoat/gamma/OIDN/motion），不是声称重建旧 master；其余改进保持一致，原始物理数据共享显示变换。

Release 构建成功。全量 **33/34，1026.50 秒**，render_queue_regression 在 phase11 超时：96×54/128 spp 小任务在 100ms 轮询间完成，导出失败夹具来不及设置。测试改为工作线程 Rendering 信号直接暂停，在 UI 安装夹具后继续，不改变生产算法。中间复测 **2/3，151.17 秒**，队列通过、AA 运动在 2.5秒观察期未收到至少三帧。持续输入保持到三帧、最多 10 秒并记录时长后，最终定向 **3/3，190.07 秒**，生产源码期间未改；这是介质更新补修前的复测时点。随后完成上述初始栈同步补修，再构建所有受影响目标、重新生成全部证据，执行最终完整 **34/34，1015.18 秒**。首次运动冷延迟仍可能存在，活性回归不能证明首帧/P99 无卡顿。

UI 最后一次：256×192 逻辑、DPI 2，持续相机/对象 2724/2504 ms，新显示帧 3/128。实际控件覆盖三输出格式、重曝光/曲线、depth AOV、历史查看设置保留；[ui-report](../output/render-fourth-fifth/2026-10-03/ui-report.json) 保存观察序列，queue-report 保存失败独立保留/继续、停止恢复及资源变化拒绝。

## 未完成与后续优先级

- R-C01/C04/Q04：普遍能量守恒、shading-normal/terminator、多次散射；R-C05：完整栈/阴影重放和高能路径丢失分析。
- R-C06/C07/C08：任意相交、不同剖分接触、裁剪/Mask/Blend/open 体积、歧义 UI 提示与真实复杂资产；困难玻璃照明/焦散另见 R-S01。
- R-C09/Q10/Q11/A07：更广泛路径/资产、多设备/4K、完整 RGB 方差、资源预算/精度/带宽和首运动冷启动/首帧延迟。
- R-Q12/Q13/Q14/W01/W02/W03：checkpoint/历史重开/持久队列，生产分解 AOV/deep/压缩/Cryptomatte，OCIO/AgX/完整输入颜色管理。
- R-Q07/Q08/S01/V03：空间 Light Tree、发光纹理内部重要性采样、折射连接/焦散与匹配 Cycles/pbrt 外部参考；并未在本批实现。

## 设计参考

参考用于设计与定义，不替代本工程验证：[pbrt 体积积分器](https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Volume_Scattering_Integrators)、[OIDN 辅助特征](https://www.openimagedenoise.org/documentation.html)、[OpenEXR 文件布局](https://openexr.com/en/latest/OpenEXRFileLayout.html)、[Disney BSDF notes](https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf)、[pbrt 路径测度](https://pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/The_Path-Space_Measurement_Equation)。本批采用的薄层和降噪保护是受限近似，与参考完整模型分别说明。
