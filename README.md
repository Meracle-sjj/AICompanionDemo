# AI队友协作生存原型 · AICompanionDemo 1.0

玩家与一名AI队友在沙丘战场中作战，通过站位、护卫／歼灭切换、局内构筑、资源消费与Boss挑战改变战局。这是一份面向**系统策划岗位**的个人可玩作品，使用 Unreal Engine 5.6.1 与 C++，运行时采用离线 FSM／Utility AI。

**[下载 Windows 1.0 试玩包（1.19 GB）](https://github.com/Meracle-sjj/AICompanionDemo/releases/download/v1.0/AICompanionDemo-Win64-v1.0.zip) · [Release 与校验文件](https://github.com/Meracle-sjj/AICompanionDemo/releases/tag/v1.0)**

**[试玩与操作](PLAYER_GUIDE.md) · [下载与发布记录](Docs/RELEASE.md) · [系统设计说明](Docs/SYSTEM_DESIGN.md) · [规则与迭代证据](Docs/EVIDENCE.md)**

**1.0为作者指定的首版发布标号。**作者已授权直接发布GitHub，后续将在实验室Windows机器试玩并反馈。包体积、真实下载位置和启动验证以发布记录为准；目前独立包已显示开始界面，核心游玩尚未验收。本机测试受Windows网络权限弹窗阻塞，这一限制随首版公开记录。正式实机截图与演示视频待补，已备[110秒录制分镜](Docs/VIDEO_SCRIPT.md)。开发自2026年8月起持续迭代；当前规则快照为2026-10-10。

## 作品希望证明什么

自动战斗怎样保留玩家决策，是这个原型的核心问题。玩家前期依赖队友火力，Lv.6解锁自身自动射击；操作重点落在站位、模式、选卡、探索和应战时机。系统设计围绕保护与追击的取舍、成长供给质量、资源机会成本以及Boss威胁的可读性展开。

| 设计问题 | 当前落地 |
| --- | --- |
| 队友怎样符合护主预期 | 视觉与受击来源共建候选，按距离／威胁／残血评分，切换与视线惩罚控制转火 |
| 自动输出怎样形成协作 | Q切换保护／追击；近距治疗、玩家状态制造与队友结算影响站位和构筑 |
| 每次成长怎样有意义 | 三选一／六选二、前置、互斥、叠加与融合；玩家武器解锁前排除相关卡 |
| 探索怎样接回战斗 | 递增价宝箱、风险祭坛、免费信标、钥匙锁箱形成不同资源出口 |
| Boss怎样考不同能力 | 召唤师清杂破盾、巨像空间预警、蠕虫火区与入地／破土；各提供专属遗物三选一 |

现有内容包括4类常规敌人、3个Boss、29项旧升级显示内容及九种Boss遗物。29项含金币填位和旧高爆兼容条目，不能等同于29张普通池技能；38项显示目录也不代表38套成熟流派。

## 从生存到构筑，再到挑战

![核心循环与资源出口](Docs/diagrams/01-core-loop.png)

经验升级三选一；普通宝箱价格随已开箱数递增；锁箱给15金和六选二；8金风险祭坛提高后续敌人生命与XP收益。3／6／9分钟出现Boss祭坛，接近后应战；12分钟开启狂暴压力。击败蠕虫后生成归途门，先完成奖励领取，再近门松E、重新按E进入感谢页；死亡则结算重开。当前没有十分钟自动胜利。

## AI设计怎样落到可解释规则

![协作AI信息与行动链](Docs/diagrams/02-companion-ai.png)

队友复用 AI Perception、NavMesh 与 MoveTo，Follow／Engage状态与Utility选敌分别负责行为状态和处理对象。护卫跟随玩家、玩家收到治疗增加10%；歼灭追击评分最优敌人、队友伤害增加10%。两模式共享评分与火力，倍率在统一Health入口结算。

玩家受击来源进入候选，解决「评分器无法保护自己没感知到的威胁」；威胁分主导、残血分低配和切换惩罚约束锁目标与横跳。评分板默认关闭，可显式开启分析，日志保留选择原因。玩家感电与猎印再把协作接到伤害层：前者由AI命中两个带电普通敌人触发，后者支持普通敌人及Boss的叠层／消费。

## 根据实际反馈改规则与表达

![火球公平性迭代案例](Docs/diagrams/03-feedback-iteration.png)

作者反馈火球飞过头顶仍扣血。旧版PIE复现球心高出胶囊顶部310cm却扣12血，定位到XY判定忽略高度和地形。修订把可见核心与连续球扫统一为35cm、三维瞄准、首次接触结算及地形同距优先，随后取得自然单发与地形销毁日志，作者确认正常工作。

其他迭代包括补护主候选信息、调整早期强化节拍、使巨像预警与落点同构、统一稀有度与HUD图标，以及将回响过程改为0.1秒1伤、保持名义10DPS并核对低帧补拍半径。详细取舍与已验范围见[系统设计说明](Docs/SYSTEM_DESIGN.md)和[证据索引](Docs/EVIDENCE.md)。

## 本人职责与AI辅助制作

作者一人主导体验目标、系统关系与规则裁决、视觉／声音方向、实际试玩反馈、范围管理和最终验收。AI agent辅助设计细化、代码、工具、资产生成接入、日志分析与文档。本人贡献定位为**个人项目主导／系统设计与体验验收，AI辅助实现**。

运行时游戏AI采用确定性规则；事件台词是固定文本与预合成语音。素材复用Epic模板、Paragon及其他资源，并包含生成插图、封面、音频与程序几何，来源及公开边界见[第三方资源说明](Docs/THIRD_PARTY.md)。本仓库源码与Cook试玩包具有不同分发范围；不能把素材复用写成全部原创制作。

## 可核对的实现与当前边界

| 阅读入口 | 系统职责 |
| --- | --- |
| [CompanionAIController](Source/AICompanionDemo/AI/CompanionAIController.cpp) | 感知候选、Utility、FSM与移动策略 |
| [WaveDirector](Source/AICompanionDemo/AI/WaveDirector.cpp) | 时间压力、经验／金币、成长供给与挑战调度 |
| [BossRelicComponent](Source/AICompanionDemo/Relics/BossRelicComponent.cpp) | 九遗物的本局状态、效果参数与结算 |
| [GameMode](Source/AICompanionDemo/AICompanionDemoGameMode.cpp) | 开始／死亡／归途门／感谢页的流程边界 |
| [UI](Source/AICompanionDemo/UI/AGENTS.md) | 图文选择、技能账本、小地图和Boss血条 |

当前已有作者试玩和局部规则、重启／GC、构建与资源Cook证据。长期性能、全部自然构筑组合、多分辨率与输入回归未形成完整验收；没有规模化外部测试、留存或A/B数据。发布包的实际启动结果和素材限制单独记录于[发布记录](Docs/RELEASE.md)。

引擎与构建环境为UE5.6.1、VS2022、MSVC14.44、Win64；源码入口是 [AICompanionDemo.uproject](AICompanionDemo.uproject)。公开源码的资产依赖、恢复与构建范围请先读[仓库说明](Docs/REPOSITORY.md)，试玩者按[操作指南](PLAYER_GUIDE.md)运行完整Cook包。

图解为原创规则示意，SVG与生成源见[图源目录](Docs/diagrams/README.md)。
