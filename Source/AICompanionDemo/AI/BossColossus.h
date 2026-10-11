/**
 * [INPUT]: 依赖 AI/BossBase 的基类契约（HealthComponent/近战框架/bHoldPosition 站桩闸/死亡链）、
 *  Engine/StaticMeshComponent + /Engine/BasicShapes/Cone（程序化石刺）、BasicShapeMaterial+MID 染色、
 *  DrawDebugHelpers（telegraph 红圈脉冲）、Core 计时
 * [OUTPUT]: 对外提供 ABossColossus——Boss2「裂地巨像」三段地刺走位博弈（v4：四十三轮用户草图重构）：
 *   ①红圈阵（玩家为心 5×5 裁圆网格、每格大红圈判定 230——圈间缝/四圈角=落脚点，圆形边缘柔和）
 *   ②锁定圈（玩家位置一次定格 4m 环刺——跑出圈/站环缝双解）
 *   ③放射射线（Boss 为心 8 向射线刺链——玩家在射线夹缝躲避，缝宽∝半径=近身必中，逼迫保持距离）；
 *   数据流统一：telegraph 期生成刺位蓝图 PendingSpikes（位置/判定半径/体量），红圈=判定圆同构绘制，
 *   slam时实体化（0.12s）+逐刺判定同帧+0.38s镜头冲击；仅原地愤怒放射模式请求BossImpact，三模式实际计数只读回读；入场欢迎刺无害
 * [POS]: AI/ Boss 家族成员二（波 2）：走位博弈型——与召唤师（清杂）、蠕虫（时机）差异化；
 *   模型待 Grux 资产到位换装（Sevarog 同管线），白 Manny 先顶机制测试
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "AI/BossBase.h"
#include "BossColossus.generated.h"

class UStaticMesh;
class UMaterialInterface;

/** 单根石刺的完整规格（Pending 蓝图与 Active 实体共用——telegraph 圈与 slam 判定同源同构） */
USTRUCT()
struct FColossusSpike
{
	GENERATED_BODY()

	/** 地面锚点（世界坐标，蓝图期即定格——判定与视觉共用此点） */
	FVector GroundLoc = FVector::ZeroVector;

	/** 逐刺判定半径（红圈阵=大圈 230 / 射线与锁定圈=小圆 90；0=不参与判定） */
	float HitRadius = 90.f;

	/** 刺体量倍率（红圈阵大刺 1.6 / 常规 1.4 底径倍数） */
	float Scale = 1.4f;

	/** 仪式性刺（入场礼）不参与伤害判定 */
	bool bHarmless = false;

	/** 贴地预警环（蓝图期一次性探地缓存：圆周离散点各取地表 Z+抬升——telegraph 期连点成环，
	 *  环随坡面起伏服帖地形；锁定圈模式的圈心刺存大圈环，环刺留空不画小圈） */
	TArray<FVector> RingPoints;

	/** Active 实体的组件（Pending 蓝图期为空） */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Comp = nullptr;

	/** 0=全弹中 1=保持 2=缩回（仅 Active 期有意义） */
	int32 Phase = 0;
	float PhaseTime = 0.f;
};

UCLASS()
class AICOMPANIONDEMO_API ABossColossus : public ABossBase
{
	GENERATED_BODY()

public:
	ABossColossus();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	UFUNCTION(BlueprintPure, Category = "Debug|Boss")
	FString DebugSlamState() const;

protected:
	// ---------- 红圈阵（模式 0，压轴）：玩家为心的圈阵——圈间缝=落脚点 ----------
	/** 网格步长即圈心距：560-2×230=100 缝（两圈间）/ 四圈角 166 宽松——双层安全区 */
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "150.0"))
	float SeaGridStep = 560.f;
	/** 红圈判定半径=预警圈半径（视觉=判定同构） */
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "100.0"))
	float SeaCircleRadius = 230.f;
	/** 裁圆半径（圆形边缘柔和——方阵四角裁去） */
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "400.0"))
	float SeaCullRadius = 1450.f;
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "0.3"))
	float SeaTelegraphSeconds = 1.4f;
	/** 红圈阵刺体量（大圈配大刺） */
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "1.0"))
	float SeaSpikeScale = 1.6f;

	// ---------- 锁定圈（模式 1，首击）：位置一次定格 ----------
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "200.0"))
	float LockRadius = 400.f;
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "0.3"))
	float LockTelegraphSeconds = 0.9f;

	// ---------- 放射射线（模式 2）：Boss 为心的射线刺链——圈径由内向外递减 ----------
	/** 射线条数（8 向均匀——射线间缝宽∝半径，近身必中逼迫保持距离） */
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "4"))
	int32 RayCount = 8;
	/** 射线刺间距/起点（起点外移避开 Boss 脚下）/段数——覆盖 250+300×6=2050（四十六轮扩大） */
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "150.0"))
	float RayStep = 300.f;
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "100.0"))
	float RayStartDist = 250.f;
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "2"))
	int32 RaysSpikesPerRay = 7;
	/** 圈径梯度：最内圈=红圈阵圈径 230×1.5（用户定标）→最外圈 170——近身大圈死地/远端小圈可躲 */
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "100.0"))
	float RayInnerRadius = 345.f;
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "50.0"))
	float RayOuterRadius = 170.f;
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "0.3"))
	float RayTelegraphSeconds = 1.2f;

	// ---------- 公共 ----------
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "5.0"))
	float SlamDamage = 25.f;
	/** 常规逐刺判定圆（射线/锁定圈用；红圈阵用 SeaCircleRadius） */
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "30.0"))
	float SpikeHitRadius = 90.f;
	UPROPERTY(EditAnywhere, Category = "Boss|Slam", meta = (ClampMin = "1.0"))
	float SlamCooldown = 2.5f;
	UPROPERTY(EditAnywhere, Category = "Boss|Camera", meta = (ClampMin = "0.05", ClampMax = "0.8"))
	float SlamShakeDuration = .38f;
	UPROPERTY(EditAnywhere, Category = "Boss|Camera", meta = (ClampMin = "0.0", ClampMax = "150.0"))
	float SlamShakeXY = 42.f;
	UPROPERTY(EditAnywhere, Category = "Boss|Camera", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float SlamShakeZ = 24.f;

private:
	/** 攻击状态机：逼近 → Telegraph（画蓝图刺位的红圈脉冲）→ Slam（实体化+判定同帧）→ Cooldown → 轮换 */
	void TickSlam(float DeltaSeconds, const FVector& PlayerLoc);
	void ExecuteSlam();
	int32 SlamExecutionCounts[3] = {0, 0, 0};
	int32 BossImpactRequests = 0;

	// ---------- Grux 动画三态机（四十七轮换装：Idle/Run 基础态按速度自动切换+瞬态叠断） ----------
	/** 基础动画按速度切换（态未变不重启——防每帧 PlayAnimation 重置循环） */
	void PlayBaseAnim();
	/** 瞬态动画（攻击蓄力/受击）：播完回基础态；判死自愈（死亡动画不被顶掉）；单柄防旧回切截断 */
	void PlayTransientAnim(UAnimationAsset* Anim);
	/** 受击表现（OnDamaged 订阅）：0.5s 节流防弹幕式抽搐 */
	UFUNCTION()
	void HandleHitReact(UHealthComponent* HealthComp, AActor* InstigatorActor);

	/** Grux 资产槽（构造期 FObjectFinder——Sevarog 同管线：SingleNode 播自带动画绕开重定向红线） */
	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> IdleAnim = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> RunAnim = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> AttackAnim = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> HitReactAnim = nullptr;
	/** 当前基础态：false=Idle true=Run（速度驱动，Tick 巡检切换） */
	bool bRunningAnim = false;
	float HitReactCooldown = 0.f;

	/** 受击动作单次伤害门槛（09-30 用户裁决"删常态受击动作，留阈值入口"）：默认 99999=禁用——
	 *  无暴击体系前单发伤害远达不到；暴击落地后调低（如 50）即成"大伤才踉跄"的分级表现 */
	UPROPERTY(EditAnywhere, Category = "Boss|Colossus", meta = (ClampMin = "0.0"))
	float HitReactDamageThreshold = 99999.f;
	FTimerHandle TransientAnimTimer;

	// ---------- 刺位蓝图（telegraph 期生成，slam 期实体化——预警与判定同源） ----------
	void BuildSeaPattern(const FVector& Center);
	void BuildLockPattern(const FVector& Center);
	void BuildRayPattern();
	void MaterializeSpikes();
	void ClearPending();
	/** 蓝图刺位探地（+80m 高空向下打 WorldStatic 取地表 Z——沙丘 ±21m 起伏下刺必须贴地，
	 *  探不到保持原 Z：锚点源（玩家/Boss）本身贴地，fallback 合理；BossAltar 高空化同款范式） */
	void ResolveGroundZ(FVector& Loc);
	/** 贴地预警环：圆周离散点各自探地+抬升 8cm——段数自适应半径（弦矢高恒<抬升量的几何保证） */
	void BuildGroundRing(const FVector& Center, float Radius, TArray<FVector>& OutPoints);

	// ---------- Active 刺动画 ----------
	void TickSpikes(float DeltaSeconds);
	void SetSpikeT(FColossusSpike& Spike, float T) const;
	/** 程序化 squash：读条下蹲宽扁 / slam 弹起拉伸 / 余韵回正——零资产的蓄力形体语言 */
	void UpdateSquash(float DeltaSeconds);

	/** 生成单根 Active 刺（Pending→实体的底层；入场礼直呼） */
	void SpawnSpike(const FVector& GroundLoc, float HitRadius, float Scale, bool bHarmless = false);

	/** 石刺模板（构造期 FObjectFinder：Cone+BasicShapeMaterial，共享 MID 染岩色） */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SpikeMesh = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SpikeMat = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> SharedSpikeMid = nullptr;

	/** telegraph 期的刺位蓝图（红圈按此绘制；slam 实体化后清空） */
	TArray<FColossusSpike> PendingSpikes;
	UPROPERTY()
	TArray<FColossusSpike> ActiveSpikes;

	/** 状态：-1=冷却/逼近中，>=0=telegraph 剩余秒 */
	float TelegraphCountdown = -1.f;
	float SlamCooldownCountdown = 0.f;
	/** 锚点（模式 0/1=玩家位置定格 / 模式 2 射线锚=Boss 自身） */
	FVector SlamCenter = FVector::ZeroVector;
	/** 模式：0=红圈阵 1=锁定圈 2=放射射线；初始 0 使首击自增后=锁定圈（温和入场，红圈阵压轴） */
	int32 SlamMode = 0;

	/** slam 弹起拉伸的余韵倒计时（squash 第二态） */
	float SquashRecoilCountdown = 0.f;

	/** 刺体基准：Cone 默认 100 高，Scale 为底径/高度共用倍数（1.4→140 底径 220 高） */
	static constexpr float SpikeHeightCm = 220.f;
	/** 全弹冲顶后的保持时长（随后缩回） */
	static constexpr float SpikeHoldSeconds = 0.45f;
};
