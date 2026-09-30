# McGuire 项目场景

已将 McGuire 资源库转换为 **57 个 learnQT 场景文件**。场景文件位于 [`resources/scenes`](../../scenes)，命名格式为 `mcguire_<场景 ID>.scene.json`；完整的机器可读清单、来源、许可、对象数量、贴图数量和验证记录见 [`conversion_manifest.json`](conversion_manifest.json)。

常用入口：

- [Bistro 外景](../../scenes/mcguire_bistro_exterior.scene.json) / [Bistro 内景](../../scenes/mcguire_bistro_interior.scene.json)
- [Crytek Sponza](../../scenes/mcguire_crytek_sponza.scene.json) / [Dabrovic Sponza](../../scenes/mcguire_dabrovic_sponza.scene.json)
- [Gallery](../../scenes/mcguire_gallery.scene.json)
- [San Miguel 完整版](../../scenes/mcguire_San_Miguel_san_miguel.scene.json) / [低多边形版](../../scenes/mcguire_San_Miguel_san_miguel_low_poly.scene.json)
- [Power Plant](../../scenes/mcguire_powerplant.scene.json)
- [Cornell Box 原版](../../scenes/mcguire_CornellBox_cornellbox_original.scene.json)
- [Vokselia Spawn](../../scenes/mcguire_vokselia_spawn.scene.json)

原始 ZIP、解压后的原始模型和贴图均保留在本目录；派生 MTL、贴图和几何位于 `derived/`。转换只改写项目场景引用，不覆盖原始资源。

转换适配包括统一场景尺度、相机和 HDR 环境、STL 的坐标系转换、缺失贴图的可追溯替代，以及 Bistro 按材质合并对象。Bistro 和 San Miguel 使用 512px 派生贴图，Gallery 使用 2048px 派生贴图；原始高分辨率图像仍保留。OBJ 的高度、镜面等未被当前材质系统支持的通道没有伪装成完整 PBR，具体限制记录在清单的 notes 中。
