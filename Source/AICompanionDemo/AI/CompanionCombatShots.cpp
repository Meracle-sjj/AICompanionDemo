/** [INPUT]: Controller火力参数、ICombatant活敌、Health伤害根入口、感电显示与UI/CombatFeedbackSubsystem处决反馈。
 * [OUTPUT]: FireShot和真实处决刀图标/实际伤害大数；感电0.5秒互斥配对/减半/5秒不消费，AI穿透折射互斥，Nova继承玩家状态。
 * [POS]: Companion火力局部实现；模式被动只由Health乘算，本文件提交原始伤害。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md */
#include "AI/CompanionAIController.h"
#include "AI/Combatant.h"
#include "AI/EnemyGrunt.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoCharacter.h"
#include "Audio/SFXLibrary.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Relics/BossRelicComponent.h"
#include "Vfx/ShockArcActor.h"
#include "UI/CombatFeedbackSubsystem.h"

// ---------- 命中链（09-24 弹道融合） ----------

void ACompanionAIController::ApplyHitChain(AActor* DirectVictim, UHealthComponent* DirectHealth, const FVector& ChainPoint, float ChainScale, AActor* InstigatorPawn)
{
	// 溅射：以本次命中点为心——每个命中都爆（旧版仅主命中且排除走廊全部命中，穿透组合收益归零：
	// 穿透打的一串怪互相吃不到溅射——用户实测"穿透的时候不能用爆炸"定罪）。溅射伤害不经本函数=链不递归
	if (bExplosiveUnlocked)
	{
		DrawDebugSphere(GetWorld(), ChainPoint, ExplosionRadius, 12, FColor::Orange, false, 0.2f, 0, 2.f);
		DrawDebugSphere(GetWorld(), ChainPoint, ExplosionRadius * 0.55f, 12, FColor::Yellow, false, 0.15f, 0, 1.5f);
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Explosion);   // [SFX] 爆炸（独立槽·稀有）——每次溅射一声，不逐受击者叠加
		TArray<AActor*> BlastCandidates;
		CollectAliveCombatants(GetWorld(), BlastCandidates);
		for (AActor* BlastVictim : BlastCandidates)
		{
			if (!BlastVictim || BlastVictim == DirectVictim)
			{
				continue;
			}
			if (FVector::DistSquared(ChainPoint, BlastVictim->GetActorLocation()) <= ExplosionRadius * ExplosionRadius)
			{
				if (UHealthComponent* BlastHealth = Cast<ICombatant>(BlastVictim)->GetHealthComponent())
				{
					const float BlastDmg = AttackDamage * ChainScale * ExplosionDamageScale;
					const float BeforeBlastHealth = BlastHealth->GetCurrentHealth();
					BlastHealth->ApplyDamage(BlastDmg, InstigatorPawn);
					if (!IsValid(BlastHealth) || !IsValid(BlastVictim)) continue;
					const float ActualBlastDamage = FMath::Max(BeforeBlastHealth - BlastHealth->GetCurrentHealth(), 0.f);
					// 伤害飘字：溅射橙色小字（区别于直击黄字）
					DrawDebugString(GetWorld(), BlastVictim->GetActorLocation() + FVector(0.f, 0.f, 90.f),
						FString::Printf(TEXT("-%.1f"), ActualBlastDamage), nullptr, FColor::Orange, 0.5f, false, 0.9f);
				}
			}
		}
	}

	if (!IsValid(DirectVictim) || !IsValid(DirectHealth)) return;

	// 寒霜链：命中即染冻减速（弹到的目标同样生效——"弹射也没法触发附加收益"清账）
	if (bFrostUnlocked)
	{
		if (AEnemyGrunt* FrostVictim = Cast<AEnemyGrunt>(DirectVictim))
		{
			FrostVictim->ApplySlow(2.f, 0.6f);
			FrostVictim->RefreshFrostResidue(2.f);
		}
	}

	// 猎印处决独立于冰/电类型，普通敌人与Boss共享；无敌直击不消费预存标记。
	if (ICombatant* MarkVictim = Cast<ICombatant>(DirectVictim); MarkVictim && DirectHealth && !DirectHealth->IsInvulnerable())
	{
		// 处决（猎杀印记）：AI 命中带印记目标消耗全部层——每层 ExecuteDamagePerMark 额外伤害（玩家攒、AI 爆）
		if (const int32 Marks = MarkVictim->ConsumeHuntMarks(); Marks > 0)
		{
			const float ExecDmg = ExecuteDamagePerMark * Marks;
			float ActualExecDamage = 0.f;
			if (IsValid(DirectHealth))
			{
				const float BeforeExecHealth = DirectHealth->GetCurrentHealth();
				DirectHealth->ApplyDamage(ExecDmg, InstigatorPawn);
				if (IsValid(DirectHealth)) ActualExecDamage = FMath::Max(BeforeExecHealth - DirectHealth->GetCurrentHealth(), 0.f);
			}
			UCombatFeedbackSubsystem::ShowExecute(GetWorld(), ChainPoint + FVector(0.f, 0.f, 60.f), ActualExecDamage, Marks);
			DrawDebugSphere(GetWorld(), ChainPoint, 40.f, 10, FColor::FromHex(TEXT("FFD24D")), false, 0.25f, 0, 2.f);
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Element] 处决结算：印记×%d 额外 %.1f 伤 → %s"),
				Marks, ActualExecDamage, *GetNameSafe(DirectVictim));
		}
	}

	// 感电改由集中半秒节拍执行；子弹/溅射/处决等伤害均不追加传导。

}

void ACompanionAIController::NovaBurst(AActor* InstigatorPawn, const FVector& Center)
{
	const TWeakObjectPtr<AAICompanionDemoCharacter> ElementSource = Cast<AAICompanionDemoCharacter>(InstigatorPawn);
	TMap<TWeakObjectPtr<AActor>, FVector> ElementHits;
	for (int32 i = 0; i < 8; ++i)
	{
		const float Angle = FMath::DegreesToRadians(i * 45.f);
		FireShot(InstigatorPawn, Center, FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0), .7f, FColor::Magenta, &ElementHits);
	}
	// 全部AI射线先结算旧猎印，整次爆射结束后统一挂玩家当前状态。
	// 重叠走廊不会在后一条射线中吃掉刚挂的新猎印，同目标仅挂一次元素。
	for (const TPair<TWeakObjectPtr<AActor>, FVector>& Hit : ElementHits)
	{
		if (!ElementSource.IsValid() || !Hit.Key.IsValid()) continue;
		if (ICombatant* Combatant = Cast<ICombatant>(Hit.Key.Get()); Combatant && !Combatant->IsDead())
		{
			FColor Color = FColor::Magenta;
			ElementSource->ApplyElementPayload(Hit.Key.Get(), 0.f, Color, Hit.Value);
		}
	}
	for (const TPair<TWeakObjectPtr<AActor>, FVector>& Hit : ElementHits)
	{
		if (!ElementSource.IsValid() || !Hit.Key.IsValid()) continue;
		if (UBossRelicComponent* Relics = ElementSource->FindComponentByClass<UBossRelicComponent>())
			Relics->NotifyElementalRayHit(ElementSource.Get(), Hit.Key.Get(), Hit.Value);
	}
}
void ACompanionAIController::FireShot(AActor* InstigatorPawn, const FVector& Origin, const FVector& Direction, float DamageScale, const FColor& HitFlashColor,
	TMap<TWeakObjectPtr<AActor>, FVector>* ElementHits)
{
	// 闪现爆射的调用者是玩家，实际子弹仍属于队友；模式伤害在Health按此归属统一乘一次。
	if (IsValid(GetPawn())) InstigatorPawn=GetPawn();
	if (UGameplayStatics::GetGlobalTimeDilation(this) < .01f || GetWorld()->IsPaused()) return;
	const APawn* CombatGateHero = UGameplayStatics::GetPlayerPawn(this, 0);
	const UHealthComponent* HeroHealth = CombatGateHero ? CombatGateHero->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!HeroHealth || HeroHealth->GetCurrentHealth() <= 0.f || HeroHealth->IsDead()) return;
	// 走廊命中三审定罪（用户实测"穿透打不到多个人"三连案）：一审零半径细线 trace 共线太苛刻；
	// 二审球形扫掠 35cm（09-26）——管道 35 + 敌人胶囊 42 ≈ 77cm 命中带宽，要两个敌人近乎串在
	// 一条直线上才都中，VS 式围杀是扇形散布，穿透十次九次只穿一个。三审（本案）抛弃物理查询，
	// 改数学走廊：全场活敌"胸口点到射线段距离 ≤ 走廊半径"即命中——与妖术师射线结算同范式
	// （PointDistToSegment 判宽），带宽纯参数可控（普通 60 / 穿透 130），零通道语义坑；沿射线
	// 投影排序保"首个吃足额"近者优先。地形/墙不进集合，两代遮挡补丁（法线判据/Landscape IsA）退役
	const FVector End = Origin + Direction * (AttackRange + 400.f);
	const FVector DirNorm = Direction.GetSafeNormal();
	const float CorridorRadius = bPierceUnlocked ? PierceCorridorRadius : ShotCorridorRadius;

	// 走廊命中集：胸口（z+40，与折射弹射点同款）入廊即算，按投影深度排序。
	// 候选集按 ICombatant 契约收集（09-30 召唤师案定罪：TActorIterator<AEnemyGrunt> 类型
	// 扫描把 ABossBase 系排除在命中集外——弹道穿过 Boss 不掉血的元凶；决策层认接口，
	// 火力层必须同认，"敌人"全项目一个定义）
	TArray<TPair<float, AActor*>> InCorridor;
	TArray<AActor*> CorridorCandidates;
	CollectAliveCombatants(GetWorld(), CorridorCandidates);
	for (AActor* Candidate : CorridorCandidates)
	{
		const FVector Chest = Candidate->GetActorLocation() + FVector(0.f, 0.f, 40.f);
		if (FMath::PointDistToSegment(Chest, Origin, End) <= CorridorRadius)
		{
			InCorridor.Add({FVector::DotProduct(Chest - Origin, DirNorm), Candidate});
		}
	}
	InCorridor.Sort([](const TPair<float, AActor*>& A, const TPair<float, AActor*>& B)
	{
		return A.Key < B.Key;
	});

	// 光束样式随升级卡变形（感知优先——抽了什么卡，弹道肉眼可见地变色变形）：
	// 穿透=红 10px 0.3s 贯穿整条走廊（09-25 用户点名加粗——视觉权重配得上走廊语义）；
	// 狂怒=紫（E5 暂态优先于卡牌常驻色——短时效果要最醒目，与狂怒道具同色系）；速射=青色；重击=加粗；基础=橙 2px
	const float Thickness = bPierceUnlocked ? 10.f : (bHeavyRoundsUnlocked ? 4.f : 2.f);
	const FColor BeamColor = bPierceUnlocked ? FColor::Red
		: (FrenzyCooldownFactor < 1.f ? FColor::FromHex(TEXT("B266FF"))
			: (bRapidFireUnlocked ? FColor::FromHex(TEXT("4DD9E6")) : FColor::Orange));
	const float LifeTime = bPierceUnlocked ? 0.3f : 0.15f;

	FVector BeamEnd = End;
	TSet<AActor*> Struck;
	int32 PiercedCount = 0;
	for (const TPair<float, AActor*>& CorridorHit : InCorridor)
	{
		AActor* Victim = CorridorHit.Value;
		const FVector HitPoint = Origin + DirNorm * CorridorHit.Key;  // 命中点=胸口在走廊轴线上的投影（飘字/火花居中可读）
		if (!IsValid(Victim)) continue;
		ICombatant* VictimCombatant = Cast<ICombatant>(Victim);
		if (!VictimCombatant || VictimCombatant->IsDead()) continue;
		UHealthComponent* Health = VictimCombatant ? VictimCombatant->GetHealthComponent() : nullptr;
		if (!Health)
		{
			continue;
		}
		Struck.Add(Victim);

		// 伤害：首个命中者吃足额 ×Scale，穿透卡让走廊内后续目标继续受创
		const bool bIsFirst = Struck.Num() == 1;
		const float Scale = bIsFirst ? DamageScale : (bPierceUnlocked ? DamageScale * PierceDamageScale : 0.f);
		if (Scale <= 0.f)
		{
			BeamEnd = HitPoint;  // 无穿透：弹道止步于首个目标
			break;
		}

		if (!IsValid(Health)) continue;
		const float BeforeShotHealth = Health->GetCurrentHealth();
		const float ShotModeMultiplier = GetModeDamageMultiplier();
		Health->ApplyDamage(AttackDamage * Scale, InstigatorPawn);
		if (!IsValid(Health)||!IsValid(Victim)) continue;
		const float ActualShotDamage = FMath::Max(BeforeShotHealth - Health->GetCurrentHealth(), 0.f);
		LastShotRawDamage = AttackDamage * Scale;
		LastShotActualDamage = ActualShotDamage;
		LastShotDisplayedDamage = FMath::RoundToFloat(ActualShotDamage * 10.f) / 10.f;
		LastShotModeMultiplier = ShotModeMultiplier;
		LastShotVictim = Victim;
		if (const APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			if (UBossRelicComponent* Relics = Hero->FindComponentByClass<UBossRelicComponent>())
			{ Relics->NotifyCompanionHit(InstigatorPawn, ActualShotDamage); }
		}
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Hit_Enemy);   // [SFX] 命中
		BeamEnd = bPierceUnlocked ? End : HitPoint;
		++PiercedCount;

		// 伤害飘字（09-22 G5，用户点名"一定要加"）：命中点上方 -N——输出看得见；首目标黄大字，穿透后续银小字
		DrawDebugString(GetWorld(), HitPoint + FVector(0.f, 0.f, 28.f),
			FString::Printf(TEXT("-%.1f"), ActualShotDamage), nullptr,
			bIsFirst ? FColor::Yellow : FColor::Silver, 0.6f, false, bIsFirst ? 1.3f : 1.0f);

		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Combat] %s命中 %s，伤害 %.1f，余血 %.1f"),
			bPierceUnlocked && !bIsFirst ? TEXT("穿透") : TEXT(""),
			*GetNameSafe(Victim), ActualShotDamage, Health->GetCurrentHealth());

		// 命中火花常驻（用户 2026-09-13 定调"击中特效保留"）且**按伤害来源分色**（同轮定调"来源不同看起来更有意思"）：
		// 重击=红 22cm 大球；常态=调用方指定的来源色（激光黄/爆射品红）
		DrawDebugSphere(GetWorld(), HitPoint, bHeavyRoundsUnlocked ? 22.f : 9.f, 8,
			bHeavyRoundsUnlocked ? FColor::Red : HitFlashColor, false, bHeavyRoundsUnlocked ? 0.18f : 0.12f, 0, 1.2f);

		// 命中链（09-24 弹道融合）：溅射+寒霜——每个命中都触发，穿透段不再"没有附加收益"
		ApplyHitChain(Victim, Health, HitPoint, Scale, InstigatorPawn);
		if (ElementHits && IsValid(Victim)) ElementHits->FindOrAdd(TWeakObjectPtr<AActor>(Victim)) = HitPoint;

		// 折射（光棱坦克，用户 2026-09-13 提案）：命中首目标后向最近敌人弹射——
		// 品红折射线+品红命中火花（与激光黄分色）；与穿透互斥，卡池与运行态双重检查，即使实例/调试绕过卡池也禁止协同
		if (bPrismUnlocked && !bPierceUnlocked && bIsFirst)
		{
			FVector LastPoint = HitPoint;
			AActor* LastVictim = Victim;
			float BounceScale = DamageScale * PrismBounceDecay;
			for (int32 Bounce = 0; Bounce < PrismBounces && BounceScale > 0.f; ++Bounce)
			{
				// 弹射点最近的其他活敌（已击中者不回头）——候选集契约收集（Boss 可为弹跳目标）
				AActor* Next = nullptr;
				float BestSq = PrismBounceRadius * PrismBounceRadius;
				TArray<AActor*> BounceCandidates;
				CollectAliveCombatants(GetWorld(), BounceCandidates);
				for (AActor* Candidate : BounceCandidates)
				{
					if (!Candidate || Candidate == LastVictim || Struck.Contains(Candidate))
					{
						continue;
					}
					const float Dsq = FVector::DistSquared(LastPoint, Candidate->GetActorLocation());
					if (Dsq < BestSq)
					{
						BestSq = Dsq;
						Next = Candidate;
					}
				}
				if (!Next)
				{
					break;
				}
				const FVector NextChest = Next->GetActorLocation() + FVector(0.f, 0.f, 40.f);
				DrawDebugLine(GetWorld(), LastPoint, NextChest, FColor::Magenta, false, 0.25f, 0, 2.f);
				if (UHealthComponent* NextHealth = Cast<ICombatant>(Next)->GetHealthComponent())
				{
					if (!IsValid(NextHealth)) break;
					const float BeforeBounceHealth = NextHealth->GetCurrentHealth();
					NextHealth->ApplyDamage(AttackDamage * BounceScale, InstigatorPawn);
					if (!IsValid(NextHealth)||!IsValid(Next)) break;
					const float ActualBounceDamage = FMath::Max(BeforeBounceHealth - NextHealth->GetCurrentHealth(), 0.f);
					if (const APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0))
					{
						if (UBossRelicComponent* Relics = Hero->FindComponentByClass<UBossRelicComponent>())
						{ Relics->NotifyCompanionHit(InstigatorPawn, ActualBounceDamage); }
					}
					DrawDebugSphere(GetWorld(), NextChest, 9.f, 8, FColor::Magenta, false, 0.12f, 0, 1.2f);
					// 伤害飘字：折射跳银小字（来源与主命中黄字分色）
					DrawDebugString(GetWorld(), NextChest + FVector(0.f, 0.f, 22.f),
						FString::Printf(TEXT("-%.1f"), ActualBounceDamage), nullptr, FColor::Silver, 0.6f, false, 1.0f);
					// 命中链：弹到的目标同样吃溅射+寒霜（链不递归——溅射伤害不二次引爆）
					ApplyHitChain(Next, NextHealth, NextChest, BounceScale, InstigatorPawn);
					if (ElementHits && IsValid(Next)) ElementHits->FindOrAdd(TWeakObjectPtr<AActor>(Next)) = NextChest;
					UE_LOG(LogAICompanionDemo, Warning, TEXT("[Combat] 折射第 %d 跳命中 %s，伤害 %.1f"),
						Bounce + 1, *GetNameSafe(Next), ActualBounceDamage);
				}
				Struck.Add(Next);
				LastPoint = NextChest;
				LastVictim = Next;
				BounceScale *= PrismBounceDecay;
			}
		}

		// 溅射/寒霜已迁入命中链 ApplyHitChain（09-24 弹道融合）——直击/穿透/弹跳统一触发

		if (!bPierceUnlocked)
		{
			break;  // 无穿透：只打第一个
		}
	}

	// 穿透复测判读（09-26 三审）：一条走廊穿了几人，日志一眼定修没修好
	if (bPierceUnlocked && PiercedCount > 0)
	{
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Combat] 穿透走廊命中 %d 人（半径 %.0fcm）"), PiercedCount, CorridorRadius);
	}

	DrawDebugLine(GetWorld(), Origin, BeamEnd,
		BeamColor, /*bPersistentLines*/ false, /*LifeTime*/ LifeTime, /*DepthPriority*/ 0, /*Thickness*/ Thickness);
}

// ---------- 持续感电（5秒残留，半秒集中配对） ----------

void ACompanionAIController::TickShockConduction(float DeltaSeconds)
{
	UWorld* World=GetWorld();
	if (!World||World->IsPaused()||UGameplayStatics::GetGlobalTimeDilation(this)<.01f) return;
	const APawn* Hero=UGameplayStatics::GetPlayerPawn(this,0);
	const UHealthComponent* HeroHealth=Hero?Hero->FindComponentByClass<UHealthComponent>():nullptr;
	if (!IsValid(GetPawn())||!HeroHealth||HeroHealth->IsDead()) { ConductionPulseAccum=0.f; return; }
	ConductionPulseAccum+=FMath::Max(DeltaSeconds,0.f);
	constexpr float PulseInterval=.5f;
	if (ConductionPulseAccum<PulseInterval) return;
	// 卡顿不追补多拍，避免同一帧爆发堆积；正常游戏秒固定半秒一拍。
	ConductionPulseAccum=FMath::Fmod(ConductionPulseAccum,PulseInterval);
	const float RawDamage=.5f*FMath::Max(FMath::Max(ConductionBaseDamage,0.f),
		FMath::Max(AttackDamage,0.f)*FMath::Max(ConductionScale,0.f));
	if (RawDamage<=0.f) return;
	const auto Ready=[](const TWeakObjectPtr<AEnemyGrunt>& WeakEnemy)
	{
		AEnemyGrunt* Enemy=WeakEnemy.Get();
		const UHealthComponent* Health=Enemy?Enemy->GetHealthComponent():nullptr;
		return IsValid(Enemy)&&!Enemy->IsDead()&&Enemy->GetShockResidue()>0.f
			&&IsValid(Health)&&!Health->IsDead()&&!Health->IsInvulnerable();
	};
	TArray<TWeakObjectPtr<AEnemyGrunt>> Candidates;
	for (TActorIterator<AEnemyGrunt> It(World);It;++It)
	{
		const TWeakObjectPtr<AEnemyGrunt> Candidate=*It;
		if (Ready(Candidate)) Candidates.Add(Candidate);
	}
	// 固定出生身份排序，保证同一拍的配对顺序不取决于容器哈希遍历。
	Candidates.Sort([](const TWeakObjectPtr<AEnemyGrunt>& A,const TWeakObjectPtr<AEnemyGrunt>& B)
		{ return A->GetUniqueID()<B->GetUniqueID(); });
	TSet<TWeakObjectPtr<AEnemyGrunt>> Paired;
	for (const TWeakObjectPtr<AEnemyGrunt>& WeakSource:Candidates)
	{
		if (Paired.Contains(WeakSource)||!Ready(WeakSource)) continue;
		TWeakObjectPtr<AEnemyGrunt> WeakDestination;
		float BestSquared=FMath::Square(FMath::Max(ConductionRadius,0.f));
		for (const TWeakObjectPtr<AEnemyGrunt>& Candidate:Candidates)
		{
			if (Candidate==WeakSource||Paired.Contains(Candidate)||!Ready(Candidate)) continue;
			const float Squared=FVector::DistSquared(WeakSource->GetActorLocation(),Candidate->GetActorLocation());
			if (Squared<=BestSquared) { BestSquared=Squared; WeakDestination=Candidate; }
		}
		if (!WeakDestination.IsValid()) continue;
		Paired.Add(WeakSource); Paired.Add(WeakDestination); // 每敌每拍只允许一条链。
		TWeakObjectPtr<UHealthComponent> SourceHealth=WeakSource->GetHealthComponent();
		TWeakObjectPtr<UHealthComponent> DestinationHealth=WeakDestination->GetHealthComponent();
		const FVector SourcePoint=WeakSource->GetActorLocation()+FVector(0,0,40);
		const FVector DestinationPoint=WeakDestination->GetActorLocation()+FVector(0,0,40);
		// 先创建自有寿命的电弧，再结算伤害；任一端致死只冻结电弧，不切断本次显示。
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AShockArcActor* Arc=World->SpawnActor<AShockArcActor>(AShockArcActor::StaticClass(),SourcePoint,FRotator::ZeroRotator,Parameters))
			if (!Arc->Connect(WeakSource.Get(),WeakDestination.Get())) Arc->Destroy();
		float ActualSource=0.f,ActualDestination=0.f;
		if (Ready(WeakSource)&&SourceHealth.IsValid())
		{
			const float Before=SourceHealth->GetCurrentHealth();
			SourceHealth->ApplyDamage(RawDamage,GetPawn());
			if (SourceHealth.IsValid()) ActualSource=FMath::Max(Before-SourceHealth->GetCurrentHealth(),0.f);
		}
		// Health广播可能改变其他目标状态，第二端及后续配对逐个复验弱引用与免伤。
		if (Ready(WeakDestination)&&DestinationHealth.IsValid()&&IsValid(GetPawn()))
		{
			const float Before=DestinationHealth->GetCurrentHealth();
			DestinationHealth->ApplyDamage(RawDamage,GetPawn());
			if (DestinationHealth.IsValid()) ActualDestination=FMath::Max(Before-DestinationHealth->GetCurrentHealth(),0.f);
		}
		const FColor Color=FColor::FromHex(TEXT("9BD7FF"));
		DrawDebugString(World,SourcePoint+FVector(0,0,65),FString::Printf(TEXT("感电 -%.1f"),ActualSource),nullptr,Color,.45f,false,1.f);
		DrawDebugString(World,DestinationPoint+FVector(0,0,65),FString::Printf(TEXT("感电 -%.1f"),ActualDestination),nullptr,Color,.45f,false,1.f);
		UE_LOG(LogAICompanionDemo,Log,TEXT("[Element] 持续感电 pair=%s/%s raw=%.3f 两端actual=%.3f/%.3f"),
			*GetNameSafe(WeakSource.Get()),*GetNameSafe(WeakDestination.Get()),RawDamage,ActualSource,ActualDestination);
	}
}

