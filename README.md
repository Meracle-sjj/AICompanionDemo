# AI队友协作生存原型 · AICompanionDemo

一份面向**系统策划岗位**的个人可玩作品。玩家与 AI 队友在沙丘战场中协作，通过站位、护卫／歼灭切换、局内构筑、资源消费与 Boss 挑战改变战局。使用 **Unreal Engine 5.6.1、C++、离线 FSM／Utility AI**，游戏运行不调用大模型。

**当前已发布：1.1（已知严重可玩性故障）· 下一修复版本：1.1.1（待实施、未发布）**

作者实测反馈：AI 队友在正常与测试模式均无法移动；显示设置会误拖拽菜单，测试 UI 存在异常换行。下一轮还将美化测试面板，并为免费初始箱增加金色火焰提示。修复目标与验收条件见 [v1.1.1 预期](https://github.com/Meracle-sjj/AICompanionDemo/issues/2)。

[现有 Windows 1.1 包（约 1.19 GB，含上述已知问题）](https://github.com/Meracle-sjj/AICompanionDemo/releases/download/v1.1/AICompanionDemo-Win64-v1.1.zip) · [源码包与 SHA-256 校验](https://github.com/Meracle-sjj/AICompanionDemo/releases/tag/v1.1)

**开发周期：国庆节整个假期＋后续 3 天，约 10 天；非假期利用空闲时间开发。**

[试玩与操作](PLAYER_GUIDE.md) · [系统设计](Docs/SYSTEM_DESIGN.md) · [1.1 更新摘要](CHANGELOG.md) · [素材来源](Docs/THIRD_PARTY.md)

![1.1 实际游戏开始页](Docs/screenshots/1.1-package-menu.png)

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

![核心循环与资源出口](Docs/diagrams/01-core-loop.png)

正常局出生点旁提供一次免费初始箱，不扣金币、不影响付费箱价格。经验升级三选一；付费宝箱价格递增；锁箱提供 15 金和六选二；8 金风险祭坛提高后续敌人生命与经验收益。

Lv.9／11／13 等级奖励来自玩家池，Lv.10／12／14 来自 AI 池；宝箱与 Boss 奖励独立。玩家等级奖励第三槽为 Flip，仅把当前奖励改抽 AI 池，之后仍需选卡，下次奖励归属不变。

3／6／9 分钟出现 Boss 祭坛，12 分钟开启狂暴压力。击败蠕虫后生成归途门，领取奖励后近门重新按 E 进入结束页；死亡可重开。

## 可解释的协作 AI

![协作 AI 的信息与行动链](Docs/diagrams/02-companion-ai.png)

AI Perception 提供感知，Follow／Engage 状态决定行为，Utility 评分决定目标，NavMesh 与 MoveTo 执行移动。护卫跟随玩家，使玩家收到的治疗增加 10%；歼灭追击目标，使队友伤害增加 10%。玩家受击来源进入候选，让队友能回应尚未直接感知到的威胁；感电和猎印将双方协作延伸到伤害结算。

## 试玩反馈如何改变设计

![火球公平性迭代案例](Docs/diagrams/03-feedback-iteration.png)

作者试玩发现火球飞过头顶仍扣血，定位到碰撞仅判断水平距离。修订将可见核心和连续球扫统一为 35 cm，使用三维胶囊判定、首次接触结算与地形优先阻挡，改善攻击表达与实际判定的一致性。

1.1 还加入显示设置、ESC／失焦暂停、事件回看，调整开局供给、队友路径请求、重复猎印说明和后期奖励分工。具体变化见[更新摘要](CHANGELOG.md)。

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

1.1 已完成 Windows 打包并发布，但作者后续实测报告队友无法移动等严重故障，构建与 ZIP 完整性通过不能代表可玩性。1.1.1 尚未实现或发布，修复后须验证最终独立包的实际输入、移动与菜单流程；自然整局、全部构筑和长期性能仍需补充测试。程序尚未签名，Windows 可能显示未知发布者提示。

公开源码供设计与代码评审，**不含 Content 资产，无法单独完整复建**。试玩请下载完整 Windows 包；机制图的 PNG／SVG 位于 [Docs/diagrams](Docs/diagrams)。
