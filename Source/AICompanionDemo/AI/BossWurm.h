/**
 * [INPUT]: BossBase死亡/探地/免伤契约、球形命中代理、WurmBodyVisual岩甲外观、同资产岩甲落火、Vfx/SFX、导演的有效杂兵死亡通知
 * [OUTPUT]: 对外提供 ABossWurm——Boss3「沙狱蠕虫」v2 岩浆飞天蠕虫（2026-10-06 用户裁决重做，替代 09-30 钻地瞬移初版）：
 *   Sky 盘旋（节链全露空中绕玩家转圈+周期掉火球自由落体→落地留火区 3s）→ Dive 俯冲扎地（头节直冲玩家，链式依次没入）
 *   → Burrow 潜行（免伤+地下快移+红圈 telegraph 定格玩家脚下 1s）→ Erupt 破土跃天（范围爆炸 600cm/25 伤+土屑迸射+垂直冲回飞行高度续接盘旋）；
 *   七节球形代理共享7000总血，默认头半径325cm、整体较原130cm放大2.5倍；每节Visibility Block命中，自制岩甲/熔核继承代理比例，缺资产保留原球。潜行按半径埋深；死亡随尸节移动并显式清理
 *   有效杂兵死亡默认反馈35伤；潜地/暂停只累计，易伤后经Health统一结算；存活/死亡每帧推进碎屑寿命，最多64件，死亡清空火系威胁
 *   BeginPlay/死亡/EndPlay同步限额/远景；入土/破土每事件只播一次1.8s首爆采样、0.55s强震；火区自有Niagara与爆闪硬寿命，专用UV炭黑贴花最多4件无伤焦痕6s退场/后2s渐淡。
 * [POS]: AI/ Boss 家族成员三（波 4 终章）：最终 Boss——"天上盘旋轰炸、破土瞬爆"的垂直威胁体；死亡演节链失引坠落（覆写 PerformDeathPresentation）
 * [PROTOCOL]: 变更时更新此头部，然后检查 ../AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
#include "AI/BossBase.h"
#include "BossWurm.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UWurmBodyVisual;
class UNiagaraComponent;
class UNiagaraSystem;
class UDecalComponent;

/** 蠕虫体节：球 mesh 组件+链式逻辑位置（组件由 Actor OwnedComponents 持有，UPROPERTY 双保险防 GC——巨像石刺同款） */
USTRUCT()
struct FWurmSegment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	/** 链式逻辑位置（每帧由头节牵引迭代，写回组件） */
	FVector Pos = FVector::ZeroVector;

	/** 本节半径（头大尾小递减——决定球 Scale 与链间距） */
	float Radius = 100.f;
};

/** 弹道碎屑：破土/入地土屑0.9s与尸节1.6s共用；存活/死亡路径均推进，最多64件，EndPlay回收。 */
USTRUCT()
struct FWurmDebris
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	FVector Pos = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;
	float GroundZ = 0.f;    // 生成时探定的反弹面
	float Life = 0.f;
	bool bBounced = false;
	bool bIsHead = false;   // 头节碎屑：落地触发大爆收尾
};

/** 下落火球：从随机体节剥离后纯重力自由落体（零追踪——用户裁决），落地转火区 */
USTRUCT()
struct FWurmFireBall
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	FVector Pos = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;
	float GroundZ = 0.f;   // 落点地表 Z（下落中每 0.1s 实时重探——七批根治：不信生成时快照，球贴地残留案）
	float ReprobeCd = 0.f; // 实时探地节流
	float Life = 6.f;      // 寿命兜底：正常下落 ~1.5s 必落地，超时=落地判定失效——强制就地转火区+日志取证，球绝不残留（10-06"小概率残留"案）
};

/** 燃烧火区：Boss自有火焰与独立落地爆闪组件；爆闪硬寿命/火区到期/淘汰/死亡/EndPlay同步销毁。 */
USTRUCT()
struct FWurmFireZone
{
	GENERATED_BODY()

	FVector Pos = FVector::ZeroVector;
	float Life = 0.f;
	float BurnCd = 0.f;
	TWeakObjectPtr<UNiagaraComponent> Vfx;
	TWeakObjectPtr<UNiagaraComponent> ImpactVfx;
	float ImpactLife = .4f;
};

/** 破土/入地爆闪硬寿命账本，与火区分开；不信Niagara自动结束。 */
struct FWurmTimedBurst
{
	TWeakObjectPtr<UNiagaraComponent> Component;
	float Life = 0.f;
};

/** 短命焦痕纯视觉：无碰撞/伤害/发光。 */
struct FWurmScorch
{
	TWeakObjectPtr<UDecalComponent> Decal;
	float Age = 0.f;
};

UCLASS()
class AICOMPANIONDEMO_API ABossWurm : public ABossBase
{
	GENERATED_BODY()

public:
	ABossWurm();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 仅编辑器只读验收；不驱动Boss状态或攻击。非编辑器返回空对象。 */
	UFUNCTION(BlueprintPure, Category = "Debug|WurmVisual")
	FString DebugBodyVisualState() const;
	/** 接受真实死亡Health；同一杂兵双通道上报仍只反馈一次，不绕过潜地免伤。 */
	void NotifyMobKilled(UHealthComponent* MobHealth);
	UFUNCTION(BlueprintPure, Category = "Debug|Wurm")
	FString DebugHazardState() const;
	/** 编辑器受控落火口，复用真实生成/生命周期；不修改其寿命或落地判定。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|Wurm")
	void DebugDropFireRock(const FVector& Location);
	UPROPERTY(EditAnywhere, Category = "Wurm|Balance", meta = (ClampMin = "0.0"))
	float MobDeathDamage = 35.f;

protected:
	void NotifyEncounterPresence(bool bLeaving = false);
	// ---------- 死亡链覆写：Actor 先落地面（金柱从地表立起），表现演节链失引坠落 ----------
	virtual void HandleDeath(UHealthComponent* HealthComp) override;

	// ---------- 死亡表现覆写：节链失引坠落弹地消散（程序化节链无骨骼，ragdoll 不适用） ----------
	virtual void PerformDeathPresentation() override;

	// ---------- 四态状态机 ----------
	enum class EWurmState : uint8
	{
		Sky,     ///< 盘旋轰炸（可打）：绕玩家转圈+周期掉火
		Dive,    ///< 俯冲扎地（可打）：头节直冲玩家脚下
		Burrow,  ///< 地下潜行（免伤）：快移接近+红圈 telegraph
		Erupt    ///< 破土跃天（可打）：范围爆炸+垂直冲回飞行高度
	};

	void EnterState(EWurmState Next);
	void TickSky(float DeltaSeconds);
	void TickDive(float DeltaSeconds);
	void TickBurrow(float DeltaSeconds);
	void TickErupt(float DeltaSeconds);

	/** 链式跟随：身体节逐节追前节保持间距（头节的蜿蜒自动涌现为全身 S 形），写回组件 */
	void FollowChain();

	/** 地下态体节钳制：链式跟随是纯几何的，坡地（v15 起伏 ±21m 远超潜行深度 4m）会把链条上的节
	 *  "顶"出地表——视觉即"半埋在地里不动的红球"（10-06 七批用户截图定罪：残留球其实是体节）。
	 *  每节探地钳中心 Z ≤ 地表-Radius-60，保证放大后节顶仍埋入地表下60cm */
	void ClampSegmentsUnderground();

	/** 【退役 10-08 十批】预告穹顶（原 DrawWurmDome）——线框球无预告语义，被玩家读作"提前出现的爆炸"
	 *  （破土/入地双双误读），整体撤除：入地=触地瞬间爆（俯冲方向=唯一预告），破土=纯露头无圈 */

	// ---------- 火系攻击与碎屑 ----------
	void SpawnFireBall();                     // 双侧对称掉落（数量×2）
	void SpawnFireBallAt(const FVector& Pos); // 单颗落火（双侧逻辑的公共落体段）
	void TickFireBalls(float DeltaSeconds);
	void TickFireZones(float DeltaSeconds);
	void InitializeHazardArtwork();
	void ConfigureHazardRock(UStaticMeshComponent* Component, float Radius);
	void DestroyFireZone(FWurmFireZone& Zone);
	void ClearFireHazards();
	void TrackGroundBurst(UNiagaraComponent* Component, float Life);
	void TickGroundEffects(float DeltaSeconds);
	void ClearGroundEffects();
	void SpawnGroundScorch(const FVector& Center);
	void ApplyPendingMobDamage();
	/** 碎屑生成：ExistingMesh 空=新建土球，非空=死亡坠落时移交节组件（Scale 已定型不重设） */
	void SpawnDebris(const FVector& Pos, const FVector& Vel, float Life, float Radius,
		UStaticMeshComponent* ExistingMesh = nullptr, bool bHead = false);
	void TickDebris(float DeltaSeconds);

	// ---------- 接触与落点爆（十三批解耦：入土/出土两套独立实现，不再共用） ----------
	/** 身体接触伤害（整体节流：任意节触碰同吃一口，BodyTouchCooldown 内不重复） */
	void TickBodyTouch(float DeltaSeconds);
	/** 【破土专用】破土爆（用户验收 ✅ 冻结：红圈预告→全套爆炸——Large 爆闪/爆光/震屏/冲击环/土屑） */
	void EruptBurst(const FVector& Center);
	/** 【入地专用】入地爆（十三批全新独立实现）：球形爆炸空间=爆炸子弹溅射同款双层线框球
	 *  （ApplyHitChain 同语言——用户点名"爆炸子弹那种球形爆炸空间"；此前四轮 NS Large 从未确认可见）+
	 *  震屏 0.5s+音效+土屑+伤害；零预告（俯冲方向=唯一预告） */
	void DiveImpactBurst(const FVector& Center);
	/** 破土预告红圈（十一批用户点名恢复——巨像同款贴地 telegraph 语言，紧张感来源） */
	void DrawTelegraphRing();
	/** 冲击环（破土爆收束层）：橙色快闪粗线=爆炸波及面瞬时表达 */
	void DrawBurstRing(const FVector& Center, float Life);

	// ---------- 体节与实体 ----------
	UPROPERTY(Transient)
	TArray<FWurmSegment> Segments;   // [0]=头节（Actor 位置与胶囊跟随它）
	UPROPERTY(Transient)
	TArray<FWurmDebris> Debris;
	UPROPERTY(Transient)
	TArray<FWurmFireBall> FireBalls;
	TArray<FWurmFireZone> FireZones;
	TArray<FWurmTimedBurst> GroundBursts;
	TArray<FWurmScorch> Scorches;

	/** 独立外观所有者；不参与球形命中、链运动或火系对象。 */
	UPROPERTY(Transient)
	TObjectPtr<UWurmBodyVisual> BodyVisual;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SegmentMid;   // 单 MID 全节共享染岩浆红（石刺同款）

	/** 构造期资产（引擎基础球+基础材质——石刺同款零资产路线） */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SegmentMesh = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SegmentMat = nullptr;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> FireRockMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FireRockBasalt;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FireRockCore;
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> HazardFlameTemplate;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> ScorchMaterial;

	/** 头节岩浆点光（物理光照照明——KeyLight 同款哲学，随 Actor=头节移动） */
	UPROPERTY(VisibleAnywhere, Category = "Wurm|Body")
	TObjectPtr<class UPointLightComponent> MagmaLight;

	// ---------- 状态机现场 ----------
	EWurmState State = EWurmState::Burrow;
	float StateTime = 0.f;
	float OrbitAngle = 0.f;        // 盘旋轨道角（Sky 态推进，Erupt 结束时从当前方位续接）
	float OrbitBobPhase = 0.f;     // 高度正弦起伏相位
	float FireDropCd = 0.f;
	float GroundProbeCd = 0.f;     // 潜行探地节流
	float GroundZCache = 0.f;      // 潜行 Z 基准（TargetGroundZ - BurrowDepth）
	FVector DiveTarget = FVector::ZeroVector;
	FVector EruptPoint = FVector::ZeroVector;

	// ---------- 调参旋钮（全部 EditAnywhere——复测手感直接 ini/实例调） ----------
	/** 盘旋轨道半径（cm）——压近玩家头顶的压迫感旋钮（10-06 复测：2000 观感飘，压到 1300） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Orbit", meta = (ClampMin = "800.0"))
	float OrbitRadius = 1300.f;
	/** 飞行高度（cm）；2.5倍节链约15m，18m确保跃升转盘旋时尾节和落火源已出土 */
	UPROPERTY(EditAnywhere, Category = "Wurm|Orbit", meta = (ClampMin = "400.0"))
	float OrbitHeight = 1800.f;
	/** 盘旋线速度（cm/s——"移动要快"的旋钮） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Orbit", meta = (ClampMin = "400.0"))
	float OrbitLineSpeed = 1300.f;
	/** 高度正弦起伏幅度（cm——飞行活物感） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Orbit")
	float OrbitBob = 250.f;
	/** 每轮盘旋持续（s，到期俯冲扎地） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Orbit", meta = (ClampMin = "2.0"))
	float SkyDuration = 7.f;

	/** 掉火间隔（s）——压迫感主旋钮（10-06 复测：1.2s 太稀"不停掉熔岩"改 0.55；每跳双侧各一颗=数量×2） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Fire", meta = (ClampMin = "0.25"))
	float FireDropInterval = 0.55f;
	/** 双侧掉落的径向偏移（cm）——轨迹内外两条落火带的间距（内侧压玩家/外侧封圈道外沿） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Fire", meta = (ClampMin = "0.0"))
	float FireDropSpread = 250.f;
	/** 火区灼烧半径（cm）——10-06 复测 250→350 威胁圈放大 */
	UPROPERTY(EditAnywhere, Category = "Wurm|Fire", meta = (ClampMin = "100.0"))
	float FireZoneRadius = 350.f;
	/** 火区存活（s） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Fire", meta = (ClampMin = "1.0"))
	float FireZoneLife = 3.f;
	/** 火区每次灼烧伤害（0.5s 一跳） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Fire", meta = (ClampMin = "1.0"))
	float FireZoneBurn = 5.f;

	/** 红圈出现到破土的倒计时（s——玩家躲避窗口） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Erupt", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float EruptWarnTime = 1.f;
	/** 破土爆炸半径=红圈半径（cm） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Erupt", meta = (ClampMin = "200.0"))
	float EruptRadius = 600.f;
	/** 破土爆炸伤害 */
	UPROPERTY(EditAnywhere, Category = "Wurm|Erupt", meta = (ClampMin = "1.0"))
	float EruptDamage = 25.f;

	/** 俯冲扎地速度（cm/s） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Move", meta = (ClampMin = "500.0"))
	float DiveSpeed = 2200.f;
	/** 地下潜行速度（cm/s——压迫感主旋钮） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Move", meta = (ClampMin = "500.0"))
	float BurrowSpeed = 3000.f;
	/** 破土上冲速度（cm/s） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Move", meta = (ClampMin = "500.0"))
	float EruptClimbSpeed = 2600.f;

	/** 体节数（含头节） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Body", meta = (ClampMin = "3", ClampMax = "12"))
	int32 SegmentCount = 7;
	/** 头节半径（cm，尾节按比例递减）；体量2.5倍，显示/命中/链间距共用此尺度 */
	UPROPERTY(EditAnywhere, Category = "Wurm|Body", meta = (ClampMin = "60.0"))
	float HeadRadius = 325.f;
	/** 身体接触伤害（10-06 用户裁决：掠身碰撞有痛感——整体一口节流，任一节触碰同吃一口） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Body", meta = (ClampMin = "0.0"))
	float BodyTouchDamage = 15.f;
	/** 身体接触伤害冷却（s）——节流闸：冷却内不重复结算（防每帧接触=秒杀） */
	UPROPERTY(EditAnywhere, Category = "Wurm|Body", meta = (ClampMin = "0.3"))
	float BodyTouchCooldown = 1.f;

	/** 潜行深度（地表 Z 之下，cm） */
	static constexpr float BurrowDepth = 500.f;

private:
	TSet<TWeakObjectPtr<UHealthComponent>> CreditedMobDeaths;
	float PendingMobDamage = 0.f;
	float TotalMobDamageApplied = 0.f;
	int32 MobDeathsCredited = 0;
	int32 FireRocksSpawned = 0;
	int32 FireRocksRetired = 0;
	int32 FireZonesRetired = 0;
	bool bApplyingMobDamage = false;
	/** 红圈期标记（Burrow 态 Phase B 起亮） */
	bool bEruptRingShown = false;

	/** 头节落地爆炸已播（死亡坠落演出尾爆） */
	bool bHeadLanded = false;

	/** 本轮俯冲入地爆已结算（每轮 Dive 重置——穿地瞬间一次性触发） */
	bool bDiveBurstDone = false;

	/** 身体接触伤害冷却余量 */
	float BodyTouchCd = 0.f;

	/** 无圈潜行段时长（Burrow 态 Phase A——先快移后亮圈；十三批 0.35→0.9：扎地与红圈拉开时距，
	 *  消除"入土时出现红圈"的归因混淆——寂静 0.9s 后亮圈，红圈清晰归属"即将破土"） */
	static constexpr float BurrowDashTime = 0.9f;
};
