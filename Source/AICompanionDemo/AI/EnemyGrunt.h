/**
 * [INPUT]: 依赖 Engine 的 ACharacter/CharacterMovementComponent（重力行走）/SkeletalMeshComponent 的 PlayAnimation 与 SetAnimationMode、AnimSequenceBase 的 GetPlayLength（动画时长自报）、AIModule 的 AISense_Sight（视觉源注册）、Kismet 的 GetPlayerPawn（猎手目标查询）、本模块 HealthComponent 的 SetMaxHealth（变体血量定型）、本模块 XpPickup（死亡掉落类）、本模块 TemporaryPickup（E5 暂态掉落类）
 * [OUTPUT]: 对外提供 AEnemyGrunt——拟人猎手敌人二代：胶囊互撞、贴地行走、**追击全自驱寻路（历 D12 MoveToActor→D16 输入常开→D17 归一：FindPathToActorSynchronously 每 0.5s 算路存路点串，HuntPlayer 沿"路径下一路点"喂输入——加速度与绕障同源，滑步案与双驱动打架案双根治；无控制器，bRunPhysicsWithNoController 主开关）**、贴脸挥拳节拍（攻击动画与伤害同拍，伤害走 HealthComponent 唯一入口；出拳双闸门：|ΔZ|≤150 层高 + ObjectType LOS——隐形敌人案根治，穿层/隔板绝不挥拳）、ragdoll 布娃娃死亡 + 定时自毁；实现 ICombatant 供队友评分器跨形体识别；M3.75 增补：EGruntVariant 三变体（Standard 基线/Rusher 快而脆/Brute 慢而硬，ApplyVariant 出生定型——移速/血量/伤害/间隔/体型/色调/经验值/蛋糕掉率八项参数集，数据差异不造新类）+ XpPickup 经验掉落（喂 XpValue，收集上报的组合根在 GameMode）+ HealthPickup 柠檬蛋糕概率掉落（蛮兵必掉，收集直接作用于玩家血量）+ 滑行呆移探针（bEnableMoveProbe，1Hz 取证器，确认修复后关）
 *  猎印/狂暴显示各有组件；感电5秒残留以动态ShockMarkVisual固定闪电字形同步，半秒持续配对不消费；死亡隐藏/EndPlay释放。ApplySummonedWarlock一次定型紫色双倍攻速召唤物，无常规掉落。
 * [POS]: AI/ 的敌对方实体（替代球体 EnemyTarget 的猎手位置，WaveDirector 的生成目标）；外观与动画构造期 C++ 内联装配（与队友 BP 的已验证组合同款：SKM_Manny_Simple + ABP_Unarmed——2026-09-07 用户要求配置零人工的取舍，BP 子类可覆盖），球类保留作回归安全网
 *  v1.3.3正式：双来源冰钟独立；本体真实弹命中才授予半秒/200cm传播资格，雪花纯显示，异人融化只消费相反来源。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AI/Combatant.h"
#include "EnemyGrunt.generated.h"

class UHealthComponent;
class UAIPerceptionStimuliSourceComponent;
class UAnimationAsset;
class AXpPickup;
class AHealthPickup;
class AGoldPickup;
class ATemporaryPickup;
class USphereComponent;
class UHuntMarkVisual;
class UEnemyEnrageVisual;
class UShockMarkVisual;
class UFrostMarkVisual;

/** 假人变体（M3.75 B3）：数据差异不造新类——同一套参数集槽位，ApplyVariant 出生定型 */
UENUM(BlueprintType)
enum class EGruntVariant : uint8
{
	Standard,   /**< 标准假人：基线参数（M3.5 行为基线） */
	Rusher,     /**< 猎犬：快速低血贴脸快——威胁分/仇恨覆盖的主要制造者 */
	Brute,      /**< 蛮兵：慢速高血高伤压阵——残血分与转火时机的戏剧素材 */
	Warlock,    /**< 妖术师（09-25 Boss 变体）：缓慢巨物，中距蓄力射线（红细线预警→粗束结算，走位可躲） */
	VoidRusher  /**< 虚空裔（09-30 召唤师召唤物）：比野生猎犬更硬更痛但稍慢可风筝，虚空紫染，巫术造物不掉战利品 */
};

UCLASS()
class AICOMPANIONDEMO_API AEnemyGrunt : public ACharacter, public ICombatant
{
	GENERATED_BODY()

public:
	AEnemyGrunt();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ---------- ICombatant：队友评分器的识别契约 ----------
	virtual bool IsDead() const override;
	virtual bool IsEngagingPlayer() const override;
	virtual UHealthComponent* GetHealthComponent() const override { return HealthComponent; }
	/** 全局狂暴只改变身体显示；数值成长仍归导演，重复开启不追加粒子组件。 */
	UFUNCTION(BlueprintCallable, Category="Enemy|Enrage") void SetEnraged(bool bInEnraged);
	UFUNCTION(BlueprintPure, Category="Enemy|Enrage") bool IsEnraged() const;
	UFUNCTION(BlueprintPure, Category="Debug|Enrage") FString DebugEnragedState() const;

	/** 近身攻击触发距离（厘米）：进入即停步开打 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt", meta = (ClampMin = "50.0"))
	float PlayerEngageRange = 160.f;

	/** 对玩家的近身伤害：单发数值（默认 5×3 敌/1.5s 间隔 ≈ 10 dps——观察者玩法的张力线，与球体时代同参数） */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt", meta = (ClampMin = "1.0"))
	float AttackDamageToPlayer = 5.f;

	/** 对玩家的攻击间隔（秒）：挥拳动画与伤害同拍出发 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt", meta = (ClampMin = "0.1"))
	float PlayerAttackInterval = 1.5f;

	/** 追击寻路重发节拍（秒）：MoveToActor 同目标重发=路径增量刷新，防每帧重请求。
	 * v15 坡地调优：0.5 在坡地锯齿路径上走老路太久（体感迟滞），0.3 保持贴坡 fresh */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt", meta = (ClampMin = "0.1"))
	float PathRefreshInterval = 0.3f;

	/** 寻路失败后的直线回退窗（秒）：窗内每一帧都喂直线输入（旧版每节流拍只回退一帧、其余帧零输入
	 * ——落在墙顶等无 NavMesh 处的敌人"一蹿一冻"原地卡死的根因）；窗满重试寻路 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt", meta = (ClampMin = "0.1"))
	float NavFallbackWindow = 1.f;

	/** 滑行呆移探针（1Hz，仅在移动时记录：速度/加速度/动画模式/实例/控制器）——
	 * "凭空平移无跑步姿势"案（2026-09-12）的取证器，根因定罪后关掉 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Debug")
	bool bEnableMoveProbe = true;

	// ---------- M3.75 变体与掉落 ----------

	/** 当前变体：默认 Standard（GameMode 静态阵与 BP 实例行为与 M3.5 完全一致）；WaveDirector 出生后经 ApplyVariant 定型 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Variant")
	EGruntVariant GruntVariant = EGruntVariant::Standard;

	/** 死亡掉落的经验值（XpPickup 携带；变体定型覆写：猎犬 1 / 标准 2 / 蛮兵 5） */
	UPROPERTY(EditAnywhere, Category = "Enemy|Variant", meta = (ClampMin = "0"))
	int32 XpValue = 2;

	/** 掉落物类：默认裸 C++ 晶体（AXpPickup）；置空则不掉落 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Variant")
	TSubclassOf<AXpPickup> XpPickupClass;

	/** 死亡掉落回血蛋糕的概率：变体定型覆写（猎犬 0.03 / 标准 0.08 / 蛮兵 0.5——用户复测三定调"爆率太高几乎不需回血"，下调） */
	UPROPERTY(EditAnywhere, Category = "Enemy|Variant", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HealDropChance = 0.08f;

	/** 回血蛋糕类：默认裸 C++ 柠檬蛋糕（AHealthPickup）；置空则不掉落 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Variant")
	TSubclassOf<AHealthPickup> HealPickupClass;

	/** 死亡掉落金币的概率：变体定型覆写（猎犬 0.25 / 标准 0.2 / 蛮兵 1.0 必掉——宝箱经济的收入端，E3） */
	UPROPERTY(EditAnywhere, Category = "Enemy|Variant", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GoldDropChance = 0.2f;

	/** 一次掉落的金币枚数：蛮兵 3 枚（重怪=小金库） */
	UPROPERTY(EditAnywhere, Category = "Enemy|Variant", meta = (ClampMin = "1"))
	int32 GoldBurst = 1;

	/** 金币类：默认裸 C++ 金币（AGoldPickup）；置空则不掉落 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Variant")
	TSubclassOf<AGoldPickup> GoldPickupClass;

	/** E5 暂态道具掉落概率：变体定型覆写（猎犬 0.02 / 标准 0.02 / 蛮兵 0.2——精英掉宝节拍） */
	UPROPERTY(EditAnywhere, Category = "Enemy|Variant", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RushDropChance = 0.02f;

	/** 暂态道具类：默认裸 C++ 三件套（ATemporaryPickup）；置空则不掉落 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Variant")
	TSubclassOf<ATemporaryPickup> RushPickupClass;

	// ---------- 高处反制（M3.75，蛮兵专属远程——用户 2026-09-13 提案：可达性触发、部分敌人反制） ----------

	/** 该变体是否具备高处投掷能力：仅蛮兵 true（全面远程会失去近战张力，用户原话"容易发生暴力"） */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt")
	bool bRangedCapable = false;

	/** 高度差触发阈值（厘米）：与近战层高闸（kMeleeHeightTolerance=150）无缝衔接——用户复测四 2026-09-13 定调消除"近战够不着/远程没到阈值"的灰区呆立 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt", meta = (ClampMin = "100.0"))
	float RangedEngageDeltaZ = 150.f;

	/** 投掷伤害：单发数值 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt", meta = (ClampMin = "1.0"))
	float RangedDamageToPlayer = 8.f;

	/** 投掷间隔（秒）：比近战拳慢——远程是反制手段不是主输出 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt", meta = (ClampMin = "0.5"))
	float RangedAttackInterval = 2.5f;

	/** 投掷命中率：玩家在移动、掩体后的走位博弈空间 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float RangedHitChance = 0.75f;

	/** 通用减速只改移动；寒霜雪花由独立状态显示，不覆盖变体身体色。 */
	void ApplySlow(float Duration, float SpeedFactor);

	// ---------- 元素标记（09-29 元素反应体系，gameplay-proposals 十二节：玩家=状态制造器，AI=反应触发器） ----------

	/** 灼烧残留只保留既有持续火焰表现；冰弹不再反向引爆或放大伤害。 */
	void RefreshBurnResidue(float Duration = 1.5f) { BurnResidue = Duration; }
	float GetBurnResidue() const { return BurnResidue; }

	/** 玩家子弹挂电，固定闪电标记与5秒残留同源；持续配对不消费，重复命中刷新至5秒。 */
	UFUNCTION(BlueprintCallable, Category="Enemy|Elements")
	void ApplyShockResidue(float Duration = 5.f);
	UFUNCTION(BlueprintPure, Category="Enemy|Elements")
	float GetShockResidue() const { return IsDead() ? 0.f : ShockResidue; }
	UFUNCTION(BlueprintPure, Category="Debug|Elements") FString DebugShockState() const;
	/** 两来源冰钟独立；仅本体真实弹命中传DirectShotSource，Nova/传播不授予接力资格。 */
	void ApplyPlayerFrost(float Duration, float SpeedFactor, AActor* DirectShotSource = nullptr);
	void ApplyCompanionFrost(float Duration, float SpeedFactor);
	float GetPlayerFrostResidue() const { return IsDead() ? 0.f : PlayerFrostResidue; }
	float GetCompanionFrostResidue() const { return IsDead() ? 0.f : CompanionFrostResidue; }
	float GetFrostResidue() const { return FMath::Max(GetPlayerFrostResidue(), GetCompanionFrostResidue()); }
	/** 异人火成功后只消费对应冰来源；本体冰消费同时清其传播资格，减速仍按原到期恢复。 */
	void ConsumeFrostResidue(bool bPlayerSource);
	UFUNCTION(BlueprintPure, Category = "Debug|Elements") FString DebugFrostState() const;

	/** 猎杀印记层数（玩家弹命中叠 1，上限随"印记强化"卡成长）——AI 命中消耗全部层=处决反应（每层额外伤） */
	virtual void AddHuntMark(int32 MaxStacks) override;
	virtual int32 GetHuntMarks() const override { return MarkStacks; }
	virtual int32 ConsumeHuntMarks() override;
	UFUNCTION(BlueprintPure, Category = "Debug|HuntMark")
	FString DebugHuntMarkState() const;

	/** 元素残留衰减（Tick 调用）：灼烧/感电走自然时——不衰减则残留体永续，反应门槛失真 */
	void TickElementResidue(float DeltaSeconds);

	/** 减速到点只恢复变体基线移速，身体配色不参与寒霜。 */

	// ---------- Warlock 蓄力射线（09-25 Boss 变体，用户点名"红射线预警→实体伤害"） ----------

	/** 蓄力倒计时：>0 = 施法站桩（红色细线预警中，玩家走位即躲）；<0 = 空闲 */
	float BeamChargeCountdown = -1.f;

	/** 蓄力能量汇聚球×8（09-30 特效升级：程序化 Mesh 粒子——蓄力期从四周向胸口收敛，仅 Warlock 显示） */
	TObjectPtr<USphereComponent> ChargeOrbs[8];
	double ChargeStartTime = 0.0;
	void UpdateChargeOrbs();
	float AccumArcSpawn = 0.f;   /** 电弧链刷新节拍（0.12s/节） */
	float ElementFxAccum = 0.f;  /** 元素残留闪烁节拍（0.3s——TickElementResidue 用） */

	/** 蓄力电弧特效 Actor（VfxLibrary Electric 常驻，结算销毁） */
	TObjectPtr<AActor> ChargeVfx;

	/** 射线冷却（秒） */
	float BeamCooldown = 2.f;

	/** 锁定射线起讫（进入蓄力瞬间取玩家当时位置——移动即脱离判定线） */
	FVector BeamStart = FVector::ZeroVector;
	FVector BeamTarget = FVector::ZeroVector;

	/** 蓄力时长（秒）：红细线预警期=走位窗口 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Warlock", meta = (ClampMin = "0.3"))
	float BeamChargeSeconds = 1.2f;

	/** 射线伤害 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Warlock", meta = (ClampMin = "1.0"))
	float BeamDamage = 15.f;

	/** 射线冷却（秒） */
	UPROPERTY(EditAnywhere, Category = "Enemy|Warlock", meta = (ClampMin = "1.0"))
	float BeamCooldownSeconds = 5.f;

	/** 命中判宽（厘米）：玩家到锁定线的垂直距离 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Warlock", meta = (ClampMin = "20.0"))
	float BeamHitWidth = 70.f;
	UFUNCTION()
	void RecoverFromSlow();

	/** 元素残留体（秒）：灼烧/感电——玩家侧反应判定面，Tick 自然衰减 */
	float BurnResidue = 0.f;
	float ShockResidue = 0.f;
	float PlayerFrostResidue = 0.f;
	float CompanionFrostResidue = 0.f;
	float FrostSpreadSourceRemaining = 0.f;
	float FrostSpreadAccum = 0.f;
	float FrostSpreadDuration = 0.f;
	float FrostSpreadSpeedFactor = 1.f;
	int32 FrostSpreadPulses = 0;
	int32 FrostSpreadApplications = 0;
	TWeakObjectPtr<AActor> FrostSpreadSource;
	void UpdateFrostVisual();
	void TickFrostSpread(float DeltaSeconds);
	void ReceiveFrostSpread(float Duration, float SpeedFactor);

	/** 猎杀印记层数：玩家弹叠、AI 弹消耗——"玩家攒、AI 爆"的节奏载体 */
	int32 MarkStacks = 0;

	/** 出生定型（可重复调用）：把变体参数集覆写到移速/血量/伤害/间隔/体型/色调/经验值——数据差异≠类型爆炸（提案 B3） */

	/** 难度注入（09-25 难度祭坛）：血量上限/经验值按难度乘子上浮—— ApplyVariant 之后由刷怪环调用 */
	void ApplyDifficulty(float HpMult, float XpMult);
	void ApplyVariant(EGruntVariant InVariant);
	/** 召唤师第2/3波的紫色妖术师：一次定型为双倍攻击节拍，无普通战利品。 */
	UFUNCTION(BlueprintCallable, Category="Enemy|Summon") void ApplySummonedWarlock();
#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, Category="Enemy|Debug") FString DebugSummonedWarlockState() const;
#endif

	/** 攻击动画：构造期 C++ 内联引用 MM_Attack_01（配置零人工），BP 子类/实例可覆盖 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Anim")
	TObjectPtr<UAnimationAsset> AttackAnimation;

protected:
	/** 逼近/攻击一体节拍：超距喂移动输入（重力行走，CharacterMovement 管贴地与互撞），贴脸按节拍挥拳 + ApplyDamage（玩家死亡/自身死亡即停手） */
	void HuntPlayer(float DeltaSeconds);

	/** 死亡回调：清攻击回切定时器、ragdoll 布娃娃（官方 CombatEnemy 同款三件套）、关胶囊碰撞、3s 自毁 */
	UFUNCTION()
	void HandleDeath(UHealthComponent* HealthComp);

private:
	bool bSummonedWarlock = false;
	/** 攻击动画播完回调：回 ABP 恢复步态（时长由资产 GetPlayLength 自报，SetTimer 精确驱动；IsDead 早退防腰斩死亡动画） */
	UFUNCTION()
	void RestoreLocomotionAnim();

	/** 出拳 LOS 闸门：胸口连线只认 WorldStatic/WorldDynamic 几何（Pawn 豁免）——被地板/墙挡住绝不隔空挥拳 */
	bool HasClearLineToPlayer(const APawn* PlayerPawn) const;

	/** 追击寻路（引擎 NavMesh 管线，全自驱版）：FindPathToActorSynchronously 节流算路存路点串 + 失败回退窗；
	 * 移动 100% 由 HuntPlayer 沿路点喂输入（单一驱动源——滑行呆移案与双驱动打架案的双根治，D7 药方完全体） */
	void RefreshChasePath(APawn* PlayerPawn, float DeltaSeconds);

	/** 当前追击路径点串（FindPathToActorSynchronously 产物；空 = 无路径，直指玩家兜底） */
	TArray<FVector> ChasePathPoints;

	/** 当前路点游标（2D 80cm 内视为抵达并前进） */
	int32 ChasePathIndex = 0;

	/** 寻路重发倒计时（秒） */
	float PathRefreshCountdown = 0.f;

	/** 直线回退窗剩余时间（秒）：>0 时 ChaseWithNav 持续让位给调用方的直线推 */
	float NavFallbackGrace = 0.f;

	/** 滑行探针节拍倒计时（秒） */
	float MoveProbeCountdown = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "Enemy")
	TObjectPtr<UHealthComponent> HealthComponent;
	/** 世界空间原创猎印，仅消费显示层数；GC/注册/销毁跟随本Actor。 */
	UPROPERTY(VisibleAnywhere, Category = "Enemy|Visual")
	TObjectPtr<UHuntMarkVisual> HuntMarkVisual;
	UPROPERTY(VisibleAnywhere, Category="Enemy|Visual") TObjectPtr<UEnemyEnrageVisual> EnrageVisual;
	UPROPERTY(Transient) TObjectPtr<UShockMarkVisual> ShockVisual;
	UPROPERTY(Transient) TObjectPtr<UFrostMarkVisual> FrostVisual;

	/** 视觉刺激源：注册进队友 AIPerception（与 EnemyTarget 同范式——感知注册在敌人体内完成，队友侧零配置） */
	UPROPERTY(VisibleAnywhere, Category = "Enemy")
	TObjectPtr<UAIPerceptionStimuliSourceComponent> StimuliSource;

	/** 对玩家的攻击冷却累计器（追击中照常累计、封顶在攻击阈值——贴脸即出手，不清零） */
	float PlayerAttackCooldown = 0.f;

	/** 变体基线移速与色调（ApplyVariant 定型记录）；减速只恢复步速。 */
	float BaseWalkSpeed = 200.f;
	FVector BaseTint = FVector(0.03f, 0.03f, 0.035f);

	/** 减速恢复定时器：到点还原基线移速。 */
	FTimerHandle FrostRecoverTimerHandle;

	/** 投掷冷却累计器（高处反制独立节拍） */
	float RangedAttackCooldown = 0.f;

	/** 游走绕向（±1，BeginPlay 随机）：够不着高处的近战敌切向游走包围而非呆立推墙（复测四 2026-09-13"业余感"治理） */
	float CircleDirection = 1.f;

	/** 攻击动画回 ABP 的定时器手柄 */
	FTimerHandle AttackAnimTimerHandle;
};
