/**
 * [INPUT]: BossBase死亡/探地/免伤契约，球形命中与火系共享MID，WurmBodyVisual独立岩甲外观，落点爆实现见BossWurmEffects.cpp
 * [OUTPUT]: 7000共享血量的七节蠕虫，默认体量2.5倍、头半径325cm；Sky→Dive→Burrow总3s（迫近2s+红圈1s、随头钻岩声）→Erupt；原球为命中代理。
 *  猎印继承BossBase；杂兵死亡反馈经真实Health，潜地先累积；岩甲落火及火区所有权在BossWurmHazards.cpp统一维护。
 *  存活/死亡路径均推进碎屑与地面爆闪硬寿命，死亡/EndPlay清理火系与焦痕；入/出土硬引用专用UV炭黑贴花，6s退场/后2s渐淡，最多4件。活体入场/死亡/EndPlay通过Encounter同步限额与远景。
 * [POS]: AI/蠕虫运动、状态与攻击真源；保持已验收数值/碰撞/时序，岩甲资产缺失退回原球；死亡尸节仍由Debris牵引，外观清理及EndPlay由独立视图负责
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "BossWurm.h"

#include "AICompanionDemo.h"
#include "AICompanionDemoCharacter.h"
#include "Audio/SFXLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMesh.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraFunctionLibrary.h"
#include "Vfx/VfxLibrary.h"
#include "Vfx/WurmBodyVisual.h"
#include "Vfx/HuntMarkVisual.h"

namespace
{
	// 岩浆系配色（节体暗红/灯光亮橙——物理光照打亮暗红节体，读出"烧红的虫"）
	const FLinearColor kSegmentTint = FLinearColor(FColor::FromHex(TEXT("C2401E")));
	const FLinearColor kMagmaLightTint = FLinearColor(FColor::FromHex(TEXT("FF5A22")));
		const FColor kFireZoneOrange = FColor::FromHex(TEXT("FF7A26"));

	constexpr float GravityCm = 980.f;          // 火球/碎屑重力
	constexpr float SphereMeshRadius = 50.f;    // 引擎 /Engine/BasicShapes/Sphere 半径（cm）
}

ABossWurm::ABossWurm()
{
	// 暂停帧只治理循环音，玩法Tick在Super之前早退，避免冻结世界仍有钻岩声。
	PrimaryActorTick.bTickEvenWhenPaused = true;
	BossName = TEXT("沙狱蠕虫");
	BossIndex = 3;
	MeleeDamage = 15.f;          // 保底参数（近战全程挂起——攻击全自绘）
	MeleeInterval = 1.2f;
	MeleeRange = 300.f;
	GetCharacterMovement()->MaxWalkSpeed = 0.f;   // 行为全自绘：SetActorLocation 直控头节
	// 白模 Manny 全程隐匿——蠕虫形体=节链（人形钻天观感违和，藏到底）
	GetMesh()->SetVisibility(false, true);

	// 头节岩浆点光（烧红的虫——物理光照哲学，构造期挂胶囊随头节移动）
	MagmaLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MagmaLight"));
	MagmaLight->SetupAttachment(GetCapsuleComponent());
	MagmaLight->SetRelativeLocation(FVector::ZeroVector);
	MagmaLight->SetIntensity(1600.f);
	MagmaLight->SetLightColor(kMagmaLightTint);
	MagmaLight->SetAttenuationRadius(1400.f);

	// 引擎基础球+基础材质（巨像石刺同款零资产路线）
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		SegmentMesh = SphereFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (MatFinder.Succeeded())
	{
		SegmentMat = MatFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ScorchFinder(TEXT("/Game/Bosses/Wurm/M_Wurm_Scorch.M_Wurm_Scorch"));
	if (ScorchFinder.Succeeded()) ScorchMaterial = ScorchFinder.Object;
}

void ABossWurm::BeginPlay()
{
	Super::BeginPlay();
	if (HuntMarkVisual) { HuntMarkVisual->SetRelativeLocation(FVector(0.f, 0.f, HeadRadius + 120.f)); }

	// 终章体量（7000——攻击窗口占比 ~8 成，总量经得住全程输出成长）
	if (HealthComponent)
	{
		HealthComponent->SetMaxHealth(7000.f, /*bFillCurrent*/ true);
	}
	bHoldPosition = true;                          // 基类追击/近战全程挂起
	HealthComponent->SetInvulnerable(9999.f);      // 开局埋地（首坑潜行即穹顶入场——教学第一课：圈亮=要出事）

	// ---------- 节链初始化（头大尾小递减，初始沿 -X 排开向地下——随即被链约束与 Burrow 接管） ----------
	const int32 N = FMath::Max(SegmentCount, 3);
	SegmentMid = SegmentMat ? UMaterialInstanceDynamic::Create(SegmentMat, this) : nullptr;
	if (SegmentMid)
	{
		SegmentMid->SetVectorParameterValue(TEXT("Color"), kSegmentTint);
	}
	const FVector RootPos = GetActorLocation();
	for (int32 i = 0; i < N; ++i)
	{
		FWurmSegment Seg;
		Seg.Radius = HeadRadius * (1.f - 0.6f * static_cast<float>(i) / FMath::Max(N - 1, 1));
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
		Comp->SetStaticMesh(SegmentMesh);
		// QueryOnly+Visibility Block：不推挤玩家，但玩家/队友弹道打任意节=经 Owner 命中掉总血
		Comp->SetCollisionObjectType(ECC_WorldDynamic);
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Comp->SetCollisionResponseToAllChannels(ECR_Ignore);
		Comp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Comp->SetCastShadow(false);   // 七节常驻不进阴影——省一帧是一帧（石刺同纪律）
		Comp->RegisterComponent();
		Comp->SetWorldScale3D(FVector(Seg.Radius / SphereMeshRadius));
		Comp->SetMaterial(0, SegmentMid);
		Seg.Pos = RootPos - FVector(i * HeadRadius * 0.9f, 0.f, i * 60.f);
		Seg.Mesh = Comp;
		Seg.Mesh->SetWorldLocation(Seg.Pos);
		Segments.Add(Seg);
	}

	TArray<UStaticMeshComponent*> Proxies;
	for (const FWurmSegment& Seg : Segments) { Proxies.Add(Seg.Mesh); }
	BodyVisual = NewObject<UWurmBodyVisual>(this);
	if (!BodyVisual->Initialize(this, Proxies)) { BodyVisual = nullptr; }
	InitializeHazardArtwork();
	EnterState(EWurmState::Burrow);
	NotifyEncounterPresence();
}

void ABossWurm::Tick(float DeltaSeconds)
{
	RefreshBurrowAudio();
	if (!GetWorld() || GetWorld()->IsPaused() || UGameplayStatics::GetGlobalTimeDilation(this) < .01f) return;
	Super::Tick(DeltaSeconds);
	TickGroundEffects(DeltaSeconds);

	// 死亡后：碎屑坠落演出接管（节链已移交 Debris，基类谢幕链在 HandleDeath 收尾）；
	// 火系威胁已在HandleDeath显式清空，尸节独立按1.6s寿命退场；不依赖3s Actor随葬清理。
	if (!HealthComponent || HealthComponent->IsDead())
	{
		TickDebris(DeltaSeconds);
		TickFireBalls(DeltaSeconds);
		TickFireZones(DeltaSeconds);
		if (BodyVisual) { BodyVisual->TickCleanup(); }
		return;
	}
	// 真实PIE曾在6.54s后仍有12件0.9s碎屑：原来仅死亡分支推进它们，存活期间永远停在出生点。
	// 与上面的死亡分支互斥，每帧只减一次寿命，破土/入地土屑无需等Boss死亡才消散。
	TickDebris(DeltaSeconds);
	if (Segments.Num() == 0)
	{
		return;
	}
	ApplyPendingMobDamage();
	if (IsDead()) return;

	StateTime += DeltaSeconds;
	switch (State)
	{
	case EWurmState::Sky:    TickSky(DeltaSeconds);    break;
	case EWurmState::Dive:   TickDive(DeltaSeconds);   break;
	case EWurmState::Burrow: TickBurrow(DeltaSeconds); break;
	case EWurmState::Erupt:  TickErupt(DeltaSeconds);  break;
	}
	// 易伤切换可结算累积的致死反馈，死亡已把Segments移交Debris，不能再读旧节链。
	if (IsDead() || Segments.IsEmpty()) return;
	if (BodyVisual)
	{
		TArray<FVector> Positions;
		for (const FWurmSegment& Seg : Segments) { Positions.Add(Seg.Pos); }
		BodyVisual->Refresh(Positions, DeltaSeconds);
		BodyVisual->TickCleanup();
	}

	// 火系持续层跨状态结算（盘旋掉的火球与地面火区不随扎地中断——战场火烧连营）
	TickFireBalls(DeltaSeconds);
	TickFireZones(DeltaSeconds);
	if (IsDead() || Segments.IsEmpty()) return;

	// 身体接触伤害（整体节流——掠身碰撞有痛感，冷却内不重复结算）
	TickBodyTouch(DeltaSeconds);
	if (IsDead() || Segments.IsEmpty()) return;

	// Actor/胶囊/血条随头节（评分目标/玩家弹瞄准点/队友走廊语义全在 Actor 位置）
	SetActorLocation(Segments[0].Pos);
}

void ABossWurm::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopBurrowAudio();
	NotifyEncounterPresence(true);
	ClearFireHazards();
	ClearGroundEffects();
	if (BodyVisual) { BodyVisual->Shutdown(); BodyVisual = nullptr; }
	for (FWurmDebris& Piece : Debris) { if (IsValid(Piece.Mesh)) Piece.Mesh->DestroyComponent(); }
	for (FWurmSegment& Segment : Segments) { if (IsValid(Segment.Mesh)) Segment.Mesh->DestroyComponent(); }
	Debris.Reset();
	Segments.Reset();
	PendingMobDamage = 0.f;
	CreditedMobDeaths.Reset();
	Super::EndPlay(EndPlayReason);
}

FString ABossWurm::DebugBodyVisualState() const
{
#if WITH_EDITOR
	int32 VisibleProxies = 0, BlockingProxies = 0;
	for (const FWurmSegment& Seg : Segments)
	{
		if (Seg.Mesh && Seg.Mesh->IsVisible()) { ++VisibleProxies; }
		if (Seg.Mesh && Seg.Mesh->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block) { ++BlockingProxies; }
	}
	return FString::Printf(TEXT("{\"alive\":%s,\"state\":%d,\"stateTime\":%.3f,\"segments\":%d,\"visuals\":%d,\"debris\":%d,\"fireBalls\":%d,\"fireZones\":%d,\"visibleProxies\":%d,\"blockingProxies\":%d}"),
		HealthComponent && !HealthComponent->IsDead() ? TEXT("true") : TEXT("false"), static_cast<int32>(State), StateTime,
		Segments.Num(), BodyVisual ? BodyVisual->GetActiveVisualCount() : 0, Debris.Num(), FireBalls.Num(), FireZones.Num(), VisibleProxies, BlockingProxies);
#else
	return TEXT("{}");
#endif
}

// ============================================================
// 四态状态机
// ============================================================

void ABossWurm::EnterState(EWurmState Next)
{
	if (Next != EWurmState::Burrow) StopBurrowAudio();
	State = Next;
	StateTime = 0.f;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

	switch (Next)
	{
	case EWurmState::Sky:
		// 从当前方位续接轨道（破土冲天→顺势入轨，零跳变）
		if (PlayerPawn && Segments.Num() > 0)
		{
			const FVector ToHead = Segments[0].Pos - PlayerPawn->GetActorLocation();
			OrbitAngle = FMath::Atan2(ToHead.Y, ToHead.X);
		}
		break;

	case EWurmState::Dive:
		// 俯冲目标=玩家脚下地表（扎地点即时定格——身后身体节将依次没入）
		if (PlayerPawn)
		{
			DiveTarget = PlayerPawn->GetActorLocation();
			ResolveGroundZ(DiveTarget);
		}
		bDiveBurstDone = false;   // 每轮入地爆重置
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] 沙狱蠕虫俯冲扎地！"));
		break;

	case EWurmState::Burrow:
		bEruptRingShown = false;
		HealthComponent->SetInvulnerable(9999.f);   // 地下=天然打不到+免伤双保险
		GroundProbeCd = 0.f;
		RefreshBurrowAudio();
		break;

	case EWurmState::Erupt:
	{
		if (Segments.Num() == 0)
		{
			break;
		}
		// 头节就位圈下（红圈期未赶到的兜底对齐——破土必须从圈内起）
		Segments[0].Pos = EruptPoint - FVector(0.f, 0.f, BurrowDepth);
		HealthComponent->ClearInvulnerable();   // 破土=可伤（露头即窗口的时序交换）
		ApplyPendingMobDamage();
		if (IsDead()) break;
		// 破土爆（十三批解耦：EruptBurst 破土专用——用户验收冻结不动）
		EruptBurst(EruptPoint);
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] 沙狱蠕虫破土！%.0f 伤 / 半径 %.0f"), EruptDamage, EruptRadius);
		break;
	}
	}
	// 潜地隐藏视觉而保留玩法层数；露头恢复，免伤期不新叠层。
	if (HuntMarkVisual) { HuntMarkVisual->SetStacks(Next == EWurmState::Burrow || IsDead() ? 0 : MarkStacks); }
}

void ABossWurm::TickSky(float DeltaSeconds)
{
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}
	const FVector PlayerLoc = PlayerPawn->GetActorLocation();

	// 轨道推进：角速度=线速度/半径（半径可调时角速度自适应，线速恆定）
	OrbitAngle += (OrbitLineSpeed / FMath::Max(OrbitRadius, 1.f)) * DeltaSeconds;
	OrbitBobPhase += DeltaSeconds * 1.2f;

	// 头节恒线速逼近轨道点（手写 ClampedToMaxSize——与 FInterpConstantTo 同义）
	const FVector OrbitTarget(
		PlayerLoc.X + FMath::Cos(OrbitAngle) * OrbitRadius,
		PlayerLoc.Y + FMath::Sin(OrbitAngle) * OrbitRadius,
		PlayerLoc.Z + OrbitHeight + FMath::Sin(OrbitBobPhase) * OrbitBob);
	FWurmSegment& Head = Segments[0];
	Head.Pos += (OrbitTarget - Head.Pos).GetClampedToMaxSize(OrbitLineSpeed * DeltaSeconds);
	FollowChain();

	// 周期掉火（从随机体节剥离——玩家看虫身在头顶即知落点带，零追踪的公平）
	FireDropCd -= DeltaSeconds;
	if (FireDropCd <= 0.f)
	{
		FireDropCd = FireDropInterval;
		SpawnFireBall();
	}

	if (StateTime >= SkyDuration)
	{
		EnterState(EWurmState::Dive);
	}
}

void ABossWurm::TickDive(float DeltaSeconds)
{
	// 头节恒速直冲地下目标——穿过地表的"依次没入"由链式跟随涌现
	FWurmSegment& Head = Segments[0];
	const FVector BurrowTarget = DiveTarget - FVector(0.f, 0.f, BurrowDepth);
	Head.Pos += (BurrowTarget - Head.Pos).GetClampedToMaxSize(DiveSpeed * DeltaSeconds);
	FollowChain();

	// 入地爆（十三批解耦：DiveImpactBurst 入地专用——爆炸子弹同款球形爆炸空间，零预告）
	if (!bDiveBurstDone && Head.Pos.Z <= DiveTarget.Z)
	{
		bDiveBurstDone = true;
		DiveImpactBurst(DiveTarget);
		// 入地伤害可反伤杀虫并移交/清空Segments，Head引用随之失效。
		if (IsDead() || IsActorBeingDestroyed() || Segments.IsEmpty()) return;
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] 沙狱蠕虫入地爆！"));
	}

	// 头节入地后身体节钳制隐匿（入地演出=依次没入，没入完的部分不得在坡地露头）
	if (bDiveBurstDone)
	{
		ClampSegmentsUnderground();
	}

	if (FVector::Dist(Head.Pos, BurrowTarget) <= 100.f)
	{
		EnterState(EWurmState::Burrow);
	}
}

void ABossWurm::TickBurrow(float DeltaSeconds)
{
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}
	const FVector PlayerLoc = PlayerPawn->GetActorLocation();
	FWurmSegment& Head = Segments[0];

	const float UndergroundDuration = FMath::Max(BurrowDuration, .1f);
	const float DashTime = FMath::Max(UndergroundDuration - FMath::Clamp(EruptWarnTime, 0.f, UndergroundDuration), 0.f);
	if (StateTime < DashTime)
	{
		// ---------- Phase A：无圈快移（压迫段——先到再亮圈） ----------
		GroundProbeCd -= DeltaSeconds;
		if (GroundProbeCd <= 0.f)
		{
			GroundProbeCd = 0.15f;
			FVector Probe = PlayerLoc;
			ResolveGroundZ(Probe);
			GroundZCache = Probe.Z;   // v15 ±21m 起伏——潜行深度跟随地形（钉死 Z 会穿出地表）
		}
		Head.Pos.Z = FMath::FInterpTo(Head.Pos.Z, GroundZCache - BurrowDepth, DeltaSeconds, 6.f);
		const FVector FlatTarget(PlayerLoc.X, PlayerLoc.Y, Head.Pos.Z);
		Head.Pos += (FlatTarget - Head.Pos).GetClampedToMaxSize(BurrowSpeed * DeltaSeconds);
	}
	else if (!bEruptRingShown)
	{
		// ---------- Phase B 起手：穹顶定格玩家脚下（预告=它将从这里露头） ----------
		// （跟随到最后一刻=无跳跃机制下必中——巨像锁定圈同款公平性纪律）
		bEruptRingShown = true;
		EruptPoint = PlayerLoc;
		ResolveGroundZ(EruptPoint);
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] 沙狱蠕虫锁定露头点——穹顶 %.1fs"), EruptWarnTime);
	}
	else
	{
		// ---------- Phase B：红圈期（潜行抵圈候场+贴地红圈 telegraph 逐帧——巨像同款语言，
		// 十一批用户点名恢复："有个红圈让玩家知道接下来要面对沙虫会提高紧张感"） ----------
		Head.Pos.Z = FMath::FInterpTo(Head.Pos.Z, EruptPoint.Z - BurrowDepth, DeltaSeconds, 6.f);
		const FVector FlatTarget(EruptPoint.X, EruptPoint.Y, Head.Pos.Z);
		Head.Pos += (FlatTarget - Head.Pos).GetClampedToMaxSize(BurrowSpeed * DeltaSeconds);
		DrawTelegraphRing();

		if (StateTime >= UndergroundDuration)
		{
			EnterState(EWurmState::Erupt);
			return;
		}
	}
	FollowChain();
	ClampSegmentsUnderground();   // 潜行全程隐匿——坡地把节顶出地表=用户误读的"残留红球"
}

void ABossWurm::TickErupt(float DeltaSeconds)
{
	// 垂直上冲穿天——节节破土由链式跟随涌现；到达飞行高度顺势回盘旋
	Segments[0].Pos.Z += EruptClimbSpeed * DeltaSeconds;
	FollowChain();

	if (Segments[0].Pos.Z >= EruptPoint.Z + OrbitHeight)
	{
		EnterState(EWurmState::Sky);
	}
}

// ============================================================
// 运动学：链式跟随（全片运动学单点真源）
// ============================================================

void ABossWurm::FollowChain()
{
	// 身体节逐节追前节保持重叠间距——头节的蜿蜒自动涌现为全身 S 形
	for (int32 i = 1; i < Segments.Num(); ++i)
	{
		const float Spacing = (Segments[i - 1].Radius + Segments[i].Radius) * 0.55f;
		FVector Dir = Segments[i].Pos - Segments[i - 1].Pos;
		if (Dir.SizeSquared() < 1.f)
		{
			Dir = FVector(-1.f, 0.f, 0.f);   // 重合兜底（初始化同位）
		}
		Segments[i].Pos = Segments[i - 1].Pos + Dir.GetSafeNormal() * Spacing;
	}
	for (FWurmSegment& Seg : Segments)
	{
		if (Seg.Mesh)
		{
			Seg.Mesh->SetWorldLocation(Seg.Pos);
		}
	}
}

void ABossWurm::ClampSegmentsUnderground()
{
	for (FWurmSegment& Seg : Segments)
	{
		FVector Probe = Seg.Pos;
		ResolveGroundZ(Probe);
		const float Ceil = Probe.Z - Seg.Radius - 60.f;   // 按半径约束节顶，整体放大后仍完整埋地
		if (Seg.Pos.Z > Ceil)
		{
			Seg.Pos.Z = Ceil;
			if (Seg.Mesh)
			{
				Seg.Mesh->SetWorldLocation(Seg.Pos);
			}
		}
	}
}


// ============================================================
// 火系攻击：下落火球 + 燃烧火区
// ============================================================

void ABossWurm::TickBodyTouch(float DeltaSeconds)
{
	BodyTouchCd -= DeltaSeconds;
	if (BodyTouchCd > 0.f || State == EWurmState::Burrow)
	{
		return;   // 地下潜行期身体埋在 5m 深处，够不着地表玩家
	}
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}
	const FVector PlayerLoc = PlayerPawn->GetActorLocation();
	for (const FWurmSegment& Seg : Segments)
	{
		// 三维触体（玩家从虫身正下方走过不算——Z 差自然拉开距离）；整体一口：
		// 任一节触碰即结算同一口伤害并进冷却（用户点名"整体添加，避免每节独立秒杀"）
		if (FVector::Dist(PlayerLoc, Seg.Pos) <= Seg.Radius + 60.f)
		{
			BodyTouchCd = BodyTouchCooldown;
			if (UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>())
			{
				PlayerHealth->ApplyDamage(BodyTouchDamage, this);
			}
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] 沙狱蠕虫触体 %.0f 伤"), BodyTouchDamage);
			break;
		}
	}
}

// ============================================================
// 碎屑系统：破土土屑 + 死亡坠落节
// ============================================================

void ABossWurm::SpawnDebris(const FVector& Pos, const FVector& Vel, float Life, float Radius,
	UStaticMeshComponent* ExistingMesh, bool bHead)
{
	// 同一Actor最多64件；上限淘汰同时回收真实组件，不能仅丢弃数据形成新的悬留。
	while (Debris.Num() >= 64)
	{
		if (IsValid(Debris[0].Mesh)) Debris[0].Mesh->DestroyComponent();
		Debris.RemoveAt(0);
	}
	FWurmDebris D;
	D.Pos = Pos;
	D.Vel = Vel;
	D.Life = Life;
	D.bIsHead = bHead;

	FVector GroundProbe = Pos;
	ResolveGroundZ(GroundProbe);
	D.GroundZ = GroundProbe.Z;

	if (ExistingMesh)
	{
		D.Mesh = ExistingMesh;   // 死亡坠落：节组件直接移交（Scale 已定型）
		D.Mesh->ComponentTags.AddUnique(TEXT("WurmDebris"));
	}
	else if (SegmentMesh)
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
		Comp->ComponentTags.Add(TEXT("WurmDebris"));
		ConfigureHazardRock(Comp, Radius);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->RegisterComponent();
		Comp->SetWorldLocation(Pos);
		D.Mesh = Comp;
	}
	if (D.Mesh)
	{
		Debris.Add(D);
	}
}

void ABossWurm::TickDebris(float DeltaSeconds)
{
	for (int32 i = Debris.Num() - 1; i >= 0; --i)
	{
		FWurmDebris& D = Debris[i];
		D.Life -= DeltaSeconds;
		if (D.Life <= 0.f)
		{
			if (D.Mesh)
			{
				D.Mesh->DestroyComponent();
			}
			Debris.RemoveAtSwap(i);
			continue;
		}

		const bool bWillCrossGround = (D.Pos.Z + D.Vel.Z * DeltaSeconds) <= D.GroundZ;
		if (bWillCrossGround && D.Vel.Z < 0.f)
		{
			// 落地：头节砸地大爆收尾；其余反弹一次后躺平
			if (D.bIsHead && !bHeadLanded)
			{
				bHeadLanded = true;
				UNiagaraComponent* HeadBurst = nullptr;
				UVfxLibrary::SpawnVfx(GetWorld(), EVfx::Explosion,
					FVector(D.Pos.X, D.Pos.Y, D.GroundZ + 60.f), 0.8f, 2.0f, &HeadBurst);
				TrackGroundBurst(HeadBurst, .8f);
				USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Explosion);
			}
			if (!D.bBounced)
			{
				D.bBounced = true;
				D.Vel.Z *= -0.35f;
				D.Vel.X *= 0.5f;
				D.Vel.Y *= 0.5f;
			}
			else
			{
				D.Vel = FVector::ZeroVector;   // 二次落地=躺平（寿命自然到期）
			}
			D.Pos.Z = D.GroundZ;
		}
		else
		{
			D.Vel.Z -= GravityCm * DeltaSeconds;
			D.Pos += D.Vel * DeltaSeconds;
		}

		// 寿命尾段沉入地表（mesh 无透明通道——下沉即消散）
		if (D.Life < 0.3f)
		{
			D.Pos.Z -= 300.f * DeltaSeconds;
		}
		if (D.Mesh)
		{
			D.Mesh->SetWorldLocation(D.Pos);
		}
	}
}

// ============================================================
// 死亡链
// ============================================================

void ABossWurm::HandleDeath(UHealthComponent* HealthComp)
{
	StopBurrowAudio();
	NotifyEncounterPresence(true);
	// 所有火焰/独立爆闪/焦痕立即回收，不等自动结束或冻结后的时钟。
	ClearFireHazards();
	ClearGroundEffects();
	PendingMobDamage = 0.f;
	// 谢幕对齐：死亡多发生在空中——先把 Actor 落到地面投影（金柱从地表立起才有"倒下"语义）
	FVector GroundLoc = GetActorLocation();
	ResolveGroundZ(GroundLoc);
	SetActorLocation(GroundLoc);

	Super::HandleDeath(HealthComp);
}

void ABossWurm::PerformDeathPresentation()
{
	// 节链失引：全部移交碎屑系统坠落弹地（ragdoll 不适用——程序化节链无骨骼）
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	bHeadLanded = false;

	for (int32 i = 0; i < Segments.Num(); ++i)
	{
		FWurmSegment& Seg = Segments[i];
		if (!Seg.Mesh)
		{
			continue;
		}
		Seg.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // 尸节不再吃弹
		const FVector OutDir = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f).GetSafeNormal();
		const FVector Vel = OutDir * FMath::FRandRange(150.f, 450.f)
			+ FVector(0.f, 0.f, FMath::FRandRange(-100.f, 150.f));
		SpawnDebris(Seg.Pos, Vel, 1.6f, Seg.Radius, Seg.Mesh, /*bHead*/ i == 0);
	}
	Segments.Empty();
}
