/**
 * [INPUT]: BossOrb visual, live Combatant/WorldStatic/Health, player's BossRelicComponent transient VFX ledger.
 * [OUTPUT]: Friendly movement/swept hit/AoE unchanged; impact Fire actor survives orb destruction under the relic cleanup ledger.
 * [POS]: AI/BossOrb friendly implementation; source player's relic death/EndPlay or absent source/ledger cancels impact VFX, orb EndPlay does not.
 * [PROTOCOL]: Update module AGENTS when this contract changes.
 */
#include "AI/BossOrb.h"
#include "AI/Combatant.h"
#include "Audio/SFXLibrary.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Health/HealthComponent.h"
#include "Relics/BossRelicComponent.h"
#include "Vfx/VfxLibrary.h"

void ABossOrb::LaunchFriendly(const FVector& Direction, float InDamage, float ExplosionRadius, AActor* DamageInstigator)
{
	bFriendly = true;
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
				{ Health->ApplyDamage(Damage, Source); }
			}
		}
		AActor* ImpactVfx = UVfxLibrary::SpawnVfx(GetWorld(), EVfx::Fire, End, 0.5f, 1.f);
		if (IsValid(Source))
		{
			if (UBossRelicComponent* Relics = Source->FindComponentByClass<UBossRelicComponent>())
			{
				Relics->TrackTransientVfx(ImpactVfx);
			}
			else if (IsValid(ImpactVfx)) { ImpactVfx->Destroy(); }
		}
		else if (IsValid(ImpactVfx)) { ImpactVfx->Destroy(); }
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Explosion);
		Destroy(); return;
	}
	LifeSeconds -= DeltaSeconds;
	if (LifeSeconds <= 0.f) { Destroy(); }
}
