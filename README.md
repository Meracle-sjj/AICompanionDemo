**[实机演示视频（Bilibili）](https://www.bilibili.com/video/BV1S9pn6KEZn/)**

> 当前实机视频展示的是 **v1.3.1** 版本；后续将继续更新。

# AI队友协作生存原型 · AICompanionDemo

**v1.3.3 本地完整构建与限定验证已完成。** [v1.3.3 正式交付入口](https://github.com/Meracle-sjj/AICompanionDemo/releases/tag/v1.3.3)；玩家附件为 `Player-Win64-v1.3.3.zip`。[本轮修订记录](https://github.com/Meracle-sjj/AICompanionDemo/issues/6)。旧版本标签、附件与反馈继续保留。

一份面向**系统策划岗位**的个人可玩作品。玩家与 AI 队友在沙丘战场中协作，通过站位、护卫／歼灭切换、局内构筑、资源消费与 Boss 挑战改变战局。使用 **Unreal Engine 5.6.1、C++、离线 FSM／Utility AI**，游戏运行不调用大模型。

本轮将普通攻击改为白色小剑；火焰亮红、流血暗红、雷电紫色，融化使用冰晶＋火焰双图与混色数字。数值显示减伤后的完整伤害，致死不按剩余血量缩小。本体冰＋队友火、队友冰＋本体火各触发一笔独立等额融化；例如原火5与融化5分别显示，保留两笔生命结算。

本体寒霜普通弹的直接、分叉与弹跳命中，每0.5游戏秒向2米内敌人传染；感染者不接力，雪花标记不改变身体颜色。闪现爆射仍是本体技能，伤害与附冰按本体归属；队友辉耀不能融化队友自己的冰。妖术师改用两条更清楚的原创短危机声，保留叠响与暂停限制；队友加速卡增加固定配音“现在，我更能跟上你的脚步了。”。开始页显示设置使用完整按钮，免费箱E居中，Boss登场保留事件提示并移除重复左上红字。

本轮已完成188.76秒完整Windows构建、官方Cook资源与配置核验、受控专项和两次独立冷启／正常退出。三条新声音已实际导入、收录并核对限定PIE起播；人耳听感与同人相似度尚未确认。两档窗口覆盖标题与设置、测试死亡重开、正常局奖励暂停及返回主菜单退出；自然整局、全部构筑、长期性能、异机与持续WASD尚未验证。游戏运行离线，无需大模型或在线语音服务。

已发布的 v1.3.1 保留顺序对白与紧急抢占、FREE! 与锁／价格／钥匙提示、贴坡宝箱、行走火球爆炸、地刺短铡刀音、地下碎岩声及结束页清账。

[v1.3.2 反馈与修订预期](https://github.com/Meracle-sjj/AICompanionDemo/issues/5)保留本轮目标；[v1.3.1 反馈记录](https://github.com/Meracle-sjj/AICompanionDemo/issues/4)和旧版本标签、附件与反馈轨迹继续保留。

**开发周期：国庆节整个假期＋后续 3 天，约 10 天；非假期利用空闲时间开发。**

[试玩与操作](PLAYER_GUIDE.md) · [系统设计](Docs/SYSTEM_DESIGN.md) · [试玩反馈与改进](Docs/PLAYTEST_FEEDBACK.md) · [1.3.3 修订摘要](CHANGELOG.md) · [素材来源](Docs/THIRD_PARTY.md)

## 自动战斗中的玩家决策

玩家前期依赖队友火力，Lv.6 解锁自身自动射击；操作重点落在站位、模式、选卡、探索和应战时机。系统围绕保护与追击的取舍、成长供给、资源机会成本和 Boss 威胁的可读性展开。

| 设计问题 | 当前实现 |
| --- | --- |
| 队友怎样符合护主预期 | 感知与受击来源共同提供候选，按距离、威胁、残血评分，切换惩罚控制转火 |
| 自动输出怎样形成协作 | Q 切换护卫／歼灭；治疗、状态制造与队友结算影响站位和构筑 |
| 每次成长怎样有意义 | 前置、互斥、叠加与融合；Lv.9 起玩家／AI 等级奖励交替 |
| 探索怎样接回战斗 | 递增价宝箱、风险祭坛、免费信标与钥匙锁箱提供不同资源出口 |
| Boss 怎样考不同能力 | 召唤师清杂破盾、巨像空间预警、蠕虫火区与入地／破土；各有专属遗物三选一 |

目前包含 4 类常规敌人、3 个 Boss、九种 Boss 遗物，以及 32 个升级／操作显示条目；其中含金币填位和 Flip 操作，不将显示条目数等同于技能数。

## 核心循环与成长

![双人协作清敌，收获经验与金币，选卡构筑，强化后回到战斗；Boss奖励与归途门为挑战支线](Docs/diagrams/01-core-loop-ai-v2.png)

清敌获得经验与金币，选卡和探索将资源转成强化，再回到更高压力的战斗。经验三选一、递增价宝箱、钥匙锁箱与风险祭坛提供不同构筑机会；正常局有一次免费初始箱。

Lv.9 起等级奖励在玩家／AI 池之间交替，F 可将本次玩家奖励转交队友。Boss 各有专属遗物三选一，独立于等级与宝箱奖励。

3／6／9 分钟出现 Boss 祭坛，12 分钟开启狂暴压力。击败蠕虫、领完奖励后，近归途门按 E 结束。图为 AI 生成的机制概念示意；详细规则见[系统设计](Docs/SYSTEM_DESIGN.md)。

## 可解释的协作 AI

![协作 AI 的信息与行动链](Docs/diagrams/02-companion-ai.png)

AI Perception 提供感知，Follow／Engage 状态决定行为，Utility 评分决定目标，NavMesh 与 MoveTo 执行移动。护卫跟随玩家，使玩家收到的治疗增加 10%；歼灭追击目标，使队友伤害增加 10%。玩家受击来源进入候选，让队友能回应尚未直接感知到的威胁；感电和猎印将双方协作延伸到伤害结算。

## 试玩反馈如何改变设计

已改善的火球空间判定与伤害数字，连同原漫画图解，集中在[试玩反馈与设计改进](Docs/PLAYTEST_FEEDBACK.md)。火球问题由作者的朋友在开发阶段试玩内部版本时发现；伤害字体、类型图标与配色由作者反馈推动。

声音仍在根据试听迭代。页面分别记录已改善的规则、当前听感反馈与下一步目标；后续关键改动继续沿用漫画图解。[当前反馈与修订预期](https://github.com/Meracle-sjj/AICompanionDemo/issues/7)。

## 本人职责与代码入口

作者一人主导体验目标、系统规则、视觉／声音方向、试玩反馈、范围管理和最终验收；AI agent 辅助设计细化、代码、工具和资产制作。贡献定位为**个人项目主导／系统设计与体验验收，AI 辅助实现**。素材包含复用资源与生成资产，来源见[素材说明](Docs/THIRD_PARTY.md)。

| 实现入口 | 职责 |
| --- | --- |
| [CompanionAIController](Source/AICompanionDemo/AI/CompanionAIController.cpp) | 感知、Utility、FSM 与移动策略 |
| [WaveDirector](Source/AICompanionDemo/AI/WaveDirector.cpp) | 时间压力、资源、成长供给与挑战调度 |
| [BossRelicComponent](Source/AICompanionDemo/Relics/BossRelicComponent.cpp) | 九遗物状态与效果结算 |
| [GameMode](Source/AICompanionDemo/AICompanionDemoGameMode.cpp) | 开始、暂停、设置、死亡与结束流程 |
| [CompanionHud](Source/AICompanionDemo/UI/CompanionHud.cpp) | 选卡、技能账本、小地图与 Boss 血条 |
| [RunEventLog](Source/AICompanionDemo/Events/RunEventLog.cpp) | 本局事件记录 |
| [HealthComponent](Source/AICompanionDemo/Health/HealthComponent.cpp)／[CombatFeedbackSubsystem](Source/AICompanionDemo/UI/CombatFeedbackSubsystem.cpp) | 统一生命结算与实际伤害漫画反馈 |

公开源码供设计与代码评审，**不含 Content 资产，无法单独完整复建**。试玩请下载完整 Windows 包；AI 概念图 PNG 与原有机制图位于 [Docs/diagrams](Docs/diagrams)。
