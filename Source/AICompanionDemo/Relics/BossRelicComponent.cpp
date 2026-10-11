/**
 * [INPUT]: 本组件数据契约、ICombatant/Health、EnemyGrunt 普通减速与冰火残留、CompanionAIController 狂怒、
 *  BossOrb::LaunchFriendly、Vfx/SFX及Niagara组件回传、Engine Cone/BasicShapeMaterial、Character::LaunchCharacter。
 * [OUTPUT]: 九遗物结算；行走火球30范围伤与闪烁三球30/20互斥共用独立爆闪；地刺真实概率触发时仅播一次铡刀短砍音，50与5s/0.5s每跳10流血保持。
 * [POS]: Relics/运行时实现；仅敌人Health次生伤害，统一重入闸；死亡/EndPlay清理弱登记Actor/Niagara，友方球命中火浪归本组件而非球的EndPlay。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "BossRelicComponent.h"
#include "AICompanionDemo.h"
#include "RelicBleedComponent.h"

#include "AI/BossOrb.h"
#include "AI/Combatant.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionCharacter.h"
#include "AI/EnemyGrunt.h"
#include "Audio/SFXLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Health/HealthComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "Templates/UnrealTemplate.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Vfx/VfxLibrary.h"

UBossRelicComponent::UBossRelicComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (Cone.Succeeded()) SpikeMesh = Cone.Object;
}

void UBossRelicComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerHealth = GetOwner()->FindComponentByClass<UHealthComponent>();
	if (OwnerHealth) OwnerHealth->OnDeath.AddDynamic(this, &UBossRelicComponent::HandleOwnerDeath);
	PreviousLocation = GetOwner()->GetActorLocation();
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		SpikeMaterial = UMaterialInstanceDynamic::Create(Base, this);
		if (SpikeMaterial) SpikeMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(.38f, .27f, .22f));
	}
}

bool UBossRelicComponent::OwnerAlive() const
{
	return !bStopped && IsValid(GetOwner()) && OwnerHealth && OwnerHealth->GetCurrentHealth() > 0.f && !OwnerHealth->IsDead();
}

bool UBossRelicComponent::CanTrigger() const
{
	return OwnerAlive() && GetWorld() && !GetWorld()->IsPaused()
		&& GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() >= .01f;
}

bool UBossRelicComponent::HasRelic(EBossRelicId Id) const
{
	return IsValidRelicId(Id) && (OwnedMask & (1 << static_cast<uint8>(Id))) != 0;
}

bool UBossRelicComponent::AcquireRelic(EBossRelicId Id)
{
	// 奖励选择处于暂停也允许入库；任何效果只在恢复游戏时推进。
	if (!IsValidRelicId(Id) || HasRelic(Id) || !OwnerAlive()) return false;
	OwnedMask |= 1 << static_cast<uint8>(Id);
	if (Id == EBossRelicId::SpiritFrenzy) NextFrenzyAt = Clock + FMath::Max(FrenzyInterval, .1f);
	if (Id == EBossRelicId::FlameWard) NextWardAt = Clock + FMath::Max(WardInterval, .1f);
	if (Id == EBossRelicId::SeismicStrike) SeismicReadyAt = Clock + FMath::Max(SeismicInterval, .1f);
	if (Id == EBossRelicId::MovingFireball) PreviousLocation = GetOwner()->GetActorLocation();
	UE_LOG(LogAICompanionDemo, Log, TEXT("[BossRelic] acquired=%s %s"), *RelicIdName(Id).ToString(), *DescribeRelic(Id));
	return true;
}

void UBossRelicComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!OwnerAlive()) { ClearRuntime(); return; }
	if (DeltaTime <= 0.f || !CanTrigger()) return;
	Clock += DeltaTime;
	TGuardValue<bool> Guard(bProcessingEffect, true);
	TickWalking(DeltaTime);
	TickPeriodic();
	TickPyres();
	TickSpikes(DeltaTime);
	FriendlyOrbs.RemoveAll([](const TWeakObjectPtr<ABossOrb>& Orb) { return !Orb.IsValid(); });
	TransientVfx.RemoveAll([](const TWeakObjectPtr<AActor>& Fx) { return !Fx.IsValid(); });
	TransientNiagaraVfx.RemoveAll([](const TWeakObjectPtr<UNiagaraComponent>& Fx) { return !Fx.IsValid(); });
	AppliedBleeds.RemoveAll([](const TWeakObjectPtr<URelicBleedComponent>& Bleed) { return !Bleed.IsValid() || Bleed->GetBleedRemaining() <= 0.f; });
}

void UBossRelicComponent::TickWalking(float DeltaTime)
{
	const FVector Position = GetOwner()->GetActorLocation();
	const float Distance = FVector::Dist2D(Position, PreviousLocation);
	PreviousLocation = Position;
	if (!HasRelic(EBossRelicId::MovingFireball)) return;
	// 闪烁由 NotifyBlink 重置；其他突然传送不算步行。上限兼容当前最高移速与低帧率。
	const float ContinuousLimit = FMath::Max(400.f, GetOwner()->GetVelocity().Size2D() * DeltaTime * 2.f + 100.f);
	if (Distance <= ContinuousLimit) TravelAccumulated += Distance;
	const float Threshold = FMath::Max(FireballTravelDistance, 1.f);
	for (int32 Emitted = 0; Emitted < 2 && TravelAccumulated >= Threshold; ++Emitted)
	{
		TravelAccumulated -= Threshold;
		SpawnMovingFireball();
	}
}

void UBossRelicComponent::SpawnMovingFireball()
{
	TArray<AActor*> Enemies;
	CollectAliveCombatants(GetWorld(), Enemies);
	const FVector Origin = GetOwner()->GetActorLocation() + FVector(0, 0, 40);
	AActor* Nearest = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (AActor* Enemy : Enemies)
	{
		const double Distance = FVector::DistSquared(Origin, Enemy->GetActorLocation());
		if (Distance < BestDistance) { BestDistance = Distance; Nearest = Enemy; }
	}
	const FVector Direction = Nearest ? (Nearest->GetActorLocation() - Origin).GetSafeNormal() : GetOwner()->GetActorForwardVector();
	FActorSpawnParameters Params;
	Params.Owner = GetOwner();
	Params.Instigator = Cast<APawn>(GetOwner());
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ABossOrb* Orb = GetWorld()->SpawnActor<ABossOrb>(Origin, Direction.Rotation(), Params))
	{
		Orb->LaunchFriendly(Direction, FireballDamage, FireballRadius, GetOwner());
		RegisterFriendlyOrb(Orb);
		UE_LOG(LogAICompanionDemo, Log, TEXT("[BossRelic] MovingFireball count=%d remainder=%.1f"), FireballsSpawned, TravelAccumulated);
	}
}

void UBossRelicComponent::RegisterFriendlyOrb(ABossOrb* Orb)
{
	if (!IsValid(Orb)) return;
	FriendlyOrbs.RemoveAll([](const TWeakObjectPtr<ABossOrb>& Existing) { return !Existing.IsValid(); });
	while (FriendlyOrbs.Num() >= 8)
	{
		if (FriendlyOrbs[0].IsValid()) FriendlyOrbs[0]->Destroy();
		FriendlyOrbs.RemoveAt(0);
	}
	FriendlyOrbs.Add(Orb);
	++FireballsSpawned;
}

void UBossRelicComponent::SpawnBlinkFireballs(const FVector& Landing)
{
	TArray<AActor*> Enemies;
	CollectAliveCombatants(GetWorld(), Enemies);
	TArray<AActor*> Available = Enemies;
	for (int32 Index = 0; Index < 3 && OwnerAlive(); ++Index)
	{
		if (Available.IsEmpty()) Available = Enemies;
		AActor* Target = nullptr;
		int32 TargetIndex = INDEX_NONE;
		double BestDistance = TNumericLimits<double>::Max();
		for (int32 CandidateIndex = 0; CandidateIndex < Available.Num(); ++CandidateIndex)
		{
			AActor* Candidate = Available[CandidateIndex];
			const ICombatant* Enemy = IsValid(Candidate) ? Cast<ICombatant>(Candidate) : nullptr;
			if (!Enemy || Enemy->IsDead() || Candidate->IsActorBeingDestroyed() || !Enemy->GetHealthComponent()) continue;
			const double Distance = FVector::DistSquared(Landing, Candidate->GetActorLocation());
			if (Distance < BestDistance) { BestDistance = Distance; Target = Candidate; TargetIndex = CandidateIndex; }
		}
		if (TargetIndex != INDEX_NONE) Available.RemoveAtSwap(TargetIndex);
		const float Angle = Index * 2.f * PI / 3.f;
		const FVector Origin = Landing + FVector(FMath::Cos(Angle) * 100.f, FMath::Sin(Angle) * 100.f, 80.f);
		const FVector Direction = Target ? (Target->GetActorLocation() - Origin).GetSafeNormal() : GetOwner()->GetActorForwardVector();
		FActorSpawnParameters Params;
		Params.Owner = GetOwner();
		Params.Instigator = Cast<APawn>(GetOwner());
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ABossOrb* Orb = GetWorld()->SpawnActor<ABossOrb>(Origin, Direction.Rotation(), Params))
		{
			Orb->LaunchFriendlyHoming(Target, FireballDamage, BlinkFireballSplashDamage, FireballRadius, GetOwner());
			RegisterFriendlyOrb(Orb);
			++BlinkFireballsSpawned;
			UE_LOG(LogAICompanionDemo, Log, TEXT("[BossRelic] BlinkFireball spawned=%d target=%s direct=%.1f splash=%.1f"),
				BlinkFireballsSpawned, *GetPathNameSafe(Target), FireballDamage, BlinkFireballSplashDamage);
		}
	}
}

void UBossRelicComponent::ActivatePeriodicFrenzy()
{
	bool bActivated = false;
	for (TActorIterator<ACompanionAIController> It(GetWorld()); It; ++It)
	{
		if (!IsValid(It->GetPawn())) continue;
		It->ActivateFrenzy(FrenzyDuration, false);
		DrawDebugSphere(GetWorld(), It->GetPawn()->GetActorLocation(), 110.f, 16, FColor(175, 72, 255), false, .4f, 0, 3.f);
		bActivated = true;
	}
	if (bActivated)
	{
		++FrenzyActivations;
		UE_LOG(LogAICompanionDemo, Log, TEXT("[BossRelic] SpiritFrenzy count=%d duration=%.2f"), FrenzyActivations, FrenzyDuration);
	}
}

void UBossRelicComponent::TickPeriodic()
{
	if (HasRelic(EBossRelicId::SpiritFrenzy) && Clock >= NextFrenzyAt)
	{
		ActivatePeriodicFrenzy();
		const double Interval = FMath::Max(FrenzyInterval, .1f);
		NextFrenzyAt += (FMath::FloorToDouble((Clock - NextFrenzyAt) / Interval) + 1) * Interval;
	}
	if (!HasRelic(EBossRelicId::FlameWard)) return;
	auto DamageWardTicks = [this]()
	{
		if (WardEndsAt <= 0) return;
		const double Until = FMath::Min(Clock, WardEndsAt);
		int32 Catchup = 0;
		while (NextWardDamageAt <= Until + .0001 && Catchup++ < 64)
		{
			FireAreaDamage(GetOwner()->GetActorLocation(), WardRadius, WardDamagePerTick);
			NextWardDamageAt += FMath::Max(WardDamageInterval, .1f);
			++WardDamageTicks;
		}
	};
	DamageWardTicks();
	if (Clock >= NextWardAt)
	{
		const double Interval = FMath::Max(WardInterval, .1f);
		const double Skipped = FMath::FloorToDouble((Clock - NextWardAt) / Interval);
		const double Start = NextWardAt + Skipped * Interval;
		NextWardAt = Start + Interval;
		WardEndsAt = Start + FMath::Max(WardDuration, .1f);
		NextWardDamageAt = Start + FMath::Max(WardDamageInterval, .1f);
		++WardActivations;
		UE_LOG(LogAICompanionDemo, Log, TEXT("[BossRelic] FlameWard count=%d duration=%.2f"), WardActivations, WardDuration);
		DamageWardTicks();
	}
	if (IsFlameWardActive())
	{
		DrawDebugSphere(GetWorld(), GetOwner()->GetActorLocation(), 110.f, 16, FColor(255, 132, 32), false, 0.f, 0, 2.8f);
		DrawDebugSphere(GetWorld(), GetOwner()->GetActorLocation(), WardRadius, 20, FColor(255, 174, 55), false, 0.f, 0, 1.2f);
	}
}

bool UBossRelicComponent::IsFlameWardActive() const
{
	return OwnerAlive() && HasRelic(EBossRelicId::FlameWard) && Clock < WardEndsAt;
}

float UBossRelicComponent::GetSpiritFrenzyCooldownRemaining() const
{
	return OwnerAlive() && HasRelic(EBossRelicId::SpiritFrenzy)
		? static_cast<float>(FMath::Max(NextFrenzyAt - Clock, 0.)) : 0.f;
}

float UBossRelicComponent::GetSpiritFrenzyCooldownTotal() const
{
	return OwnerAlive() && HasRelic(EBossRelicId::SpiritFrenzy) ? FMath::Max(FrenzyInterval, .1f) : 0.f;
}

float UBossRelicComponent::GetSpiritFrenzyActiveRemaining() const
{
	if (!OwnerAlive() || !HasRelic(EBossRelicId::SpiritFrenzy) || !GetWorld()) return 0.f;
	float Remaining = 0.f;
	for (TActorIterator<ACompanionAIController> It(GetWorld()); It; ++It)
	{
		Remaining = FMath::Max(Remaining, It->GetFrenzyRemaining());
	}
	return Remaining;
}

float UBossRelicComponent::GetPotionFrenzyDurationMultiplier() const
{
	return HasRelic(EBossRelicId::SpiritFrenzy) ? FMath::Max(PotionFrenzyMultiplier, 1.f) : 1.f;
}

float UBossRelicComponent::ModifyIncomingDamage(float Damage) const
{
	float Result = FMath::Max(Damage, 0.f);
	if (!OwnerAlive()) return Result;
	if (HasRelic(EBossRelicId::StoneCarapace)) Result *= 1.f - FMath::Clamp(CarapaceReduction, 0.f, .95f);
	if (IsFlameWardActive()) Result *= 1.f - FMath::Clamp(WardReduction, 0.f, .95f);
	return Result;
}

void UBossRelicComponent::NotifyCompanionHit(AActor* CompanionPawn, float ActualDamage)
{
	if (bProcessingEffect || !CanTrigger() || !HasRelic(EBossRelicId::BloodLink) || ActualDamage <= 0
		|| !Cast<ACompanionCharacter>(CompanionPawn)
		|| FVector::DistSquared2D(CompanionPawn->GetActorLocation(), GetOwner()->GetActorLocation()) > FMath::Square(BloodLinkRange)) return;
	TGuardValue<bool> Guard(bProcessingEffect, true);
	LatestBloodDamage = ActualDamage;
	LatestHealAttempted = ActualDamage * FMath::Clamp(BloodLinkHealingRatio, 0.f, 1.f);
	const float Before = OwnerHealth->GetCurrentHealth();
	OwnerHealth->Heal(LatestHealAttempted);
	LatestHealActual = FMath::Max(OwnerHealth->GetCurrentHealth() - Before, 0.f);
	if (LatestHealActual > 0)
	{
		DrawDebugLine(GetWorld(), CompanionPawn->GetActorLocation(), GetOwner()->GetActorLocation(), FColor(112, 255, 154), false, .16f, 0, 2.f);
		DrawDebugString(GetWorld(), GetOwner()->GetActorLocation() + FVector(0, 0, 105),
			FString::Printf(TEXT("+%.1f"), LatestHealActual), nullptr, FColor(112, 255, 154), .3f, true);
	}
}

void UBossRelicComponent::DebugPrimaryAttack(AActor* PrimaryTarget, const FVector& ImpactPoint)
{
#if WITH_EDITOR
	NotifyPrimaryAttack(GetOwner(), PrimaryTarget, ImpactPoint);
#endif
}

void UBossRelicComponent::NotifyPrimaryAttack(AActor* Attacker, AActor* PrimaryTarget, const FVector& ImpactPoint)
{
	// 主目标已被本次直击杀死也照常爆炸；此钩只接受玩家主攻，队友走独立吸血口。
	if (bProcessingEffect || !CanTrigger() || Attacker != GetOwner()) return;
	TGuardValue<bool> Guard(bProcessingEffect, true);
	TryStoneSpikes(ImpactPoint);
	if (HasRelic(EBossRelicId::SeismicStrike) && Clock >= SeismicReadyAt)
	{
		SeismicReadyAt = Clock + FMath::Max(SeismicInterval, .1f);
		++SeismicActivations;
		Blast(ImpactPoint, SeismicRadius, SeismicDamage, true);
	}
	if (Attacker == GetOwner() && HasRelic(EBossRelicId::VolcanicRounds))
	{
		++VolcanicActivations;
		Blast(ImpactPoint, VolcanicRadius, VolcanicDamage, false);
	}
}

void UBossRelicComponent::NotifyElementalRayHit(AActor* Attacker, AActor* Target, const FVector& ImpactPoint)
{
	if (bProcessingEffect || !CanTrigger() || Attacker != GetOwner() || !IsValid(Target)) return;
	ICombatant* Combatant = Cast<ICombatant>(Target);
	UHealthComponent* Health = Combatant ? Combatant->GetHealthComponent() : nullptr;
	if (!IsValid(Health) || Health->IsInvulnerable()) return;
	TGuardValue<bool> Guard(bProcessingEffect, true);
	TryStoneSpikes(ImpactPoint);
}

void UBossRelicComponent::TryStoneSpikes(const FVector& ImpactPoint)
{
	if (HasRelic(EBossRelicId::StoneSpikes) && FMath::FRand() < FMath::Clamp(SpikeChance, 0.f, 1.f))
	{
		const FVector Ground = GroundPoint(ImpactPoint);
		SpawnSpikeVisuals(Ground);
		// 只随这次概率成功的地刺发动；流血拍、受击和免伤反馈均不重播。
		USFXLibrary::PlaySFX(GetWorld(), EGameSFX::RelicStoneCleaver);
		TArray<AActor*> Enemies;
		CollectAliveCombatants(GetWorld(), Enemies);
		for (AActor* Enemy : Enemies)
		{
			if (FVector::DistSquared2D(Enemy->GetActorLocation(), Ground) > FMath::Square(SpikeRadius)) continue;
			if (DamageEnemy(Enemy, SpikeDamage) > 0)
			{
				if (URelicBleedComponent* Bleed = URelicBleedComponent::ApplyTo(Enemy, GetOwner(), SpikeBleedDuration, SpikeBleedDamage, SpikeBleedInterval))
					AppliedBleeds.AddUnique(Bleed);
				if (AEnemyGrunt* Grunt = Cast<AEnemyGrunt>(Enemy); Grunt && !Grunt->IsDead()) Grunt->ApplySlow(SpikeSlowDuration, SpikeSpeedFactor);
			}
		}
		++SpikeActivations;
	}
}

void UBossRelicComponent::NotifyPlayerDamaged(AActor* Source, float ActualDamage)
{
	if (bProcessingEffect || !CanTrigger() || ActualDamage <= 0 || !HasRelic(EBossRelicId::StoneCarapace)) return;
	TGuardValue<bool> Guard(bProcessingEffect, true);
	// 火球等间接伤害回溯 Owner；最多八级防止错误环链，不向玩家/队友反伤。
	AActor* Enemy = Source;
	for (int32 Depth = 0; IsValid(Enemy) && Depth < 8; ++Depth)
	{
		if (ICombatant* Combatant = Cast<ICombatant>(Enemy))
		{
			if (!Combatant->IsDead() && DamageEnemy(Enemy, ThornsDamage) > 0)
			{
				++ThornsActivations;
				DrawDebugLine(GetWorld(), GetOwner()->GetActorLocation(), Enemy->GetActorLocation(), FColor(223, 178, 105), false, .18f, 0, 4.f);
			}
			return;
		}
		Enemy = Enemy->GetOwner();
	}
}

float UBossRelicComponent::DamageEnemy(AActor* Target, float Damage)
{
	ICombatant* Combatant = IsValid(Target) ? Cast<ICombatant>(Target) : nullptr;
	if (!Combatant || Combatant->IsDead() || Damage <= 0 || !OwnerAlive()) return 0.f;
	UHealthComponent* Health = Combatant->GetHealthComponent();
	if (!Health) return 0.f;
	const float Before = Health->GetCurrentHealth();
	Health->ApplyDamage(Damage, GetOwner());
	return FMath::Max(Before - Health->GetCurrentHealth(), 0.f);
}

void UBossRelicComponent::Blast(const FVector& Center, float Radius, float Damage, bool bKnockUp)
{
	TArray<AActor*> Enemies;
	CollectAliveCombatants(GetWorld(), Enemies);
	for (AActor* Enemy : Enemies)
	{
		if (FVector::DistSquared2D(Center, Enemy->GetActorLocation()) > FMath::Square(Radius)) continue;
		if (DamageEnemy(Enemy, Damage) <= 0) continue;
		// Boss 保持现有控制免疫；普通敌人使用真实 CharacterMovement 下一拍击飞。
		if (bKnockUp)
			if (AEnemyGrunt* Grunt = Cast<AEnemyGrunt>(Enemy); Grunt && !Grunt->IsDead())
			{
				FVector Outward = (Grunt->GetActorLocation() - Center).GetSafeNormal2D();
				Grunt->LaunchCharacter(Outward * 260.f + FVector(0, 0, SeismicUpSpeed), false, true);
			}
		DrawDebugString(GetWorld(), Enemy->GetActorLocation() + FVector(0, 0, 95), FString::Printf(TEXT("%.0f"), Damage), nullptr, FColor(255, 174, 72), .25f, true);
	}
	DrawDebugSphere(GetWorld(), Center, Radius, 18, FColor(255, 122, 38), false, .2f, 0, 5.f);
	DrawDebugSphere(GetWorld(), Center, Radius * .72f, 14, FColor(255, 210, 100), false, .13f, 0, 3.f);
	SpawnTrackedVfx(static_cast<uint8>(EVfx::Explosion), Center, .35f, bKnockUp ? 1.2f : .7f);
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Explosion);
}

void UBossRelicComponent::FireAreaDamage(const FVector& Center, float Radius, float Damage)
{
	TArray<AActor*> Enemies;
	CollectAliveCombatants(GetWorld(), Enemies);
	for (AActor* Enemy : Enemies)
	{
		if (FVector::DistSquared2D(Center, Enemy->GetActorLocation()) > FMath::Square(Radius)) continue;
		AEnemyGrunt* Grunt = Cast<AEnemyGrunt>(Enemy);
		const bool bMelt = Grunt && Grunt->GetFrostResidue() > 0.f;
		const float Actual = DamageEnemy(Enemy, Damage * (bMelt ? 2.f : 1.f));
		if (Actual <= 0) continue;
		if (Grunt)
		{
			if (bMelt) Grunt->ConsumeFrostResidue();
			Grunt->RefreshBurnResidue(1.5f);
		}
		const FVector Point = Enemy->GetActorLocation();
		DrawDebugString(GetWorld(), Point + FVector(0, 0, 110),
			bMelt ? FString::Printf(TEXT("融化 %.0f"), Actual) : FString::Printf(TEXT("%.0f"), Actual),
			nullptr, bMelt ? FColor(112, 225, 255) : FColor(255, 173, 62), .22f, true);
		DrawDebugSphere(GetWorld(), Point, bMelt ? 75.f : 30.f, 10, bMelt ? FColor(112, 225, 255) : FColor(255, 173, 62), false, .16f, 0, 3.f);
		if (bMelt)
		{
			++MeltReactions;
			SpawnTrackedVfx(static_cast<uint8>(EVfx::Frost), Point, .35f, .7f);
			SpawnTrackedVfx(static_cast<uint8>(EVfx::Fire), Point, .35f, .7f);
		}
	}
}

void UBossRelicComponent::NotifyBlink(const FVector& Start, const FVector& End)
{
	PreviousLocation = End; // 任意成功闪烁都排除于行走计数，是否持有火圈无关。
	if (bProcessingEffect || !CanTrigger()) return;
	TGuardValue<bool> Guard(bProcessingEffect, true);
	if (HasRelic(EBossRelicId::MovingFireball)) SpawnBlinkFireballs(End);
	if (HasRelic(EBossRelicId::BlinkPyre))
	{
		SpawnPyre(Start);
		SpawnPyre(End);
	}
}

void UBossRelicComponent::SpawnPyre(const FVector& Location)
{
	while (Pyres.Num() >= 6)
	{
		if (Pyres[0].Vfx.IsValid()) Pyres[0].Vfx->Destroy();
		Pyres.RemoveAt(0);
	}
	FPyre Pyre;
	Pyre.Center = GroundPoint(Location);
	Pyre.EndsAt = Clock + FMath::Max(PyreDuration, .1f);
	Pyre.NextDamageAt = Clock + FMath::Max(PyreDamageInterval, .1f);
	CacheGroundRing(Pyre.Center, PyreRadius, Pyre.Ring);
	Pyre.Vfx = UVfxLibrary::SpawnVfx(GetWorld(), EVfx::Fire, Pyre.Center, PyreDuration, 1.2f);
	Pyres.Add(MoveTemp(Pyre));
}

void UBossRelicComponent::TickPyres()
{
	for (int32 Index = Pyres.Num() - 1; Index >= 0; --Index)
	{
		FPyre& Pyre = Pyres[Index];
		const double Until = FMath::Min(Clock, Pyre.EndsAt);
		int32 Catchup = 0;
		while (Pyre.NextDamageAt <= Until + .0001 && Catchup++ < 64)
		{
			FireAreaDamage(Pyre.Center, PyreRadius, PyreDamagePerTick);
			Pyre.NextDamageAt += FMath::Max(PyreDamageInterval, .1f);
			++PyreDamageTicks;
		}
		if (Clock >= Pyre.EndsAt)
		{
			if (Pyre.Vfx.IsValid()) Pyre.Vfx->Destroy();
			Pyres.RemoveAt(Index);
		}
		else DrawRing(Pyre.Ring, FColor(255, 135, 36), 0.f, 4.f);
	}
}

FVector UBossRelicComponent::GroundPoint(const FVector& Location) const
{
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetOwner());
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Location + FVector(0, 0, 8000), Location - FVector(0, 0, 10000), Objects, Params))
		return Hit.ImpactPoint;
	return Location;
}

void UBossRelicComponent::CacheGroundRing(const FVector& Center, float Radius, TArray<FVector>& Out) const
{
	Out.Reset(24);
	for (int32 Index = 0; Index < 24; ++Index)
	{
		const float Angle = Index * 2.f * PI / 24;
		Out.Add(GroundPoint(Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0)) + FVector(0, 0, 8));
	}
}

void UBossRelicComponent::DrawRing(const TArray<FVector>& Ring, const FColor& Color, float Life, float Width) const
{
	for (int32 Index = 0; Index < Ring.Num(); ++Index)
		DrawDebugLine(GetWorld(), Ring[Index], Ring[(Index + 1) % Ring.Num()], Color, false, Life, 0, Width);
}

void UBossRelicComponent::SpawnSpikeVisuals(const FVector& Location)
{
	if (!SpikeMesh) return;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		while (Spikes.Num() >= 24)
		{
			if (UStaticMeshComponent* Old = Spikes[0].Mesh.Get()) { SpikeComponents.Remove(Old); Old->DestroyComponent(); }
			Spikes.RemoveAt(0);
		}
		const float Angle = Index * 2.f * PI / 5;
		const FVector Ground = GroundPoint(Location + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (Index ? SpikeRadius * .6f : 0.f));
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(GetOwner());
		Mesh->SetStaticMesh(SpikeMesh);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCastShadow(false);
		Mesh->SetMobility(EComponentMobility::Movable);
		if (SpikeMaterial) Mesh->SetMaterial(0, SpikeMaterial);
		Mesh->RegisterComponent();
		Mesh->SetWorldLocation(Ground);
		Mesh->SetWorldScale3D(FVector(.75f, .75f, .01f));
		SpikeComponents.Add(Mesh);
		FRelicSpike Spike;
		Spike.Mesh = Mesh;
		Spike.Ground = Ground;
		Spikes.Add(Spike);
	}
}

void UBossRelicComponent::TickSpikes(float DeltaTime)
{
	for (int32 Index = Spikes.Num() - 1; Index >= 0; --Index)
	{
		FRelicSpike& Spike = Spikes[Index];
		Spike.Age += DeltaTime;
		UStaticMeshComponent* Mesh = Spike.Mesh.Get();
		if (!Mesh || Spike.Age >= .92f)
		{
			if (Mesh) { SpikeComponents.Remove(Mesh); Mesh->DestroyComponent(); }
			Spikes.RemoveAt(Index);
			continue;
		}
		const float Progress = Spike.Age < .12f ? Spike.Age / .12f : (Spike.Age < .57f ? 1.f : (.92f - Spike.Age) / .35f);
		Mesh->SetWorldScale3D(FVector(.75f, .75f, FMath::Max(.01f, 1.8f * Progress)));
		Mesh->SetWorldLocation(Spike.Ground + FVector(0, 0, 90.f * Progress));
	}
}

void UBossRelicComponent::TrackTransientVfx(AActor* Fx)
{
	if (!IsValid(Fx)) { return; }
	if (!OwnerAlive()) { Fx->Destroy(); return; }
	TransientVfx.RemoveAll([](const TWeakObjectPtr<AActor>& Existing) { return !Existing.IsValid(); });
	TransientVfx.AddUnique(Fx);
}

void UBossRelicComponent::SpawnTrackedVfx(uint8 Kind, const FVector& Point, float Life, float Scale)
{
	UNiagaraComponent* NiagaraFx = nullptr;
	TrackTransientVfx(UVfxLibrary::SpawnVfx(GetWorld(), static_cast<EVfx>(Kind), Point, Life, Scale, &NiagaraFx));
	if (IsValid(NiagaraFx))
	{
		if (!OwnerAlive()) { NiagaraFx->DestroyComponent(); return; }
		TransientNiagaraVfx.RemoveAll([](const TWeakObjectPtr<UNiagaraComponent>& Fx) { return !Fx.IsValid(); });
		TransientNiagaraVfx.AddUnique(NiagaraFx);
	}
}

void UBossRelicComponent::TrackTransientNiagara(UNiagaraComponent* Fx, float Life)
{
	if (!IsValid(Fx)) return;
	if (!OwnerAlive()) { Fx->DestroyComponent(); return; }
	TransientNiagaraVfx.RemoveAll([](const TWeakObjectPtr<UNiagaraComponent>& Existing) { return !Existing.IsValid(); });
	TransientNiagaraVfx.AddUnique(Fx);
	TWeakObjectPtr<UNiagaraComponent> WeakFx(Fx);
	FTimerHandle LifeHandle;
	GetWorld()->GetTimerManager().SetTimer(LifeHandle, [WeakFx]()
	{
		if (UNiagaraComponent* Component = WeakFx.Get())
		{
			Component->DeactivateImmediate();
			if (IsValid(Component)) Component->DestroyComponent();
		}
	}, FMath::Max(Life, .01f), false);
}

void UBossRelicComponent::HandleOwnerDeath(UHealthComponent* Health)
{
	ClearRuntime();
}

void UBossRelicComponent::ClearRuntime()
{
	if (bStopped) return;
	bStopped = true;
	SetComponentTickEnabled(false);
	WardEndsAt = 0;
	TravelAccumulated = 0;
	for (const TWeakObjectPtr<ABossOrb>& Orb : FriendlyOrbs) if (Orb.IsValid()) Orb->Destroy();
	for (const FPyre& Pyre : Pyres) if (Pyre.Vfx.IsValid()) Pyre.Vfx->Destroy();
	for (const TWeakObjectPtr<AActor>& Fx : TransientVfx) if (Fx.IsValid()) Fx->Destroy();
	for (const TWeakObjectPtr<UNiagaraComponent>& Fx : TransientNiagaraVfx) if (Fx.IsValid()) Fx->DestroyComponent();
	for (UStaticMeshComponent* Mesh : SpikeComponents) if (IsValid(Mesh)) Mesh->DestroyComponent();
	for (const TWeakObjectPtr<URelicBleedComponent>& Bleed : AppliedBleeds) if (Bleed.IsValid()) Bleed->StopBleeding();
	AppliedBleeds.Reset();
	FriendlyOrbs.Reset(); Pyres.Reset(); TransientVfx.Reset(); TransientNiagaraVfx.Reset(); SpikeComponents.Reset(); Spikes.Reset();
}

void UBossRelicComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (OwnerHealth) OwnerHealth->OnDeath.RemoveDynamic(this, &UBossRelicComponent::HandleOwnerDeath);
	ClearRuntime();
	Super::EndPlay(EndPlayReason);
}

FString UBossRelicComponent::DescribeRelic(EBossRelicId Id) const
{
	switch (Id)
	{
	case EBossRelicId::BloodLink: return FString::Printf(TEXT("队友距你 %.0f 米内时，直击实际伤害的 %.0f%% 恢复你的生命。"), BloodLinkRange / 100.f, BloodLinkHealingRatio * 100.f);
	case EBossRelicId::SpiritFrenzy: return FString::Printf(TEXT("每 %.0f 秒使队友紫色狂暴 %.0f 秒；狂暴药剂持续时间增加 %.0f%%。"), FrenzyInterval, FrenzyDuration, (PotionFrenzyMultiplier - 1.f) * 100.f);
	case EBossRelicId::MovingFireball: return FString::Printf(TEXT("累计行走 %.0f 米发射一颗爆炸火球，造成 %.0f 范围伤害；闪烁落点生成三颗追踪必中火球，直击 %.0f、周围 %.0f 伤害，二者不叠加。爆炸半径 %.1f 米；闪烁不计行走距离。"), FireballTravelDistance / 100.f, FireballDamage, FireballDamage, BlinkFireballSplashDamage, FireballRadius / 100.f);
	case EBossRelicId::StoneSpikes: return FString::Printf(TEXT("主攻命中有 %.0f%% 概率唤出地刺，半径 %.1f 米造成 %.0f 伤害，并使存活敌人流血 %.0f 秒，每 %.1f 秒造成 %.0f 伤害；重复地刺刷新流血，不叠层。普通敌人速度降至 %.0f%%，持续 %.0f 秒。"), SpikeChance * 100.f, SpikeRadius / 100.f, SpikeDamage, SpikeBleedDuration, SpikeBleedInterval, SpikeBleedDamage, SpikeSpeedFactor * 100.f, SpikeSlowDuration);
	case EBossRelicId::SeismicStrike: return FString::Printf(TEXT("每 %.0f 秒准备一次震爆；下一次主攻命中时，半径 %.1f 米造成 %.0f 伤害并击飞普通敌人。"), SeismicInterval, SeismicRadius / 100.f, SeismicDamage);
	case EBossRelicId::StoneCarapace: return FString::Printf(TEXT("永久减伤 %.0f%%；实际受击时，对伤害来源敌人反伤 %.0f。"), CarapaceReduction * 100.f, ThornsDamage);
	case EBossRelicId::VolcanicRounds: return FString::Printf(TEXT("玩家每次主攻命中附加爆炸，半径 %.1f 米造成 %.0f 伤害；散射、穿透与次生效果不重复触发。"), VolcanicRadius / 100.f, VolcanicDamage);
	case EBossRelicId::FlameWard: return FString::Printf(TEXT("每 %.0f 秒获得 %.0f 秒火盾，期间减伤 %.0f%%；半径 %.1f 米每 %.1f 秒灼烧 %.0f 伤害，碰到冰残留触发一次双倍融化。"), WardInterval, WardDuration, WardReduction * 100.f, WardRadius / 100.f, WardDamageInterval, WardDamagePerTick);
	case EBossRelicId::BlinkPyre: return FString::Printf(TEXT("闪烁起点和终点留下半径 %.1f 米火圈，持续 %.0f 秒，每 %.1f 秒造成 %.0f 火伤；可触发融化，最多保留六个火圈。"), PyreRadius / 100.f, PyreDuration, PyreDamageInterval, PyreDamagePerTick);
	default: return TEXT("");
	}
}

FString UBossRelicComponent::DebugState() const
{
	FString IDs;
	for (uint8 Index = 0; Index < static_cast<uint8>(EBossRelicId::Count); ++Index)
	{
		const EBossRelicId Id = static_cast<EBossRelicId>(Index);
		if (!HasRelic(Id)) continue;
		if (!IDs.IsEmpty()) IDs += TEXT(",");
		IDs += FString::Printf(TEXT("\"%s\""), *RelicIdName(Id).ToString());
	}
	int32 LiveOrbs = 0;
	for (const TWeakObjectPtr<ABossOrb>& Orb : FriendlyOrbs) if (Orb.IsValid()) ++LiveOrbs;
	int32 LiveActors = 0, LiveNiagara = 0;
	for (const TWeakObjectPtr<AActor>& Fx : TransientVfx) if (Fx.IsValid()) ++LiveActors;
	for (const TWeakObjectPtr<UNiagaraComponent>& Fx : TransientNiagaraVfx) if (Fx.IsValid()) ++LiveNiagara;
	return FString::Printf(TEXT("{\"ownedIDs\":[%s],\"ownedMask\":%d,\"clock\":%.3f,\"stopped\":%s,\"distanceAccumulatedCm\":%.3f,\"fireballsSpawned\":%d,\"blinkFireballsSpawned\":%d,\"liveFireballs\":%d,\"liveTrackedVfx\":%d,\"liveTrackedVfxActors\":%d,\"liveTrackedVfxNiagara\":%d,\"frenzyActivations\":%d,\"frenzyDuration\":%.3f,\"potionDurationMultiplier\":%.3f,\"frenzyNextIn\":%.3f,\"frenzyActiveRemaining\":%.3f,\"wardActivations\":%d,\"wardActive\":%s,\"wardRemaining\":%.3f,\"wardDamageTicks\":%d,\"pyreCount\":%d,\"pyreDamageTicks\":%d,\"spikeActivations\":%d,\"liveSpikeMeshes\":%d,\"seismicActivations\":%d,\"seismicReadyIn\":%.3f,\"volcanicActivations\":%d,\"thornsActivations\":%d,\"meltReactions\":%d,\"latestBloodDamage\":%.3f,\"latestHealAttempted\":%.3f,\"latestHealActual\":%.3f}"),
		*IDs, OwnedMask, Clock, bStopped ? TEXT("true") : TEXT("false"), TravelAccumulated, FireballsSpawned, BlinkFireballsSpawned, LiveOrbs, LiveActors + LiveNiagara, LiveActors, LiveNiagara,
		FrenzyActivations, FrenzyDuration, GetPotionFrenzyDurationMultiplier(), GetSpiritFrenzyCooldownRemaining(), GetSpiritFrenzyActiveRemaining(),
		WardActivations, IsFlameWardActive() ? TEXT("true") : TEXT("false"), FMath::Max(WardEndsAt - Clock, 0.), WardDamageTicks, Pyres.Num(), PyreDamageTicks,
		SpikeActivations, Spikes.Num(), SeismicActivations, HasRelic(EBossRelicId::SeismicStrike) ? FMath::Max(SeismicReadyAt - Clock, 0.) : 0.,
		VolcanicActivations, ThornsActivations, MeltReactions, LatestBloodDamage, LatestHealAttempted, LatestHealActual);
}
