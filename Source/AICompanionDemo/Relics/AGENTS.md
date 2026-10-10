# Source/AICompanionDemo/Relics/
> L2 | 父级: /Source/AICompanionDemo/AGENTS.md

Boss 战后遗物的共享 ID 与本局持有状态。组件附着玩家，第一版九种 ID 固定 0..8；每 Boss 对应连续三个 ID，后续只追加。不持久化到下一局。

- BossRelicTypes.h：稳定 EBossRelicId、RelicIdName（资源/HUD）、中文名称与 Boss 编号；无宿主/UI 依赖。
- BossRelicComponent.h / .cpp：九种效果与参数唯一真源，DescribeRelic 从实际参数生成文案，DebugState 返回只读 JSON；AcquireRelic 可在奖励暂停时调用，但周期与伤害只在恢复后推进。紫魂的GetSpiritFrenzyCooldownRemaining/Total只读Clock/NextFrenzyAt；GetSpiritFrenzyActiveRemaining读取队友GetFrenzyRemaining真实Timer（包含药剂延长），不维护效果持续时间副本；未持有或持有者死亡均为0。DebugState的frenzyNextIn/frenzyActiveRemaining共用这些出口。

外部接线契约：玩家每主攻首个真实命中调用一次 NotifyPrimaryAttack，主目标被直击杀死仍结算周围爆炸；散射/穿透/折射的后续分支与任何次生伤害不重复调用。闪现轰击各真实命中通过独立NotifyElementalRayHit仅判定地刺；整次八方爆射同目标一次，不调用主攻震爆/熔核爆弹。主攻与射线均复用TryStoneSpikes，独立入口各先持有统一重入闸，地刺/流血次生不回调该口。队友每直击真实扣血调用 NotifyCompanionHit，元素、处决、溅射伤害不计入吸血。Health 在扣血前 ModifyIncomingDamage；玩家 HandleHurt 带真实受伤量 NotifyPlayerDamaged；成功闪烁调用 NotifyBlink（即使没有 BlinkPyre 也排除传送于距离计数）。狂暴药剂通过 GetPotionFrenzyDurationMultiplier 乘时长，周期狂暴 ActivateFrenzy(Duration,false) 不再乘药剂系数。

次生效果直接写 ICombatant 的 Health，不走共享主攻/元素印记链；统一重入闸防反伤、吸血、主攻爆炸互相递归。伤害只面向活敌接口，因此不伤玩家/队友。普通敌人可减速/击飞，Boss 控制免疫。烈焰护体与闪烁火圈读 EnemyGrunt 冰残留，有冰才单拍伤害×2并消耗；火伤保留灼烧残留供后续冰弹相向反应，不触发无关感电/猎杀印记。

物理/VFX：友方火球复用 BossOrb::LaunchFriendly；石刺复用引擎 Cone/BasicShapeMaterial，无碰撞、无导航、无阴影，最多24件；火圈生成时缓存 WorldStatic 贴地环，最多6个；友方火球最多8颗。周期狂暴沿用队友紫色材质/弹道，附队友位置短紫光环，不使用 Magic 金色粒子。临时Actor与Niagara分别弱登记：VfxLibrary可选回传Explosion真实UNiagaraComponent，SpawnTrackedVfx记录两种返回；友方球命中Fire Actor通过公开TrackTransientVfx登记到玩家遗物组件，球自毁不提前删除火浪。自然LifeSpan/AutoDestroy不延长；死亡或EndPlay直接Destroy/DestroyComponent，迟到登记在持有者已死时立即销毁，友方球结算后来源/遗物组件已不存在也立即销毁火浪，过期弱引用清退。DebugState的liveTrackedVfxActors/liveTrackedVfxNiagara/liveTrackedVfx按实际IsValid逐项计数，不把数组长度当存活数；火圈仍由Pyres单独管理。Clock 只用游戏 DeltaTime，暂停/有效 TimeDilation<0.01（含引擎把0钳为0.0001）不推进；玩家血量归零立即拒绝效果，死亡或 EndPlay 清理火圈、临时 VFX、友方球与石刺，并停 Tick。遗物 inventory 留在死者供界面读取，新局由新玩家组件重置。

默认数值：血链8m/25%实际直击伤治疗玩家；紫魂20s周期/5s狂暴/药剂×1.5；移动15m发球/30伤/3m爆炸；石刺20%/15伤/1.8m/速度0.6×2s；震爆6s蓄好下一主攻/35伤/4.5m/普通敌人向上480cm/s；岩甲15%减伤/10反伤；玩家火爆每主攻12伤/2.5m；火盾10s周期/4s/20%减伤/4m每0.5s灼烧3；闪烁两端3m/4s/每0.5s火伤4。岩甲与火盾减伤乘算，同时持有是总减伤32%。

验收边界：本目录实现不自行编译或控制编辑器，组合根负责关闭编辑器后正常构建、真实 PIE、死亡/暂停/重启/GC 以及资源 Cook 验证。周期火盾和火圈重叠可各自伤害，但冰残留只供首个火伤消耗。低帧率按到期节拍结算，最长单帧补算64拍防极端参数阻塞；过去敌人位置不回放。当前开始等待首周期，震爆也等待6s才备好。

RelicBleedComponent.h/.cpp：StoneSpikes首次范围命中活敌时动态附加，默认5秒/0.5秒每拍3伤（完整10拍30伤），再次命中刷新剩余到至少5秒、不重置已有拍进度、不叠强度。Boss沿Health免伤规则，潜地/护盾期间该拍实际伤害0且不补账。次生伤害不调用主攻击/元素/猎印链；弱来源玩家死亡、目标死亡、到期及EndPlay清血滴并停Tick。复用Epic ParagonGrux P_Grux_ApplyBleed，每次短血滴登记最长1秒、最多8份，默认入场即有可见血滴；暂停/低流速不推进。DescribeRelic从真实Duration/Interval/Damage参数产生卡面说明，UI元数据短句同步“流血”。

[PROTOCOL]: 结构/接口/默认数值变化时更新；共享宿主桥与 UI 归父模块及对应目录。
