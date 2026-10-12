/**
 * [INPUT]: 导演游戏时钟/速度曲线与狂暴状态、EnemyGrunt绝对倍率合成、ICombatant实际死亡、ABossWurm清杂反馈入口。
 * [OUTPUT]: 现存/新生/召唤杂兵共享12分钟加速与14分钟高紫焰/免减速；真实死亡传给活蠕虫。
 * [POS]: 导演局部接线，不持有蠕虫血量或狂暴特效；幂等去重分别归导演计数和蠕虫反馈。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "AI/WaveDirector.h"
#include "AI/BossWurm.h"
#include "AI/Combatant.h"
#include "AI/EnemyGrunt.h"
#include "EngineUtils.h"
#include "Health/HealthComponent.h"
#include "WaveDirectorPrivate.h"

using namespace WaveDirectorPrivate;

bool AWaveDirector::IsMobUnstoppable() const
{
	return bEnraged && ElapsedSeconds >= kUnstoppableStartSeconds;
}

void AWaveDirector::SetEnraged(bool bInEnraged)
{
	bEnraged = bInEnraged;
	UpdateMobPressure();
}

float AWaveDirector::GetMobSpeedMultiplier() const
{
	if (!bEndlessMode) return 1.f;
	const float Minutes = GetElapsedMinutes();
	float Scale = FMath::Min(FMath::Pow(SpeedGrowthPerMinute, Minutes), SpeedGrowthCap);
	if (bEnraged) Scale *= FMath::Pow(kEnrageSpeedPerMinute, FMath::Max(0.f, Minutes - kEnrageStartSeconds / 60.f));
	return FMath::Min(Scale, SpeedGrowthCap * 2.5f);
}

void AWaveDirector::UpdateMobPressure()
{
	for (TActorIterator<AEnemyGrunt> It(GetWorld()); It; ++It)
		if (!It->IsDead()) It->ApplyEndgamePressure(GetMobSpeedMultiplier(), bEnraged, IsMobUnstoppable());
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
