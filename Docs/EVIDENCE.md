# 规则与迭代证据索引

核对日期：2026-10-10。规则引用来自当前文件正文，历史体验结论来自作者已有验收记录；源码存在、局部专项、人工试玩和完整发布验证分别表述。

## 现役规则

| 展示结论 | 可阅读的实现入口 |
| --- | --- |
| 视觉候选、受击来源候选、三维评分与Follow／Engage | [CompanionAIController.cpp](../Source/AICompanionDemo/AI/CompanionAIController.cpp)：`HandlePlayerDamaged`、`EvaluateTargets`、`UpdateBrain` |
| 26／30米感知、0.25秒决策、0.3／0.6／0.1权重、0.1切换惩罚、两10%模式被动 | [CompanionAIController.h](../Source/AICompanionDemo/AI/CompanionAIController.h)：参数与getter；无视线0.35惩罚在同名cpp |
| 模式倍率只在伤害／治疗入口乘算 | [HealthComponent.cpp](../Source/AICompanionDemo/Health/HealthComponent.cpp)：`ApplyDamage`、`Heal` |
| 3／6／9分钟祭坛、12分钟狂暴；无600秒自动胜利 | [WaveDirector.cpp](../Source/AICompanionDemo/AI/WaveDirector.cpp)：构造、`UpdateEndless` |
| 普通三选一、锁箱六选二、卡池前置／互斥／权重；高爆退出现役奖励 | [WaveDirector.cpp](../Source/AICompanionDemo/AI/WaveDirector.cpp)：`EnterUpgradePause`、`RollOffers`、`ApplyUpgrade`、`GrantFreeUpgrade` |
| 宝箱3+2n、风险祭坛生命／XP线性倍率 | [WaveDirector.h](../Source/AICompanionDemo/AI/WaveDirector.h)：`GetNextChestCost`、`GetDifficultyHpMult`、`GetDifficultyXpMult`；[DifficultyAltar.h](../Source/AICompanionDemo/AI/DifficultyAltar.h)：8金默认值 |
| 九种Boss遗物与串行领取，局内持有 | [BossRelicTypes.h](../Source/AICompanionDemo/Relics/BossRelicTypes.h)、[BossRelicComponent.cpp](../Source/AICompanionDemo/Relics/BossRelicComponent.cpp)、[WaveDirectorRelics.cpp](../Source/AICompanionDemo/AI/WaveDirectorRelics.cpp) |
| Lv.6玩家自动射击；闪烁8米／5秒、召回20秒 | [Character.cpp](../Source/AICompanionDemo/AICompanionDemoCharacter.cpp)：`Tick`、`PerformDash`、`RecallCompanion`；[Character.h](../Source/AICompanionDemo/AICompanionDemoCharacter.h)：默认参数 |
| 感电20／40下限与30%／55%成长，4米内两带电杂兵配对 | [CompanionCombatShots.cpp](../Source/AICompanionDemo/AI/CompanionCombatShots.cpp)：`TriggerShockConduction`；默认参数在Controller.h，过载写入在WaveDirector.cpp |
| 玩家穿透＋单跳折射协同；散射互斥；猎印接口覆盖Boss | [CharacterCombat.cpp](../Source/AICompanionDemo/AICompanionDemoCharacterCombat.cpp)、[Combatant.h](../Source/AICompanionDemo/AI/Combatant.h)、[BossBase.cpp](../Source/AICompanionDemo/AI/BossBase.cpp) |
| 蠕虫死亡生成门，领奖优先、松E后新按E；终局停止导演并真暂停 | [WaveDirector.cpp](../Source/AICompanionDemo/AI/WaveDirector.cpp)：`NotifyBossKilled`；[GameMode.cpp](../Source/AICompanionDemo/AICompanionDemoGameMode.cpp)：`NotifyWurmDefeated`、`TryEnterEndingPortal`；[EndingPortal.cpp](../Source/AICompanionDemo/AI/EndingPortal.cpp)、[WaveDirectorEnding.cpp](../Source/AICompanionDemo/AI/WaveDirectorEnding.cpp) |
| 火球单发、三维35cm球扫、地形同距优先 | [BossSummonerBarrage.cpp](../Source/AICompanionDemo/AI/BossSummonerBarrage.cpp)、[BossOrb.h](../Source/AICompanionDemo/AI/BossOrb.h)、[BossOrb.cpp](../Source/AICompanionDemo/AI/BossOrb.cpp) |
| 默认地图与运行时模块 | [DefaultEngine.ini](../Config/DefaultEngine.ini)、[AICompanionDemo.uproject](../AICompanionDemo.uproject) |

部分源码头部、旧注释和兼容字段保留历史口径。上表以现役函数正文为准，例如 `SurvivalGoalSeconds=600` 不构成当前胜利规则，锁箱旧「免费三选一」注释不代表当前六选二流程。

## 已有验收的准确范围

| 日期 | 已有事实 | 仍不能据此宣称 |
| --- | --- | --- |
| 2026-09-06 | M3人工验收登记：攻击／护卫逻辑基本正常；候选评分、切换惩罚和受击来源链形成可观察闭环 | 当前全部Boss／构筑已完成自然整局回归 |
| 2026-10-08 | 升级／遗物队列、九遗物效果、猎印普通怪及三Boss叠层／消费、UI／地图／GC与资源Cook等专项有记录 | 所有自然前置链、概率与全部组合已平衡 |
| 2026-10-08 | 回响局部夹具正常与5FPS限制下中心累计45伤、圈外无伤；历史半径补拍检查有记录 | 5FPS可流畅游玩或已经完成性能优化 |
| 2026-10-09晚 | 旧火球高出胶囊顶部310cm仍扣12血的隔离复现；新版自然单发及地形销毁日志；作者确认正常工作 | 新几何／全地形／全角度／生命周期草案均已执行 |
| 2026-10-09 23:18 | 蠕虫焦痕改专用UV材质；Editor／Game构建与相关资源Cook通过，实际玩家镜头可见；作者实玩确认OK。本地检查点 `beece1d7` | 完整Win64发布、长期性能或全部自然流程已经封板 |

本表整理自原工作档案的 `STATUS.md`、`validation.md`、`decision-log.md`、`system-design-resume-project-brief-20261008.md`、`ui-feedback-integration.md`、`boss-relics-integration.md`、`hunt-mark-boss-integration.md`、`boss-orb-alignment-20261009.md` 和 `wurm-scorch-visibility-20261009.md`。原档案保留失败、原始日志、截图及未验边界，未整体公开于本仓库；这里仅列可公开的摘要，不虚构不可访问的原始日志链接。

## 本次展示整理

README已按当前规则改写，三张机制图为原创可编辑SVG并附PNG预览。它们是规则示意，不是游戏画面；未用概念图替代运行截图。原README备份留在本聊天工作区，游戏逻辑与资产由本次发布负责人统一处理。

试玩包、SHA-256、体积、独立启动与发布限制见[发布记录](RELEASE.md)。资源和授权边界见[第三方资源说明](THIRD_PARTY.md)。
