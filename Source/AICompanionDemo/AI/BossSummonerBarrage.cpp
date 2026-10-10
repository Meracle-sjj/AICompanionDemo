/**
 * [INPUT]: 召唤师真实护盾/眩晕状态，BossOrb可调速度发射，Health存活与世界暂停，召唤妖术师由EnemyGrunt定型。
 * [OUTPUT]: 常态0.6s/1600cm/s单发，眩晕1.2s/800cm/s仍发；从实际枪口三维瞄准固定直线，24弹上限/死亡/EndPlay清理/只读快照保持。
 * [POS]: BossSummoner弹幕与所有权局部实现，三波推进/破盾继续由BossSummoner.cpp负责，不另造战斗时钟。
 * [PROTOCOL]: 变更时更新此头部，然后检查 ../AGENTS.md
 */
#include "AI/BossSummoner.h"
#include "AI/BossOrb.h"
#include "AI/EnemyGrunt.h"
#include "AICompanionDemo.h"
#include "Engine/World.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

void ABossSummoner::TickOrbBarrage(float DeltaSeconds, const APawn* PlayerPawn)
{
	if (!GetWorld() || GetWorld()->IsPaused() || UGameplayStatics::GetGlobalTimeDilation(this) < .01f
		|| !HealthComponent || HealthComponent->IsDead()) return;
	const UHealthComponent* PlayerHealth = PlayerPawn ? PlayerPawn->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!PlayerHealth || PlayerHealth->IsDead()) return;
	ActiveOrbs.RemoveAll([](const TWeakObjectPtr<ABossOrb>& Orb) { return !Orb.IsValid() || Orb->IsActorBeingDestroyed(); });
	OrbCooldown = FMath::Max(0.f, OrbCooldown - DeltaSeconds);
	if (OrbCooldown > 0.f) return;
	const float StateScale = bShieldUp ? 1.f : FMath::Clamp(StunnedFireRateScale, .1f, 1.f);
	OrbCooldown = FMath::Max(.15f, OrbInterval) / StateScale;
	const FVector Aim = (PlayerPawn->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	if (Aim.IsNearlyZero()) return;
	const int32 Count = 1;
	const int32 Limit = FMath::Clamp(MaxActiveOrbs, 3, 48);
	++OrbVolleysFired;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// 淘汰最旧飞行体，不能只从弱表Remove留下无归属火球。
		while (ActiveOrbs.Num() >= Limit)
		{
			if (ActiveOrbs[0].IsValid()) ActiveOrbs[0]->Destroy();
			ActiveOrbs.RemoveAt(0);
		}
		const FVector Muzzle = GetActorLocation() + FVector(0.f, 0.f, 150.f) + Aim * 120.f;
		const FVector Direction = (PlayerPawn->GetActorLocation() - Muzzle).GetSafeNormal();
		FActorSpawnParameters Spawn;
		Spawn.Owner = this;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ABossOrb* Orb = GetWorld()->SpawnActor<ABossOrb>(
			Muzzle, FRotator::ZeroRotator, Spawn))
		{
			Orb->Launch(Direction, 12.f, FMath::Max(100.f, OrbSpeed) * StateScale);
			ActiveOrbs.Add(Orb);
			++OrbsSpawned;
		}
	}
}

void ABossSummoner::ClearSummonerCombat()
{
	GetWorldTimerManager().ClearTimer(TransientAnimTimer);
	for (const TWeakObjectPtr<ABossOrb>& Orb : ActiveOrbs) if (Orb.IsValid()) Orb->Destroy();
	ActiveOrbs.Reset();
	// 孤儿召唤单位不留在下一场；静默退场不发掉落/XP/伤虫死亡奖励。
	for (const TWeakObjectPtr<AEnemyGrunt>& Minion : Minions)
		if (Minion.IsValid() && !Minion->IsDead()) Minion->Destroy();
	Minions.Reset();
}

void ABossSummoner::HandleDeath(UHealthComponent* HealthComp)
{
	ClearSummonerCombat();
	Super::HandleDeath(HealthComp);
}

void ABossSummoner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearSummonerCombat();
	Super::EndPlay(EndPlayReason);
}

FString ABossSummoner::DebugSummonerCombatState() const
{
#if WITH_EDITOR
	int32 AliveMinions = 0, Warlocks = 0, AliveOrbs = 0;
	FString OrbStates;
	for (const TWeakObjectPtr<AEnemyGrunt>& Minion : Minions)
		if (Minion.IsValid() && !Minion->IsDead())
		{
			++AliveMinions;
			if (Minion->GruntVariant == EGruntVariant::Warlock) ++Warlocks;
		}
	for (const TWeakObjectPtr<ABossOrb>& Orb : ActiveOrbs)
		if (Orb.IsValid() && !Orb->IsActorBeingDestroyed())
		{
			if (AliveOrbs++) OrbStates += TEXT(",");
			OrbStates += Orb->DebugOrbCombatState();
		}
	const float StateScale = bShieldUp ? 1.f : FMath::Clamp(StunnedFireRateScale, .1f, 1.f);
	return FString::Printf(TEXT("{\"wave\":%d,\"shield\":%s,\"dead\":%s,\"minions\":%d,\"warlocks\":%d,\"activeOrbs\":%d,\"volleyCount\":%d,\"volleyInterval\":%.4f,\"orbSpeed\":%.3f,\"cooldown\":%.4f,\"volleyTotal\":%d,\"spawnedTotal\":%d,\"maxOrbs\":%d,\"animTimerActive\":%s,\"orbs\":[%s]}"),
		CycleIndex + 1, bShieldUp ? TEXT("true") : TEXT("false"), IsDead() ? TEXT("true") : TEXT("false"),
		AliveMinions, Warlocks, AliveOrbs, 1, FMath::Max(.15f, OrbInterval) / StateScale,
		FMath::Max(100.f, OrbSpeed) * StateScale, OrbCooldown, OrbVolleysFired, OrbsSpawned,
		FMath::Clamp(MaxActiveOrbs, 3, 48), GetWorldTimerManager().IsTimerActive(TransientAnimTimer) ? TEXT("true") : TEXT("false"), *OrbStates);
#else
	return TEXT("{}");
#endif
}
