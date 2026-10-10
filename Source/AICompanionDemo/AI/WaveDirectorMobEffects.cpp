/**
 * [INPUT]: WaveDirector狂暴状态、AEnemyGrunt::SetEnraged、ICombatant实际死亡、ABossWurm清杂反馈入口。
 * [OUTPUT]: 现存/新生杂兵共享狂暴开关；普通导演杂兵和召唤杂兵都能将真实死亡传给活蠕虫。
 * [POS]: 导演局部接线，不持有蠕虫血量或狂暴特效；幂等去重分别归导演计数和蠕虫反馈。
 * [PROTOCOL]: 变更时同步模块AGENTS。
 */
#include "AI/WaveDirector.h"
#include "AI/BossWurm.h"
#include "AI/Combatant.h"
#include "AI/EnemyGrunt.h"
#include "EngineUtils.h"
#include "Health/HealthComponent.h"

void AWaveDirector::SetEnraged(bool bInEnraged)
{
	bEnraged = bInEnraged;
	for (TActorIterator<AEnemyGrunt> It(GetWorld()); It; ++It) It->SetEnraged(bEnraged && !It->IsDead());
}

void AWaveDirector::DebugSetEnraged(bool bInEnraged)
{
#if WITH_EDITOR
	SetEnraged(bInEnraged);
#endif
}

void AWaveDirector::NotifyMobDeath(AActor* Mob)
{
	if (!IsValid(Mob) || Mob->IsA<ABossBase>()) return;
	const ICombatant* Combatant = Cast<ICombatant>(Mob);
	UHealthComponent* Health = Combatant ? Combatant->GetHealthComponent() : nullptr;
	if (!Health || !Health->IsDead()) return;
	for (TActorIterator<ABossWurm> It(GetWorld()); It; ++It)
		if (!It->IsDead()) It->NotifyMobKilled(Health);
}
