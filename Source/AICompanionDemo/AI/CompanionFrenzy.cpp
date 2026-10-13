/**
 * [INPUT]: Controller唯一FrenzyTimer/开火节拍、玩家Health死亡、BossRelicComponent药剂时长，以及EnemyEnrageVisual预Cook终局火焰。
 * [OUTPUT]: 药剂/遗物普通狂暴与异源重叠5秒紫炎；紫炎伤害倍率只读2，遗物持续时间只读真实Timer，死亡/失去Pawn/EndPlay即时清理。
 * [POS]: AI/队友狂暴状态唯一所有者；Relics只据实际状态冻结周期，Health只在落地时消费倍率，UI只消费值快照，复用Vfx不拥有资产副本。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "AI/CompanionAIController.h"
#include "AICompanionDemo.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Relics/BossRelicComponent.h"
#include "TimerManager.h"
#include "Vfx/EnemyEnrageVisual.h"

namespace
{
	constexpr float PurpleFrenzyDuration = 5.f;
}

void ACompanionAIController::ActivateFrenzy(float Duration, bool bFromPotion)
{
	const APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
	UHealthComponent* HeroHealth = Hero ? Hero->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!GetWorld() || !IsValid(GetPawn()) || !HeroHealth || HeroHealth->IsDead()
		|| HeroHealth->GetCurrentHealth() <= 0.f || Duration <= 0.f) return;
	if (BoundPlayerHealth.Get() != HeroHealth)
	{
		if (UHealthComponent* Previous = BoundPlayerHealth.Get())
		{
			Previous->OnDamaged.RemoveDynamic(this, &ACompanionAIController::HandlePlayerDamaged);
			Previous->OnDeath.RemoveDynamic(this, &ACompanionAIController::HandleFrenzyPlayerDeath);
		}
		BoundPlayerHealth = HeroHealth;
		HeroHealth->OnDamaged.AddUniqueDynamic(this, &ACompanionAIController::HandlePlayerDamaged);
		HeroHealth->OnDeath.AddUniqueDynamic(this, &ACompanionAIController::HandleFrenzyPlayerDeath);
		if (IsValid(PurpleFrenzyVisual)) PurpleFrenzyVisual->SetLifeSource(HeroHealth);
	}
	const float PreviousRemaining = GetFrenzyRemaining();
	const bool bOverlaps = PreviousRemaining > 0.f && (FrenzySource == ECompanionFrenzySource::Purple
		|| (bFromPotion && FrenzySource == ECompanionFrenzySource::Relic)
		|| (!bFromPotion && FrenzySource == ECompanionFrenzySource::Potion));
	if (bOverlaps)
	{
		FrenzySource = ECompanionFrenzySource::Purple;
		Duration = PurpleFrenzyDuration;
		++PurpleFrenzyActivations;
	}
	else
	{
		if (bFromPotion)
			if (const UBossRelicComponent* Relics = Hero->FindComponentByClass<UBossRelicComponent>())
				Duration *= Relics->GetPotionFrenzyDurationMultiplier();
		FrenzySource = bFromPotion ? ECompanionFrenzySource::Potion : ECompanionFrenzySource::Relic;
		Duration = FMath::Max(Duration, PreviousRemaining);
	}
	FrenzyCooldownFactor = .5f;
	// 只重挂仍在接敌的实际节拍；未接敌时由原决策入口采用新系数。
	if (GetWorldTimerManager().IsTimerActive(FireTimerHandle))
		GetWorldTimerManager().SetTimer(FireTimerHandle, this, &ACompanionAIController::FireAtTarget,
			AttackInterval * FrenzyCooldownFactor, true);
	GetWorldTimerManager().SetTimer(FrenzyTimerHandle, this, &ACompanionAIController::RestoreFireRate, Duration, false);
	RefreshFrenzyVisual();
	UE_LOG(LogAICompanionDemo, Log, TEXT("[Combat] frenzy source=%d duration=%.3f purple=%s damageMultiplier=%.1f"),
		static_cast<int32>(FrenzySource), Duration, bOverlaps ? TEXT("true") : TEXT("false"), GetFrenzyDamageMultiplier());
}

float ACompanionAIController::GetRelicFrenzyRemaining() const
{
	return FrenzySource == ECompanionFrenzySource::Relic || FrenzySource == ECompanionFrenzySource::Purple
		? GetFrenzyRemaining() : 0.f;
}

bool ACompanionAIController::IsPurpleFrenzyActive() const
{
	return FrenzySource == ECompanionFrenzySource::Purple && GetFrenzyRemaining() > 0.f;
}

void ACompanionAIController::RefreshFrenzyVisual()
{
	APawn* Buddy = GetPawn();
	if (!IsValid(Buddy)) return;
	if (USkeletalMeshComponent* Mesh = Buddy->FindComponentByClass<USkeletalMeshComponent>())
		Mesh->SetVectorParameterValueOnMaterials(TEXT("Paint Tint"),
			FrenzySource == ECompanionFrenzySource::None ? FVector::OneVector : FVector(.35f, .08f, .55f));
	if (IsPurpleFrenzyActive())
	{
		if (!IsValid(PurpleFrenzyVisual))
		{
			PurpleFrenzyVisual = NewObject<UEnemyEnrageVisual>(Buddy);
			Buddy->AddInstanceComponent(PurpleFrenzyVisual);
			PurpleFrenzyVisual->SetLifeSource(BoundPlayerHealth.Get());
			PurpleFrenzyVisual->RegisterComponent();
		}
		PurpleFrenzyVisual->SetEnraged(true);
		PurpleFrenzyVisual->SetHeightened(true);
	}
	else if (IsValid(PurpleFrenzyVisual))
	{
		PurpleFrenzyVisual->SetHeightened(false);
		PurpleFrenzyVisual->SetEnraged(false);
	}
}

void ACompanionAIController::StopFrenzy(bool bResumeFire)
{
	const bool bFireWasActive = GetWorld() && GetWorldTimerManager().IsTimerActive(FireTimerHandle);
	if (GetWorld()) GetWorldTimerManager().ClearTimer(FrenzyTimerHandle);
	FrenzySource = ECompanionFrenzySource::None;
	FrenzyCooldownFactor = 1.f;
	RefreshFrenzyVisual();
	if (bResumeFire && bFireWasActive)
		GetWorldTimerManager().SetTimer(FireTimerHandle, this, &ACompanionAIController::FireAtTarget, AttackInterval, true);
	else if (!bResumeFire && GetWorld()) GetWorldTimerManager().ClearTimer(FireTimerHandle);
}

void ACompanionAIController::RestoreFireRate()
{
	StopFrenzy(true);
	UE_LOG(LogAICompanionDemo, Log, TEXT("[Combat] 狂暴结束，开火间隔恢复 %.2f"), AttackInterval);
}

void ACompanionAIController::HandleFrenzyPlayerDeath(UHealthComponent* Health)
{
	StopFrenzy(false);
}

void ACompanionAIController::OnUnPossess()
{
	StopFrenzy(false);
	if (IsValid(PurpleFrenzyVisual)) PurpleFrenzyVisual->DestroyComponent();
	PurpleFrenzyVisual = nullptr;
	Super::OnUnPossess();
}

void ACompanionAIController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UHealthComponent* Health = BoundPlayerHealth.Get())
	{
		Health->OnDamaged.RemoveDynamic(this, &ACompanionAIController::HandlePlayerDamaged);
		Health->OnDeath.RemoveDynamic(this, &ACompanionAIController::HandleFrenzyPlayerDeath);
	}
	StopFrenzy(false);
	if (IsValid(PurpleFrenzyVisual)) PurpleFrenzyVisual->DestroyComponent();
	PurpleFrenzyVisual = nullptr;
	BoundPlayerHealth.Reset();
	Super::EndPlay(Reason);
}
