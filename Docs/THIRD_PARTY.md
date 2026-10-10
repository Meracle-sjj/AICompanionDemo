# 第三方资源与分发边界

核查日期：2026-10-10。本文根据实际源码、Config、Content 文件路径、制作记录与下列官方页面登记来源。它不是「全部素材原创」或「全部授权已审定」声明；免费、已导入、能 Cook 都不能单独证明取得了公开分发权。

## 公开源码与可玩包

公开源码快照不包含原工程的 `Content/`、旧 `.git/`、引擎、构建中间物或原始下载包。源码可用于阅读系统实现，**缺少地图、蓝图、输入和视听资源，不能直接打开游玩或重现完整打包**。原 Content 在本地完整工程保留，读者应从各权利方取得适用资源后自行接入。

可玩包应由完整本地工程生成，只交付应用程序及其运行所需的 Cook 数据；不附带可编辑的第三方原 `.uasset`、FBX、纹理、源音频或素材下载包。合法集成资源能够进入成品，不表示它们能够进入公开源素材仓。

2026-10-10 的独立发布候选将 `BGM_Track` 留空，并用 `DirectoriesToNeverCook=/Game/BGM` 排除整个音乐目录。**原工程的 Suno 曲目和原 Config 保留**。候选仍需核对实际 Cook 清单、封包与独立启动结果；将音量设成零不能替代从交付文件中排除音乐。

## 现役资源与 Content 留存登记

| 资源 / 工程位置 | 实读来源证据 | 许可与交付口径 |
| --- | --- | --- |
| Unreal Engine 5.6.1、第三人称模板、Manny 与模板变体；`Characters/Mannequins`、`ThirdPerson`、`Input`、`Variant_*` | `.uproject`、项目 AGENTS、`EnemyGrunt.cpp` 构造中的 Manny 路径、模板源码版权头 | [UE EULA](https://www.unrealengine.com/eula/unreal) §1 区分 Examples 与引擎代码，§5(b)允许 Examples 源码分享。保留 Epic 版权头；不把引擎源码/工具纳入公开仓，不将 Epic 内容改称自有作品。成品按 §4 的运行时交付和终端许可要求处理。 |
| Epic Games **Paragon: Sevarog**；`ParagonSevarog`，召唤师模型与动画 | `BossSummoner.cpp` 资源路径、Content 实文件与[官方资产页](https://www.fab.com/listings/a4882b5e-cfad-4830-a3dd-46a6c31a79b2) | 页面允许用于 Unreal Engine 项目，并限制用 PARAGON 商标命名/宣传游戏。[Epic Content License Agreement](https://www.unrealengine.com/eula/content) §3(a)允许不可分离的成品 object code；§4限制源格式共享。原资产不放公开源码仓。 |
| Epic Games **Paragon: Grux**；`ParagonGrux`，巨像模型/动画、蠕虫材质依赖 | `BossColossus.cpp`、`assets/wurm-body/v1/README.md` 实际列出 Slate 颜色/法线与 Lava_crack 三张 Epic 纹理；[官方资产页](https://www.fab.com/listings/8c4bac2c-f7f7-4632-a644-47f4e104f5d8) | 同上。蠕虫程序几何可单独登记为自行生成；引用 Epic 纹理的 UE 材质与完整视觉不能概括为全部自制。 |
| Fab 掉落物与交互模型；`Content/Fab/` 下十一组资源 | `DefaultGame.ini` 的 MeshOverride 路径，见下表 | 缺少逐项 listing ID、作者、取得时许可类型与加库凭证，**许可未逐项审定**。Fab 上的资源可能采用不同许可，不从目录名或 Free 字样推定为 CC0。公开仓排除原资源。 |
| `Fire_EXP_Vol01_Free`，火/爆炸 Niagara | 源码路径、资源审计记录；与目录名称对应的[Realistic Fire & Explosion VFX Vol.1 官方候选页](https://www.fab.com/listings/2adf4e4e-5f1d-4543-9e8c-192fe3c9a8ed) | 官方页面名称可核对，但未取得下载/加库记录来证明本地包与 listing 的对应和许可版本。不能仅因 Free Starter Pack 宣称原素材可公开。 |
| `_SplineVFX`，火/电 Niagara 与相关材质 | 源码 `/Game/_SplineVFX/...` 路径、资源审计；[Free Spline VFX 官方页](https://www.fab.com/listings/2b923e61-b02d-4cc9-bd0b-b067c9e6056e)列有 Fire/ElectricLightning 等名称 | 取得时许可与账户记录待补。原包不公开；项目自有特效也须检查是否引用包内纹理/材质。 |
| `F_FreeFlameFx_Pack` | 实际 Content 目录留存；本次 Source 检索未发现该根路径直接引用，未确认是否被其他资产间接引用或进入最终 Cook | 尚未证明其准确 listing ID、发布者、取得许可。不要以同名搜索结果代替本地来源凭证。原包不公开。 |
| Kenney 拾取/UI 八个音效槽；`Content/SFX` | `tools/diag_sfx_import.py` 的逐音源映射与现役 `DefaultGame.ini` | [Impact Sounds](https://kenney.nl/assets/impact-sounds)、[RPG Audio](https://kenney.nl/assets/rpg-audio)、[Interface Sounds](https://kenney.nl/assets/interface-sounds)官方页均标 CC0。[Kenney 说明](https://kenney.nl/support)允许商用且不强制署名；保留来源记录，不使用其商标图案暗示官方合作。 |
| qubodup **Explosive.flac**，`SFX/Boss/boss_impact_real` | `assets/boss-impact/v1/SOURCE.json`、原页面存档、下载 SHA；v2 QA 记载同源裁为首爆 1.8 秒 | [原上传页](https://freesound.org/people/qubodup/sounds/189778/)实际标 CC0。使用的是官方 HQ MP3 预览经 PCM 转码/裁剪，非原 24-bit FLAC。按作者建议保留此链接；“来自美国政府影片”是上传者说明，未独立核实原影片。 |
| 程序音效；`Content/SFX/Synth` | `tools/synth_gen.py` 与导入脚本，使用数学波形/噪声生成 | 登记为项目程序生成的音效，不沿用脚本中“零版权”这种不精确说法。未在该生成脚本中发现外部采样读取。 |
| 豆包 **seed-audio-1.0** 固定台词与音频；`Content/Voice` | `tools/gen_voice_doubao.ps1` 写明实际模型/端点；`assets/beacon-voice/v1`、`v2/QA.json` 记录提示词、音频 SHA 与导入源 | 这是预合成声音，游戏不进行运行时 TTS 调用。[官方产品文档](https://docs.volcengine.com/docs/DoubaoVoice/ProductOverview-9?lang=zh)能核对产品身份；[产品和服务协议](https://docs.volcengine.com/docs/Legalagreements/Productsandservicesagreement?lang=zh)要求结合订单及专用条款。未见本账户适用的专用条款/输出分发凭证，不能据 API 成功或“authorized pipeline”字样认定公开发布权限。网页体验服务条款不能直接套到此 API 账户。 |
| Suno 生成 BGM；原 `Content/BGM/BGM_AI_Generated` | `tools/diag_bgm_import.py`、旧 STATUS、原 `DefaultGame.ini` 指向用户提供的 `AI_generated_BGM.wav` | **候选不播放且不 Cook。**未见作品 ID、生成/下载时间、账户订阅与适用授权记录。旧文档“版权链路干净 / CC 干净”没有可核实凭证，本文不采纳。[Suno 当前条款](https://suno.com/terms)区分账户、获准下载与商用条件；不能把现在订阅或本地有文件当成该曲的授权证明。 |
| AI 插画；`UI/UpgradeCards`、`StartScreen`、`BossRelics`、`EndingScreen` | 任务仓 `assets/upgrade-cards` 的提示词/manifest、`start-cover/v1/QA.json`、`boss-relics/v1/manifest.json`、`ending-screen/v3/QA.json` 均登记内置 imagegen 与原图 SHA | 登记为 AI 辅助生成/编辑的概念插画，不能写成全部人工手绘或实机截图。Boss 插画与游戏内 Epic 模型/纹理分开登记；生成记录证明来源，不替代对输入参考与第三方权利的核查。公开源码快照同样排除这些 UE 纹理。 |
| 沙漠/砂岩和蠕虫程序几何；`Environment/Desert`、`Bosses/Wurm` | 任务仓生成脚本、OBJ/MTL、manifest 与相应 Content/AGENTS；蠕虫说明明确纹理复用 | 可登记自行制作的几何/规则；完整材质的引用依赖仍按各原许可处理。 |

现役 Fab 配置路径清单（名称来自 `Config/DefaultGame.ini`，并非已核实的官方作品标题）：

| Content/Fab 目录 | 用途 |
| --- | --- |
| `Game_ready_low_poly_crystal` | 经验 |
| `Coin` | 金币 |
| `Cake_Roll` | 治疗 |
| `Renaissance_Wooden_Chest` | 普通宝箱 |
| `Potion_Flask` | 狂怒药剂 |
| `Eagles_Medieval_Battle_Shield` | 圣盾 |
| `Standard_Magnet` | 磁吸 |
| `Obelisk` | 急救信标 |
| `Old_Rusty_Key` | 钥匙 |
| `_TreasureChestChallenge_Monster_Chest` | 锁箱 |
| `FREE_Ritual_Stone` | 难度祭坛 |

[Fab 官方许可页](https://www.fab.com/eula)的可读摘要允许在项目中集成并分发素材，禁止单独再分发素材；摘要本身注明不具法律约束力。本次网页工具未返回完整动态 EULA 正文，且各资产页的 “License terms” 字段未返回具体许可；因此未用这个摘要替代逐项取得许可的证据。

## 历史候选与未使用项

旧任务记录曾调研 **Quaternius Ultimate Monsters** 并解压 FBX，但本次实读 `EnemyGrunt.cpp` 构造仍引用 Manny；Content 目录与所读现役资源路径中未发现 UltimateMonsters、Blob 或 Alien 接入。该包不登记为已确认的现役依赖。

[Ultimate Monsters 包页](https://quaternius.com/packs/ultimatemonsters.html)仍标 CC0；[官网统一 QAL v1.0](https://quaternius.com/license.html)更新于 2026-08-28，允许成品集成并限制原素材再分发。两页口径存在差异，应保留具体下载版本的许可，不把全部 Quaternius 现下载资源概括为 CC0；本源码快照不附带该包。

原 BGM 目录还保留历史备选曲目，未逐项追溯；候选排除整个目录，不能将“现役已静音”扩大为备选音乐也获准分发。

## 发布前还需补齐的证据

Fab 道具/VFX：逐资源作者、官方 listing、取得时间、许可类型/版本和账户加库或下载记录；若为 CC BY，补上准确署名与修改说明。TTS：与生成批次对应的账户服务、订单/专用条款及输出分发权。Suno：若将来恢复，须核对具体曲目的生成与获准下载记录、订阅及授权；当前候选持续排除。

仅完成本地封包不代表完成上述许可核验。若缺口无法补齐，公开可玩版需在独立候选中替换或排除相关资源，再核对 Cook 文件与运行结果；原完整工程保留。

## 可玩成品的终端使用条款

以下条款供随可玩成品一同交付；本文件不授予源素材再分发权。

获准取得本演示成品的用户可运行该程序，并在正常游玩与展示范围内使用其中资源。Unreal Engine 技术及第三方许可内容仅作为成品的不可分离运行部分提供。相关许可方不对这些技术与内容作任何陈述或保证，不承担相关责任；在适用法律允许的最大范围内，包括适销性、特定用途适用性、非侵权等保证与条件均予排除。

本演示不向终端用户授予从成品中提取第三方素材、将其作为素材包发布、许可给他人或纳入其他项目的权利。各第三方资源仍归相应权利方，适用其原许可。

保留引擎随附的第三方许可证与 Epic 版权标识；若成品设置制作人员/鸣谢页，按 [UE EULA §7(a)](https://www.unrealengine.com/eula/unreal)加入其要求的引擎署名。

## 本次核查方式

1.0核查阶段只读核对实际 Source、Default*.ini、Content 路径、任务仓来源/提示词/QA/导入脚本，并实际访问本文链接中的官方许可/资产页面；该阶段未运行音频或图像生成API。未导出第三方原资源，未用下载页存在或Cook成功推定许可。公网页面为核查当日状态，既有素材仍须按实际取得时适用的许可核对。

1.1按作者要求，使用项目既有豆包seed-audio-1.0离线制作管线新增固定台词“看来你找回了一些自己的能力”。实际MP3为4.44秒，SHA256 `bf317aa40c0b0bbe2facf3c268108efea25966f3d7ee65667151fd23202d13dd`；已导入`/Game/Voice/voice_playerattackunlock`，游戏运行时播放预生成SoundWave，不调用TTS服务。来源、生成与导入记录保留于任务仓；这项生成记录同样不替代上述账户条款和输出分发权证明。
