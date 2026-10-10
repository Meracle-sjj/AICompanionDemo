# AICompanionDemo - UE5 C++ 第三人称 AI 队友演示

10-09晚蠕虫视觉/声音修订：BossImpact采用首爆1.8s同源采样；火区自有Niagara及独立爆闪均硬寿命/死亡/EndPlay回收。用户实玩确认声音/残火正常，另反馈焦痕不可见；现入/出土焦痕改为Content/Bosses/Wurm独立程序UV炭黑Decal，避开原Cave9的Niagara颜色通道世界Z约定，不改素材包/地形。最多4件、6s退场/末2s按ScorchOpacity渐淡，构造期硬引用保留Cook。当前构建/真实画面与边界见指挥部docs/wurm-scorch-visibility-20261009.md、wurm-fx-cleanup-20261009.md与STATUS。

UE 5.6.1 + C++（Win64）+ VS 2022 NativeGame 工具链（MSVC 14.44）

北极星：AI 队友闭环 = AIPerception 感知 → C++ FSM + utility scoring 决策 → MoveTo 行为 → 屏幕调试/DrawDebug 可解释。MVP 禁 RL/BT/StateTree/EQS（选型依据：任务仓库 docs/research.md R6）。

<directory>
Source/ - C++ 游戏模块与构建目标（模块地图见 Source/AICompanionDemo/AGENTS.md；UI 展示层见 Source/AICompanionDemo/UI/AGENTS.md；Relics/ 为附着玩家的九遗物组件，见 Source/AICompanionDemo/Relics/AGENTS.md）
Config/ - 引擎与项目默认配置（Maps&Modes、输入等 ini）
Content/ - 模板与游戏资产；UI/UpgradeCards/ 为29张升级无字纹理，UI/StartScreen/ 为合作开始页封面，UI/BossRelics/ 为九张Boss遗物无字纹理，分别见各目录AGENTS.md；UpgradeCardPresentation按稳定ID提供升级/遗物资源路径，UpgradeCardView与CompanionHud消费同一资料表。Bosses/Wurm/ 为三份蠕虫StaticMesh与两材质，由Vfx/WurmBodyVisual附属到原命中代理；Environment/Desert/ 为现役沙地与外围砂岩资产。字符串加载目录的Cook归属统一见Config/DefaultGame.ini及对应资产地图。
Builds/ - 打包产物输出（.gitignore 排除，不入库）
Docs/ - 求职展示与公开交付文档（SYSTEM_DESIGN/EVIDENCE/VIDEO_SCRIPT、THIRD_PARTY/REPOSITORY、RELEASE/RELEASE_VALIDATION；diagrams为三张原创SVG/PNG及可维护生成源）
Tools/ - 公开发布工具（Make-ReleaseSnapshot.ps1白名单导出当前工作树、Build-Windows.ps1检查完整资产候选并调用UE官方UAT；不stage/push/删文件）
README.md / PLAYER_GUIDE.md - 系统策划作品入口与玩家操作；公开源码不含第三方Content，独立包状态以Docs/RELEASE为准
Content/Environment/Desert/ - 现役504m Landscape沙地与外围砂岩视觉资产（两材质/三原创StaticMesh，17地形对象绑定、42件外围常驻无碰撞景观；资源契约/来源与保存规则见该目录AGENTS.md，真实运行/Cook与边界见指挥部docs/desert-map-integration.md）
</directory>

<config>
AICompanionDemo.uproject - 模块声明（Runtime/PostDefault，AdditionalDependencies: Engine/AIModule/UMG；插件 StateTree/GameplayStateTree/ModelingToolsEditorMode/RemoteControl/PCG/ChaosNiagara）。PostDefault确保Default插件脚本类先注册，再让游戏CDO读取FireMid；10-08现役地图Cook修正及证据见指挥部desert-map-integration.md。
</config>

构建命令（引擎位于 A:\UE_5.6）：
- 编译：`A:\UE_5.6\Engine\Build\BatchFiles\Build.bat AICompanionDemoEditor Win64 Development -project="A:\UnrealProjects\AICompanionDemo\AICompanionDemo.uproject"`
- 打包：RunUAT BuildCookRun（Development / Win64，M0 验证闭环）
- 任务书与调研文档在上层任务仓库 A:\craziestIdea\（basicIdea.txt + docs/）

2026-10-08 当前结构：AI/WaveDirector持有本局64×64连续探索场与升级/锁箱/Boss遗物奖励队列；UI只复制地图显示快照。Boss各三项遗物通过独特金红卡面领取，不再自动授予旧蠕虫高爆卡；Relics/BossRelicComponent负责效果，猎印通过ICombatant在普通敌人和BossBase家族叠层/队友消费；HuntMarkVisual仅负责头顶实际层数字、1–2金黄/3+红色展示。本次小地图/遗物/猎印交付状态以A:/craziestIdea/docs/STATUS.md和实际验收报告为准，本成员地图不替代编译、PIE、重启GC或Cook结果。

2026-10-09 战斗/终局结构：WaveDirectorMobEffects接全局狂暴与杂兵死亡伤虫，EnemyEnrageVisual负责贴身紫红Niagara；RelicBleedComponent为地刺活敌附可刷新流血，伤害仍归Health。BossWurmHazards统一落火岩块/火区所有权与清杂反馈，素材复用Bosses/Wurm岩甲/双材质。GameMode在蠕虫死亡后生成EndingPortal，导演奖励队列优先；玩家近门松键后按E进入EndingScreen并真正暂停世界。EndingScreen使用Content/UI/EndingScreen原创无字背景（源及提示词在指挥部assets/ending-screen），显式AlwaysCook，真实时钟1秒渐入与按钮保护，反馈邮箱为用户确认值，重开走关卡重载。

本日新反馈结构：CompanionCombatShots统一AI弹道与双带电目标消费/伤害，ShockMarkVisual固定图形和ShockArcActor自有Spline分管标记与放电，不改源Niagara。基础感电给玩家一次70%分叉并避开已有弹路命中目标。WaveDirectorWurmPressure与BossWurmEncounter共享虫战10杂兵/3妖术准入和宿主退出，CharacterWurmCamera自持实际BP臂长基线/1.6倍插值与短震。Q模式数值在Health单入口读取实际Controller（护卫玩家治疗+10%、歼灭AI伤害+10%），HUD只显示真实来源；模式增伤不使领域触发感电。信标同路径两SoundWave改用冷峻自然女声v2，源在指挥部assets/beacon-voice/v2，v1保留。编译/真实专项结果与未验边界以指挥部STATUS及shock-wurm-mode-refinement-20261009.md为准。

八项反馈修订结构：AI/BossSummonerBarrage负责单发弹幕与活弹所有权（10-09晚由三发回调，频率/速度保持），BossOrb以可见35cm核心连续球扫玩家胶囊/BlockPawn实体、地形先到即销毁；EnemyGruntSummoned负责紫色双倍攻速妖术师；BossBase全家族共享玩家拉远镜头，UI/CompanionHudBosses集中显示屏幕上方真血条。玩家穿透与一次折射可协同，AI保持互斥；Nova继承玩家当前状态与独立地刺判定，不重复主攻遗物。感电默认两端基础20/过载40，模式数值与实际飘字同步。Content/SFX/Boss为CC0真实爆炸独立BossImpact，Ending稳定路径使用无飘带/无佩剑v3战损双人图。源码/资源导入不等于运行验收，当前结果见指挥部docs/combat-polish-20261009.md、boss-orb-alignment-20261009.md和STATUS。

[PROTOCOL]: 本文件是 L1 唯一真源（CLAUDE.md 为 @AGENTS.md 导入桩）；架构变更/顶级模块增删时更新此文件
