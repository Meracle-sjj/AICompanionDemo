# 资源来源与分发说明

本文对应 v1.3.1 修订，最终验证与发布尚待确认，下载可用性以[Release 页面](https://github.com/Meracle-sjj/AICompanionDemo/releases/tag/v1.3.1)为准。作品包含素材复用、AI 辅助生成与程序制作，保留相应权利归属。本轮新增原创数学合成的短铡刀音与新版低频碎岩循环、四条沿原参考录音生成的豆包固定配音，以及代码矢量图标；原 BGM 继续沿用作者此前明确的分发确认。

## 主要来源

| 资源 | 用途与来源 | 当前说明 |
| --- | --- | --- |
| Unreal Engine 5.6.1、第三人称模板与 Manny | 引擎、玩家与杂兵基础、模板源码 | 保留 Epic 版权头及引擎随附许可；按 [UE EULA](https://www.unrealengine.com/eula/unreal)区分模板、引擎与成品分发范围 |
| Epic Paragon: Sevarog | 召唤师模型与动画，[官方资产页](https://www.fab.com/listings/a4882b5e-cfad-4830-a3dd-46a6c31a79b2) | 按资产适用许可用于 Unreal Engine 成品，原资产不公开；不使用 PARAGON 商标宣传本作 |
| Epic Paragon: Grux | 巨像模型／动画与蠕虫材质纹理，[官方资产页](https://www.fab.com/listings/8c4bac2c-f7f7-4632-a644-47f4e104f5d8) | 蠕虫几何为程序制作，其完整材质包含 Epic 纹理；适用 [Epic Content License Agreement](https://www.unrealengine.com/eula/content) |
| Fab 交互道具 | 经验、金币、治疗、宝箱、药剂、圣盾、磁吸、信标、钥匙、锁箱与祭坛模型 | 原资源不公开；逐项作者、listing 与取得时许可凭证仍待补齐，不统一声明为 CC0 |
| Niagara 火／爆炸／电特效包 | `Fire_EXP_Vol01_Free`、`_SplineVFX`；工程另保留 `F_FreeFlameFx_Pack` | [火与爆炸候选官方页](https://www.fab.com/listings/2adf4e4e-5f1d-4543-9e8c-192fe3c9a8ed)、[Spline VFX 官方页](https://www.fab.com/listings/2b923e61-b02d-4cc9-bd0b-b067c9e6056e)；准确来源对应与取得时许可待补，保留目录不代表全部进入成品 |
| Kenney 音效 | 拾取与 UI 声音，来自 [Impact Sounds](https://kenney.nl/assets/impact-sounds)、[RPG Audio](https://kenney.nl/assets/rpg-audio)、[Interface Sounds](https://kenney.nl/assets/interface-sounds) | 官方页标注 CC0 |
| qubodup · Explosive.flac | Boss 爆炸音效，[原上传页](https://freesound.org/people/qubodup/sounds/189778/) | 页面标注 CC0；使用 HQ MP3 预览转码，并裁剪为首爆采样 |
| 程序音效与几何 | 数学波形／噪声音效，沙漠砂岩和蠕虫几何；v1.3.1 新增短铡刀音与新版低频碎岩循环 | 两条新声由固定种子、带限噪声及撞击／正弦波形原创合成，不使用第三方音频采样；材质引用的第三方纹理仍适用原许可 |
| 初始箱光晕与光点 | 原创局部 UV 代码公式生成环晕与星光材质，组件参数设为柔和紫色 | 沿用原创美术实现，宝箱原模型来源不变；没有为本项新增外部图片、贴图、模型或音频 |
| 宝箱与数值反馈图标 | FREE! 标记、锁／钥匙、金币／刀图标由项目代码绘制，字体使用引擎默认粗体 | 本轮未下载额外图标或字体资产；矢量表现不改变宝箱模型、引擎字体或其他素材的来源 |
| AI 辅助插画 | 升级、Boss 遗物、开始页与结束页插画 | AI 辅助生成／编辑，属于插画；与实际游戏画面、人工手绘及 Epic 模型分别说明 |
| 豆包 seed-audio-1.0 | 队友固定台词与预合成语音，[官方产品说明](https://docs.volcengine.com/docs/DoubaoVoice/ProductOverview-9?lang=zh) | v1.3.1 新增折射一句与成功前三箱三句，沿用原队友角色录音作为生成参考；字幕按实际新录音路由，不宣称已通过人耳同人确认。游戏运行不调用 TTS；账户适用的专用条款与输出分发凭证仍按既有记录保留，参见[产品与服务协议](https://docs.volcengine.com/docs/Legalagreements/Productsandservicesagreement?lang=zh) |

Fab 道具与特效的逐项取得许可、豆包语音输出的账户凭证尚未完成全面核验。来源页面、免费标记或成功导入不能替代这些记录。[Fab 许可摘要](https://www.fab.com/eula)也不能代替具体资源取得时的许可。

## 下载包与公开源码

发行成品包只提供程序及运行所需的 Cook 数据，不提供可编辑的第三方原始资产。公开源码排除 `Content/`、引擎和原素材下载包，缺少地图、蓝图、输入与视听资源，不能直接游玩或完整复建。源码公开展示不授予统一的 MIT／CC0 或第三方素材再分发许可。

v1.1／v1.2 曾因当时生成、下载、订阅与分发凭证缺口排除原 Suno BGM。2026-10-11 作者明确确认：“恢复原曲：我确认它是我生成且可以公开分发的音乐”。v1.3 按该确认恢复原曲，确认不泛化为其他备选曲目或平台账户的授权结论。原曲来源仍为作者生成的 Suno 音乐，[Suno 条款](https://suno.com/terms)对应原服务。

其他资源的许可凭证缺口继续保留，不由本次原曲确认或语音生成成功自动补齐。素材来源记录保留在本地，避免在公开仓散布第三方原素材。

## 成品使用

获准取得本演示成品的用户可运行程序，并在正常游玩与展示范围内使用其中资源。Unreal Engine 技术及第三方内容作为成品不可分离的运行部分提供；相关许可方不作保证，并在适用法律允许的范围内排除相关责任，包括适销性、特定用途适用性与非侵权保证。

本演示不授予从成品中提取第三方素材、作为素材包再发布或纳入其他项目的权利。保留引擎随附许可证及 Epic 版权标识；制作人员／鸣谢页按 [UE EULA §7(a)](https://www.unrealengine.com/eula/unreal)的要求署名。

[返回作品入口](../README.md) · [玩家指南](../PLAYER_GUIDE.md)
