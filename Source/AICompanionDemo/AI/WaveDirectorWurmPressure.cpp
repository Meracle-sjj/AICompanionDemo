/**
 * [INPUT]: 导演生产环点/活敌账本，ICombatant存活契约，BossWurm真实生命与EndPlay，Character镜头接口。
 * [OUTPUT]: 活蠕虫期间总杂兵≤10、Warlock≤3；导演/召唤师共享准入，入场与外部新生超员按远近静默退场。
 * [POS]: 导演蠕虫战压力局部实现；不施伤、不发死亡/掉落/经验/35伤虫，OnDestroyed只校正导演自有登记。
 * [PROTOCOL]: 变更时更新此头部，然后检查 ../AGENTS.md
 */
#include "AI/WaveDirector.h"
#include "AI/BossWurm.h"
#include "AI/Combatant.h"
#include "AICompanionDemoCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	int32 CountPressureWurms(UWorld* World, const ABossWurm* Leaving = nullptr)
	{
		int32 Count = 0;
		if (World) for (TActorIterator<ABossWurm> It(World); It; ++It)
			if (*It != Leaving && !It->IsActorBeingDestroyed() && !It->IsDead()) ++Count;
		return Count;
	}

	void CollectPressureMobs(UWorld* World, TArray<AActor*>& Mobs)
	{
		if (!World) return;
		TArray<AActor*> Combatants;
		CollectAliveCombatants(World, Combatants);
		for (AActor* Actor : Combatants)
			if (!Actor->IsA<ABossBase>() && !Actor->IsActorBeingDestroyed()) Mobs.Add(Actor);
	}

	bool IsPressureWarlock(const AActor* Mob)
	{
		const AEnemyGrunt* Grunt = Cast<AEnemyGrunt>(Mob);
		return Grunt && Grunt->GruntVariant == EGruntVariant::Warlock;
	}
}

void AWaveDirector::RefreshWurmEncounter(const ABossWurm* Leaving)
{
	bWurmPressureActive = !IsRunTerminated() && CountPressureWurms(GetWorld(), Leaving) > 0;
	if (AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
		Hero->SetWurmEncounterActive(bWurmPressureActive);
	if (!bWurmPressureActive) return;

	TArray<AActor*> Mobs;
	CollectPressureMobs(GetWorld(), Mobs);
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector Center = Player ? Player->GetActorLocation() : GetActorLocation();
	// 保留近处战斗连续性，远处超员退出；妖术师先独立压到3，再压总数到10。
	Mobs.Sort([Center](const AActor& A, const AActor& B)
	{
		const double AD = FVector::DistSquared2D(A.GetActorLocation(), Center);
		const double BD = FVector::DistSquared2D(B.GetActorLocation(), Center);
		return AD == BD ? A.GetUniqueID() < B.GetUniqueID() : AD < BD;
	});
	int32 Warlocks = 0;
	for (const AActor* Mob : Mobs) if (IsPressureWarlock(Mob)) ++Warlocks;
	const int32 WarlockLimit = FMath::Clamp(WurmMaxAliveWarlocks, 0, 3);
	for (int32 Index = Mobs.Num() - 1; Index >= 0 && Warlocks > WarlockLimit; --Index)
	{
		if (!IsPressureWarlock(Mobs[Index])) continue;
		// AActor::Destroyed只路由EndPlay/OnDestroyed；不调用Health死亡委托。
		if (Mobs[Index]->Destroy(false, false))
		{
			Mobs.RemoveAt(Index);
			--Warlocks;
			++WurmTrimmedMobs;
		}
	}
	const int32 MobLimit = FMath::Clamp(WurmMaxAliveMobs, 1, 10);
	for (int32 Index = Mobs.Num() - 1; Index >= MobLimit; --Index)
		if (Mobs[Index]->Destroy(false, false)) ++WurmTrimmedMobs;
}

bool AWaveDirector::CanSpawnMob(EGruntVariant Variant)
{
	if (IsRunTerminated()) return false;
	RefreshWurmEncounter();
	if (!bWurmPressureActive) return true;
	TArray<AActor*> Mobs;
	CollectPressureMobs(GetWorld(), Mobs);
	int32 Warlocks = 0;
	for (const AActor* Mob : Mobs) if (IsPressureWarlock(Mob)) ++Warlocks;
	if (Mobs.Num() >= FMath::Clamp(WurmMaxAliveMobs, 1, 10)
		|| (Variant == EGruntVariant::Warlock && Warlocks >= FMath::Clamp(WurmMaxAliveWarlocks, 0, 3)))
	{
		++WurmRejectedSpawns;
		return false;
	}
	return true;
}

void AWaveDirector::TickWurmPressure(float DeltaSeconds)
{
	if (!bWurmPressureActive || IsRunTerminated() || !GetWorld() || GetWorld()->IsPaused()
		|| UGameplayStatics::GetGlobalTimeDilation(this) < .01f) return;
	WurmPressureScanCountdown -= DeltaSeconds;
	if (WurmPressureScanCountdown > 0.f) return;
	WurmPressureScanCountdown = .25f;
	// 两个正式入口已即时准入；此节拍仅治理外部调试生成/运行时变体改写与失效宿主。
	RefreshWurmEncounter();
}

void AWaveDirector::HandleEnemyRemoved(AActor* Enemy)
{
	if (RegisteredAliveEnemies.Remove(Enemy) > 0) AliveCount = FMath::Max(AliveCount - 1, 0);
}

AActor* AWaveDirector::DebugSpawnMobVariant(EGruntVariant Variant)
{
#if WITH_EDITOR
	AActor* Mob = SpawnEnemyAtRing(Variant);
	if (Mob) ++AliveCount;
	return Mob;
#else
	return nullptr;
#endif
}

FString AWaveDirector::DebugWurmPressureState() const
{
#if WITH_EDITOR
	TArray<AActor*> Mobs;
	CollectPressureMobs(GetWorld(), Mobs);
	int32 Warlocks = 0;
	for (const AActor* Mob : Mobs) if (IsPressureWarlock(Mob)) ++Warlocks;
	return FString::Printf(TEXT("{\"active\":%s,\"liveWurms\":%d,\"liveMobs\":%d,\"liveWarlocks\":%d,\"mobLimit\":%d,\"warlockLimit\":%d,\"directorAlive\":%d,\"registeredAlive\":%d,\"kills\":%d,\"xp\":%d,\"level\":%d,\"trimmed\":%d,\"rejected\":%d}"),
		bWurmPressureActive ? TEXT("true") : TEXT("false"), CountPressureWurms(GetWorld()), Mobs.Num(), Warlocks,
		FMath::Clamp(WurmMaxAliveMobs, 1, 10), FMath::Clamp(WurmMaxAliveWarlocks, 0, 3), AliveCount,
		RegisteredAliveEnemies.Num(), KillCount, CurrentXp, Level, WurmTrimmedMobs, WurmRejectedSpawns);
#else
	return TEXT("{}");
#endif
}
