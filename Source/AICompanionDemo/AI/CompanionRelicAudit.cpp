/**
 * [INPUT]: CompanionFireShot/Nova实际账本、来源狂暴唯一Timer与共享紫炎显示组件。
 * [OUTPUT]: 真实狂暴/遗物剩余、紫炎状态/倍率/火焰实例JSON与编辑器射击/Nova桥；伤害快照区分raw、实际扣血和完整显示值。
 * [POS]: AI/只读验收与正式剩余出口，不复制持续时间或写入伤害倍率；状态所有者仍是CompanionFrenzy。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "AI/CompanionAIController.h"
#include "Engine/World.h"
#include "AICompanionDemoCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Vfx/EnemyEnrageVisual.h"
float ACompanionAIController::GetFrenzyRemaining() const
{
	return GetWorld() && IsValid(GetPawn()) && FrenzySource != ECompanionFrenzySource::None && FrenzyCooldownFactor < 1.f
		? FMath::Max(GetWorldTimerManager().GetTimerRemaining(FrenzyTimerHandle), 0.f) : 0.f;
}
void ACompanionAIController::DebugFireAt(AActor* Target)
{
#if WITH_EDITOR
	APawn* Buddy = GetPawn();
	if (!Buddy || !IsValid(Target)) { return; }
	const FVector Origin = Buddy->GetActorLocation()+FVector(0.f,0.f,40.f);
	FireShot(Buddy, Origin, (Target->GetActorLocation()+FVector(0.f,0.f,40.f)-Origin).GetSafeNormal(), 1.f);
#endif
}
FString ACompanionAIController::DebugFrenzyState() const
{
#if WITH_EDITOR
	const TCHAR* Source = FrenzySource == ECompanionFrenzySource::Potion ? TEXT("potion")
		: FrenzySource == ECompanionFrenzySource::Relic ? TEXT("relic")
		: FrenzySource == ECompanionFrenzySource::Purple ? TEXT("purple") : TEXT("none");
	return FString::Printf(TEXT("{\"factor\":%.3f,\"remaining\":%.3f,\"interval\":%.3f,\"source\":\"%s\",\"purple\":%s,\"relicRemaining\":%.3f,\"damageMultiplier\":%.3f,\"purpleActivations\":%d,\"visual\":%s}"),
		FrenzyCooldownFactor, GetFrenzyRemaining(), AttackInterval, Source, IsPurpleFrenzyActive() ? TEXT("true") : TEXT("false"),
		GetRelicFrenzyRemaining(), GetFrenzyDamageMultiplier(), PurpleFrenzyActivations,
		IsValid(PurpleFrenzyVisual) ? *PurpleFrenzyVisual->DebugVisualState() : TEXT("null"));
#else
	return TEXT("{}");
#endif
}

void ACompanionAIController::DebugNovaBurst(const FVector& Center)
{
#if WITH_EDITOR
	AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (IsValid(Hero)) NovaBurst(Hero, Center);
#endif
}

FString ACompanionAIController::DebugLastShotState() const
{
#if WITH_EDITOR
	return FString::Printf(TEXT("{\"victim\":\"%s\",\"raw\":%.4f,\"actual\":%.4f,\"displayed\":%.4f,\"mode_multiplier\":%.4f,\"frenzy_multiplier\":%.4f}"),
		*GetNameSafe(LastShotVictim.Get()), LastShotRawDamage, LastShotActualDamage, LastShotDisplayedDamage, LastShotModeMultiplier, LastShotFrenzyMultiplier);
#else
	return TEXT("{}");
#endif
}
