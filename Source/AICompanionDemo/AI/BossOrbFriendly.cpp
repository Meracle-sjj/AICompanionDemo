/**
 * [INPUT]: BossOrb外观、活ICombatant/Health、玩家遗物清理账本；步行旧路径仍读WorldStatic。
 * [OUTPUT]: 步行直线火球保持每敌30范围结算；闪烁三球直击30/邻敌20互斥；两者共用蠕虫入地橙黄球与Explosion爆闪，墙面也触发步行爆炸。
 * [POS]: AI/友方弹体；火球/爆炸类别与命中前落点交Health统一反馈，玩家死亡/EndPlay回收，命中爆闪交遗物登记硬寿命，不随球Destroy截断。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "AI/BossOrb.h"
#include "AICompanionDemo.h"
#include "AI/Combatant.h"
#include "Audio/SFXLibrary.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Health/HealthComponent.h"
#include "NiagaraComponent.h"
#include "Relics/BossRelicComponent.h"
#include "Vfx/VfxLibrary.h"

void ABossOrb::LaunchFriendly(const FVector& Direction, float InDamage, float ExplosionRadius, AActor* DamageInstigator)
{
	bFriendly = true;
	bFriendlyHoming = false;
	FriendlyTarget.Reset();
	bHasFriendlyTargetLocation = false;
	FriendlyHomingSpeed = 1200.f;
	Velocity = Direction.GetSafeNormal() * 1200.f;
	Damage = FMath::Max(InDamage, 0.f);
	FriendlyExplosionRadius = FMath::Max(ExplosionRadius, 1.f);
	FriendlyInstigator = DamageInstigator;
}

void ABossOrb::TickFriendly(float DeltaSeconds)
{
	AActor* Source = FriendlyInstigator.Get();
	UHealthComponent* SourceHealth = Source ? Source->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!Source || !SourceHealth || SourceHealth->IsDead() || SourceHealth->GetCurrentHealth() <= 0.f) { Destroy(); return; }
	if (DeltaSeconds <= 0.f || GetWorld()->IsPaused()
		|| GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() < .01f) return;
	if (bFriendlyHoming) { TickFriendlyHoming(DeltaSeconds, Source); return; }
	const FVector Start = GetActorLocation();
	FVector End = Start + Velocity * DeltaSeconds;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RelicFireball), false, this);
	Params.AddIgnoredActor(Source);
	FHitResult Wall;
	const bool bWall = GetWorld()->LineTraceSingleByObjectType(Wall, Start, End,
		FCollisionObjectQueryParams(ECC_WorldStatic), Params);
	if (bWall) { End = Wall.ImpactPoint; }
	TArray<AActor*> Targets;
	CollectAliveCombatants(GetWorld(), Targets);
	AActor* Victim = nullptr;
	float BestT = 2.f;
	const FVector Segment = End - Start;
	for (AActor* Target : Targets)
	{
		const FVector Center = Target->GetActorLocation();
		const float T = Segment.IsNearlyZero() ? 0.f : FVector::DotProduct(Center-Start, Segment) / Segment.SizeSquared();
		if (T >= 0.f && T <= 1.f && T < BestT && FMath::PointDistToSegment(Center, Start, End) <= 120.f)
		{ BestT = T; Victim = Target; }
	}
	if (Victim) { End = Start + Segment * BestT; }
	SetActorLocation(End);
	if (Victim || bWall)
	{
		for (AActor* Target : Targets)
		{
			if (FVector::DistSquared(Target->GetActorLocation(), End) <= FMath::Square(FriendlyExplosionRadius))
			{
				if (UHealthComponent* Health = Target->FindComponentByClass<UHealthComponent>())
				{ Health->ApplyCombatDamage(Damage, Source, {ECombatDamageKind::Fireball, Target->GetActorLocation() + FVector(0, 0, 90)}); }
			}
		}
		SpawnFriendlyExplosionFx(Source);
		Destroy(); return;
	}
	LifeSeconds -= DeltaSeconds;
	if (LifeSeconds <= 0.f) { Destroy(); }
}

void ABossOrb::LaunchFriendlyHoming(AActor* Target, float DirectDamage, float SplashDamage, float ExplosionRadius, AActor* DamageInstigator)
{
	LaunchFriendly(GetActorForwardVector(), DirectDamage, ExplosionRadius, DamageInstigator);
	bFriendlyHoming = true;
	FriendlySplashDamage = FMath::Max(SplashDamage, 0.f);
	FriendlyTarget = Target;
	bHasFriendlyTargetLocation = IsValid(Target);
	if (bHasFriendlyTargetLocation) FriendlyTargetLastLocation = Target->GetActorLocation();
	LifeSeconds = 6.f; // 只有没有存活目标的候场阶段才推进；锁定有效目标后不会半途超时。
}

void ABossOrb::TickFriendlyHoming(float DeltaSeconds, AActor* Source)
{
	AActor* Target = FriendlyTarget.Get();
	const ICombatant* Enemy = IsValid(Target) && !Target->IsActorBeingDestroyed() ? Cast<ICombatant>(Target) : nullptr;
	if (!Enemy || Enemy->IsDead() || !Enemy->GetHealthComponent())
	{
		FriendlyTarget.Reset();
		bHasFriendlyTargetLocation = false;
		FriendlyHomingSpeed = 1200.f;
		Target = nullptr;
		TArray<AActor*> Targets;
		CollectAliveCombatants(GetWorld(), Targets);
		double BestDistance = TNumericLimits<double>::Max();
		for (AActor* Candidate : Targets)
		{
			const ICombatant* Combatant = Cast<ICombatant>(Candidate);
			if (!IsValid(Candidate) || Candidate->IsActorBeingDestroyed() || !Combatant || !Combatant->GetHealthComponent()) continue;
			const double Distance = FVector::DistSquared(GetActorLocation(), Candidate->GetActorLocation());
			if (Distance < BestDistance) { BestDistance = Distance; Target = Candidate; }
		}
		FriendlyTarget = Target;
		Enemy = Target ? Cast<ICombatant>(Target) : nullptr;
	}
	if (!Target || !Enemy)
	{
		LifeSeconds -= DeltaSeconds;
		if (LifeSeconds <= 0.f) Destroy();
		return;
	}
	LifeSeconds = 6.f;
	const FVector Destination = Target->GetActorLocation();
	const FVector ToTarget = Destination - GetActorLocation();
	// 虫头自绘SetActorLocation，不能用CMC/GetVelocity估速；逐帧位置真源确保追得上1300–2600cm/s的虫。
	const float TargetTravel = bHasFriendlyTargetLocation ? FVector::Dist(Destination, FriendlyTargetLastLocation) : 0.f;
	const bool bDiscontinuousMove = TargetTravel > FMath::Max(500.f, 4000.f * DeltaSeconds);
	const float TargetSpeed = bHasFriendlyTargetLocation && !bDiscontinuousMove ? TargetTravel / FMath::Max(DeltaSeconds, .0001f) : 0.f;
	FriendlyTargetLastLocation = Destination;
	bHasFriendlyTargetLocation = true;
	if (bDiscontinuousMove) FriendlyHomingSpeed = 1200.f; // 瞬移只重置采样，不制造单帧超高速。
	FriendlyHomingSpeed = FMath::FInterpTo(FriendlyHomingSpeed, FMath::Clamp(TargetSpeed + 400.f, 1200.f, 4000.f), DeltaSeconds, 12.f);
	const float HomingSpeed = FriendlyHomingSpeed;
	Velocity = ToTarget.GetSafeNormal() * HomingSpeed;
	// 必中球是独立追踪通道，不复用步行球碰墙或其他目标截断；低帧率也钳制到当前目标。
	if (ToTarget.SizeSquared() <= FMath::Square(120.f + HomingSpeed * DeltaSeconds))
	{
		if (Enemy->GetHealthComponent()->IsInvulnerable())
		{
			// 护盾/潜地期间跟随候场，窗口开放后再命中；不空爆浪费必中球。
			SetActorLocation(Destination + FVector(0, 0, 130.f));
			return;
		}
		SetActorLocation(Destination);
		ExplodeFriendlyHoming(Target, Source);
		return;
	}
	SetActorLocation(GetActorLocation() + ToTarget.GetClampedToMaxSize(HomingSpeed * DeltaSeconds));
}

void ABossOrb::ExplodeFriendlyHoming(AActor* Victim, AActor* Source)
{
	const FVector Center = GetActorLocation();
	TArray<AActor*> Targets;
	CollectAliveCombatants(GetWorld(), Targets);
	// 每颗球每个目标只有一个分支：直接命中者只吃直击，其余目标才吃范围伤害。
	for (AActor* Target : Targets)
	{
		const ICombatant* Enemy = IsValid(Target) ? Cast<ICombatant>(Target) : nullptr;
		UHealthComponent* SourceHealth = IsValid(Source) ? Source->FindComponentByClass<UHealthComponent>() : nullptr;
		if (!SourceHealth || SourceHealth->IsDead() || IsActorBeingDestroyed()) return;
		if (!Enemy || Enemy->IsDead() || Target->IsActorBeingDestroyed()) continue;
		const bool bDirect = Target == Victim;
		if (!bDirect && FVector::DistSquared(Target->GetActorLocation(), Center) > FMath::Square(FriendlyExplosionRadius)) continue;
		if (UHealthComponent* Health = Enemy->GetHealthComponent())
		{
			const float Before = Health->GetCurrentHealth();
			Health->ApplyCombatDamage(bDirect ? Damage : FriendlySplashDamage, Source,
				{bDirect ? ECombatDamageKind::Fireball : ECombatDamageKind::Explosion, Target->GetActorLocation() + FVector(0, 0, 90)});
			const float Actual = IsValid(Health) ? FMath::Max(Before - Health->GetCurrentHealth(), 0.f) : 0.f;
			UE_LOG(LogAICompanionDemo, Log, TEXT("[BossRelic] BlinkFireball target=%s branch=%s damage=%.3f"),
				*GetPathNameSafe(Target), bDirect ? TEXT("direct") : TEXT("splash"), Actual);
		}
	}
	if (IsActorBeingDestroyed()) return;
	SpawnFriendlyExplosionFx(Source);
	Destroy();
}

// ---------- 两条友方弹路共用爆闪；伤害仍归各自结算 ----------
void ABossOrb::SpawnFriendlyExplosionFx(AActor* Source)
{
	if (!GetWorld() || IsActorBeingDestroyed()) return;
	const FVector Center = GetActorLocation();
	// 同蠕虫DiveImpactBurst：橙黄双层球形爆炸空间，叠统一Explosion爆闪。
	DrawDebugSphere(GetWorld(), Center, FriendlyExplosionRadius, 12, FColor::Orange, false, .4f, 0, 3.f);
	DrawDebugSphere(GetWorld(), Center, FriendlyExplosionRadius * .55f, 12, FColor::Yellow, false, .3f, 0, 2.f);
	UNiagaraComponent* Burst = nullptr;
	UVfxLibrary::SpawnVfx(GetWorld(), EVfx::Explosion, Center, .7f, 2.f, &Burst);
	UBossRelicComponent* Relics = IsValid(Source) ? Source->FindComponentByClass<UBossRelicComponent>() : nullptr;
	if (Relics) Relics->TrackTransientNiagara(Burst, .7f);
	else if (IsValid(Burst)) Burst->DestroyComponent();
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Explosion);
}

FString ABossOrb::DebugFriendlyHomingState() const
{
#if WITH_EDITOR
	const AActor* Target = FriendlyTarget.Get();
	const ICombatant* Enemy = IsValid(Target) ? Cast<ICombatant>(Target) : nullptr;
	const UHealthComponent* Health = Enemy ? Enemy->GetHealthComponent() : nullptr;
	return FString::Printf(TEXT("{\"homing\":%s,\"target\":\"%s\",\"targetAlive\":%s,\"targetInvulnerable\":%s,\"directDamage\":%.3f,\"splashDamage\":%.3f,\"radius\":%.3f,\"idleLife\":%.3f}"),
		bFriendlyHoming ? TEXT("true") : TEXT("false"), *GetPathNameSafe(Target), Health && !Health->IsDead() ? TEXT("true") : TEXT("false"),
		Health && Health->IsInvulnerable() ? TEXT("true") : TEXT("false"), Damage, FriendlySplashDamage, FriendlyExplosionRadius, LifeSeconds);
#else
	return TEXT("{}");
#endif
}
