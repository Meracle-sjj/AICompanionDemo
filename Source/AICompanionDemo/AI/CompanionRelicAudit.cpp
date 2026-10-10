/** [INPUT]: Real companion FireShot/NovaBurst and frenzy timer. [OUTPUT]: Actual/displayed shot damage evidence, editor-only Nova bridge and timer JSON.
 * [POS]: Companion state query and focused relic validation bridge; no independent clock. [PROTOCOL]: Keep module AGENTS synchronized. */
#include "AI/CompanionAIController.h"
#include "Engine/World.h"
#include "AICompanionDemoCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
float ACompanionAIController::GetFrenzyRemaining() const
{
	return GetWorld() && IsValid(GetPawn()) && FrenzyCooldownFactor < 1.f
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
	return FString::Printf(TEXT("{\"factor\":%.3f,\"remaining\":%.3f,\"interval\":%.3f}"),
		FrenzyCooldownFactor, GetFrenzyRemaining(), AttackInterval);
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
	return FString::Printf(TEXT("{\"victim\":\"%s\",\"raw\":%.4f,\"actual\":%.4f,\"displayed\":%.4f,\"mode_multiplier\":%.4f}"),
		*GetNameSafe(LastShotVictim.Get()), LastShotRawDamage, LastShotActualDamage, LastShotDisplayedDamage, LastShotModeMultiplier);
#else
	return TEXT("{}");
#endif
}
