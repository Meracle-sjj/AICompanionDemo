# Source/AICompanionDemo/Events/
> L2 | 父级：../AGENTS.md

R10本局事件记录的数据层。`URunEventLog`只为Game/PIE World创建，每世界独立保存最近100条`FRunEventEntry`，超量淘汰最旧记录；不持有Actor、音频或界面对象，不推进玩法，也不生成示例事件。

`Publish(UWorld*, Speaker, Text, EventId)`仅由真实事件所有者在游戏线程调用；保留原发言者和文本，空文本/无世界静默返回。`Seq`从1持续递增，`RunTime`读取游戏世界时间，`EventId`仅作来源元数据，重复同类发言仍分别记录。`GetEntries()`返回按发生顺序排列的const引用，HUD通过`GetRevision()`判断更新，不缓存跨Publish的数组元素地址。每条记录同时进入`[RunEvent]`引擎日志。

重新加载关卡创建新World即得到空记录与重置序列；Deinitialize清理。若调用方复用同一个World开启另一局，必须另行定义显式清局协议，不能声称World生命周期自动覆盖该情况。暂停中不Tick本模块，时间只来自世界。

成员：`RunEventLog.h`定义共享字段和静态发布/只读接口；`RunEventLog.cpp`实现发布、Game/PIE过滤、有界淘汰及清理。调用方Controller/Character/Director和HUD由集成代理负责。源码存在不等于运行、重开或独立包验收通过。

[PROTOCOL]: 接口、容量、World范围或清局协议改变时更新本文件，并检查父级成员地图。
