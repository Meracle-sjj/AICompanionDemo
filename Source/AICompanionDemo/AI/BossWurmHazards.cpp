/**
 * [INPUT]: BossWurm火球/火区状态、WorldStatic探地、已有Wurm岩甲网格与Basalt/Core材质、FireMid Niagara与VfxLibrary爆炸组件回传。
 * [OUTPUT]: 有纹理落火、Boss自有不随身移动的火区Niagara与0.4s落地爆闪；自然/上限/死亡/EndPlay同步停发并销毁组件，未完成的火花不留给AutoDestroy。
 *  有效杂兵死亡默认35伤，潜地/暂停累计至真实易伤后结算；DebugHazardState分别回读火焰与爆闪组件。
 * [POS]: 蠕虫局部实现；状态机仍归BossWurm，所有伤害经Health，不绕过免伤、不另造计时源。
 * [PROTOCOL]: 变更时同步模块AGENTS和实际验收记录。
 */
#include "AI/BossWurm.h"
#include "AI/BossBase.h"
#include "AI/Combatant.h"
#include "AICompanionDemo.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Vfx/VfxLibrary.h"
#include "Templates/UnrealTemplate.h"

namespace
{
	void DestroyHazardVfx(TWeakObjectPtr<UNiagaraComponent>& Vfx)
	{
		if (UNiagaraComponent* Component = Vfx.Get())
		{
			Component->DeactivateImmediate();
			if (IsValid(Component)) Component->DestroyComponent();
		}
		Vfx.Reset();
	}
}

void ABossWurm::InitializeHazardArtwork()
{
	HazardFlameTemplate = LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/Fire/Loop/NS_Sub_FireMid_Loop_001.NS_Sub_FireMid_Loop_001"));
	FireRockMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Bosses/Wurm/SM_Wurm_Body.SM_Wurm_Body"));
	FireRockBasalt = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Bosses/Wurm/M_Wurm_Basalt.M_Wurm_Basalt"));
	FireRockCore = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Bosses/Wurm/M_Wurm_Core.M_Wurm_Core"));
	if (!FireRockMesh || !FireRockBasalt || !FireRockCore ||
		FireRockMesh->GetMaterialIndex(TEXT("Basalt")) == INDEX_NONE ||
		FireRockMesh->GetMaterialIndex(TEXT("MagmaCore")) == INDEX_NONE)
	{
		FireRockMesh = nullptr;
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[WurmHazard] 岩甲落火缺资源/槽，回退原球"));
	}
}

void ABossWurm::ConfigureHazardRock(UStaticMeshComponent* Component, float Radius)
{
	Component->SetStaticMesh(FireRockMesh ? FireRockMesh : SegmentMesh);
	Component->SetWorldScale3D(FVector(Radius / 50.f));
	Component->SetWorldRotation(FRotator(FMath::FRandRange(-180.f, 180.f), FMath::FRandRange(-180.f, 180.f), FMath::FRandRange(-180.f, 180.f)));
	Component->SetCanEverAffectNavigation(false);
	if (FireRockMesh)
	{
		Component->SetMaterial(FireRockMesh->GetMaterialIndex(TEXT("Basalt")), FireRockBasalt);
		Component->SetMaterial(FireRockMesh->GetMaterialIndex(TEXT("MagmaCore")), FireRockCore);
	}
	else Component->SetMaterial(0, SegmentMid);
}

void ABossWurm::SpawnFireBall()
{
	if (Segments.IsEmpty() || !SegmentMesh) return;
	const FWurmSegment& Source = Segments[FMath::RandRange(0, Segments.Num() - 1)];
	const FVector Radial(FMath::Cos(OrbitAngle), FMath::Sin(OrbitAngle), 0.f);
	const FVector Base = Source.Pos - FVector(0.f, 0.f, Source.Radius * .8f);
	SpawnFireBallAt(Base - Radial * (FireDropSpread + FMath::FRandRange(0.f, 80.f)));
	SpawnFireBallAt(Base + Radial * (FireDropSpread + FMath::FRandRange(0.f, 80.f)));
}

void ABossWurm::SpawnFireBallAt(const FVector& Position)
{
	if (!SegmentMesh || IsDead()) return;
	// 上限淘汰必须销毁组件，不能仅从逻辑数组移除。
	while (FireBalls.Num() >= 24)
	{
		if (IsValid(FireBalls[0].Mesh)) FireBalls[0].Mesh->DestroyComponent();
		FireBalls.RemoveAt(0);
		++FireRocksRetired;
	}
	FWurmFireBall Ball;
	Ball.Pos = Position;
	FVector Ground = Position;
	ResolveGroundZ(Ground);
	Ball.GroundZ = Ground.Z;
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
	Component->ComponentTags.Add(TEXT("WurmFireRock"));
	ConfigureHazardRock(Component, 25.f);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	Component->RegisterComponent();
	Component->SetWorldLocation(Position);
	Ball.Mesh = Component;
	FireBalls.Add(Ball);
	++FireRocksSpawned;
}

void ABossWurm::DestroyFireZone(FWurmFireZone& Zone)
{
	DestroyHazardVfx(Zone.Vfx);
	DestroyHazardVfx(Zone.ImpactVfx);
	Zone.ImpactLife = 0.f;
	++FireZonesRetired;
}

void ABossWurm::ClearFireHazards()
{
	for (FWurmFireBall& Ball : FireBalls)
	{
		if (IsValid(Ball.Mesh)) Ball.Mesh->DestroyComponent();
		++FireRocksRetired;
	}
	FireBalls.Reset();
	for (FWurmFireZone& Zone : FireZones) DestroyFireZone(Zone);
	FireZones.Reset();
}

void ABossWurm::TickFireBalls(float DeltaSeconds)
{
	for (int32 Index = FireBalls.Num() - 1; Index >= 0; --Index)
	{
		FWurmFireBall& Ball = FireBalls[Index];
		Ball.Life -= DeltaSeconds;
		Ball.ReprobeCd -= DeltaSeconds;
		if (Ball.ReprobeCd <= 0.f)
		{
			Ball.ReprobeCd = .1f;
			FVector Ground = Ball.Pos;
			ResolveGroundZ(Ground);
			Ball.GroundZ = Ground.Z;
		}
		Ball.Vel.Z -= 980.f * DeltaSeconds;
		Ball.Pos += Ball.Vel * DeltaSeconds;
		if (IsValid(Ball.Mesh)) Ball.Mesh->SetWorldLocation(Ball.Pos);
		if (Ball.Pos.Z > Ball.GroundZ + 25.f && Ball.Life > 0.f) continue;
		if (Ball.Life <= 0.f)
		{
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[WurmHazard] 落火超时退场 PosZ=%.1f GroundZ=%.1f"), Ball.Pos.Z, Ball.GroundZ);
			FVector Ground = Ball.Pos;
			ResolveGroundZ(Ground);
			Ball.GroundZ = Ground.Z;
		}
		FWurmFireZone Zone;
		Zone.Pos = FVector(Ball.Pos.X, Ball.Pos.Y, Ball.GroundZ);
		Zone.Life = FireZoneLife;
		const FVector VfxPosition = Zone.Pos + FVector(0.f, 0.f, 30.f);
		// Explosion分支的LifeSpan不参与Niagara寿命；捕获组件并由火区时钟硬截止。
		UNiagaraComponent* Impact = nullptr;
		UVfxLibrary::SpawnVfx(GetWorld(), EVfx::Explosion, VfxPosition, .4f, .6f, &Impact);
		Zone.ImpactVfx = Impact;
		if (Impact) Impact->ComponentTags.Add(TEXT("WurmHazardImpact"));
		if (HazardFlameTemplate)
		{
			// 组件归Boss持有，但不附到根组件；世界坐标固定在落点，Boss飞行不拖动火区。
			UNiagaraComponent* Flame = NewObject<UNiagaraComponent>(this);
			Flame->ComponentTags.Add(TEXT("WurmHazard"));
			Flame->SetAsset(HazardFlameTemplate);
			Flame->SetAutoActivate(false);
			Flame->SetAutoDestroy(false);
			Flame->SetCanEverAffectNavigation(false);
			Flame->RegisterComponent();
			Flame->SetWorldLocation(VfxPosition);
			Flame->SetWorldScale3D(FVector(1.2f));
			Zone.Vfx = Flame;
			Flame->Activate(true);
		}
		while (FireZones.Num() >= 14) { DestroyFireZone(FireZones[0]); FireZones.RemoveAt(0); }
		FireZones.Add(Zone);
		if (IsValid(Ball.Mesh)) Ball.Mesh->DestroyComponent();
		FireBalls.RemoveAtSwap(Index);
		++FireRocksRetired;
	}
}

void ABossWurm::TickFireZones(float DeltaSeconds)
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	UHealthComponent* Health = Player ? Player->FindComponentByClass<UHealthComponent>() : nullptr;
	for (int32 Index = FireZones.Num() - 1; Index >= 0; --Index)
	{
		FWurmFireZone& Zone = FireZones[Index];
		Zone.ImpactLife -= DeltaSeconds;
		if (Zone.ImpactLife <= 0.f) DestroyHazardVfx(Zone.ImpactVfx);
		Zone.Life -= DeltaSeconds;
		if (Zone.Life <= 0.f) { DestroyFireZone(Zone); FireZones.RemoveAtSwap(Index); continue; }
		FVector Previous;
		for (int32 Point = 0; Point <= 16; ++Point)
		{
			const float Angle = Point / 16.f * 2.f * PI;
			const FVector Position = Zone.Pos + FVector(FMath::Cos(Angle) * FireZoneRadius, FMath::Sin(Angle) * FireZoneRadius, 8.f);
			if (Point > 0) DrawDebugLine(GetWorld(), Previous, Position, FColor::FromHex(TEXT("FF7A26")), false, .05f, 0, 5.f);
			Previous = Position;
		}
		Zone.BurnCd -= DeltaSeconds;
		if (Zone.BurnCd <= 0.f)
		{
			Zone.BurnCd = .5f;
			if (Health && Player && FVector::Dist2D(Player->GetActorLocation(), Zone.Pos) <= FireZoneRadius)
			{
				Health->ApplyDamage(FireZoneBurn, this);
				// 玩家反伤可同步杀虫并清空FireZones，不能继续引用旧Zone/索引。
				if (IsDead()) return;
			}
		}
	}
}

void ABossWurm::NotifyMobKilled(UHealthComponent* MobHealth)
{
	if (IsDead() || !IsValid(MobHealth) || !MobHealth->IsDead() || MobDeathDamage <= 0.f ||
		!IsValid(MobHealth->GetOwner()) || MobHealth->GetOwner()->IsA<ABossBase>()) return;
	const TWeakObjectPtr<UHealthComponent> Key(MobHealth);
	if (CreditedMobDeaths.Contains(Key)) return;
	CreditedMobDeaths.Add(Key);
	for (auto It = CreditedMobDeaths.CreateIterator(); It; ++It) { if (!It->IsValid()) It.RemoveCurrent(); }
	++MobDeathsCredited;
	PendingMobDamage += MobDeathDamage;
	ApplyPendingMobDamage();
}

void ABossWurm::ApplyPendingMobDamage()
{
	if (bApplyingMobDamage || PendingMobDamage <= 0.f || IsDead() || !HealthComponent ||
		HealthComponent->IsInvulnerable() || State == EWurmState::Burrow || !GetWorld() || GetWorld()->IsPaused() ||
		GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation() < .01f) return;
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const UHealthComponent* PlayerHealth = Player ? Player->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!PlayerHealth || PlayerHealth->IsDead() || PlayerHealth->GetCurrentHealth() <= 0.f) return;
	TGuardValue<bool> Guard(bApplyingMobDamage, true);
	const float Damage = PendingMobDamage;
	PendingMobDamage = 0.f;
	const float Before = HealthComponent->GetCurrentHealth();
	HealthComponent->ApplyDamage(Damage, const_cast<APawn*>(Player));
	TotalMobDamageApplied += FMath::Max(Before - HealthComponent->GetCurrentHealth(), 0.f);
	UE_LOG(LogAICompanionDemo, Log, TEXT("[WurmBalance] 清杂反馈 %.1f，累计实际 %.1f，余血 %.1f"), Damage, TotalMobDamageApplied, HealthComponent->GetCurrentHealth());
}

void ABossWurm::DebugDropFireRock(const FVector& Location)
{
#if WITH_EDITOR
	SpawnFireBallAt(Location);
#endif
}

FString ABossWurm::DebugHazardState() const
{
#if WITH_EDITOR
	int32 LiveMeshes = 0, TrackedMeshes = 0, LiveVfx = 0, WorldVfx = 0, LiveDebris = 0, LiveImpacts = 0;
	TArray<UStaticMeshComponent*> Components;
	GetComponents(Components);
	for (UStaticMeshComponent* Component : Components)
	{
		if (!IsValid(Component) || !Component->IsRegistered()) continue;
		if (Component->ComponentHasTag(TEXT("WurmFireRock"))) ++LiveMeshes;
		if (Component->ComponentHasTag(TEXT("WurmDebris"))) ++LiveDebris;
	}
	for (const FWurmFireBall& Ball : FireBalls) if (IsValid(Ball.Mesh)) ++TrackedMeshes;
	for (const FWurmFireZone& Zone : FireZones)
	{
		if (Zone.Vfx.IsValid() && Zone.Vfx->IsRegistered()) ++LiveVfx;
		if (Zone.ImpactVfx.IsValid() && Zone.ImpactVfx->IsRegistered()) ++LiveImpacts;
	}
	TArray<UNiagaraComponent*> FlameComponents;
	GetComponents(FlameComponents);
	for (const UNiagaraComponent* Component : FlameComponents)
		if (IsValid(Component) && Component->IsRegistered() && Component->ComponentHasTag(TEXT("WurmHazard"))) ++WorldVfx;
	return FString::Printf(TEXT("{\"alive\":%s,\"invulnerable\":%s,\"mobDeathDamage\":%.3f,\"mobDeathsCredited\":%d,\"pendingMobDamage\":%.3f,\"totalMobDamageApplied\":%.3f,\"fireRocksSpawned\":%d,\"fireRocksRetired\":%d,\"fireBallData\":%d,\"liveFireRockMeshes\":%d,\"orphanFireRockMeshes\":%d,\"fireZoneData\":%d,\"liveFireZoneVfx\":%d,\"worldFireZoneVfx\":%d,\"orphanFireZoneVfx\":%d,\"liveImpactNiagara\":%d,\"liveDebrisMeshes\":%d,\"fireZonesRetired\":%d,\"rockArtworkReady\":%s}"),
		IsDead() ? TEXT("false") : TEXT("true"), HealthComponent && HealthComponent->IsInvulnerable() ? TEXT("true") : TEXT("false"),
		MobDeathDamage, MobDeathsCredited, PendingMobDamage, TotalMobDamageApplied, FireRocksSpawned, FireRocksRetired,
		FireBalls.Num(), LiveMeshes, FMath::Max(LiveMeshes - TrackedMeshes, 0), FireZones.Num(), LiveVfx, WorldVfx,
		FMath::Max(WorldVfx - LiveVfx, 0), LiveImpacts, LiveDebris, FireZonesRetired,
		FireRockMesh && FireRockBasalt && FireRockCore ? TEXT("true") : TEXT("false"));
#else
	return TEXT("{}");
#endif
}
