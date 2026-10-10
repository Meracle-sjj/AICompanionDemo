/**
 * [INPUT]: EnemyGrunt普通Warlock参数、既有变体定型和Mesh Paint Tint。
 * [OUTPUT]: 紫色召唤妖术师幂等定型：蓄力/冷却/近战间隔减半，不掉常规战利品；编辑器真实只读快照。
 * [POS]: 召唤师强化波次的实体差异，攻击仍走EnemyGrunt原状态机，不另造计时器。
 * [PROTOCOL]: 变更时更新此头部，然后检查AGENTS.md。
 */
#include "AI/EnemyGrunt.h"
#include "Components/SkeletalMeshComponent.h"

void AEnemyGrunt::ApplySummonedWarlock()
{
	if (bSummonedWarlock || IsDead()) return;
	ApplyVariant(EGruntVariant::Warlock);
	bSummonedWarlock = true;
	BeamChargeSeconds *= 0.5f;
	BeamCooldownSeconds *= 0.5f;
	BeamCooldown *= 0.5f;
	PlayerAttackInterval *= 0.5f;
	BaseTint = FVector(0.62f, 0.12f, 1.f);
	GetMesh()->SetVectorParameterValueOnMaterials(TEXT("Paint Tint"), BaseTint);
	XpValue = 0;
	XpPickupClass = nullptr;
	HealDropChance = GoldDropChance = RushDropChance = 0.f;
	GoldBurst = 0;
}

#if WITH_EDITOR
FString AEnemyGrunt::DebugSummonedWarlockState() const
{
	return FString::Printf(TEXT("{\"special\":%s,\"warlock\":%s,\"charge\":%.4f,\"interval\":%.4f,\"remaining\":%.4f,\"charging\":%.4f,\"melee\":%.4f,\"xp\":%d,\"heal_drop\":%.4f,\"gold_drop\":%.4f,\"rush_drop\":%.4f,\"tint\":[%.4f,%.4f,%.4f]}"),
		bSummonedWarlock ? TEXT("true") : TEXT("false"), GruntVariant == EGruntVariant::Warlock ? TEXT("true") : TEXT("false"),
		BeamChargeSeconds, BeamCooldownSeconds, BeamCooldown, BeamChargeCountdown, PlayerAttackInterval, XpValue,
		HealDropChance, GoldDropChance, RushDropChance, BaseTint.X, BaseTint.Y, BaseTint.Z);
}
#endif
