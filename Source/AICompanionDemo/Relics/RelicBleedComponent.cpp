/**
 * [INPUT]: RelicBleedComponent契约、活ICombatant血量、ParagonGrux真实血滴及游戏暂停状态。
 * [OUTPUT]: 可刷新流血由遗物真实参数传入，v1.3默认5s/0.5s每跳10；最多8份短命血滴，真实扣血/拍数/剩余时间只读验收。
 * [POS]: 地刺次生伤害只调用Health.ApplyDamage；不调用NotifyPrimaryAttack或元素/猎印链。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "RelicBleedComponent.h"
#include "AI/Combatant.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "UObject/ConstructorHelpers.h"

URelicBleedComponent::URelicBleedComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	static ConstructorHelpers::FObjectFinder<UParticleSystem> Blood(TEXT("/Game/ParagonGrux/FX/Particles/Abilities/Primary/FX/P_Grux_ApplyBleed.P_Grux_ApplyBleed"));
	if (Blood.Succeeded()) BloodSystem = Blood.Object;
}

void URelicBleedComponent::BeginPlay()
{
	Super::BeginPlay();
	if (const ICombatant* Enemy = Cast<ICombatant>(GetOwner())) TargetHealth = Enemy->GetHealthComponent();
	if (TargetHealth) TargetHealth->OnDeath.AddDynamic(this, &URelicBleedComponent::HandleTargetDeath);
}

URelicBleedComponent* URelicBleedComponent::ApplyTo(AActor* Target, AActor* Source, float Duration, float Damage, float Interval)
{
	const ICombatant* Enemy = IsValid(Target) ? Cast<ICombatant>(Target) : nullptr;
	if (!Enemy || Enemy->IsDead() || !IsValid(Source) || Duration <= 0.f || Damage <= 0.f) return nullptr;
	URelicBleedComponent* Bleed = Target->FindComponentByClass<URelicBleedComponent>();
	if (!Bleed)
	{
		Bleed = NewObject<URelicBleedComponent>(Target);
		Target->AddInstanceComponent(Bleed);
		Bleed->RegisterComponent();
	}
	Bleed->Refresh(Source, Duration, Damage, Interval);
	return Bleed;
}

void URelicBleedComponent::Refresh(AActor* Source, float Duration, float Damage, float Interval)
{
	if (!TargetHealth || TargetHealth->IsDead()) return;
	DamageSource = Source;
	Remaining = FMath::Max(Remaining, Duration);
	DamagePerTick = FMath::Max(Damage, 0.f);
	TickInterval = FMath::Max(Interval, .1f);
	if (!bActive) Accumulated = 0.f;
	bActive = true;
	++RefreshCount;
	SetComponentTickEnabled(true);
	// 首次命中即有血滴；之后随每拍真实扣血脉冲，不生成地面圈或Debug线冒充流血。
	SpawnBlood();
}

bool URelicBleedComponent::SourceAlive() const
{
	const AActor* Source = DamageSource.Get();
	const UHealthComponent* Health = Source ? Source->FindComponentByClass<UHealthComponent>() : nullptr;
	return Health && !Health->IsDead() && Health->GetCurrentHealth() > 0.f;
}

void URelicBleedComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (!bActive || !TargetHealth || TargetHealth->IsDead() || !SourceAlive()) { StopBleeding(); return; }
	if (!GetWorld() || GetWorld()->IsPaused() || GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() < .01f) return;
	TickBloodVisuals(DeltaTime);
	Accumulated += FMath::Min(DeltaTime, Remaining);
	Remaining = FMath::Max(Remaining - DeltaTime, 0.f);
	for (int32 Catchup=0; bActive && Accumulated + KINDA_SMALL_NUMBER >= TickInterval && Catchup < 64; ++Catchup)
	{
		Accumulated = FMath::Max(Accumulated - TickInterval, 0.f);
		const float Before = TargetHealth->GetCurrentHealth();
		TargetHealth->ApplyDamage(DamagePerTick, DamageSource.Get());
		const float Applied = FMath::Max(Before - TargetHealth->GetCurrentHealth(), 0.f);
		++DamageTicks;
		TotalDamage += Applied;
		if (Applied > 0.f && bActive)
		{
			SpawnBlood();
			DrawDebugString(GetWorld(), GetOwner()->GetActorLocation() + FVector(0, 0, 115),
				FString::Printf(TEXT("流血 -%.0f"), Applied), nullptr, FColor(235, 30, 65), .45f, true, 1.15f);
		}
	}
	if (Remaining <= 0.f) StopBleeding();
}

void URelicBleedComponent::SpawnBlood()
{
	if (!BloodSystem || !GetOwner()->GetRootComponent()) return;
	while (BloodVisuals.Num() >= 8)
	{
		if (UParticleSystemComponent* Old = BloodVisuals[0].Component.Get()) Old->DestroyComponent();
		BloodVisuals.RemoveAt(0);
	}
	UParticleSystemComponent* Blood = UGameplayStatics::SpawnEmitterAttached(BloodSystem, GetOwner()->GetRootComponent(),
		NAME_None, FVector(0, 0, 30), FRotator::ZeroRotator, FVector(.9f), EAttachLocation::KeepRelativeOffset, true);
	if (Blood)
	{
		Blood->SetCanEverAffectNavigation(false);
		Blood->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FBloodVisual Visual;
		Visual.Component = Blood;
		BloodVisuals.Add(Visual);
	}
}

void URelicBleedComponent::TickBloodVisuals(float DeltaTime)
{
	for (int32 Index=BloodVisuals.Num()-1; Index>=0; --Index)
	{
		FBloodVisual& Visual = BloodVisuals[Index];
		Visual.Remaining -= DeltaTime;
		if (!Visual.Component.IsValid() || Visual.Remaining <= 0.f)
		{
			if (UParticleSystemComponent* Blood = Visual.Component.Get()) Blood->DestroyComponent();
			BloodVisuals.RemoveAtSwap(Index);
		}
	}
}

void URelicBleedComponent::StopBleeding()
{
	bActive = false;
	Remaining = Accumulated = 0.f;
	SetComponentTickEnabled(false);
	for (const FBloodVisual& Visual : BloodVisuals) if (UParticleSystemComponent* Blood = Visual.Component.Get()) Blood->DestroyComponent();
	BloodVisuals.Reset();
	DamageSource.Reset();
}

void URelicBleedComponent::HandleTargetDeath(UHealthComponent* Health) { StopBleeding(); }

void URelicBleedComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (TargetHealth) TargetHealth->OnDeath.RemoveDynamic(this, &URelicBleedComponent::HandleTargetDeath);
	StopBleeding();
	Super::EndPlay(Reason);
}

FString URelicBleedComponent::DebugBleedState() const
{
#if WITH_EDITOR
	int32 LiveVisuals = 0;
	for (const FBloodVisual& Visual : BloodVisuals) if (Visual.Component.IsValid()) ++LiveVisuals;
	return FString::Printf(TEXT("{\"active\":%s,\"remaining\":%.4f,\"damagePerTick\":%.2f,\"interval\":%.3f,\"damageTicks\":%d,\"totalDamage\":%.3f,\"refreshCount\":%d,\"liveBloodVisuals\":%d,\"bloodAssetLoaded\":%s}"),
		bActive ? TEXT("true") : TEXT("false"), Remaining, DamagePerTick, TickInterval, DamageTicks, TotalDamage,
		RefreshCount, LiveVisuals, BloodSystem ? TEXT("true") : TEXT("false"));
#else
	return TEXT("{}");
#endif
}
