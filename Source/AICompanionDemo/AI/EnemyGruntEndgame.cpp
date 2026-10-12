/**
 * [INPUT]: 导演绝对速度倍率/狂暴阶段，宿主变体原速、移动减速与EnrageVisual显示。
 * [OUTPUT]: 幂等合成实际步速；14分钟免移动减速并抬高紫焰，保留双来源寒霜和融化资格。
 * [POS]: EnemyGrunt的终局移动接线；时间规则归导演，粒子尺寸归纯显示组件。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "AI/EnemyGrunt.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Vfx/EnemyEnrageVisual.h"

void AEnemyGrunt::ApplyEndgamePressure(float SpeedMultiplier, bool bInEnraged, bool bInUnstoppable)
{
	if (IsDead() || !FMath::IsFinite(SpeedMultiplier) || SpeedMultiplier < 1.f) return;
	BaseWalkSpeed = VariantWalkSpeed * SpeedMultiplier;
	bUnstoppable = bInEnraged && bInUnstoppable;
	if (bUnstoppable)
	{
		MovementSlowFactor = 1.f;
		GetWorldTimerManager().ClearTimer(FrostRecoverTimerHandle);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		Movement->MaxWalkSpeed = BaseWalkSpeed * MovementSlowFactor;
	SetEnraged(bInEnraged);
	if (EnrageVisual) EnrageVisual->SetHeightened(bUnstoppable);
}

FString AEnemyGrunt::DebugMovementPressureState() const
{
#if WITH_EDITOR
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	return FString::Printf(TEXT("{\"variantSpeed\":%.4f,\"baseSpeed\":%.4f,\"actualSpeed\":%.4f,\"slowFactor\":%.4f,\"unstoppable\":%s,\"enraged\":%s}"),
		VariantWalkSpeed, BaseWalkSpeed, Movement ? Movement->MaxWalkSpeed : 0.f, MovementSlowFactor,
		bUnstoppable ? TEXT("true") : TEXT("false"), IsEnraged() ? TEXT("true") : TEXT("false"));
#else
	return TEXT("{}");
#endif
}
