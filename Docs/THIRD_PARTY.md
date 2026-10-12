# 资源来源与分发说明 · v1.3.5-preview

**本文对应 v1.3.5-preview；v1.3.3 继续保留为稳定版。** 本轮显示与数字调整未新增第三方素材；继承 v1.3.4-preview 的原创现场召唤柱材质与作者选定的 CC0 Sword #10 独立声音资源。历史 v1.3.4 的 Editor 18.73 秒构建、完整 Win64／Cook／两次冷启及限定运行按其范围保留，游戏内人耳听感另验。以下 v1.3.3 资源记录保留历史。

v1.3.3新增两条原创数学合成危机PCM（0.42／0.28秒）与一条队友加速固定配音“现在，我更能跟上你的脚步了。”；均新增SoundWave，保留全部旧声音源和资产。两短声不使用第三方采样或连续长滑音，配音沿现有豆包seed-audio-1.0和原角色录音参考离线生成，游戏运行不联网调用TTS。三声已实际导入、受控起播并由最终完整Cook核对非空Export／Bulk，原BGM唯一收录；本地完整Win64和两次独立冷启／正常退出已完成。人耳听词／同人相似度／危机感尚未确认，不以导入、起播或Cook代替听感判断。交付入口：[v1.3.3 正式交付入口](https://github.com/Meracle-sjj/AICompanionDemo/releases/tag/v1.3.3)。[本轮修订记录](https://github.com/Meracle-sjj/AICompanionDemo/issues/6)。

伤害小剑、冰火融化双图、寒霜雪花与免费箱居中E均由代码矢量绘制，沿用引擎字体，不新增外部图标或字体。三张README AI概念图保留既有标注和来源，不当作实际游戏截图。原BGM继续沿用作者此前明确的分发确认；以下第三方素材权利与账户适用条款边界保持。

## 主要来源

v1.3.4-preview采用作者网站试听C（StarNinjas Sword #10），先行接入记录见[本轮选择](https://github.com/Meracle-sjj/AICompanionDemo/issues/7#issuecomment-6119526234)。独立48k双声道PCM16源保留约1.138秒完整挥斩，仅重采样并规范峰值，未重新合成或裁剪音色；新SoundWave已真实导入，实际玩家弹射／主攻成功地刺分别有音频线程起播记录。槽位音量1.0，调试混音表0.33原件保留；Cook已核资源，新包人耳混音仍待确认。旧原创铡刀音及原资源不改，公开只收入来源及许可说明。

现场召唤红柱使用Engine Cylinder与原创独立Unlit／Additive材质，法线／视向和世界高度公式柔化边缘，无外部纹理或Time节点。新材质已真实保存，三秒扩张与冻结已有受控运行记录；所审画面采用临时近址／镜头夹具，尚未验证自然玩家远址或新完整包表现。

| 资源 | 用途与来源 | 当前说明 |
| --- | --- | --- |
| Unreal Engine 5.6.1、第三人称模板与 Manny | 引擎、玩家与杂兵基础、模板源码 | 保留 Epic 版权头及引擎随附许可；按 [UE EULA](https://www.unrealengine.com/eula/unreal)区分模板、引擎与成品分发范围 |
| Epic Paragon: Sevarog | 召唤师模型与动画，[官方资产页](https://www.fab.com/listings/a4882b5e-cfad-4830-a3dd-46a6c31a79b2) | 按资产适用许可用于 Unreal Engine 成品，原资产不公开；不使用 PARAGON 商标宣传本作 |
| Epic Paragon: Grux | 巨像模型／动画与蠕虫材质纹理，[官方资产页](https://www.fab.com/listings/8c4bac2c-f7f7-4632-a644-47f4e104f5d8) | 蠕虫几何为程序制作，其完整材质包含 Epic 纹理；适用 [Epic Content License Agreement](https://www.unrealengine.com/eula/content) |
| Fab 交互道具 | 经验、金币、治疗、宝箱、药剂、圣盾、磁吸、信标、钥匙、锁箱与祭坛模型 | 原资源不公开；逐项作者、listing 与取得时许可凭证仍待补齐，不统一声明为 CC0 |
| Niagara 火／爆炸／电特效包 | `Fire_EXP_Vol01_Free`、`_SplineVFX`；工程另保留 `F_FreeFlameFx_Pack` | [火与爆炸候选官方页](https://www.fab.com/listings/2adf4e4e-5f1d-4543-9e8c-192fe3c9a8ed)、[Spline VFX 官方页](https://www.fab.com/listings/2b923e61-b02d-4cc9-bd0b-b067c9e6056e)；准确来源对应与取得时许可待补，保留目录不代表全部进入成品 |
| Kenney 音效 | 拾取与 UI 声音，来自 [Impact Sounds](https://kenney.nl/assets/impact-sounds)、[RPG Audio](https://kenney.nl/assets/rpg-audio)、[Interface Sounds](https://kenney.nl/assets/interface-sounds) | 官方页标注 CC0 |
| qubodup · Explosive.flac | Boss 爆炸音效，[原上传页](https://freesound.org/people/qubodup/sounds/189778/) | 页面标注 CC0；使用 HQ MP3 预览转码，并裁剪为首爆采样 |
| StarNinjas · Sword #10 | 裂地獠牙地刺发动音，[原作者页面](https://opengameart.org/content/20-sword-sound-effects-attacks-and-clashes) | 作者以真实刀具录音制作，按[CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/)提供；保留完整挥斩，仅重采样为48k双声道并规范峰值。原音源与官方包仅本地保存，公开源码只保留来源和许可说明，成品使用Cook数据 |
| 程序音效与几何 | 数学波形／噪声音效，沙漠砂岩和蠕虫几何；v1.3.1短铡刀音与低频碎岩循环，v1.3.2旧短声保留，v1.3.3妖术师v3两条危机PCM | 由数学波形和噪声原创合成，不使用第三方音频采样；本轮两声不是新 TTS 或外部下载音频，人耳听感另验。材质引用的第三方纹理仍适用原许可 |
| 初始箱光晕与光点 | 原创局部 UV 代码公式生成环晕与星光材质，组件参数设为柔和紫色 | 沿用原创美术实现，宝箱原模型来源不变；没有为本项新增外部图片、贴图、模型或音频 |
| Boss 召唤预备红柱 | Engine 基础圆柱与原创 M_SummonColumn 柔边材质，三游戏秒扩大覆盖祭坛 | 无新增第三方图片、纹理或特效采样；时间归游戏Actor，材质只读取实例参数，旧屏幕数字版已归档 |
| 宝箱与数值反馈图标 | FREE!、锁／钥匙、金币及伤害图样由项目代码绘制；v1.3.3十族原创伤害矢量、慢落雪花与居中E键帽，字体沿用引擎默认粗体 | 未下载额外图标、图片或字体资产；矢量表现不改变宝箱模型、引擎字体或其他素材的来源 |
| AI 辅助插画 | 升级、Boss 遗物、开始页与结束页插画；README 的循环、火球空间判定与伤害／声音迭代三张概念图 | 插画与概念图经 AI 生成／编辑；三张新 README 图使用内置 imagegen 管线，属于机制／设计方向示意，与实际游戏画面、人工手绘及 Epic 模型分别说明 |
| 豆包 seed-audio-1.0 | 队友固定台词与预合成语音，[官方产品说明](https://docs.volcengine.com/docs/DoubaoVoice/ProductOverview-9?lang=zh) | v1.3.1折射／前三箱录音及v1.3.3队友加速一句，沿用原队友角色录音作为生成参考；字幕按实际新录音路由，不宣称已通过人耳同人确认。游戏运行不调用 TTS；账户适用的专用条款与输出分发凭证仍按既有记录保留，参见[产品与服务协议](https://docs.volcengine.com/docs/Legalagreements/Productsandservicesagreement?lang=zh) |

Fab 道具与特效的逐项取得许可、豆包语音输出的账户凭证尚未完成全面核验。来源页面、免费标记或成功导入不能替代这些记录。[Fab 许可摘要](https://www.fab.com/eula)也不能代替具体资源取得时的许可。

## 下载包与公开源码

发行成品包只提供程序及运行所需的 Cook 数据，不提供可编辑的第三方原始资产。公开源码排除 `Content/`、引擎和原素材下载包，缺少地图、蓝图、输入与视听资源，不能直接游玩或完整复建。源码公开展示不授予统一的 MIT／CC0 或第三方素材再分发许可。

v1.1／v1.2 曾因当时生成、下载、订阅与分发凭证缺口排除原 Suno BGM。2026-10-11 作者明确确认：“恢复原曲：我确认它是我生成且可以公开分发的音乐”。v1.3 按该确认恢复原曲，确认不泛化为其他备选曲目或平台账户的授权结论。原曲来源仍为作者生成的 Suno 音乐，[Suno 条款](https://suno.com/terms)对应原服务。

其他资源的许可凭证缺口继续保留，不由本次原曲确认或语音生成成功自动补齐。素材来源记录保留在本地，避免在公开仓散布第三方原素材。

## 成品使用

获准取得本演示成品的用户可运行程序，并在正常游玩与展示范围内使用其中资源。Unreal Engine 技术及第三方内容作为成品不可分离的运行部分提供；相关许可方不作保证，并在适用法律允许的范围内排除相关责任，包括适销性、特定用途适用性与非侵权保证。

本演示不授予从成品中提取第三方素材、作为素材包再发布或纳入其他项目的权利。保留引擎随附许可证及 Epic 版权标识；制作人员／鸣谢页按 [UE EULA §7(a)](https://www.unrealengine.com/eula/unreal)的要求署名。

[返回作品入口](../README.md) · [玩家指南](../PLAYER_GUIDE.md)

<!-- V134_ACTUAL_PACKAGE_RECEIPT -->

### 历史阶段收据：v1.3.4-preview 发布前

以下保留当时的候选状态；v1.3.4-preview 后续已作为预览发布。当前 v1.3.5-preview 的范围见下一节，旧包结果不代替本轮验收。

本轮新完整 Win64 BuildCookRun 已于 181.92 秒成功；官方容器核对所选刀剑声、光柱材质、原版妖术师两声与唯一原 BGM，包内 1.3.4-preview／声音路由核同。两独立子进程冷启（1280×720、960×540）均挂载开始菜单并按 EngineLoop 时限正常 OS0 退出、无 Fatal；仅启动与退出，不代键鼠、自然整局或人耳混音。Cook 汇总 0 错误／5 警告，日志中的既有 KeyGlow 缺失原件保留。当前为本地试玩候选，尚无新正式 Release。

<!-- V135_PREVIEW_RECEIPT -->

## v1.3.5-preview：显示与数字可读性

本轮修复游戏高 DPI 感知与无边框所在屏幕物理像素查询，并调整伤害数字的字号、密度透明度和错层排布；伤害账本保持。

- 显示：启用 UE 官方游戏高 DPI 模式。无边框在应用前查询窗口所在显示器的当前物理尺寸，不将逻辑像素或 EDID 原生尺寸当作桌面模式。新包已在 150% 缩放下核到 2560×1600；窗口客户端 960×540 也已核同。
- 录屏：OBS 32.2.2 的“自动”对 `UnrealWindow` 会选择 BitBlt，本机历史日志也确认该选择。可在“窗口采集 → 属性 → 采集方式”选择“Windows 10 (1903或更新版本)”尝试 WGC，或用游戏采集。当前核查没有改变 OBS、驱动、系统缩放、UE 窗口类或 DX12；WGC 的实际录屏效果单独等待反馈。
- 字号：伤害 0–19 使用 20pt，20–100 按十点档递增至 32pt，1000 仍封顶 32pt；实际零伤害仍按既有规则静默。机制图标、配色及完整伤害值保留，金币沿原表现。
- 密度：实际绘出的伤害数字从 12 条到 40 条时，不透明度由 1 降至 0.5；继续增多保持 0.5。正常密度淡化最多50%透明，寿命末的自然消失仍单独淡出。
- 错层：按实际字体、图标和描边矩形搭交错梯子；边缘向下或邻列借位，保留4单位间隔与8单位边距。每条最多96次找位，放不下时暂缓旧条绘制，不改变事件或伤害。

v1.3.5-preview完整Win64 BuildCookRun183.60秒成功，官方Cook已核版本/HighDPI真及继承的声音/材质/唯一原BGM；两正常非unattended、非RenderOffscreen新包在144DPI/150%下，原生窗口960×540、无边框2560×1600客户端尺寸与物理模式核同，逐屏DPI感知上下文为2且实际OS0。Cook汇总0错误/5警告，既有KeyGlow日志保留。实际NativePaint限定正控15字号/同点64融化绘60抑4/24边缘/40邻近均有矩形不交叠、4间隔/8边距、96尝试上限；拥挤alpha正常阶段>=.5，寿命末淡出另计。显示选择/应用/保存入口在PIE核过，独立包物理尺寸另证；没有代物理鼠标/键盘或自然整局，OBS WGC实际采集尚待用户确认。

[本轮反馈与预期](https://github.com/Meracle-sjj/AICompanionDemo/issues/8) · [试玩预览](https://github.com/Meracle-sjj/AICompanionDemo/releases/tag/v1.3.5-preview)。稳定版仍为v1.3.3，旧记录按各自时点保留。

本轮没有新增第三方素材；显示方案复用UE/Windows官方接口，OBS只作为开源行为核查参考，未复制其实现：[Epic UE-310825](https://issues.unrealengine.com/issue/UE-310825)、[OBS32.2.2自动采集选择](https://github.com/obsproject/obs-studio/blob/32.2.2/plugins/win-capture/window-capture.c#L113-L167)、[Microsoft物理显示模式接口](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaysettingsw)。
