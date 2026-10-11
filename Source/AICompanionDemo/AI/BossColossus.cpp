/**
 * [INPUT]: 依赖 BossColossus.h 的状态机契约、AI/BossBase 的基类行为（逼近/近战/bHoldPosition 闸/死亡链）、
 *  Engine/StaticMeshComponent + /Engine/BasicShapes/Cone 与 BasicShapeMaterial（程序化石刺+MID 染色）、
 *  DrawDebugHelpers（telegraph 红圈脉冲）、HealthComponent 的玩家伤害口、Kismet 的 GetPlayerPawn
 * [OUTPUT]: 对外提供 ABossColossus 的实现——BeginPlay 入场欢迎刺（bHarmless）+体量覆写（血 4500 慢速 180）；
 *  TickSlam 三模式（读条期按 PendingSpikes 蓝图画红圈脉冲）：①红圈阵（5×5 裁圆大圈 21 只）②锁定圈（大圈预告环刺）
 *  ③放射射线（8 向逐刺小圈）；ExecuteSlam 蓝图实体化（刺破地 0.12s）+逐刺各半径判定同帧；
 *  squash蓄力/实际落锤0.38s震屏（42XY/24Z，小于虫80/46）；仅原地愤怒放射模式播BossImpact，另两模式静音；刺0.12s→0.45s→0.35s销毁
 *   **✅ 原型跑通（2026-10-05 用户验收：BossBase 浮空修复后移动/追击/三段地刺/近战全链验证，登记为原型）**；
 *   受击动作已删常态（09-30 用户裁决"打怪一直抽搐很诡异"）：HitReactDamageThreshold 默认 99999=禁用，
 *   暴击体系落地后调低即"大伤才踉跄"分级（数据源 HealthComponent::LastDamageTaken）
 * [POS]: AI/ Boss 家族成员二实现：走位博弈型 Boss——Pending 蓝图统一预警与判定数据源，
 *  读条站桩=巨像的输出破绽窗口（博弈语义闭环）
 *  三模式地刺共用一次Health语义伤害入口，反馈以实际扣血和受击前落点提交现有漫画层。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "BossColossus.h"

#include "AICompanionDemo.h"
#include "AICompanionDemoCharacter.h"
#include "Audio/SFXLibrary.h"
#include "Animation/AnimationAsset.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 巨像色板（岩壳红纹——与祭坛红同族；不同 .cpp 匿名常量加类型前缀：kAltarRed 撞名案教训）
	const FColor kColossusRing = FColor::FromHex(TEXT("FF4A2E"));      // telegraph 红圈
	const FLinearColor kColossusSpikeTint(0.38f, 0.27f, 0.22f);        // 石刺岩棕
}

ABossColossus::ABossColossus()
{
	BossName = TEXT("裂地巨像");
	BossIndex = 2;
	MeleeDamage = 22.f;
	MeleeInterval = 2.5f;
	MeleeRange = 260.f;
	if (GetCharacterMovement())
	{
		// 巨像移速（四十七轮用户定标"比最低等级杂兵慢一些"）：360=介于蛮兵 300 与标准兵 450 之间——
		// 读条期照常行走（仅放射模式站桩蓄力）；移动=CharacterMovement 程序位移与模型无关
		GetCharacterMovement()->MaxWalkSpeed = 360.f;

		// 程序化石刺模板（引擎基础形状：Cone+基础材质——MID 染岩色，EmergencyBeacon 同范式）
		static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
		if (ConeFinder.Succeeded())
		{
			SpikeMesh = ConeFinder.Object;
		}
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (MatFinder.Succeeded())
		{
			SpikeMat = MatFinder.Object;
		}
	}

	// ---------- Grux 换装（四十七轮：兽型巨汉双持重斧——Sevarog 同管线 SingleNode 播自带动画） ----------
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> GruxMeshFinder(TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Meshes/Grux.Grux"));
	if (GruxMeshFinder.Succeeded())
	{
		GetMesh()->SetSkeletalMeshAsset(GruxMeshFinder.Object);
		GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	}
	static ConstructorHelpers::FObjectFinder<UAnimationAsset> GruxIdleFinder(TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/Idle.Idle"));
	if (GruxIdleFinder.Succeeded())
	{
		IdleAnim = GruxIdleFinder.Object;
		GetMesh()->SetAnimation(IdleAnim);   // SingleNode 默认动画
	}
	static ConstructorHelpers::FObjectFinder<UAnimationAsset> GruxRunFinder(TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/Run_Fwd.Run_Fwd"));
	if (GruxRunFinder.Succeeded())
	{
		RunAnim = GruxRunFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UAnimationAsset> GruxAttackFinder(TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/PrimaryAttack_LA.PrimaryAttack_LA"));
	if (GruxAttackFinder.Succeeded())
	{
		AttackAnim = GruxAttackFinder.Object;   // 放射蓄力=挥斧愤怒姿势
	}
	static ConstructorHelpers::FObjectFinder<UAnimationAsset> GruxHitFinder(TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/HitReact_Front.HitReact_Front"));
	if (GruxHitFinder.Succeeded())
	{
		HitReactAnim = GruxHitFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UAnimationAsset> GruxDeathFinder(TEXT("/Game/ParagonGrux/Characters/Heroes/Grux/Animations/Death_A.Death_A"));
	if (GruxDeathFinder.Succeeded())
	{
		DeathPresentationAnim = GruxDeathFinder.Object;   // 基类表现槽
	}

	// 血条随大体型抬高（Grux 兽型巨汉高于 Manny——260 顶会穿模）
	if (HealthBarComp)
	{
		HealthBarComp->SetRelativeLocation(FVector(0.f, 0.f, 330.f));
	}
}

void ABossColossus::BeginPlay()
{
	Super::BeginPlay();

	// 体量覆写（基类 BeginPlay 已设 2500——巨像是站桩读条型，全程可伤血牛；数值复测再调）
	if (HealthComponent)
	{
		HealthComponent->SetMaxHealth(4500.f, /*bFillCurrent*/ true);
		HealthComponent->OnDamaged.AddDynamic(this, &ABossColossus::HandleHitReact);
	}

	// 入场仪式：脚下环形石刺齐弹（"裂地而出"的主题表演）。刺锚探地落位（祭坛 spawn 抬升 180cm
	// 时胶囊尚未落地——不探地刺浮空 1.8m）。仪式刺无害化
	if (GetWorld())
	{
		for (int32 i = 0; i < 10; ++i)
		{
			const float Angle = (static_cast<float>(i) / 10.f) * 2.f * PI;
			FVector SpikeLoc = GetActorLocation() + FVector(FMath::Cos(Angle) * 320.f, FMath::Sin(Angle) * 320.f, 0.f);
			ResolveGroundZ(SpikeLoc);
			SpawnSpike(SpikeLoc, 0.f, 1.4f, /*bHarmless*/ true);
		}
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(96, 4.f, kColossusRing,
				TEXT("裂地巨像 苏醒！小心脚下红圈！"));
		}
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] 裂地巨像入场——环形欢迎刺 ×10（无害仪式）"));
	}
}

void ABossColossus::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HealthComponent || HealthComponent->IsDead())
	{
		return;
	}

	HitReactCooldown = FMath::Max(HitReactCooldown - DeltaSeconds, 0.f);
	PlayBaseAnim();   // 速度驱动 Idle/Run 两态巡检（态变才重启动画）
	UpdateSquash(DeltaSeconds);
	TickSpikes(DeltaSeconds);

	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		TickSlam(DeltaSeconds, PlayerPawn->GetActorLocation());
	}
}

void ABossColossus::TickSlam(float DeltaSeconds, const FVector& PlayerLoc)
{
	SlamCooldownCountdown = FMath::Max(SlamCooldownCountdown - DeltaSeconds, 0.f);

	// Telegraph 读条中：站桩（读条期近战/追击由基类 bHoldPosition 挂起——破绽窗口）。
	// 红圈按蓝图缓存的贴地环连点绘制（环随坡面起伏服帖地形——四十五轮用户裁决）；
	// 刺 slam 时刻才破地——红圈期零刺可见
	if (TelegraphCountdown >= 0.f)
	{
		TelegraphCountdown -= DeltaSeconds;

		// 线宽脉冲（半径已定死于贴地环缓存——动感从线宽来）
		const float Width = 5.f + 2.5f * (0.5f + 0.5f * FMath::Sin(GetGameTimeSinceCreation() * 8.f));
		for (const FColossusSpike& Pending : PendingSpikes)
		{
			const int32 Num = Pending.RingPoints.Num();
			if (Num < 2)
			{
				continue;
			}
			for (int32 i = 0; i < Num; ++i)
			{
				DrawDebugLine(GetWorld(), Pending.RingPoints[i], Pending.RingPoints[(i + 1) % Num],
					kColossusRing, false, 0.1f, 0, Width);
			}
		}

		if (TelegraphCountdown < 0.f)
		{
			ExecuteSlam();
			SlamCooldownCountdown = SlamCooldown;
		}
		return;   // 读条站桩
	}

	// 冷却结束：按模式轮换开 Telegraph（玩家 25m 内才出手）。蓝图此刻一次定格。
	// 四十七轮用户裁决：读条期照常行走（锚已定格不受 Boss 移动影响）——仅放射模式站桩蓄力
	// （以自身为中心的环阵是"捶地大招"，蓄力姿势+原地是仪式的一部分）
	if (SlamCooldownCountdown <= 0.f &&
		FVector::Dist2D(GetActorLocation(), PlayerLoc) <= 2500.f)
	{
		SquashRecoilCountdown = 0.f;
		SlamMode = (SlamMode + 1) % 3;   // 首击自增后=1 锁定圈（温和入场），红圈阵压轴
		SlamCenter = PlayerLoc;          // 模式 0/1 锚=玩家定格；模式 2 射线锚=Boss（Build 内自理）
		if (SlamMode == 0)
		{
			BuildSeaPattern(PlayerLoc);
			TelegraphCountdown = SeaTelegraphSeconds;
		}
		else if (SlamMode == 1)
		{
			BuildLockPattern(PlayerLoc);
			TelegraphCountdown = LockTelegraphSeconds;
		}
		else
		{
			BuildRayPattern();
			TelegraphCountdown = RayTelegraphSeconds;
			bHoldPosition = true;   // 放射=捶地大招：站桩蓄力（愤怒挥斧+原地读条）
			PlayTransientAnim(AttackAnim);   // 播完回基础态；slam 打断即回（瞬态单柄防旧回切）
		}
	}
	else
	{
		bHoldPosition = false;   // 冷却/逼近期恢复追击
	}
}

FString ABossColossus::DebugSlamState() const
{
#if WITH_EDITOR
	return FString::Printf(TEXT("{\"mode\":%d,\"telegraphRemaining\":%.4f,\"cooldownRemaining\":%.4f,\"seaExecutions\":%d,\"lockExecutions\":%d,\"rayExecutions\":%d,\"bossImpactRequests\":%d}"),
		SlamMode, TelegraphCountdown, SlamCooldownCountdown, SlamExecutionCounts[0], SlamExecutionCounts[1], SlamExecutionCounts[2], BossImpactRequests);
#else
	return TEXT("{}");
#endif
}

void ABossColossus::ExecuteSlam()
{
	if (SlamMode >= 0 && SlamMode < 3) ++SlamExecutionCounts[SlamMode];
	// 实际落锤时才震，预告期保持镜头稳定；强度小于虫破土/入土。
	if (AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
		Hero->PulseBossCameraShake(SlamShakeDuration, SlamShakeXY, SlamShakeZ);
	if (SlamMode == 2)
	{
		++BossImpactRequests;
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::BossImpact);
	}
	// 蓝图实体化（此刻刺才破地——红圈期零刺可见）+squash 弹起
	MaterializeSpikes();
	SquashRecoilCountdown = 0.35f;

	// 逐刺判定（各刺自带半径——红圈阵大圆/射线小圆；仪式刺与零半径豁免）
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!PlayerPawn)
	{
		return;
	}
	bool bHitPlayer = false;
	for (const FColossusSpike& Spike : ActiveSpikes)
	{
		if (!Spike.bHarmless && Spike.HitRadius > 0.f &&
			FVector::Dist2D(Spike.GroundLoc, PlayerPawn->GetActorLocation()) <= Spike.HitRadius)
		{
			bHitPlayer = true;
			break;
		}
	}

	if (bHitPlayer)
	{
		if (UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>())
		{
			PlayerHealth->ApplyCombatDamage(SlamDamage, this, {ECombatDamageKind::Spike, PlayerPawn->GetActorLocation() + FVector(0, 0, 160)});
		}
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] 裂地巨像地刺命中 %.0f 伤（模式 %d 红圈阵/锁定圈/放射）"), SlamDamage, SlamMode);
	}
	else
	{
		UE_LOG(LogAICompanionDemo, Log, TEXT("[Boss] 裂地巨像地刺落空（模式 %d，蓝图 %d 刺）——走位博弈成立"), SlamMode, ActiveSpikes.Num());
	}
}

// ---------- 刺位蓝图（telegraph 期定格，slam 期实体化） ----------

void ABossColossus::BuildSeaPattern(const FVector& Center)
{
	ClearPending();
	// 5×5 裁圆网格：步长 560 圈心距、判定 230——两圈间缝 100（裕量 50>胶囊 42 紧钻）、
	// 四圈角 166 宽松——双层安全区；裁圆边缘柔和（用户裁决"方形边缘过于生硬"）
	const int32 Steps = 2;   // 5×5
	for (int32 GX = -Steps; GX <= Steps; ++GX)
	{
		for (int32 GY = -Steps; GY <= Steps; ++GY)
		{
			FVector Loc = Center + FVector(GX * SeaGridStep, GY * SeaGridStep, 0.f);
			if (FVector::Dist2D(Loc, Center) <= SeaCullRadius)
			{
				ResolveGroundZ(Loc);   // 沙丘起伏——刺位逐个贴地
				FColossusSpike& Pending = PendingSpikes.AddDefaulted_GetRef();
				Pending.GroundLoc = Loc;
				Pending.HitRadius = SeaCircleRadius;
				Pending.Scale = SeaSpikeScale;
				BuildGroundRing(Loc, SeaCircleRadius, Pending.RingPoints);
			}
		}
	}
	UE_LOG(LogAICompanionDemo, Log, TEXT("[Boss] 红圈阵蓝图定格：圆心 %s 共 %d 圈"), *Center.ToCompactString(), PendingSpikes.Num());
}

void ABossColossus::BuildLockPattern(const FVector& Center)
{
	ClearPending();
	for (int32 i = 0; i < 8; ++i)
	{
		const float Angle = (static_cast<float>(i) / 8.f) * 2.f * PI;
		FVector Loc = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * LockRadius;
		ResolveGroundZ(Loc);
		FColossusSpike& Pending = PendingSpikes.AddDefaulted_GetRef();
		Pending.GroundLoc = Loc;
		Pending.HitRadius = SpikeHitRadius;
	}
	FVector CenterLoc = Center;
	ResolveGroundZ(CenterLoc);
	FColossusSpike& CenterSpike = PendingSpikes.AddDefaulted_GetRef();   // 圈心一根（站圈心不动也要挨）
	CenterSpike.GroundLoc = CenterLoc;
	CenterSpike.HitRadius = SpikeHitRadius;
	// 锁定圈的预警环=大圈贴地环挂在圈心刺上（环刺留空不画小圈——过大预警不骗玩家去死）
	BuildGroundRing(CenterLoc, LockRadius, CenterSpike.RingPoints);
}

void ABossColossus::BuildRayPattern()
{
	ClearPending();
	// 8 向均匀放射（世界角度阵列——锚定 Boss，读条站桩方向恒定）：玩家在射线夹缝躲避。
	// 圈径由内向外递减（四十六轮用户定标：内圈=红圈阵圈径×1.5=345 → 外圈 170）——
	// 近身大圈死地（内侧缝彻底闭合）/远端小圈可躲；刺体量随圈径同梯度
	for (int32 Ray = 0; Ray < RayCount; ++Ray)
	{
		const float Angle = (static_cast<float>(Ray) / RayCount) * 2.f * PI;
		const FVector Dir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		for (int32 Step = 0; Step < RaysSpikesPerRay; ++Step)
		{
			const float T = RaysSpikesPerRay > 1 ? static_cast<float>(Step) / (RaysSpikesPerRay - 1) : 0.f;
			FVector Loc = GetActorLocation() + Dir * (RayStartDist + RayStep * Step);
			ResolveGroundZ(Loc);
			FColossusSpike& Pending = PendingSpikes.AddDefaulted_GetRef();
			Pending.GroundLoc = Loc;
			Pending.HitRadius = FMath::Lerp(RayInnerRadius, RayOuterRadius, T);
			Pending.Scale = FMath::Lerp(1.7f, 1.2f, T);
			BuildGroundRing(Loc, Pending.HitRadius, Pending.RingPoints);
		}
	}
}

void ABossColossus::ResolveGroundZ(FVector& Loc)
{
	FHitResult FloorHit;
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(ColossusSpikeDrop), false, this);
	FCollisionObjectQueryParams FloorObjects(ECC_WorldStatic);
	const FVector ProbeStart = FVector(Loc.X, Loc.Y, Loc.Z + 8000.f);
	if (GetWorld()->LineTraceSingleByObjectType(FloorHit, ProbeStart,
		ProbeStart - FVector(0.f, 0.f, 24000.f), FloorObjects, FloorParams))
	{
		Loc.Z = FloorHit.ImpactPoint.Z;
	}
}

void ABossColossus::BuildGroundRing(const FVector& Center, float Radius, TArray<FVector>& OutPoints)
{
	// 段数自适应半径：弦矢高 R(1-cos(π/N)) 恒 < 抬升 8cm（345/24 段→3.0、230/19→3.2、90/10→4.4）
	const int32 Segments = FMath::Clamp(FMath::RoundToInt(Radius / 12.f), 10, 24);
	OutPoints.Reset(Segments);
	for (int32 i = 0; i < Segments; ++i)
	{
		const float A = (static_cast<float>(i) / Segments) * 2.f * PI;
		FVector Point = Center + FVector(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, 0.f);
		ResolveGroundZ(Point);
		Point.Z += 8.f;   // 抬升量盖过弦矢高——弦线不切入坡面
		OutPoints.Add(Point);
	}
}

void ABossColossus::MaterializeSpikes()
{
	for (const FColossusSpike& Pending : PendingSpikes)
	{
		SpawnSpike(Pending.GroundLoc, Pending.HitRadius, Pending.Scale, Pending.bHarmless);
	}
	PendingSpikes.Reset();
}

void ABossColossus::ClearPending()
{
	PendingSpikes.Reset();
}

// ---------- Active 刺动画 ----------

void ABossColossus::SpawnSpike(const FVector& GroundLoc, float HitRadius, float Scale, bool bHarmless)
{
	if (!SpikeMesh || !SpikeMat)
	{
		return;
	}

	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetStaticMesh(SpikeMesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // 纯视觉（伤害走逐刺距离判定）
	Comp->SetCastShadow(false);   // 数十根短命刺不进阴影——省一帧是一帧
	Comp->RegisterComponent();
	Comp->SetWorldScale3D(FVector(Scale, Scale, Scale * (SpikeHeightCm / 140.f)));   // 底径=140×Scale，高等比
	Comp->SetWorldLocation(GroundLoc + FVector(0.f, 0.f, -SpikeHeightCm * Scale * 0.5f));   // 埋地起手

	// 共享 MID 染岩色（单 MID 全体刺复用）
	if (!SharedSpikeMid)
	{
		SharedSpikeMid = UMaterialInstanceDynamic::Create(SpikeMat, this);
		if (SharedSpikeMid)
		{
			SharedSpikeMid->SetVectorParameterValue(TEXT("Color"), kColossusSpikeTint);
		}
	}
	Comp->SetMaterial(0, SharedSpikeMid);

	FColossusSpike& Spike = ActiveSpikes.AddDefaulted_GetRef();
	Spike.Comp = Comp;
	Spike.GroundLoc = GroundLoc;
	Spike.HitRadius = HitRadius;
	Spike.Scale = Scale;
	Spike.bHarmless = bHarmless;
	Spike.Phase = 0;   // 全弹中
	Spike.PhaseTime = 0.f;
}

void ABossColossus::TickSpikes(float DeltaSeconds)
{
	for (int32 i = ActiveSpikes.Num() - 1; i >= 0; --i)
	{
		FColossusSpike& Spike = ActiveSpikes[i];
		if (!Spike.Comp)
		{
			ActiveSpikes.RemoveAt(i);
			continue;
		}

		Spike.PhaseTime += DeltaSeconds;
		float T = 0.f;
		switch (Spike.Phase)
		{
		case 0:   // 全弹：0.12s 冲顶（slam 时刻破地——红圈期零刺可见）
			T = FMath::Min(Spike.PhaseTime / 0.12f, 1.f);
			SetSpikeT(Spike, T);
			if (T >= 1.f)
			{
				Spike.Phase = 1;
				Spike.PhaseTime = 0.f;
			}
			break;

		case 1:   // 保持：矗立示形（威胁的余韵）
			if (Spike.PhaseTime >= SpikeHoldSeconds)
			{
				Spike.Phase = 2;
				Spike.PhaseTime = 0.f;
			}
			break;

		case 2:   // 缩回：0.35s 沉地销毁
			T = 1.f - FMath::Min(Spike.PhaseTime / 0.35f, 1.f);
			SetSpikeT(Spike, T);
			if (T <= 0.f)
			{
				Spike.Comp->DestroyComponent();
				ActiveSpikes.RemoveAt(i);
			}
			break;
		}
	}
}

void ABossColossus::SetSpikeT(FColossusSpike& Spike, float T) const
{
	// t=0 埋地 → t=1 全弹（露高留底部埋深余量，防缩回时底面闪现）
	const float H = SpikeHeightCm * Spike.Scale;
	const float CenterZ = FMath::Lerp(-H * 0.5f, H * 0.5f - H * 0.12f, T);
	Spike.Comp->SetWorldLocation(Spike.GroundLoc + FVector(0.f, 0.f, CenterZ));
}

void ABossColossus::UpdateSquash(float DeltaSeconds)
{
	if (!GetMesh())
	{
		return;
	}
	if (HealthComponent && HealthComponent->IsDead())
	{
		GetMesh()->SetRelativeScale3D(FVector(1.f));   // 判死复位——别让尸体停在下蹲形态
		return;
	}

	// 下蹲蓄力仅放射模式（站桩捶地大招）——行走读条时下蹲观感怪异（四十七轮裁决"读条正常行走"）
	FVector Target = FVector(1.f);
	if (TelegraphCountdown >= 0.f && SlamMode == 2)
	{
		Target = FVector(1.1f, 1.1f, 0.72f);   // 站桩下蹲：宽扁蓄力
	}
	else if (SquashRecoilCountdown > 0.f)
	{
		SquashRecoilCountdown -= DeltaSeconds;
		Target = FVector(0.94f, 0.94f, 1.14f);   // slam 弹起：纵向拉伸
	}
	GetMesh()->SetRelativeScale3D(FMath::VInterpTo(GetMesh()->GetRelativeScale3D(), Target, DeltaSeconds, 10.f));
}

// ---------- Grux 动画三态机 ----------

void ABossColossus::PlayBaseAnim()
{
	if (!GetMesh() || (HealthComponent && HealthComponent->IsDead()))
	{
		return;   // 判死自愈——死亡动画不被基础态顶掉
	}
	const bool bMoving = GetVelocity().Size2D() > 50.f;
	if (bMoving == bRunningAnim)
	{
		return;   // 态未变不重启（防每帧 PlayAnimation 重置循环进度）
	}
	bRunningAnim = bMoving;
	UAnimationAsset* Anim = bMoving ? RunAnim : IdleAnim;
	if (Anim)
	{
		GetMesh()->Stop();
		GetMesh()->PlayAnimation(Anim, true);
	}
}

void ABossColossus::PlayTransientAnim(UAnimationAsset* Anim)
{
	if (!Anim || !GetMesh())
	{
		return;
	}
	GetMesh()->Stop();
	GetMesh()->PlayAnimation(Anim, false);
	const float PlayLength = Anim->GetPlayLength();
	GetWorldTimerManager().ClearTimer(TransientAnimTimer);   // 瞬态互顶：旧回切作废（攻击播一半挨打→Hitreact 接管）
	TWeakObjectPtr<ABossColossus> WeakThis(this);
	GetWorldTimerManager().SetTimer(TransientAnimTimer, [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->PlayBaseAnim();   // 播完回基础态（内部判死自愈）
		}
	}, PlayLength, false);
}

void ABossColossus::HandleHitReact(UHealthComponent* /*HealthComp*/, AActor* /*InstigatorActor*/)
{
	// 受击表现（召唤师同款标准）：0.5s 节流防弹幕式抽搐。
	// 09-30 用户裁决"删常态受击动作"：高频弹下 0.5s 一次的踉跄=Boss 永远在抽搐（诡异）——
	// 单次伤害低于阈值一律不播（阈值默认 99999=禁用；暴击体系落地后调低即成"大伤才踉跄"分级）
	if (!HealthComponent || HealthComponent->GetLastDamageTaken() < HitReactDamageThreshold)
	{
		return;
	}
	if (!HitReactAnim || !GetMesh() || (HealthComponent && HealthComponent->IsDead()))
	{
		return;
	}
	if (HitReactCooldown > 0.f)
	{
		return;
	}
	HitReactCooldown = 0.5f;
	PlayTransientAnim(HitReactAnim);
}
